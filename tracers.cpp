#include "tracers.h"
#include "camera.h"
#include "entities.h"
#include "esp.h"
#include "fov.h"
#include "imgui.h"
#include <windows.h>
#include <unordered_map>
#include <algorithm>
#include <cmath>
#include <cstring>

namespace tracers {
Settings s;
bool measureRequested = false;

namespace {
constexpr uintptr_t RVA_EntityArray = 0x1464750;   // CEntity** (g_CEntitySystem + 0x80)
constexpr uintptr_t RVA_MaxIndex    = 0x14A47FE;   // u16 highest used entity index (g_CEntitySystem + 0x4012E)
constexpr uintptr_t ENT_id = 0x04, ENT_class = 0x118, ENT_x = 0x4C, ENT_y = 0x5C, ENT_z = 0x6C, CLS_name = 0x08;

struct Pt { float x, y, z; double t; };
struct Track {
    int owner = 0;                 // 0 enemy, 1 self, 2 team
    std::vector<Pt> pts;
    double lastSeen = 0, born = 0;
    bool decided = false;
    double undecidedSince = 0;
};

std::unordered_map<unsigned, Track> g_tracks;
std::unordered_map<uintptr_t, int> g_classKind;      // IEntityClass* -> 1 bullet, 0 other
std::vector<std::string> g_recent;                  // recent non-bullet class names (for diagnostics)
Stats g_stats;

bool okPtr(uintptr_t v) { return v > 0x10000 && v < 0x00007FFFFFFF0000ull; }
template <class T> bool rd(uintptr_t a, T& out) { __try { out = *(volatile T*)a; return true; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; } }

bool readName(uintptr_t cls, char* buf, size_t cap) {
    __try {
        uintptr_t np = *(volatile uintptr_t*)(cls + CLS_name);
        if (np < 0x10000) return false;
        size_t i = 0;
        for (; i + 1 < cap; ++i) { char c = ((volatile char*)np)[i]; if (!c) break; if ((unsigned char)c < 0x20) return false; buf[i] = c; }
        buf[i] = 0;
        return i > 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

bool nameIsBullet(const char* n) {
    char l[64]; size_t i = 0;
    for (; n[i] && i < 63; ++i) l[i] = (char)tolower((unsigned char)n[i]);
    l[i] = 0;
    return strstr(l, "ball") || strstr(l, "bullet") || strstr(l, "buck") || strstr(l, "pellet") || strstr(l, "minie") ||
           strstr(l, "slug") || strstr(l, "shot") && !strstr(l, "shotgun_");
}

// 1 = bullet entity class, 0 = anything else; cached per class pointer
int kindOf(uintptr_t cls) {
    auto it = g_classKind.find(cls);
    if (it != g_classKind.end()) return it->second;
    char name[64] = {};
    int k = 0;
    if (readName(cls, name, sizeof(name))) {
        k = nameIsBullet(name) ? 1 : 0;
        if (k) { g_stats.lastBulletClass = name; ++g_stats.bulletClasses; }
        else if (g_recent.size() < 40) g_recent.push_back(name);
    }
    g_classKind[cls] = k;
    return k;
}

double nowSec() {
    static LARGE_INTEGER f = [] { LARGE_INTEGER x; QueryPerformanceFrequency(&x); return x; }();
    LARGE_INTEGER c; QueryPerformanceCounter(&c);
    return (double)c.QuadPart / (double)f.QuadPart;
}

Measured g_meas;

// Fit z(t) = c0 + c1 t + c2 t^2 to a finished trail: gravity = -2 c2, initial vertical speed = c1, horizontal speed = path / time.
void fitTrack(const Track& tr) {
    size_t n = tr.pts.size();
    if (n < 6) return;
    double t0 = tr.pts.front().t, T = tr.pts.back().t - t0;
    if (T < 0.12) return;
    double S[5] = {}, Z[3] = {};
    for (const Pt& p : tr.pts) {
        double t = p.t - t0, tk = 1;
        for (int k = 0; k < 5; ++k) { S[k] += tk; if (k < 3) Z[k] += p.z * tk; tk *= t; }
    }
    double a[3][4] = { { S[0], S[1], S[2], Z[0] }, { S[1], S[2], S[3], Z[1] }, { S[2], S[3], S[4], Z[2] } };
    for (int i = 0; i < 3; ++i) {                       // Gaussian elimination
        int piv = i;
        for (int r = i + 1; r < 3; ++r) if (fabs(a[r][i]) > fabs(a[piv][i])) piv = r;
        if (fabs(a[piv][i]) < 1e-12) return;
        for (int c = 0; c < 4; ++c) std::swap(a[i][c], a[piv][c]);
        for (int r = i + 1; r < 3; ++r) { double f = a[r][i] / a[i][i]; for (int c = i; c < 4; ++c) a[r][c] -= f * a[i][c]; }
    }
    double c2 = a[2][3] / a[2][2];
    double c1 = (a[1][3] - a[1][2] * c2) / a[1][1];
    double g = -2.0 * c2;
    double dx = tr.pts.back().x - tr.pts.front().x, dy = tr.pts.back().y - tr.pts.front().y;
    double vh = sqrt(dx * dx + dy * dy) / T;
    double v0 = sqrt(vh * vh + c1 * c1);
    if (!(g > 2.0 && g < 40.0 && v0 > 100.0 && v0 < 700.0)) return;
    if (g_meas.samples == 0) { g_meas.gravity = (float)g; g_meas.speed = (float)v0; }
    else { g_meas.gravity += 0.2f * ((float)g - g_meas.gravity); g_meas.speed += 0.2f * ((float)v0 - g_meas.speed); }
    ++g_meas.samples;
}

// The bullet's game object holds the shooter's entity id at +0xA0. Objects live in a std::map<u32 entityId, Bullet*> at
// *(*(g_gameData + 48) + 208) (same lookup the game's bullet spawner uses; node: key at +32, value at +40, isnil byte at +25).
uintptr_t bulletObject(unsigned id) {
    __try {
        uintptr_t base = (uintptr_t)GetModuleHandleW(nullptr);
        uintptr_t gd = *(volatile uintptr_t*)(base + 0x15E8DF0);
        if (!okPtr(gd)) return 0;
        uintptr_t reg = *(volatile uintptr_t*)(gd + 48);
        if (!okPtr(reg)) return 0;
        uintptr_t head = *(volatile uintptr_t*)(reg + 208);
        if (!okPtr(head)) return 0;
        uintptr_t best = head, node = *(volatile uintptr_t*)(head + 8);
        for (int guard = 0; guard < 64 && okPtr(node) && !*(volatile unsigned char*)(node + 25); ++guard) {
            if (*(volatile unsigned*)(node + 32) >= id) { best = node; node = *(volatile uintptr_t*)(node); }
            else node = *(volatile uintptr_t*)(node + 16);
        }
        if (best == head || *(volatile unsigned char*)(best + 25) || *(volatile unsigned*)(best + 32) != id) return 0;
        uintptr_t obj = *(volatile uintptr_t*)(best + 40);
        return okPtr(obj) ? obj : 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
unsigned shooterOf(unsigned bulletEntityId) {
    uintptr_t obj = bulletObject(bulletEntityId);
    unsigned s = 0;
    if (obj && rd(obj + 0xA0, s)) return s;
    return 0;
}
// 0 enemy, 1 self, 2 team, -1 = not known yet
int ownerFromShooter(unsigned shooter) {
    if (!shooter) return -1;
    unsigned local = 0;
    rd((uintptr_t)GetModuleHandleW(nullptr) + 0x15E94F0, local);
    if (local && shooter == local) return 1;
    const auto& snap = entities::current();
    for (const auto& e : snap.list) {
        if (e.id != shooter) continue;
        if (e.isLocal) return 1;
        if (snap.localFaction != 0 && e.faction == snap.localFaction) return 2;
        return 0;
    }
    return 0;      // a shooter we have no record of: treat as enemy
}

int decideOwner(float x, float y, float z) {
    const auto& snap = entities::current();
    const entities::Info* best = nullptr; float bd = 12.f * 12.f;
    for (const auto& e : snap.list) {
        if (e.state == 2) continue;
        float dx = e.x - x, dy = e.y - y, dz = (e.z + 1.4f) - z;
        float d = dx * dx + dy * dy + dz * dz;
        if (d < bd) { bd = d; best = &e; }
    }
    if (!best) return 0;
    if (best->isLocal) return 1;
    if (snap.localFaction != 0 && best->faction == snap.localFaction) return 2;
    return 0;
}
} // namespace

void update() {
    g_stats.entitiesScanned = 0; g_stats.bulletsNow = 0;
    const double t = nowSec();
    uintptr_t base = (uintptr_t)GetModuleHandleW(nullptr), arr = 0;
    unsigned short maxIdx = 0;
    if ((s.enabled || measureRequested) && rd(base + RVA_EntityArray, arr) && okPtr(arr) && rd(base + RVA_MaxIndex, maxIdx)) {
        for (unsigned i = 0; i <= maxIdx && i < 65536; ++i) {
            uintptr_t e = 0;
            if (!rd(arr + 8ull * i, e) || !okPtr(e)) continue;
            ++g_stats.entitiesScanned;
            uintptr_t cls = 0;
            if (!rd(e + ENT_class, cls) || !okPtr(cls)) continue;
            if (kindOf(cls) != 1) continue;
            unsigned flags = 0, id = 0; float x, y, z;
            if (!rd(e, flags) || (flags & 0x20)) continue;          // marked removed
            if (!rd(e + ENT_id, id) || !rd(e + ENT_x, x) || !rd(e + ENT_y, y) || !rd(e + ENT_z, z)) continue;
            if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z) || fabsf(x) > 1e6f) continue;
            ++g_stats.bulletsNow;
            Track& tr = g_tracks[id];
            // bullet entities are pooled: a reused one jumps from the old shot's end to the new muzzle. Start a fresh trail then.
            if (!tr.pts.empty()) {
                const Pt& lp = tr.pts.back();
                float jx = x - lp.x, jy = y - lp.y, jz = z - lp.z;
                if (jx * jx + jy * jy + jz * jz > 40.f * 40.f || t - tr.lastSeen > 0.25) { tr.pts.clear(); tr.decided = false; }
            }
            if (!tr.decided) {
                int o = ownerFromShooter(shooterOf(id));
                if (o >= 0) { tr.owner = o; tr.decided = true; tr.born = t; }
                else if (tr.pts.empty()) { tr.owner = decideOwner(x, y, z); tr.born = t; tr.undecidedSince = t; }
                else if (t - tr.undecidedSince > 0.3) { tr.decided = true; }   // fall back to the nearest-player guess
            }
            tr.lastSeen = t;
            if (tr.pts.empty() || fabsf(tr.pts.back().x - x) + fabsf(tr.pts.back().y - y) + fabsf(tr.pts.back().z - z) > 0.02f) {
                if (tr.pts.size() > 600) tr.pts.erase(tr.pts.begin());
                tr.pts.push_back({ x, y, z, t });
            }
        }
    }
    // drop trails that have faded
    for (auto it = g_tracks.begin(); it != g_tracks.end();) {
        if (t - it->second.lastSeen > s.lifetime + 0.05f) { fitTrack(it->second); it = g_tracks.erase(it); } else ++it;
    }
    g_stats.tracks = (int)g_tracks.size();
    int moving = 0; for (auto& kv : g_tracks) if (kv.second.pts.size() > 1) ++moving;
    g_stats.moving = moving;
}

void draw() {
    if (!s.enabled || g_tracks.empty()) return;
    camera::Cam cam = camera::read();
    if (!cam.valid) return;
    ImVec2 disp = ImGui::GetIO().DisplaySize;
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    const float fovDeg = fov::effective(esp::s.fov);
    const double t = nowSec();
    const camera::LocalPos lp = camera::readLocal();

    for (auto& kv : g_tracks) {
        const Track& tr = kv.second;
        if (tr.pts.size() < 2) continue;
        if ((tr.owner == 1 && !s.showSelf) || (tr.owner == 2 && !s.showTeam) || (tr.owner == 0 && !s.showEnemy)) continue;
        const float* c = tr.owner == 1 ? s.colSelf : tr.owner == 2 ? s.colTeam : s.colEnemy;
        if (lp.valid) {
            float dx = tr.pts.front().x - lp.pos.x, dy = tr.pts.front().y - lp.pos.y, dz = tr.pts.front().z - lp.pos.z;
            if (dx * dx + dy * dy + dz * dz > s.maxDistance * s.maxDistance) continue;
        }
        for (size_t i = 1; i < tr.pts.size(); ++i) {
            const Pt& a = tr.pts[i - 1]; const Pt& b = tr.pts[i];
            float age = (float)(t - b.t);
            float fade = 1.f - age / s.lifetime;
            if (fade <= 0.f) continue;
            float ax, ay, bx, by;
            if (!camera::worldToScreen(cam, { a.x, a.y, a.z }, disp.x, disp.y, fovDeg, false, ax, ay)) continue;
            if (!camera::worldToScreen(cam, { b.x, b.y, b.z }, disp.x, disp.y, fovDeg, false, bx, by)) continue;
            ImU32 col = IM_COL32((int)(c[0] * 255), (int)(c[1] * 255), (int)(c[2] * 255), (int)(c[3] * 255 * fade));
            if (s.glow) {
                ImU32 g = IM_COL32((int)(c[0] * 255), (int)(c[1] * 255), (int)(c[2] * 255), (int)(c[3] * 70 * fade));
                dl->AddLine(ImVec2(ax, ay), ImVec2(bx, by), g, s.thickness * 3.f);
            }
            dl->AddLine(ImVec2(ax, ay), ImVec2(bx, by), col, s.thickness);
        }
    }
}

Measured measured() { return g_meas; }

Stats stats() {
    Stats st = g_stats;
    st.sampleClasses.clear();
    int n = 0;
    for (auto it = g_recent.rbegin(); it != g_recent.rend() && n < 8; ++it, ++n) { st.sampleClasses += *it; st.sampleClasses += ' '; }
    return st;
}
}
