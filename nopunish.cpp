#include "nopunish.h"
#include <windows.h>

namespace nopunish {
bool enabled = false;

namespace {
constexpr uintptr_t RVA_BansEnabled = 0x15CFD80, RVA_Threshold = 0x15CFD84;
bool g_applied = false;
int   g_origBans = 1;
float g_origThr = 0.f;

bool rd(uintptr_t a, void* out, size_t n) { __try { memcpy(out, (const void*)a, n); return true; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; } }
bool wr(uintptr_t a, const void* in, size_t n) { __try { memcpy((void*)a, in, n); return true; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; } }
}

void tick() {
    const uintptr_t base = (uintptr_t)GetModuleHandleW(nullptr);
    if (enabled) {
        if (!g_applied) {
            if (!rd(base + RVA_BansEnabled, &g_origBans, 4) || !rd(base + RVA_Threshold, &g_origThr, 4)) return;
            g_applied = true;
        }
        int zero = 0; float big = 999999.f;
        wr(base + RVA_BansEnabled, &zero, 4);
        wr(base + RVA_Threshold, &big, 4);
    } else if (g_applied) {
        restore();
    }
}

void restore() {
    if (!g_applied) return;
    const uintptr_t base = (uintptr_t)GetModuleHandleW(nullptr);
    wr(base + RVA_BansEnabled, &g_origBans, 4);
    wr(base + RVA_Threshold, &g_origThr, 4);
    g_applied = false;
}
}
