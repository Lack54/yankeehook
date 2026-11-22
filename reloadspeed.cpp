#include "reloadspeed.h"
#include "weaponscan.h"
#include "crashlog.h"
#include "magic.h"
#include "aim.h"
#include "volleyfx.h"
#include "camera.h"
#include <windows.h>
#include <tlhelp32.h>
#include <cstring>
#include <cstdio>
#include <cwchar>
#include <vector>

namespace reloadspeed {
bool  enabled = false;
float speed = 25.f;
float timeScale = 0.02f;
bool  forceFinish = false;
bool  probe = false;
Diag  diag;
bool  secondR = true;
bool  autoReload = true;
bool  infiniteAmmo = false;
bool  infiniteClip = false;
bool  clearFlags = false;
static volatile bool g_pendingR = false;
volatile int volleyLeft = 0;
int volleyPerTick = 1;
int volleyCount = 12, volleyKey = 0;
int volleyTotal = 12;
int volleyDelayMs = 150, volleyJitterMs = 80;
bool fireHot = false; int fireKey = 1, fireMs = 350;           // hold-to-fire (hipfire) hotkey and its rate
static unsigned long long g_nextFire = 0;
bool volleyRipple = false;                 // true: fire the row in order, false: random order (realistic volley)
bool volleyLockAim = true;                 // aim captured when the volley starts, so the recoil of earlier shots cannot move later ones
static float g_lockPos[3], g_lockFwd[3], g_lockRight[3]; static bool g_locked = false;
static int g_perm[64];
static unsigned long long g_due = 0;
static unsigned g_rng = 12345;
static unsigned rnd() { g_rng = g_rng * 214013u + 2531011u; return (g_rng >> 16) & 0x7FFF; }
void volleyStart(int n) {
    if (n < 1) n = 1; if (n > 64) n = 64;
    volleyTotal = n;
    for (int i = 0; i < n; ++i) g_perm[i] = i;
    if (!volleyRipple) for (int i = n - 1; i > 0; --i) { int j = rnd() % (i + 1); int t = g_perm[i]; g_perm[i] = g_perm[j]; g_perm[j] = t; }
    g_rng ^= (unsigned)GetTickCount64();
    g_due = 0;
    g_locked = false;
    {
        camera::Cam cam = camera::read();
        if (cam.valid) {
            g_lockPos[0] = cam.pos.x; g_lockPos[1] = cam.pos.y; g_lockPos[2] = cam.pos.z;
            g_lockFwd[0] = cam.fwd.x; g_lockFwd[1] = cam.fwd.y; g_lockFwd[2] = cam.fwd.z;
            g_lockRight[0] = cam.right.x; g_lockRight[1] = cam.right.y; g_lockRight[2] = cam.right.z;
            g_locked = true;
        }
    }
    volleyLeft = n;
}
bool volleyHot = true;
VolleyDiag volley;

namespace {
constexpr uintptr_t RVA_StartReload = 0x9B04D0;        // 0x1409B04D0
constexpr uintptr_t RVA_LocalPlayerId = 0x15E94F0;     // g_LocalPlayerEntityId (u32)
constexpr size_t    kStolen = 15;
// mov [rsp+8],rbx / mov [rsp+10h],rbp / mov [rsp+18h],rsi   (position independent)
const uint8_t kExpected[kStolen] = { 0x48,0x89,0x5c,0x24,0x08, 0x48,0x89,0x6c,0x24,0x10, 0x48,0x89,0x74,0x24,0x18 };

using StartFn = long long(__fastcall*)(long long, int);
StartFn  oStart = nullptr;
uint8_t* g_target = nullptr;
uint8_t* g_tramp = nullptr;
bool     g_hooked = false;

struct Saved { uintptr_t addr; float orig[16]; bool scaled; };
Saved g_saved[16];
int   g_nSaved = 0;

bool okPtr(uintptr_t v) { return v > 0x10000 && v < 0x00007FFFFFFF0000ull; }

bool isLocalWeapon(long long a1) {
    __try {
        uintptr_t base = (uintptr_t)GetModuleHandleW(nullptr);
        uintptr_t ent = *(volatile uintptr_t*)(a1 + 8);
        if (!okPtr(ent)) return false;
        unsigned owner = *(volatile unsigned*)(ent + 0xD8);
        unsigned local = *(volatile unsigned*)(base + RVA_LocalPlayerId);
        diag.ownerId = owner; diag.localId = local;
        return owner != 0 && owner == local;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

// Shrink the weapon's reload table. Idempotent: always scales from the saved originals.
void scaleTable(long long a1) {
    __try {
        uintptr_t fm = *(volatile uintptr_t*)(a1 + 0x10);
        if (!okPtr(fm)) return;
        uintptr_t t = *(volatile uintptr_t*)(fm + 0x40);
        if (!okPtr(t)) return;
        Saved* s = nullptr;
        for (int i = 0; i < g_nSaved; ++i) if (g_saved[i].addr == t) s = &g_saved[i];
        if (!s) {
            if (g_nSaved >= 16) return;
            s = &g_saved[g_nSaved++];
            s->addr = t; s->scaled = false;
            for (int i = 0; i < 16; ++i) s->orig[i] = *(volatile float*)(t + i * 4);
        }
        diag.tablePtr = t;
        memcpy(diag.tableOrig, s->orig, sizeof(diag.tableOrig));
        for (int i = 0; i < 16; ++i)
            if (s->orig[i] > 0.f) *(volatile float*)(t + i * 4) = s->orig[i] * timeScale;
        s->scaled = true;
        ++diag.tablesScaled;
        for (int i = 0; i < 16; ++i) diag.tableNow[i] = *(volatile float*)(t + i * 4);
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
}

void boostAction(long long a1, float spd) {
    __try {
        uintptr_t ent = *(volatile uintptr_t*)(a1 + 8);
        if (!okPtr(ent)) return;
        uintptr_t act = *(volatile uintptr_t*)(ent + 0x1A0);
        if (!okPtr(act)) return;
        memcpy(diag.actDump, (const void*)act, sizeof(diag.actDump));
        *(volatile float*)(act + 0x54) = spd;
        ++diag.boosted;
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
}

// ---------------- reload probe ----------------
// At reload start, snapshot the weapon object, its fire mode block, the reload table and the owning player; for 14 s compare
// them every 100 ms and log which dwords change and when, to find what keeps a reload going.
struct Region { const char* name; uintptr_t base; size_t len; std::vector<unsigned> prev; };
struct Change { int region; unsigned off; unsigned firstMs, lastMs; int count; unsigned firstOld, lastNew; };
struct Probe {
    bool active = false;
    ULONGLONG start = 0, lastPoll = 0;
    std::vector<Region> regions;
    std::vector<Change> changes;
} g_probe;

bool snap(uintptr_t base, size_t len, std::vector<unsigned>& out) {
    out.assign(len / 4, 0);
    __try { memcpy(out.data(), (const void*)base, len); return true; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool readQ(uintptr_t a, uintptr_t& v) { __try { v = *(volatile uintptr_t*)a; return true; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; } }

void probeStart(long long a1) {
    if (g_probe.active) return;
    g_probe = Probe();
    auto add = [&](const char* name, uintptr_t base, size_t len) {
        if (!okPtr(base)) return;
        Region r{ name, base, len, {} };
        if (snap(base, len, r.prev)) g_probe.regions.push_back(std::move(r));
    };
    uintptr_t ent = 0, fm = 0, tbl = 0, owner = 0;
    readQ(a1 + 8, ent); readQ(a1 + 0x10, fm);
    if (okPtr(fm)) readQ(fm + 0x40, tbl);
    // owning player = local player object
    uintptr_t base = (uintptr_t)GetModuleHandleW(nullptr);
    readQ(base + 0x15E94A0, owner);
    add("weapon", (uintptr_t)a1, 0x300);
    add("weaponEntity", ent, 0x200);
    add("fireModeObj", fm, 0x100);
    add("reloadTable", tbl, 0x40);
    add("player", owner, 0x840);
    // objects the weapon/player only point at: the reload action, the player's Mannequin actions and controller, the entity's
    // event scheduler (entity + 112)
    uintptr_t p = 0;
    if (okPtr(ent) && readQ(ent + 0x1A0, p)) add("reloadAction", p, 0x80);
    if (okPtr(ent)) add("scheduler", ent + 112, 0x100);
    if (okPtr(owner)) {
        const unsigned offs[5] = { 1712, 1720, 1616, 1624, 1752 };
        const char* names[5] = { "pAct1712", "pAct1720", "pAct1616", "pAct1624", "pAct1752" };
        for (int i = 0; i < 5; ++i) if (readQ(owner + offs[i], p)) add(names[i], p, 0x80);
        uintptr_t c = 0;
        if (readQ(owner + 408, c) && okPtr(c) && readQ(c + 64, p)) add("controller", p, 0x100);
    }
    g_probe.start = GetTickCount64(); g_probe.lastPoll = 0; g_probe.active = true;
    snprintf(diag.probeStatus, sizeof(diag.probeStatus), "probe running...");
}

void probeFinish() {
    g_probe.active = false;
    wchar_t path[MAX_PATH];
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    wchar_t* slash = wcsrchr(path, L'\\'); if (slash) *(slash + 1) = 0;
    wcscat_s(path, L"yankeehook_reload_probe.log");
    FILE* f = nullptr;
    if (_wfopen_s(&f, path, L"w") == 0 && f) {
        fprintf(f, "region offset firstMs lastMs changes firstOld lastNew   (changes <= 40 only; 14 s window)\n");
        for (auto& c : g_probe.changes) {
            if (c.count > 40) continue;
            fprintf(f, "%-12s +%04X %6u %6u %3d  %08X -> %08X   (float %.3f -> %.3f)\n", g_probe.regions[c.region].name, c.off,
                    c.firstMs, c.lastMs, c.count, c.firstOld, c.lastNew, *(float*)&c.firstOld, *(float*)&c.lastNew);
        }
        fprintf(f, "\nfields that changed more than 40 times (timers / sway): region offset firstMs lastMs changes first -> last\n");
        int shown = 0;
        for (auto& c : g_probe.changes) {
            if (c.count <= 40 || shown >= 80) continue;
            fprintf(f, "%-12s +%04X %6u %6u %4d  %08X -> %08X   (float %.3f -> %.3f)\n", g_probe.regions[c.region].name, c.off,
                    c.firstMs, c.lastMs, c.count, c.firstOld, c.lastNew, *(float*)&c.firstOld, *(float*)&c.lastNew);
            ++shown;
        }
        fclose(f);
        snprintf(diag.probeStatus, sizeof(diag.probeStatus), "probe done: yankeehook_reload_probe.log");
    } else snprintf(diag.probeStatus, sizeof(diag.probeStatus), "probe: could not write log");
    g_probe = Probe();
}

void probePoll() {
    if (!g_probe.active) return;
    ULONGLONG now = GetTickCount64();
    if (now - g_probe.lastPoll < 100) return;
    g_probe.lastPoll = now;
    unsigned ms = (unsigned)(now - g_probe.start);
    for (size_t r = 0; r < g_probe.regions.size(); ++r) {
        Region& rg = g_probe.regions[r];
        std::vector<unsigned> cur;
        if (!snap(rg.base, rg.len, cur)) continue;
        for (size_t i = 0; i < cur.size(); ++i) {
            if (cur[i] == rg.prev[i]) continue;
            bool found = false;
            for (auto& c : g_probe.changes)
                if (c.region == (int)r && c.off == (unsigned)(i * 4)) { c.lastMs = ms; ++c.count; c.lastNew = cur[i]; found = true; break; }
            if (!found && g_probe.changes.size() < 4000)
                g_probe.changes.push_back({ (int)r, (unsigned)(i * 4), ms, ms, 1, rg.prev[i], cur[i] });
            rg.prev[i] = cur[i];
        }
    }
    if (ms > 14000) probeFinish();
}

// The reload-complete event handler (sub_1409B3C90) does: if (*(u32*)(weapon+400) == savedId) { weapon->vfn[+416](weapon, 1);
// weapon->vfn[+520](weapon, step); }.  Do the same call straight away, then change the id so the pending event is a no-op.
void forceComplete(long long a1, int step) {
    static int depth = 0;
    if (depth >= 8) return;
    ++depth;
    __try {
        uintptr_t vt = *(volatile uintptr_t*)a1;
        if (okPtr(vt)) {
            typedef void(__fastcall* F416)(long long, bool);
            typedef long long(__fastcall* F520)(long long, unsigned);
            typedef int(__fastcall* FInt)(long long);
            uintptr_t p416 = *(volatile uintptr_t*)(vt + 416), p520 = *(volatile uintptr_t*)(vt + 520);
            unsigned id = *(volatile unsigned*)(a1 + 400);
            if (okPtr(p416) && okPtr(p520)) {
                ((F416)p416)(a1, true);                       // sub_1409B1160 (FinishReload)
                ((F520)p520)(a1, (unsigned)step);             // sub_1409B0F10 (OnReloadEnd): clears busy flag, tells the entity
                *(volatile unsigned*)(a1 + 400) = id + 7;     // makes the still-pending completion event a no-op
                ++diag.forced;

                // Ammo is stored in the weapon's item object (the weapon's +8): ammo class pointers at +608 / +616, loaded rounds
                // (dword) at +628 / +632 and reserve rounds at +636 / +640 (sub_140998810 getter, sub_140998840 setter,
                // sub_1409989D0 reserve getter). The weapon's ammo class is **(weapon+0x10) + 0x28.
                uintptr_t ent = *(volatile uintptr_t*)(a1 + 8), P = *(volatile uintptr_t*)(a1 + 0x10);
                uintptr_t params = okPtr(P) ? *(volatile uintptr_t*)P : 0;
                uintptr_t cls = okPtr(params) ? *(volatile uintptr_t*)(params + 0x28) : 0;
                uintptr_t pCap = *(volatile uintptr_t*)(vt + 64);
                if (okPtr(ent) && okPtr(cls) && okPtr(pCap)) {
                    int cap = ((FInt)pCap)(a1);                // clip size (sub_1409AF5B0)
                    diag.clipCap = cap;
                    for (int i = 0; i < 2; ++i) {
                        if (*(volatile uintptr_t*)(ent + 608 + 8 * i) != cls) continue;
                        volatile int* clip = (volatile int*)(ent + 628 + 4 * i);
                        volatile int* res  = (volatile int*)(ent + 636 + 4 * i);
                        diag.clipBefore = *clip; diag.entGet928 = *res;
                        int need = cap - *clip;
                        if (need > 0 && cap > 0 && cap < 1000 && *res > 0) {
                            int take = need < *res ? need : *res;   // same bookkeeping as a real reload: reserve -> clip
                            *clip = *clip + take;
                            *res  = *res - take;
                        }
                        diag.clipAfter = *clip; diag.entGet928After = *res;
                        break;
                    }
                }
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
    --depth;
}

long long __fastcall hkStartReload(long long a1, int step) {
    crashlog::note("hkStartReload");
    ++diag.calls;
    bool mine = enabled && isLocalWeapon(a1);
    if (mine) { ++diag.localHits; scaleTable(a1); }
    if (mine && probe) probeStart(a1);
    long long r = oStart(a1, step);
    if (mine) {
        boostAction(a1, speed);
        diag.weapon = (unsigned long long)a1; diag.startTick = GetTickCount64(); diag.busyMs = -1; diag.timerMax = 0;
        if (forceFinish) { forceComplete(a1, step); if (secondR) g_pendingR = true; }
    }
    return r;
}

bool prologueMatches() {
    __try { return memcmp(g_target, kExpected, kStolen) == 0; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

// Suspend every other thread while the stolen bytes are rewritten.
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

// weapon+0x88 is the countdown the weapon update ticks down every frame; CanFire (sub_1409AD580) needs it <= 0.
void clearTimer() {
    __try {
        if (!diag.weapon) return;
        volatile float* t = (volatile float*)(diag.weapon + 0x88);
        float v = *t;
        diag.timerNow = v;
        if (v > diag.timerMax) diag.timerMax = v;
        if (forceFinish && v > 0.f) *t = 0.f;

        // The R handler (weapon vfn +176, sub_1409AF860) calls the reload request and, once it returns, stores its result in
        // weapon+0x7E and marks the item busy again (entity vfn +504 -> byte at item+121). That happens after the forced finish,
        // so the weapon stays "busy" until a second R cancels it. Undo that for a short window after every forced reload.
        if (clearFlags && forceFinish && GetTickCount64() - diag.startTick < 2500) {
            uintptr_t ent = *(volatile uintptr_t*)(diag.weapon + 8);
            if (okPtr(ent) && *(volatile unsigned char*)(ent + 120) == 0) *(volatile unsigned char*)(ent + 121) = 0;
            *(volatile unsigned char*)(diag.weapon + 126) = 0;
            *(volatile unsigned char*)(diag.weapon + 419) = 0;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
}

// Evaluate every condition CanShoot (sub_1409AF790) combines, live, so the one that stays false can be seen.
void gatePoll() {
    __try {
        long long w = (long long)diag.weapon;
        if (!w) return;
        uintptr_t mod = (uintptr_t)GetModuleHandleW(nullptr);
        uintptr_t vt = *(volatile uintptr_t*)w;
        if (!okPtr(vt)) return;
        typedef unsigned char(__fastcall* FB)(long long);
        typedef unsigned char(__fastcall* FB2)(long long, unsigned char);
        typedef int(__fastcall* FI)(long long);
        auto& g = diag.gate;
        g.canShoot = ((FB2)*(volatile uintptr_t*)(vt + 168))(w, 1);
        g.b127 = *(volatile unsigned char*)(w + 127);
        g.b418 = *(volatile unsigned char*)(w + 418);
        g.timer = *(volatile float*)(w + 0x88);
        g.v72 = ((FB)*(volatile uintptr_t*)(vt + 72))(w);
        g.cap = ((FI)*(volatile uintptr_t*)(vt + 64))(w);
        uintptr_t ent = *(volatile uintptr_t*)(w + 8);
        if (okPtr(ent)) { g.e121 = *(volatile unsigned char*)(ent + 121); g.e120 = *(volatile unsigned char*)(ent + 120); }
        uintptr_t b = *(volatile uintptr_t*)(w + 96), e = *(volatile uintptr_t*)(w + 104);
        int n = (okPtr(b) && e >= b) ? (int)((e - b) / 8) : 0;
        if (n > 6) n = 6;
        g.nComp = n;
        for (int i = 0; i < n; ++i) {
            uintptr_t c = *(volatile uintptr_t*)(b + 8 * i);
            uintptr_t cvt = okPtr(c) ? *(volatile uintptr_t*)c : 0;
            if (!okPtr(cvt)) { g.compRes[i] = -1; continue; }
            g.compVt[i] = (unsigned)(cvt > mod ? cvt - mod : 0);
            g.compRes[i] = ((FB)*(volatile uintptr_t*)(cvt + 32))(c);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) { diag.weapon = 0; }
}

void poll() {
    probePoll();
    gatePoll();
    if (diag.weapon && GetTickCount64() - diag.startTick < 20000) clearTimer();
    if (!diag.weapon || diag.busyMs >= 0) return;
    __try {
        unsigned char busy = *(volatile unsigned char*)(diag.weapon + 127);   // set to 1 by the reload start, cleared on completion
        if (busy == 0) diag.busyMs = (int)(GetTickCount64() - diag.startTick);
    } __except (EXCEPTION_EXECUTE_HANDLER) { diag.weapon = 0; }
}

void restoreTables() {
    __try {
        for (int i = 0; i < g_nSaved; ++i) {
            if (!g_saved[i].scaled) continue;
            for (int k = 0; k < 16; ++k) *(volatile float*)(g_saved[i].addr + k * 4) = g_saved[i].orig[k];
            g_saved[i].scaled = false;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
}

// ---- second hook: the weapon update (sub_1409AF090, called every game tick on the game thread) ----
namespace {
constexpr uintptr_t RVA_Update = 0x9AF090;
constexpr size_t    kStolen2 = 15;
const uint8_t kExpected2[kStolen2] = { 0x48,0x89,0x5c,0x24,0x08, 0x48,0x89,0x6c,0x24,0x10, 0x48,0x89,0x74,0x24,0x18 };
using UpdFn = long long(__fastcall*)(long long, float, unsigned);
UpdFn    oUpdate = nullptr;
uint8_t* g_target2 = nullptr;
uint8_t* g_tramp2 = nullptr;
bool     g_hooked2 = false;

// Second R: with the item still marked busy by the R handler, the handler (weapon vfn +176 = sub_1409AF860) takes its
// "reload in progress" branch, which is the cancel/ready transition the manual second press triggers.
void doSecondR(long long a1) {
    __try {
        if (GetTickCount64() - diag.startTick < 60) return;       // let the R handler finish first
        g_pendingR = false;
        uintptr_t ent = *(volatile uintptr_t*)(a1 + 8);
        bool busy = okPtr(ent) && *(volatile unsigned char*)(ent + 121) != 0;
        bool key = *(volatile unsigned char*)(a1 + 126) != 0;
        diag.secondRBusy = busy; diag.secondRKey = key;
        // The R handler only requests a cancel while the weapon timer is inside a small window (0 < timer < params+0x10), which
        // it is not after a forced reload, so a second call does nothing. Run the weapon's own reset instead:
        // sub_1409AD540 = sub_1409AF320 (clears +124..+128, recomputes the fire interval, clears +417/+418) then clears +136 and +432.
        // No call into the game here (sub_1409AF320 takes a second argument that its callers leave in a register, so calling it
        // by hand is unsafe). Do what it does to the weapon's own fields instead:
        //   +124 = 0, dword at +125 (bytes 125..128, which holds the busy flag at +127 and the key flag at +126) = 0,
        //   dword +136 = 0, word at +417 (bytes 417, 418) = 0, then +419 = 0 and +432 = 0 (as sub_1409AD540 does).
        *(volatile unsigned char*)(a1 + 124) = 0;
        *(volatile unsigned int*)(a1 + 125) = 0;
        *(volatile unsigned int*)(a1 + 136) = 0;
        *(volatile unsigned short*)(a1 + 417) = 0;
        *(volatile unsigned char*)(a1 + 419) = 0;
        *(volatile unsigned char*)(a1 + 432) = 0;
        if (okPtr(ent) && *(volatile unsigned char*)(ent + 120) == 0) *(volatile unsigned char*)(ent + 121) = 0;   // item busy byte
        ++diag.secondRCount;
    } __except (EXCEPTION_EXECUTE_HANDLER) { g_pendingR = false; }
}

// Auto refill ("no R press"): when the local player's firearm has an empty clip and reserve ammo, load it straight away and
// clear the same weapon flags that made the manual R + auto-cock work. Only plain memory writes, no call into the game (an earlier
// version sent the R request from here, which re-entered the reload hook and crashed).
// Ammo layout: item (weapon+8) ammo class pointers at +608 / +616, loaded rounds at +628 / +632, reserve at +636 / +640;
// the weapon's class is **(weapon+0x10) + 0x28 and its clip size is the u16 at *(weapon+0x10)[0] + 0x3E.
unsigned long long g_lastAuto = 0;
void doAutoReload(long long a1) {
    __try {
        unsigned long long now = GetTickCount64();
        if (now - g_lastAuto < 120 || g_pendingR || now - diag.startTick < 300) return;
        uintptr_t ent = *(volatile uintptr_t*)(a1 + 8);
        if (!okPtr(ent)) return;
        unsigned owner = *(volatile unsigned*)(ent + 216);
        unsigned local = *(volatile unsigned*)((uintptr_t)GetModuleHandleW(nullptr) + RVA_LocalPlayerId);
        if (owner == 0 || owner != local) return;
        if (*(volatile unsigned char*)(a1 + 127)) return;                    // a real reload is in progress
        uintptr_t P = *(volatile uintptr_t*)(a1 + 0x10);
        uintptr_t params = okPtr(P) ? *(volatile uintptr_t*)P : 0;
        uintptr_t cls = okPtr(params) ? *(volatile uintptr_t*)(params + 0x28) : 0;
        if (!okPtr(cls)) return;
        int cap = *(volatile unsigned short*)(params + 0x3E);
        if (cap < 1 || cap > 500) return;
        for (int i = 0; i < 2; ++i) {
            if (*(volatile uintptr_t*)(ent + 608 + 8 * i) != cls) continue;
            volatile int* clip = (volatile int*)(ent + 628 + 4 * i);
            volatile int* res  = (volatile int*)(ent + 636 + 4 * i);
            if (*clip == 0 && *res > 0) {
                int take = cap < *res ? cap : *res;
                *clip = take; *res = *res - take;
                *(volatile unsigned char*)(a1 + 126) = 0;                    // reload key flag
                *(volatile unsigned char*)(a1 + 419) = 0;
                *(volatile unsigned short*)(a1 + 417) = 0;
                if (*(volatile unsigned char*)(ent + 120) == 0) *(volatile unsigned char*)(ent + 121) = 0;   // item busy byte
                g_lastAuto = now; ++diag.autoReloads;
            }
            break;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
}

// Infinite pocket ammo: reserve counts live in the weapon's item object (weapon+8) at +636 / +640, one per ammo slot
// (class pointers at +608 / +616). Keep them topped up for the local player's weapons. Plain memory writes on the game thread.
void doInfiniteAmmo(long long a1) {
    __try {
        uintptr_t ent = *(volatile uintptr_t*)(a1 + 8);
        if (!okPtr(ent)) return;
        unsigned owner = *(volatile unsigned*)(ent + 216);
        unsigned local = *(volatile unsigned*)((uintptr_t)GetModuleHandleW(nullptr) + RVA_LocalPlayerId);
        if (owner == 0 || owner != local) return;
        // the class this weapon fires and its clip size (u16 at params+0x3E), for the clip refill
        uintptr_t P = *(volatile uintptr_t*)(a1 + 0x10);
        uintptr_t params = okPtr(P) ? *(volatile uintptr_t*)P : 0;
        uintptr_t fireCls = okPtr(params) ? *(volatile uintptr_t*)(params + 0x28) : 0;
        int cap = okPtr(params) ? (int)*(volatile unsigned short*)(params + 0x3E) : 0;
        for (int i = 0; i < 2; ++i) {
            uintptr_t cls = *(volatile uintptr_t*)(ent + 608 + 8 * i);
            if (!okPtr(cls)) continue;
            if (infiniteAmmo) {
                volatile int* res = (volatile int*)(ent + 636 + 4 * i);
                if (*res >= 0 && *res < 500) *res = 999;
            }
            if (infiniteClip && cls == fireCls && cap >= 1 && cap <= 500 && !*(volatile unsigned char*)(a1 + 127)) {
                volatile int* clip = (volatile int*)(ent + 628 + 4 * i);
                if (*clip >= 0 && *clip < cap) *clip = cap;                      // never lets the loaded count drop
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
}

namespace {
int volleyFilter(EXCEPTION_POINTERS* ep) {
    volley.exCode = ep->ExceptionRecord->ExceptionCode;
    volley.exRva = (unsigned)((uintptr_t)ep->ExceptionRecord->ExceptionAddress - (uintptr_t)GetModuleHandleW(nullptr));
    ++volley.exCount;
    return EXCEPTION_EXECUTE_HANDLER;
}
bool isShootRva(uintptr_t rva) {
    static const uintptr_t k[] = { 0x9AD620, 0x9AE3A0, 0x9B12A0, 0x9B2380, 0x9B2F70, 0x9B36D0 };
    for (uintptr_t v : k) if (v == rva) return true;
    return false;
}
// Fire the weapon's own shoot function (vtable slot 28 = +224) for the local player's held weapon. It takes only `this`.
void doVolley(long long a1) {
    __try {
        ++volley.entered;
        uintptr_t ent = *(volatile uintptr_t*)(a1 + 8);
        if (!okPtr(ent)) { ++volley.badEnt; return; }
        unsigned owner = *(volatile unsigned*)(ent + 216);
        unsigned local = *(volatile unsigned*)((uintptr_t)GetModuleHandleW(nullptr) + RVA_LocalPlayerId);
        volley.owner = owner; volley.local = local; volley.ent = ent;
        if (owner == 0 || owner != local) { ++volley.notMine; return; }
        uintptr_t held = weaponscan::heldItemPtr();
        volley.held = held;
        if (held && ent != held) { ++volley.noMatch; return; }       // not the weapon in your hands
        uintptr_t* vt = *(uintptr_t**)a1;
        if (!okPtr((uintptr_t)vt)) return;
        uintptr_t fn = vt[28];
        uintptr_t base = (uintptr_t)GetModuleHandleW(nullptr);
        volley.shootRva = (unsigned)(fn - base);
        if (!isShootRva(fn - base)) { ++volley.rejected; return; }       // another fire mode of the same item (melee etc.): let the real one take it
        uintptr_t P = *(volatile uintptr_t*)(a1 + 0x10);
        uintptr_t params = okPtr(P) ? *(volatile uintptr_t*)P : 0;
        uintptr_t fireCls = okPtr(params) ? *(volatile uintptr_t*)(params + 0x28) : 0;
        int cap = okPtr(params) ? (int)*(volatile unsigned short*)(params + 0x3E) : 0;
        int n = volleyPerTick < 1 ? 1 : (volleyPerTick > 60 ? 60 : volleyPerTick);
        for (int k = 0; k < n && volleyLeft > 0; ++k) {
            unsigned long long nowT = GetTickCount64();
            if (g_due == 0) g_due = nowT;
            if (nowT < g_due) break;                              // wait for the next shot's turn
            // make the weapon ready again: the same state the weapon's own reset leaves (no reload, no cooldown, not "busy")
            *(volatile unsigned char*)(a1 + 124) = 0;
            *(volatile unsigned int*)(a1 + 125) = 0;          // bytes 125..128: key flag +126, reloading flag +127
            *(volatile unsigned int*)(a1 + 136) = 0;
            *(volatile unsigned short*)(a1 + 417) = 0;
            *(volatile unsigned char*)(a1 + 419) = 0;
            *(volatile unsigned char*)(a1 + 0x1A2) = 0;
            *(volatile float*)(a1 + 0x88) = 0.f;
            if (*(volatile unsigned char*)(ent + 120) == 0) *(volatile unsigned char*)(ent + 121) = 0;   // item busy byte
            for (int i = 0; i < 2; ++i) {
                uintptr_t cls = *(volatile uintptr_t*)(ent + 608 + 8 * i);
                if (okPtr(cls) && cls == fireCls && cap >= 1 && cap <= 500) {
                    volatile int* clip = (volatile int*)(ent + 628 + 4 * i);
                    if (*clip >= 0 && *clip < 2) *clip = cap > 2 ? 2 : cap;
                }
            }
            // The weapon's shoot virtual is Shoot(this, const Vec3* pos, const Vec3* dir) (sub_1409B2380): it spawns the bullet at pos
            // and launches it along dir at the fire mode's speed. Fire from the camera along the view, one slot of the row per shot.
            float pos[3], dir[3], anchor[3] = {};
            {
                float P[3], F[3], R[3];
                if (volleyLockAim && g_locked) {
                    memcpy(P, g_lockPos, 12); memcpy(F, g_lockFwd, 12); memcpy(R, g_lockRight, 12);
                } else {
                    camera::Cam cam = camera::read();
                    if (!cam.valid) { volleyLeft = 0; return; }
                    P[0] = cam.pos.x; P[1] = cam.pos.y; P[2] = cam.pos.z;
                    F[0] = cam.fwd.x; F[1] = cam.fwd.y; F[2] = cam.fwd.z;
                    R[0] = cam.right.x; R[1] = cam.right.y; R[2] = cam.right.z;
                }
                int N = volleyTotal < 1 ? 1 : volleyTotal;
                int step = N - volleyLeft; if (step < 0) step = 0; if (step > 63) step = 63;
                int idx = g_perm[step];
                float off = magic::lineOn ? (idx - (N - 1) * 0.5f) * magic::lineSpacing : 0.f;
                for (int q = 0; q < 3; ++q) { pos[q] = P[q] + F[q] * 0.6f + R[q] * off; dir[q] = F[q]; anchor[q] = P[q]; }
            }
            volleyfx::begin(pos, anchor);
            char ok = ((char(__fastcall*)(long long, const float*, const float*))fn)(a1, pos, dir);
            volleyfx::end();
            volley.lastRet = (unsigned char)ok;
            if (ok) { --volleyLeft; ++volley.fired; g_due += (unsigned long long)(volleyDelayMs + (volleyJitterMs > 0 ? (int)(rnd() % (unsigned)(volleyJitterMs + 1)) : 0)); magic::lineUntil = GetTickCount64() + 400; volley.failRun = 0; }
            else {
                ++volley.failed;
                // which gate refused? CanShoot (vtable +168) tells whether it was the ready check
                volley.canShoot = ((char(__fastcall*)(long long, char))vt[21])(a1, 1);
                if (++volley.failRun > 120) { volleyLeft = 0; volley.failRun = 0; }
                break;                                          // try again next tick
            }
        }
    } __except (volleyFilter(GetExceptionInformation())) { volleyLeft = 0; volleyfx::end(); }
}
}

// Magic shot: fire the weapon's own shoot call (this, pos, dir) with the bullet starting right in front of the current magic target.
void doMagicShot(long long a1) {
    __try {
        uintptr_t ent = *(volatile uintptr_t*)(a1 + 8);
        if (!okPtr(ent)) return;
        unsigned owner = *(volatile unsigned*)(ent + 216);
        unsigned local = *(volatile unsigned*)((uintptr_t)GetModuleHandleW(nullptr) + RVA_LocalPlayerId);
        if (owner == 0 || owner != local) return;
        uintptr_t held = weaponscan::heldItemPtr();
        if (held && ent != held) return;
        uintptr_t* vt = *(uintptr_t**)a1;
        if (!okPtr((uintptr_t)vt)) return;
        uintptr_t fn = vt[28];
        if (!isShootRva(fn - (uintptr_t)GetModuleHandleW(nullptr))) return;
        float T[3];
        camera::Cam cam = camera::read();
        if (!aim::magicTarget(T) || !cam.valid) { magic::pendingShots = 0; return; }
        float dx = T[0] - cam.pos.x, dy = T[1] - cam.pos.y, dz = T[2] - cam.pos.z;
        float len = sqrtf(dx * dx + dy * dy + dz * dz);
        if (!(len > 0.5f)) { magic::pendingShots = 0; return; }
        float nd[3] = { dx / len, dy / len, dz / len };
        float dist = aim::s.magicSpawnDist; if (dist > len - 0.2f) dist = len - 0.2f; if (dist < 0.f) dist = 0.f;
        float np[3] = { T[0] - nd[0] * dist, T[1] - nd[1] * dist, T[2] - nd[2] * dist };
        // ready the weapon again (same state reset as the volley), keep a round in the clip
        uintptr_t P = *(volatile uintptr_t*)(a1 + 0x10);
        uintptr_t params = okPtr(P) ? *(volatile uintptr_t*)P : 0;
        uintptr_t fireCls = okPtr(params) ? *(volatile uintptr_t*)(params + 0x28) : 0;
        int cap = okPtr(params) ? (int)*(volatile unsigned short*)(params + 0x3E) : 0;
        *(volatile unsigned char*)(a1 + 124) = 0;
        *(volatile unsigned int*)(a1 + 125) = 0;
        *(volatile unsigned int*)(a1 + 136) = 0;
        *(volatile unsigned short*)(a1 + 417) = 0;
        *(volatile unsigned char*)(a1 + 419) = 0;
        *(volatile unsigned char*)(a1 + 0x1A2) = 0;
        *(volatile float*)(a1 + 0x88) = 0.f;
        if (*(volatile unsigned char*)(ent + 120) == 0) *(volatile unsigned char*)(ent + 121) = 0;
        for (int i = 0; i < 2; ++i) {
            uintptr_t cls = *(volatile uintptr_t*)(ent + 608 + 8 * i);
            if (okPtr(cls) && cls == fireCls && cap >= 1 && cap <= 500) {
                volatile int* clip = (volatile int*)(ent + 628 + 4 * i);
                if (*clip >= 0 && *clip < 2) *clip = cap > 2 ? 2 : cap;
            }
        }
        magic::lineUntil = GetTickCount64() + 400;          // lets the bullet's start clear the 'predicted' flag so it can hurt
        char ok = ((char(__fastcall*)(long long, const float*, const float*))fn)(a1, np, nd);
        if (ok) { ++magic::diag.magicFired; --magic::pendingShots; magic::magicIgnoreUntil = GetTickCount64() + 400; }
        else { ++magic::diag.magicFailed; if (magic::diag.magicFailed % 20 == 0) magic::pendingShots = 0; }
    } __except (EXCEPTION_EXECUTE_HANDLER) { magic::pendingShots = 0; }
}

long long __fastcall hkUpdate(long long a1, float dt, unsigned a3) {
    crashlog::note("hkUpdate");
    long long r = oUpdate(a1, dt, a3);
    if (infiniteAmmo || infiniteClip) doInfiniteAmmo(a1);
    if (volleyLeft > 0) doVolley(a1);
    else if (magic::pendingShots > 0) doMagicShot(a1);
    if (enabled && forceFinish) {
        if (g_pendingR && secondR && (unsigned long long)a1 == diag.weapon) doSecondR(a1);
        else if (autoReload) doAutoReload(a1);
    }
    return r;
}

bool prologue2Matches() {
    __try { return memcmp(g_target2, kExpected2, kStolen2) == 0; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

bool installUpdate() {
    if (g_hooked2) return true;
    g_target2 = (uint8_t*)((uintptr_t)GetModuleHandleW(nullptr) + RVA_Update);
    if (!prologue2Matches()) return false;
    g_tramp2 = (uint8_t*)VirtualAlloc(nullptr, 64, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (!g_tramp2) return false;
    memcpy(g_tramp2, kExpected2, kStolen2);
    uint8_t jmp[6] = { 0xFF, 0x25, 0, 0, 0, 0 };
    memcpy(g_tramp2 + kStolen2, jmp, 6);
    uintptr_t back = (uintptr_t)(g_target2 + kStolen2);
    memcpy(g_tramp2 + kStolen2 + 6, &back, 8);
    oUpdate = (UpdFn)g_tramp2;
    uint8_t patch[kStolen2];
    memset(patch, 0x90, sizeof(patch));
    memcpy(patch, jmp, 6);
    uintptr_t det = (uintptr_t)&hkUpdate;
    memcpy(patch + 6, &det, 8);
    Frozen f; freeze(f);
    bool ok = writeCode(g_target2, patch, kStolen2);
    thaw(f);
    g_hooked2 = ok;
    if (!ok) { VirtualFree(g_tramp2, 0, MEM_RELEASE); g_tramp2 = nullptr; }
    return ok;
}

void removeUpdate() {
    if (!g_hooked2) return;
    Frozen f; freeze(f);
    writeCode(g_target2, kExpected2, kStolen2);
    thaw(f);
    g_hooked2 = false;
}
} // namespace

void volleyPoll() {
    static const int kVk[] = { VK_XBUTTON1, VK_XBUTTON2, VK_RBUTTON, VK_LMENU, VK_LSHIFT, VK_CAPITAL };
    static bool down = false;
    HWND fg = GetForegroundWindow(); DWORD pid = 0;
    if (fg) GetWindowThreadProcessId(fg, &pid);
    bool d = volleyHot && pid == GetCurrentProcessId() && (GetAsyncKeyState(kVk[volleyKey < 0 ? 0 : (volleyKey > 5 ? 5 : volleyKey)]) & 0x8000) != 0;
    if (d && !down && volleyLeft <= 0) volleyStart(volleyCount);
    down = d;
    // hold-to-fire: one shot from the hip along your view every fireMs while the key is held
    {
        bool fd = fireHot && pid == GetCurrentProcessId() && (GetAsyncKeyState(kVk[fireKey < 0 ? 0 : (fireKey > 5 ? 5 : fireKey)]) & 0x8000) != 0;
        unsigned long long t = GetTickCount64();
        if (fd && volleyLeft <= 0 && t >= g_nextFire) { volleyStart(1); g_nextFire = t + (fireMs < 30 ? 30 : fireMs); }
    }
}

bool install() {
    if (g_hooked) return true;
    diag.installResult = "failed before patch";
    g_target = (uint8_t*)((uintptr_t)GetModuleHandleW(nullptr) + RVA_StartReload);
    if (!prologueMatches()) { diag.installResult = "prologue mismatch (game version differs)"; return false; }

    // trampoline: stolen bytes, then jmp [rip+0] -> target + kStolen
    g_tramp = (uint8_t*)VirtualAlloc(nullptr, 64, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (!g_tramp) return false;
    memcpy(g_tramp, kExpected, kStolen);
    uint8_t jmp[6] = { 0xFF, 0x25, 0, 0, 0, 0 };
    memcpy(g_tramp + kStolen, jmp, 6);
    uintptr_t back = (uintptr_t)(g_target + kStolen);
    memcpy(g_tramp + kStolen + 6, &back, 8);
    oStart = (StartFn)g_tramp;

    // patch: jmp [rip+0] -> hkStartReload (14 bytes) + 1 nop
    uint8_t patch[kStolen];
    memset(patch, 0x90, sizeof(patch));
    memcpy(patch, jmp, 6);
    uintptr_t det = (uintptr_t)&hkStartReload;
    memcpy(patch + 6, &det, 8);

    Frozen f; freeze(f);
    bool ok = writeCode(g_target, patch, kStolen);
    thaw(f);
    g_hooked = ok;
    diag.installResult = ok ? (installUpdate() ? "hooked (reload + update)" : "hooked (reload only)") : "patch write failed";
    if (!ok) { VirtualFree(g_tramp, 0, MEM_RELEASE); g_tramp = nullptr; }
    return ok;
}

void uninstall() {
    enabled = false;
    restoreTables();
    if (!g_hooked) return;
    removeUpdate();
    Frozen f; freeze(f);
    writeCode(g_target, kExpected, kStolen);
    thaw(f);
    g_hooked = false;
    Sleep(300);                       // let any call already inside the trampoline return
    if (g_tramp) { VirtualFree(g_tramp, 0, MEM_RELEASE); g_tramp = nullptr; }
    if (g_tramp2) { VirtualFree(g_tramp2, 0, MEM_RELEASE); g_tramp2 = nullptr; }
}
}
