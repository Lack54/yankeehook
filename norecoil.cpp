#include "norecoil.h"
#include <windows.h>

namespace norecoil {
Diag diag;
namespace {
struct Site { uintptr_t rva; unsigned char orig; };
Site g_sites[2] = { { 0x9901E0, 0x48 }, { 0x990950, 0x48 } };   // both start with 48 (mov [rsp+..],rbx / mov rax,rsp)
bool g_on = false;

bool patchByte(Site& s, unsigned char v) {
    unsigned char* p = (unsigned char*)((uintptr_t)GetModuleHandleW(nullptr) + s.rva);
    DWORD old;
    if (!VirtualProtect(p, 1, PAGE_EXECUTE_READWRITE, &old)) return false;
    unsigned char cur = *(volatile unsigned char*)p;
    bool ok = true;
    if (v == 0xC3) { if (cur == s.orig) *(volatile unsigned char*)p = 0xC3; else if (cur != 0xC3) ok = false; }
    else { if (cur == 0xC3) *(volatile unsigned char*)p = s.orig; }
    VirtualProtect(p, 1, old, &old);
    FlushInstructionCache(GetCurrentProcess(), p, 1);
    return ok;
}
}

void set(bool on) {
    if (on == g_on) return;
    bool ok = true;
    for (auto& s : g_sites) ok &= patchByte(s, on ? 0xC3 : s.orig);
    g_on = on;
    diag.state = on ? (ok ? "kick calls disabled" : "patch failed (bytes differ)") : "off";
}
void restore() { for (auto& s : g_sites) patchByte(s, s.orig); g_on = false; diag.state = "off"; }
}
