#include "volleyfx.h"
#include <windows.h>
#include <tlhelp32.h>
#include <cstring>
#include <cmath>
#include <vector>

namespace volleyfx {
bool enabled = true;
Diag diag;

namespace {
constexpr uintptr_t RVA_Spawn = 0x28E0A0;
constexpr size_t kStolen = 15;
// push rbp / push rbx / push rsi / push r12 / push r13 / push r14 / push r15 / mov rbp,rsp   (no RIP-relative operands)
const uint8_t kExpected[kStolen] = { 0x40,0x55, 0x53, 0x56, 0x41,0x54, 0x41,0x55, 0x41,0x56, 0x41,0x57, 0x48,0x8b,0xec };
using Fn = long long(__fastcall*)(long long, long long, const float*, long long);
Fn oFn = nullptr; uint8_t* g_target = nullptr; uint8_t* g_tramp = nullptr; bool g_hooked = false;

volatile bool g_active = false; volatile DWORD g_tid = 0;
float g_pos[3] = {}, g_anchor[3] = {};

long long __fastcall hkSpawn(long long mgr, long long fx, const float* pos, long long orient) {
    ++diag.spawns;
    if (g_active && enabled && g_tid == GetCurrentThreadId()) {
        float np[3]; bool close = false;
        __try {
            float dx = pos[0] - g_anchor[0], dy = pos[1] - g_anchor[1], dz = pos[2] - g_anchor[2];
            close = (dx * dx + dy * dy + dz * dz) < 36.f;                // a muzzle effect: within 6 m of where the shot starts
        } __except (EXCEPTION_EXECUTE_HANDLER) { close = false; }
        if (close) {
            np[0] = g_pos[0]; np[1] = g_pos[1]; np[2] = g_pos[2];
            ++diag.moved;
            return oFn(mgr, fx, np, orient);
        }
    }
    return oFn(mgr, fx, pos, orient);
}

bool prologueMatches() { __try { return memcmp(g_target, kExpected, kStolen) == 0; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; } }
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
    DWORD old; if (!VirtualProtect(dst, n, PAGE_EXECUTE_READWRITE, &old)) return false;
    memcpy(dst, src, n); VirtualProtect(dst, n, old, &old); FlushInstructionCache(GetCurrentProcess(), dst, n); return true;
}
}

bool install() {
    if (g_hooked) return true;
    g_target = (uint8_t*)((uintptr_t)GetModuleHandleW(nullptr) + RVA_Spawn);
    if (!prologueMatches()) { diag.installResult = "prologue mismatch"; return false; }
    g_tramp = (uint8_t*)VirtualAlloc(nullptr, 64, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (!g_tramp) { diag.installResult = "alloc failed"; return false; }
    memcpy(g_tramp, kExpected, kStolen);
    uint8_t jmp[6] = { 0xFF, 0x25, 0, 0, 0, 0 };
    memcpy(g_tramp + kStolen, jmp, 6);
    uintptr_t back = (uintptr_t)(g_target + kStolen);
    memcpy(g_tramp + kStolen + 6, &back, 8);
    oFn = (Fn)g_tramp;
    uint8_t patch[kStolen]; memset(patch, 0x90, sizeof(patch)); memcpy(patch, jmp, 6);
    uintptr_t det = (uintptr_t)&hkSpawn; memcpy(patch + 6, &det, 8);
    Frozen f; freeze(f);
    bool ok = writeCode(g_target, patch, kStolen);
    thaw(f);
    g_hooked = ok;
    diag.installResult = ok ? "hooked" : "patch failed";
    if (!ok) { VirtualFree(g_tramp, 0, MEM_RELEASE); g_tramp = nullptr; }
    return ok;
}

void uninstall() {
    g_active = false;
    if (!g_hooked) return;
    Frozen f; freeze(f);
    writeCode(g_target, kExpected, kStolen);
    thaw(f);
    g_hooked = false;
    Sleep(300);
    if (g_tramp) { VirtualFree(g_tramp, 0, MEM_RELEASE); g_tramp = nullptr; }
}

void begin(const float pos[3], const float anchor[3]) {
    memcpy(g_pos, pos, 12); memcpy(g_anchor, anchor, 12);
    g_tid = GetCurrentThreadId();
    g_active = true;
}
void end() { g_active = false; }
}
