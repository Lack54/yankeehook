#include "aim.h"
#include "camera.h"
#include "entities.h"
#include "esp.h"
#include "fov.h"
#include "menu.h"
#include "tracers.h"
#include "weaponscan.h"
#include "magic.h"
#include "visible.h"
#include "imgui.h"
#include <windows.h>
#include <unordered_map>
#include <cmath>
#include <algorithm>

namespace aim {
Settings s;

const char* const kKeyNames[] = { "Mouse 4", "Mouse 5", "Right mouse", "Left Alt", "Left Shift", "Caps Lock" };
const int kKeyCount = 6;
static const int kKeyVk[] = { VK_XBUTTON1, VK_XBUTTON2, VK_RBUTTON, VK_LMENU, VK_LSHIFT, VK_CAPITAL };

namespace {
struct Vel { float x = 0, y = 0, z = 0; float vx = 0, vy = 0, vz = 0; double t = 0; };
std::unordered_map<unsigned, Vel> g_vel;
Stats g_stats;
bool g_haveAim = false;
float g_aimX = 0, g_aimY = 0;          // predicted aim point on screen
float g_remX = 0, g_remY = 0;          // fractional mouse counts carried over

struct MagicTarget { volatile long seq = 0; bool valid = false; float x = 0, y = 0, z = 0, vx = 0, vy = 0; double t = 0; float g = 0; };
bool g_haveMagic = false; float g_magSx = 0, g_magSy = 0, g_magDist = 0;
double nowSec();
struct VisEntry { double t = 0; bool vis = true; };
std::unordered_map<unsigned, VisEntry> g_vis;
int g_rayBudget = 0;
// Cached line-of-sight test from the camera to a point (60 ms cache per bot, at most 10 new rays per frame).
bool isVisible(unsigned id, const camera::Cam& cam, const camera::V3& p) {
    if (!s.visibleOnly) return true;
    VisEntry& v = g_vis[id];
    double now = nowSec();
    if (now - v.t < 0.06) return v.vis;
    if (g_rayBudget <= 0) return v.vis;
    --g_rayBudget;
    v.vis = visible::lineClear(cam.pos.x, cam.pos.y, cam.pos.z, p.x, p.y, p.z);
    v.t = now;
    return v.vis;
}
MagicTarget g_mt;                       // written on the render thread, read by the bullet hook (stale reads are harmless)

double nowSec() {
    static LARGE_INTEGER f = [] { LARGE_INTEGER x; QueryPerformanceFrequency(&x); return x; }();
    LARGE_INTEGER c; QueryPerformanceCounter(&c);
    return (double)c.QuadPart / (double)f.QuadPart;
}

bool gameFocused() {
    HWND fg = GetForegroundWindow();
    if (!fg) return false;
    DWORD pid = 0;
    GetWindowThreadProcessId(fg, &pid);
    return pid == GetCurrentProcessId();
}

void sendMove(int dx, int dy) {
    if (!dx && !dy) return;
    INPUT in{};
    in.type = INPUT_MOUSE;
    in.mi.dx = dx; in.mi.dy = dy;
    in.mi.dwFlags = MOUSEEVENTF_MOVE;
    SendInput(1, &in, sizeof(in));
    ++g_stats.moves;
}
} // namespace

void update() {
    g_stats.active = false; g_stats.haveTarget = false; g_haveAim = false;
    tracers::measureRequested = s.enabled || s.magic;   // let the tracer scan measure bullet speed and gravity
    g_mt.valid = false; g_haveMagic = false; g_rayBudget = 10;
    if (g_vis.size() > 400) g_vis.clear();
    if (!s.enabled && !s.magic) { g_vel.clear(); return; }

    const entities::Snapshot& snap = entities::current();
    const double now = nowSec();
    // per-player velocity from consecutive frames
    for (const auto& e : snap.list) {
        Vel& v = g_vel[e.id];
        if (v.t > 0) {
            double dt = now - v.t;
            if (dt > 0.002 && dt < 0.5) {
                float nx = (e.x - v.x) / (float)dt, ny = (e.y - v.y) / (float)dt;
                if (fabsf(nx) < 30.f && fabsf(ny) < 30.f) { v.vx += 0.25f * (nx - v.vx); v.vy += 0.25f * (ny - v.vy); }
                else { v.vx = v.vy = 0; }
            }
        }
        v.x = e.x; v.y = e.y; v.z = e.z; v.t = now;
    }

    if (s.magic) {
        camera::Cam mc = camera::read();
        if (mc.valid) {
            ImVec2 md = ImGui::GetIO().DisplaySize;
            const float mfov = fov::effective(esp::s.fov);
            const bool mTeam = esp::s.teamCheck && snap.localFaction != 0;
            float best = s.magicFovPx;
            for (const auto& e : snap.list) {
                if (e.isLocal || e.state != 1) continue;
                if (mTeam && e.faction == snap.localFaction) continue;
                if (e.dist > s.maxDistance || e.dist < 1.f) continue;
                float h = esp::boxHeightFor(e.stance, e.state) * s.aimHeight;
                camera::V3 p{ e.x, e.y, e.z + h };
                float sx, sy;
                if (!camera::worldToScreen(mc, p, md.x, md.y, mfov, false, sx, sy)) continue;
                float px = sqrtf((sx - md.x * 0.5f) * (sx - md.x * 0.5f) + (sy - md.y * 0.5f) * (sy - md.y * 0.5f));
                if (px >= best) continue;
                if (!isVisible(e.id, mc, p)) continue;
                best = px;
                float vx = 0, vy = 0;
                if (s.predict) { auto it = g_vel.find(e.id); if (it != g_vel.end()) { vx = it->second.vx; vy = it->second.vy; } }
                g_mt.x = p.x; g_mt.y = p.y; g_mt.z = p.z; g_mt.vx = vx; g_mt.vy = vy; g_mt.t = now;
                tracers::Measured mm = tracers::measured();
                g_mt.g = (s.useMeasured && mm.samples >= 3) ? mm.gravity : s.gravity;
                g_mt.valid = true; g_haveMagic = true; g_magSx = sx; g_magSy = sy; g_magDist = e.dist;
            }
        }
    }
    if (!s.enabled) return;
    if (menu::isOpen() || !gameFocused()) return;
    const int vk = kKeyVk[std::clamp(s.key, 0, kKeyCount - 1)];
    if (!(GetAsyncKeyState(vk) & 0x8000)) { g_remX = g_remY = 0; return; }

    camera::Cam cam = camera::read();
    if (!cam.valid) return;
    ImVec2 disp = ImGui::GetIO().DisplaySize;
    const float cx = disp.x * 0.5f, cy = disp.y * 0.5f;
    const float fovDeg = fov::effective(esp::s.fov);

    // ballistics
    float v0 = weaponscan::heldBulletSpeed();
    if (v0 < 20.f) v0 = 289.f;                                        // ammo default
    float g = s.gravity;
    tracers::Measured m = tracers::measured();
    if (s.useMeasured && m.samples >= 3) g = m.gravity;
    g_stats.bulletSpeed = v0; g_stats.gravityUsed = g;

    const entities::Info* best = nullptr;
    float bestPx = s.fovPx, bx = 0, by = 0, bt = 0, bd = 0;
    const bool useTeam = esp::s.teamCheck && snap.localFaction != 0;
    for (const auto& e : snap.list) {
        if (e.isLocal || e.state != 1) continue;
        if (useTeam && e.faction == snap.localFaction) continue;
        if (e.dist > s.maxDistance || e.dist < 1.f) continue;

        float h = esp::boxHeightFor(e.stance, e.state) * s.aimHeight;
        camera::V3 p0{ e.x, e.y, e.z + h };
        float vx = 0, vy = 0;
        if (s.predict) { auto it = g_vel.find(e.id); if (it != g_vel.end()) { vx = it->second.vx; vy = it->second.vy; } }
        // Ballistic solution. The bullet leaves along the aim direction at speed v0 and falls under gravity g, so to reach a point at
        // horizontal distance d and height dz the launch angle satisfies  tan(a) = (v^2 - sqrt(v^4 - g (g d^2 + 2 dz v^2))) / (g d).
        // The flight time is d / (v cos a). The target is moved by velocity * time, and the solution is repeated (4 passes).
        const float gEff = (s.drop && !weaponscan::noDrop) ? g : 0.f;
        float t = e.dist / v0;
        camera::V3 p = p0;
        bool unreachable = false;
        for (int i = 0; i < 4; ++i) {
            p = { p0.x + vx * t, p0.y + vy * t, p0.z };
            float dx = p.x - cam.pos.x, dy = p.y - cam.pos.y, dz = p.z - cam.pos.z;
            float d = sqrtf(dx * dx + dy * dy);
            if (gEff < 0.01f || d < 1.f) { t = sqrtf(d * d + dz * dz) / v0; continue; }
            float v2 = v0 * v0;
            float disc = v2 * v2 - gEff * (gEff * d * d + 2.f * dz * v2);
            if (disc < 0.f) { unreachable = true; break; }             // out of range for this bullet speed
            float tanA = (v2 - sqrtf(disc)) / (gEff * d);
            p.z = cam.pos.z + d * tanA;                                // aim at this height above the camera at distance d
            t = d / (v0 / sqrtf(1.f + tanA * tanA));
        }
        if (unreachable) continue;

        float sx, sy;
        if (!camera::worldToScreen(cam, p, disp.x, disp.y, fovDeg, false, sx, sy)) continue;
        float px = sqrtf((sx - cx) * (sx - cx) + (sy - cy) * (sy - cy));
        if (px < bestPx && isVisible(e.id, cam, p0)) { bestPx = px; best = &e; bx = sx; by = sy; bt = t; bd = e.dist; }
    }
    if (!best) return;

    g_stats.haveTarget = true; g_stats.active = true; g_stats.flightTime = bt; g_stats.targetDist = bd;
    g_haveAim = true; g_aimX = bx; g_aimY = by;

    // closed-loop mouse move: counts proportional to the pixel error (the game's sensitivity decides how far a count turns,
    // so use "strength" to tune it; the loop converges as long as the direction is right)
    float ex = bx - cx, ey = by - cy;
    float gain = s.strength * 0.3f;
    float mx = std::clamp(ex * gain + g_remX, -s.maxStep, s.maxStep);
    float my = std::clamp(ey * gain + g_remY, -s.maxStep, s.maxStep);
    int ix = (int)lroundf(mx), iy = (int)lroundf(my);
    g_remX = mx - ix; g_remY = my - iy;
    sendMove(ix, iy);
}

void draw() {
    ImVec2 disp = ImGui::GetIO().DisplaySize;
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    if (s.magic) {
        static int lastRed = 0; static double flashT = -10;
        int r = (int)magic::diag.redirected;
        if (r != lastRed) { lastRed = r; flashT = nowSec(); }
        const bool flash = nowSec() - flashT < 0.6;
        const ImVec2 c(disp.x * 0.5f, disp.y * 0.5f);
        dl->AddCircle(c, s.magicFovPx, IM_COL32(108, 181, 255, 60), 64, 1.f);
        if (g_haveMagic) {
            ImU32 col = flash ? IM_COL32(80, 255, 120, 255) : IM_COL32(108, 181, 255, 230);
            dl->AddLine(c, ImVec2(g_magSx, g_magSy), col, 1.f);
            dl->AddCircle(ImVec2(g_magSx, g_magSy), 9.f, col, 24, 2.f);
            char b[48]; snprintf(b, sizeof(b), "MAGIC %.0fm", g_magDist);
            dl->AddText(ImVec2(g_magSx + 12, g_magSy - 7), col, b);
        }
        char hud[96];
        snprintf(hud, sizeof(hud), "MAGIC BULLET  %s  shots redirected: %d", g_haveMagic ? "TARGET LOCKED" : "no target", r);
        dl->AddText(ImVec2(c.x - 110, c.y + s.magicFovPx + 8), flash ? IM_COL32(80, 255, 120, 255) : IM_COL32(255, 255, 255, 200), hud);
    }
    if (!s.enabled) return;
    if (s.drawFov) dl->AddCircle(ImVec2(disp.x * 0.5f, disp.y * 0.5f), s.fovPx, IM_COL32(255, 255, 255, 70), 64, 1.f);
    if (g_haveAim) {
        dl->AddLine(ImVec2(g_aimX - 6, g_aimY), ImVec2(g_aimX + 6, g_aimY), IM_COL32(255, 220, 60, 230), 1.5f);
        dl->AddLine(ImVec2(g_aimX, g_aimY - 6), ImVec2(g_aimX, g_aimY + 6), IM_COL32(255, 220, 60, 230), 1.5f);
    }
}

Stats stats() { return g_stats; }

bool magicTarget(float out[3]) {
    if (!g_mt.valid) return false;
    const double age = nowSec() - g_mt.t;
    if (age > 0.25) return false;
    out[0] = g_mt.x + g_mt.vx * (float)age; out[1] = g_mt.y + g_mt.vy * (float)age; out[2] = g_mt.z;
    return true;
}

bool magicDirection(float ox, float oy, float oz, float v0, float out[3]) {
    if (!g_mt.valid) return false;
    const double age = nowSec() - g_mt.t;
    if (age > 0.25) return false;                              // target data too old (menu open / frame stalled)
    const float gEff = (s.drop && !weaponscan::noDrop) ? g_mt.g : 0.f;
    float t = 0.f;
    float px = g_mt.x, py = g_mt.y, pz = g_mt.z;
    {   float dx = px - ox, dy = py - oy, dz = pz - oz; t = sqrtf(dx * dx + dy * dy + dz * dz) / v0; }
    for (int i = 0; i < 4; ++i) {
        px = g_mt.x + g_mt.vx * (t + (float)age); py = g_mt.y + g_mt.vy * (t + (float)age); pz = g_mt.z;
        float dx = px - ox, dy = py - oy, dz = pz - oz;
        float d = sqrtf(dx * dx + dy * dy);
        if (gEff < 0.01f || d < 1.f) { t = sqrtf(d * d + dz * dz) / v0; continue; }
        float v2 = v0 * v0;
        float disc = v2 * v2 - gEff * (gEff * d * d + 2.f * dz * v2);
        if (disc < 0.f) return false;
        float tanA = (v2 - sqrtf(disc)) / (gEff * d);
        pz = oz + d * tanA;
        t = d / (v0 / sqrtf(1.f + tanA * tanA));
    }
    float dx = px - ox, dy = py - oy, dz = pz - oz;
    float l = sqrtf(dx * dx + dy * dy + dz * dz);
    if (l < 0.5f) return false;
    out[0] = dx / l; out[1] = dy / l; out[2] = dz / l;
    return true;
}
}
