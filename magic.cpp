#include "magic.h"
#include "aim.h"
#include "camera.h"
#include "weaponscan.h"
#include "crashlog.h"
#include <windows.h>
#include <tlhelp32.h>
#include <cmath>
#include <cstring>
#include <vector>

namespace magic {
Diag diag;
volatile int pendingShots = 0; volatile unsigned long long magicIgnoreUntil = 0;
bool volleyDamage = true; bool lineOn = false; float lineSpacing = 0.4f; int lineCount = 9; volatile unsigned long long lineUntil = 0; volatile int lineApplied = 0;

namespace {
constexpr uintptr_t RVA_Shoot = 0x9B2380;         // Shoot(this, const Vec3* pos, const Vec3* dir) = sub_1409B2380
constexpr uintptr_t RVA_SetPhase = 0x982C20;          // 0x140982C20
constexpr uintptr_t RVA_LocalPlayerId = 0x15E94F0;    // g_LocalPlayerEntityId (u32)
constexpr size_t    kStolen = 16;
// push rbp / push rbx / push rsi / lea rbp,[rsp-30h] / sub rsp,130h   (no RIP-relative operands in these 16 bytes)
const uint8_t kExpected[kStolen] = { 0x40,0x55, 0x53, 0x56, 0x48,0x8d,0x6c,0x24,0xd0, 0x48,0x81,0xec,0x30,0x01,0x00,0x00 };

using PhaseFn = char(__fastcall*)(long long, char);
PhaseFn  oPhase = nullptr;
uint8_t* g_target = nullptr;
uint8_t* g_tramp = nullptr;
bool     g_hooked = false;

bool okPtr(uintptr_t v) { return v > 0x10000 && v < 0x00007FFFFFFF0000ull; }

// Plain types only (SEH).
void redirect(long long a1) {
    __try {
        unsigned shooter = *(volatile unsigned*)(a1 + 0xA0);
        unsigned local = *(volatile unsigned*)((uintptr_t)GetModuleHandleW(nullptr) + RVA_LocalPlayerId);
        if (!local || shooter != local) return;
        ++diag.mine;

        // where the bullet starts: its entity position (entity = *(bullet+8), matrix translation at +0x4C/+0x5C/+0x6C)
        float ox = 0, oy = 0, oz = 0; bool haveO = false;
        uintptr_t ent = *(volatile uintptr_t*)(a1 + 8);
        camera::Cam cam = camera::read();
        if (okPtr(ent)) {
            ox = *(volatile float*)(ent + 0x4C); oy = *(volatile float*)(ent + 0x5C); oz = *(volatile float*)(ent + 0x6C);
            haveO = std::isfinite(ox) && std::isfinite(oy) && std::isfinite(oz);
            if (haveO && cam.valid) {                                   // must be near the camera, otherwise it is not a muzzle position
                float dx = ox - cam.pos.x, dy = oy - cam.pos.y, dz = oz - cam.pos.z;
                if (dx * dx + dy * dy + dz * dz > 25.f) haveO = false;
            }
        }
        if (!haveO) { if (!cam.valid) return; ox = cam.pos.x; oy = cam.pos.y; oz = cam.pos.z; }

        float v0 = weaponscan::heldBulletSpeed();
        if (v0 < 20.f) v0 = 289.f;
        float dir[3];
        if (!aim::magicDirection(ox, oy, oz, v0, dir)) { ++diag.noTarget; return; }

        float* d = (float*)(a1 + 0x7C);
        float ol = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
        diag.lastDotWithOld = ol > 1e-4f ? (d[0] * dir[0] + d[1] * dir[1] + d[2] * dir[2]) / ol : 0.f;
        d[0] = dir[0]; d[1] = dir[1]; d[2] = dir[2];
        ++diag.redirected;
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
}

// Shift this bullet's start position along the camera's right axis (row of positions, centred on the muzzle).
void offsetPosition(long long a1) {
    __try {
        unsigned shooter = *(volatile unsigned*)(a1 + 0xA0);
        unsigned local = *(volatile unsigned*)((uintptr_t)GetModuleHandleW(nullptr) + RVA_LocalPlayerId);
        if (!local || shooter != local) return;
        uintptr_t ent = *(volatile uintptr_t*)(a1 + 8);
        if (!okPtr(ent)) return;
        camera::Cam cam = camera::read();
        if (!cam.valid) return;
        float ox = *(volatile float*)(ent + 0x4C), oy = *(volatile float*)(ent + 0x5C), oz = *(volatile float*)(ent + 0x6C);
        float dx = ox - cam.pos.x, dy = oy - cam.pos.y, dz = oz - cam.pos.z;
        if (!(dx * dx + dy * dy + dz * dz < 25.f)) return;            // only a bullet that is at your muzzle
        static int idx = 0; static unsigned long long lastTick = 0;
        unsigned long long now = GetTickCount64();
        if (now - lastTick > 600) idx = 0;
        lastTick = now;
        int n = lineCount < 1 ? 1 : (lineCount > 60 ? 60 : lineCount);
        float k = ((idx++ % n) - (n - 1) * 0.5f) * lineSpacing;
        *(volatile float*)(ent + 0x4C) = ox + cam.right.x * k;
        *(volatile float*)(ent + 0x5C) = oy + cam.right.y * k;
        *(volatile float*)(ent + 0x6C) = oz + cam.right.z * k;
        ++lineApplied;
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
}

// Bullets made by the (pos, dir) shoot variant carry bullet flag 4 (+0x60); hits only apply damage when it is clear.
void clearPredictedFlag(long long a1) {
    __try {
        unsigned shooter = *(volatile unsigned*)(a1 + 0xA0);
        unsigned local = *(volatile unsigned*)((uintptr_t)GetModuleHandleW(nullptr) + RVA_LocalPlayerId);
        if (!local || shooter != local) return;
        *(volatile unsigned*)(a1 + 0x60) &= ~4u;
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
}

// A bullet of yours just started flying (a normal shot): queue a magic shot, fired through the position/direction shoot call
// (the same way the volley fires), starting right in front of the target.
void queueMagic(long long a1) {
    __try {
        unsigned shooter = *(volatile unsigned*)(a1 + 0xA0);
        unsigned local = *(volatile unsigned*)((uintptr_t)GetModuleHandleW(nullptr) + RVA_LocalPlayerId);
        if (!local || shooter != local) return;
        unsigned long long now = GetTickCount64();
        if (now < magicIgnoreUntil) return;
        float T[3];
        if (!aim::magicTarget(T)) return;
        magicIgnoreUntil = now + 400;
        if (pendingShots < 4) ++pendingShots;
        ++diag.queued;
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
}

char __fastcall hkSetPhase(long long a1, char phase) {
    if (phase == 1) {
        ++diag.phase1;
        if (GetTickCount64() < lineUntil) { if (volleyDamage) clearPredictedFlag(a1); }
        else if (aim::s.magic) queueMagic(a1);
        // (the old SetPhase direction rewrite is retired: magic bullet now redirects through the shoot call, see hkShoot)
    }
    return oPhase(a1, phase);
}

bool prologueMatches() {
    __try { return memcmp(g_target, kExpected, kStolen) == 0; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

struct Frozen { std::vector<HANDLE> h; };
void freeze(Frozen& f) {
    DWORD me = GetCurrentThreadId(), pid = GetCurrentProcessId();
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snap == INVALID_HANDLE_VALUE) return;
    THREADENTRY32 te{}; te.dwSize = sizeof(te);
    if (Thread32First(snap, &te)) do {
        if (te.th32OwnerProcessID != pid || te.th32ThreadID == me) continue;
        HANDLE t = OpenThread(THREAD_SUSPEND_RESUME, FALSE, te.th32ThreadID);
        if (t) { if (SuspendThread(t) != (DWORD)-1) f.h.push_back(t); else CloseHandle(t); }
    } while (Thread32Next(snap, &te));
    CloseHandle(snap);
}
void thaw(Frozen& f) { for (HANDLE t : f.h) { ResumeThread(t); CloseHandle(t); } f.h.clear(); }

bool writeCode(uint8_t* dst, const uint8_t* src, size_t n) {
    DWORD old;
    if (!VirtualProtect(dst, n, PAGE_EXECUTE_READWRITE, &old)) return false;
    memcpy(dst, src, n);
    VirtualProtect(dst, n, old, &old);
    FlushInstructionCache(GetCurrentProcess(), dst, n);
    return true;
}
} // namespace

namespace {
// ---- Shoot(this, pos, dir): the weapon's shoot virtual (vtable slot 28, +224). The caller (the game's fire logic, or our volley)
// passes the muzzle position and the aim direction; magic bullet swaps in the direction to the best target (lead + drop solved from the
// bullet's real start position), then lets the original run.
constexpr size_t kStolenS = 15;
// mov rax,rsp / mov [rax+20h],rbx / mov [rax+18h],r8 / mov [rax+10h],rdx   (no RIP-relative operands)
const uint8_t kExpectedS[kStolenS] = { 0x48,0x8b,0xc4, 0x48,0x89,0x58,0x20, 0x4c,0x89,0x40,0x18, 0x48,0x89,0x50,0x10 };
using ShootFn = char(__fastcall*)(long long, const float*, const float*);
ShootFn  oShoot = nullptr;
uint8_t* g_targetS = nullptr;
uint8_t* g_trampS = nullptr;
bool     g_hookedS = false;

char __fastcall hkShoot(long long a1, const float* pos, const float* dir) {
    ++diag.shootCalls;
    if (aim::s.magic) {
        float nd[3], np[3]; bool ok = false;
        __try {
            uintptr_t ent = *(volatile uintptr_t*)(a1 + 8);
            unsigned owner = okPtr(ent) ? *(volatile unsigned*)(ent + 216) : 0;
            unsigned local = *(volatile unsigned*)((uintptr_t)GetModuleHandleW(nullptr) + RVA_LocalPlayerId);
            if (owner && owner == local) {
                ++diag.shootLocal;
                // Instead of bending the bullet's path, start it right in front of the target: pos = target - dir * distance
                float T[3];
                if (aim::magicTarget(T)) {
                    float dx = T[0] - pos[0], dy = T[1] - pos[1], dz = T[2] - pos[2];
                    float len = sqrtf(dx * dx + dy * dy + dz * dz);
                    if (len > 0.5f && std::isfinite(len)) {
                        float dist = aim::s.magicSpawnDist; if (dist > len - 0.2f) dist = len - 0.2f; if (dist < 0.f) dist = 0.f;
                        nd[0] = dx / len; nd[1] = dy / len; nd[2] = dz / len;
                        np[0] = T[0] - nd[0] * dist; np[1] = T[1] - nd[1] * dist; np[2] = T[2] - nd[2] * dist;
                        ok = true;
                    }
                }
                if (!ok) ++diag.noTarget;
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) { ok = false; }
        if (ok) { ++diag.shootRedirected; magicIgnoreUntil = GetTickCount64() + 400; return oShoot(a1, np, nd); }
    }
    return oShoot(a1, pos, dir);
}

bool prologueSMatches() { __try { return memcmp(g_targetS, kExpectedS, kStolenS) == 0; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; } }

bool installShoot() {
    if (g_hookedS) return true;
    g_targetS = (uint8_t*)((uintptr_t)GetModuleHandleW(nullptr) + RVA_Shoot);
    if (!prologueSMatches()) { diag.shootInstall = "prologue mismatch"; return false; }
    g_trampS = (uint8_t*)VirtualAlloc(nullptr, 64, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (!g_trampS) { diag.shootInstall = "alloc failed"; return false; }
    memcpy(g_trampS, kExpectedS, kStolenS);
    uint8_t jmp[6] = { 0xFF, 0x25, 0, 0, 0, 0 };
    memcpy(g_trampS + kStolenS, jmp, 6);
    uintptr_t back = (uintptr_t)(g_targetS + kStolenS);
    memcpy(g_trampS + kStolenS + 6, &back, 8);
    oShoot = (ShootFn)g_trampS;
    uint8_t patch[kStolenS]; memset(patch, 0x90, sizeof(patch)); memcpy(patch, jmp, 6);
    uintptr_t det = (uintptr_t)&hkShoot; memcpy(patch + 6, &det, 8);
    Frozen f; freeze(f);
    bool ok = writeCode(g_targetS, patch, kStolenS);
    thaw(f);
    g_hookedS = ok;
    diag.shootInstall = ok ? "hooked" : "patch failed";
    if (!ok) { VirtualFree(g_trampS, 0, MEM_RELEASE); g_trampS = nullptr; }
    return ok;
}
void uninstallShoot() {
    if (!g_hookedS) return;
    Frozen f; freeze(f);
    writeCode(g_targetS, kExpectedS, kStolenS);
    thaw(f);
    g_hookedS = false;
}
}

bool install() {
    if (g_hooked) return true;
    diag.installResult = "failed before patch";
    g_target = (uint8_t*)((uintptr_t)GetModuleHandleW(nullptr) + RVA_SetPhase);
    if (!prologueMatches()) { diag.installResult = "prologue mismatch (game version differs)"; return false; }

    g_tramp = (uint8_t*)VirtualAlloc(nullptr, 64, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (!g_tramp) return false;
    memcpy(g_tramp, kExpected, kStolen);
    uint8_t jmp[6] = { 0xFF, 0x25, 0, 0, 0, 0 };
    memcpy(g_tramp + kStolen, jmp, 6);
    uintptr_t back = (uintptr_t)(g_target + kStolen);
    memcpy(g_tramp + kStolen + 6, &back, 8);
    oPhase = (PhaseFn)g_tramp;

    uint8_t patch[kStolen];
    memset(patch, 0x90, sizeof(patch));
    memcpy(patch, jmp, 6);
    uintptr_t det = (uintptr_t)&hkSetPhase;
    memcpy(patch + 6, &det, 8);

    Frozen f; freeze(f);
    bool ok = writeCode(g_target, patch, kStolen);
    thaw(f);
    g_hooked = ok;
    diag.installResult = ok ? "hooked" : "patch write failed";
    installShoot();
    if (!ok) { VirtualFree(g_tramp, 0, MEM_RELEASE); g_tramp = nullptr; }
    return ok;
}

void uninstall() {
    uninstallShoot();
    if (!g_hooked) return;
    Frozen f; freeze(f);
    writeCode(g_target, kExpected, kStolen);
    thaw(f);
    g_hooked = false;
    Sleep(300);
    if (g_tramp) { VirtualFree(g_tramp, 0, MEM_RELEASE); g_tramp = nullptr; }
}
}
