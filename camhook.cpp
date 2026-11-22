#include "camhook.h"
#include <windows.h>
#include <tlhelp32.h>
#include <cmath>
#include <cstring>
#include <vector>

namespace camhook {
bool thirdPerson = false; float tpDistance = 3.0f, tpHeight = 0.3f;
bool freecam = false; float fcSpeed = 8.f;
int tpKey = 5, fcKey = 6;                              // F6, F7
const char* const kKeyNames[] = { "F1","F2","F3","F4","F5","F6","F7","F8","F9","F10","F11","F12","Insert","Home","End" };
const int kKeyCount = 15;
static const int kVk[] = { VK_F1,VK_F2,VK_F3,VK_F4,VK_F5,VK_F6,VK_F7,VK_F8,VK_F9,VK_F10,VK_F11,VK_F12,VK_INSERT,VK_HOME,VK_END };
Diag diag;

namespace {
constexpr uintptr_t RVA_SetCam = 0x3CDAC0;
constexpr size_t kStolen = 15;
const uint8_t kExpected[kStolen] = { 0x48,0x89,0x5c,0x24,0x08, 0x48,0x89,0x74,0x24,0x10, 0x48,0x89,0x7c,0x24,0x18 };
using Fn = long long(__fastcall*)(void*, long long, long long);
Fn oFn = nullptr; uint8_t* g_target = nullptr; uint8_t* g_tramp = nullptr; bool g_hooked = false;

bool okPtr(uintptr_t v) { return v > 0x10000 && v < 0x00007FFFFFFF0000ull; }
double nowSec() {
    static LARGE_INTEGER f = [] { LARGE_INTEGER x; QueryPerformanceFrequency(&x); return x; }();
    LARGE_INTEGER c; QueryPerformanceCounter(&c); return (double)c.QuadPart / (double)f.QuadPart;
}
bool gameFocused() { HWND fg = GetForegroundWindow(); DWORD pid = 0; if (fg) GetWindowThreadProcessId(fg, &pid); return pid == GetCurrentProcessId(); }
bool down(int vk) { return (GetAsyncKeyState(vk) & 0x8000) != 0; }

float g_fcPos[3] = {}; bool g_fcInit = false; double g_lastT = 0;

// m = Matrix34 as 12 floats (rows of 4). Returns true if it looks like a camera (orthonormal-ish basis).
bool sane(const float* m) {
    for (int i = 0; i < 12; ++i) if (!std::isfinite(m[i]) || fabsf(m[i]) > 1e7f) return false;
    float fl = sqrtf(m[1] * m[1] + m[5] * m[5] + m[9] * m[9]);
    float rl = sqrtf(m[0] * m[0] + m[4] * m[4] + m[8] * m[8]);
    return fl > 0.95f && fl < 1.05f && rl > 0.95f && rl < 1.05f;
}

void edit(float* m) {
    const float rx = m[0], ry = m[4], rz = m[8];       // right
    const float fx = m[1], fy = m[5], fz = m[9];       // forward
    float px = m[3], py = m[7], pz = m[11];
    if (freecam) {
        double t = nowSec(); float dt = (float)(t - g_lastT); g_lastT = t;
        if (!(dt > 0.f) || dt > 0.1f) dt = 0.016f;
        if (!g_fcInit) { g_fcPos[0] = px; g_fcPos[1] = py; g_fcPos[2] = pz; g_fcInit = true; }
        if (gameFocused()) {
            float v = fcSpeed * (down(VK_SHIFT) ? 4.f : 1.f) * dt;
            float f = (down('W') ? 1.f : 0.f) - (down('S') ? 1.f : 0.f);
            float r = (down('D') ? 1.f : 0.f) - (down('A') ? 1.f : 0.f);
            float u = (down(VK_SPACE) ? 1.f : 0.f) - (down(VK_CONTROL) ? 1.f : 0.f);
            g_fcPos[0] += (fx * f + rx * r) * v; g_fcPos[1] += (fy * f + ry * r) * v; g_fcPos[2] += (fz * f + rz * r + u) * v;
        }
        px = g_fcPos[0]; py = g_fcPos[1]; pz = g_fcPos[2];
    } else g_fcInit = false;
    if (thirdPerson) {
        px -= fx * tpDistance; py -= fy * tpDistance; pz -= fz * tpDistance - tpHeight;
    }
    m[3] = px; m[7] = py; m[11] = pz;
}

long long __fastcall hkSetCam(void* thisp, long long a2, long long a3) {
    ++diag.calls;
    float saved[12]; float* m = nullptr; bool did = false;
    if (thirdPerson || freecam) {
        __try {
            uintptr_t cam = *(volatile uintptr_t*)(a3 + 0x18);
            if (okPtr(cam)) {
                m = (float*)cam;
                if (sane(m)) { memcpy(saved, m, sizeof(saved)); edit(m); did = true; ++diag.applied; }
                else ++diag.rejected;
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) { did = false; }
    }
    long long r = oFn(thisp, a2, a3);
    if (did) { __try { memcpy(m, saved, sizeof(saved)); } __except (EXCEPTION_EXECUTE_HANDLER) {} }
    return r;
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
} // namespace

bool install() {
    if (g_hooked) return true;
    g_target = (uint8_t*)((uintptr_t)GetModuleHandleW(nullptr) + RVA_SetCam);
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
    uintptr_t det = (uintptr_t)&hkSetCam; memcpy(patch + 6, &det, 8);
    Frozen f; freeze(f);
    bool ok = writeCode(g_target, patch, kStolen);
    thaw(f);
    g_hooked = ok;
    diag.installResult = ok ? "hooked" : "patch failed";
    if (!ok) { VirtualFree(g_tramp, 0, MEM_RELEASE); g_tramp = nullptr; }
    return ok;
}

void uninstall() {
    thirdPerson = false; freecam = false;
    if (!g_hooked) return;
    Frozen f; freeze(f);
    writeCode(g_target, kExpected, kStolen);
    thaw(f);
    g_hooked = false;
    Sleep(300);
    if (g_tramp) { VirtualFree(g_tramp, 0, MEM_RELEASE); g_tramp = nullptr; }
}

bool blockKey(int vk) {
    if (!freecam) return false;
    return vk == 'W' || vk == 'A' || vk == 'S' || vk == 'D' || vk == VK_SPACE || vk == VK_CONTROL || vk == VK_LCONTROL || vk == VK_RCONTROL ||
           vk == VK_SHIFT || vk == VK_LSHIFT || vk == VK_RSHIFT;
}

void poll() {
    static bool tpDown = false, fcDown = false;
    bool focus = gameFocused();
    bool t = focus && tpKey >= 0 && tpKey < kKeyCount && down(kVk[tpKey]);
    bool f = focus && fcKey >= 0 && fcKey < kKeyCount && down(kVk[fcKey]);
    if (t && !tpDown) thirdPerson = !thirdPerson;
    if (f && !fcDown) freecam = !freecam;
    tpDown = t; fcDown = f;
}
}
