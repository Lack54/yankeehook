#include "fov.h"
#include <windows.h>

namespace fov {
static constexpr uintptr_t RVA_cl_fov = 0x15CF858;

bool  enabled = false;
bool  pauseWhileAiming = true;
float value = 90.f;
static float g_original = 0.f;
static bool  g_haveOriginal = false;
static bool  g_applied = false;

static bool rdf(float& out) {
    __try { out = *(volatile float*)((uintptr_t)GetModuleHandleW(nullptr) + RVA_cl_fov); return true; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
static bool wrf(float v) {
    __try { *(volatile float*)((uintptr_t)GetModuleHandleW(nullptr) + RVA_cl_fov) = v; return true; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

bool read(float& out) { return rdf(out); }

void tick() {
    if (enabled) {
        float cur;
        if (!rdf(cur)) return;
        if (!g_applied) {
            if (cur < 5.f || cur > 170.f) return;   // not a sane FOV: don't touch
            g_original = cur; g_haveOriginal = true; g_applied = true;
        }
        // While aiming the game moves cl_fov to its own zoom value. Leave that alone, otherwise our value fights the zoom.
        if (pauseWhileAiming) {
            float d1 = cur - g_original, d2 = cur - value;
            if ((d1 > 0.05f || d1 < -0.05f) && (d2 > 0.05f || d2 < -0.05f)) return;
        }
        wrf(value);
    } else if (g_applied) {
        restore();
    }
}

void restore() {
    if (g_applied && g_haveOriginal) wrf(g_original);
    g_applied = false;
}

float effective(float base) {
    return (enabled && g_applied) ? value : base;
}
}
