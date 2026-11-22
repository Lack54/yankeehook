#include "weapon.h"
#include <windows.h>

namespace weapon {
static constexpr uintptr_t RVA_Holder = 0x15CF820;   // game CVar holder (static)
static constexpr uintptr_t OFF_SupInFormation = 0x1C4, OFF_SupSkirmishing = 0x1C8, OFF_SupOutOfLine = 0x1CC, OFF_ReloadCancel = 0x1A8;

bool  noSuppression = false;
bool  noHeadBob = false;
bool  noRespawnTimer = false;
bool  reloadCancel = false;
float reloadCancelValue = 0.1f;

static float g_orig[4];

static bool rd(uintptr_t off, float& v);   // defined below
static bool wr(uintptr_t off, float v);

// Game.Player.Headbob.{Idle,QuickTime,DoubleQuick,Charge,Water}.{Width,Height} (holder offsets from re_notes/05b_cvars_full.md)
static const uintptr_t kBobOff[10] = { 0x298, 0x29C, 0x2A4, 0x2A8, 0x2B0, 0x2B4, 0x2BC, 0x2C0, 0x2C8, 0x2CC };
static float g_bobOrig[10];
static bool  g_bobApplied = false;
// Deployment.Flag.IntervalDefault (holder+0x30, default 5.0) and Deployment.Flag.IntervalDelayed (holder+0x34, default 10.0):
// the registration calls (0x14082de10 / 0x14082de53) store to rdi+0x30 / rdi+0x34.
static float g_depOrig[2];
static bool  g_depApplied = false;
bool deployIntervals(float& a, float& b) { return rd(0x30, a) && rd(0x34, b); }
static void restoreDeploy() {
    if (!g_depApplied) return;
    wr(0x30, g_depOrig[0]); wr(0x34, g_depOrig[1]);
    g_depApplied = false;
}
static void tickDeploy() {
    if (noRespawnTimer) {
        if (!g_depApplied) {
            float a, b;
            if (!rd(0x30, a) || !rd(0x34, b) || !(a >= 0.f && a < 600.f && b >= 0.f && b < 600.f)) return;   // not what we expect: don't touch
            g_depOrig[0] = a; g_depOrig[1] = b; g_depApplied = true;
        }
        wr(0x30, 0.01f); wr(0x34, 0.01f);
    } else restoreDeploy();
}

static void restoreBob() {
    if (!g_bobApplied) return;
    for (int i = 0; i < 10; ++i) wr(kBobOff[i], g_bobOrig[i]);
    g_bobApplied = false;
}
static void tickBob() {
    if (noHeadBob) {
        if (!g_bobApplied) {
            for (int i = 0; i < 10; ++i) { float v; if (!rd(kBobOff[i], v) || !(v >= 0.f && v < 5.f)) return; g_bobOrig[i] = v; }
            g_bobApplied = true;
        }
        for (int i = 0; i < 10; ++i) wr(kBobOff[i], 0.f);
    } else restoreBob();
}
static bool  g_supApplied = false, g_cancelApplied = false;

static float* slot(uintptr_t off) { return (float*)((uintptr_t)GetModuleHandleW(nullptr) + RVA_Holder + off); }
static bool rd(uintptr_t off, float& v) { __try { v = *(volatile float*)slot(off); return true; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; } }
static bool wr(uintptr_t off, float v) { __try { *(volatile float*)slot(off) = v; return true; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; } }

bool read(float& a, float& b, float& c, float& d) {
    return rd(OFF_SupInFormation, a) && rd(OFF_SupSkirmishing, b) && rd(OFF_SupOutOfLine, c) && rd(OFF_ReloadCancel, d);
}

static bool sane(float v) { return v >= 0.f && v <= 1.5f; }

void tick() {
    tickBob();
    tickDeploy();
    if (noSuppression) {
        if (!g_supApplied) {
            float a, b, c;
            if (!rd(OFF_SupInFormation, a) || !rd(OFF_SupSkirmishing, b) || !rd(OFF_SupOutOfLine, c)) return;
            if (!sane(a) || !sane(b) || !sane(c)) return;   // not what we expect: don't touch
            g_orig[0] = a; g_orig[1] = b; g_orig[2] = c; g_supApplied = true;
        }
        wr(OFF_SupInFormation, 0.f); wr(OFF_SupSkirmishing, 0.f); wr(OFF_SupOutOfLine, 0.f);
    } else if (g_supApplied) {
        wr(OFF_SupInFormation, g_orig[0]); wr(OFF_SupSkirmishing, g_orig[1]); wr(OFF_SupOutOfLine, g_orig[2]);
        g_supApplied = false;
    }

    if (reloadCancel) {
        if (!g_cancelApplied) {
            float d;
            if (!rd(OFF_ReloadCancel, d) || !sane(d)) return;
            g_orig[3] = d; g_cancelApplied = true;
        }
        wr(OFF_ReloadCancel, reloadCancelValue);
    } else if (g_cancelApplied) {
        wr(OFF_ReloadCancel, g_orig[3]);
        g_cancelApplied = false;
    }
}

void restore() {
    restoreBob();
    restoreDeploy();
    if (g_supApplied) { wr(OFF_SupInFormation, g_orig[0]); wr(OFF_SupSkirmishing, g_orig[1]); wr(OFF_SupOutOfLine, g_orig[2]); g_supApplied = false; }
    if (g_cancelApplied) { wr(OFF_ReloadCancel, g_orig[3]); g_cancelApplied = false; }
}
}
