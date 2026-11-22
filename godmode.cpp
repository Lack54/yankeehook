#include "godmode.h"
#include <windows.h>
#include <cmath>

namespace godmode {
bool enabled = false;

static constexpr uintptr_t RVA_pLocalPlayer = 0x15E94A0;
static constexpr uintptr_t PL_state = 0x50, PL_health = 0xA0, PL_maxHealth = 0xA4, PL_invuln = 0x4A4;

static uintptr_t g_player = 0;
static unsigned char g_orig = 0;
static bool g_applied = false;

static uintptr_t localPlayer() {
    __try {
        uintptr_t p = *(volatile uintptr_t*)((uintptr_t)GetModuleHandleW(nullptr) + RVA_pLocalPlayer);
        return p > 0x10000 ? p : 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}

void restore() {
    if (!g_applied) return;
    __try { if (g_player) *(volatile unsigned char*)(g_player + PL_invuln) = g_orig; } __except (EXCEPTION_EXECUTE_HANDLER) {}
    g_applied = false;
}

void tick() {
    if (!enabled) { restore(); return; }
    uintptr_t p = localPlayer();
    if (!p) return;
    __try {
        if (g_applied && p != g_player) g_applied = false;           // respawned: a new player object
        if (!g_applied) { g_player = p; g_orig = *(volatile unsigned char*)(p + PL_invuln); g_applied = true; }
        *(volatile unsigned char*)(p + PL_invuln) = 1;
        // backup: keep health full while alive
        if (*(volatile unsigned char*)(p + PL_state) == 1) {
            float mx = *(volatile float*)(p + PL_maxHealth);
            if (std::isfinite(mx) && mx > 0.f && mx < 100000.f) *(volatile float*)(p + PL_health) = mx;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
}
}
