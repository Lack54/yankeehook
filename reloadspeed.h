#pragma once

// Instant reload for the local player.
//
// sub_1409B04D0 (0x1409B04D0) is the weapon's StartReload(weapon, step):
//   * the reload duration comes from a 16-float table: table = *(*(weapon + 0x10) + 0x40); step index at weapon + 0x1A0;
//     standing times at table[0..7], kneeling at table[8..15];
//   * the completion is scheduled as a delayed event after that duration;
//   * the reload animation is a Mannequin action stored at *(weapon + 8) + 0x1A0 (its +0x54 field starts at 1.0f).
// The detour (local player's weapons only; owner id is *(u32*)(*(weapon + 8) + 0xD8), compared with g_LocalPlayerEntityId
// 0x1415E94F0) shrinks that table before the original runs and raises the action's speed field afterwards.
namespace reloadspeed {
extern bool  enabled;       // instant reload on/off
extern float speed;         // playback multiplier for the reload action
extern float timeScale;     // table scale (reload duration multiplier), e.g. 0.02
extern bool  infiniteClip;  // keep the loaded rounds full (nothing is ever used up, so no reload is needed)
extern bool  infiniteAmmo;  // keep the reserve ("pocket") ammo of your weapons topped up
extern bool  autoReload;    // start a reload by itself when the clip is empty (no R press)
extern bool  secondR;       // after a forced reload, press R once more through the game's own handler (what the manual 2nd R does)
extern bool  clearFlags;    // alternative: just clear the busy flags the R handler sets
extern bool  probe;         // write a log of what changes in memory during one reload (reload debugging)
extern bool  forceFinish;   // experimental: run the reload-complete calls right after the reload starts
// Volley: fires the held weapon's own shoot function N times (one or a few per game tick) in the direction you aim.
extern volatile int volleyLeft;    // shots still to fire
extern int  volleyPerTick;         // shots per weapon update
extern int  volleyCount, volleyKey, volleyTotal;   // bullets per volley, hotkey index (aim key list)
extern bool volleyHot;
extern int  volleyDelayMs, volleyJitterMs;   // gap between shots (ms) and random extra gap
extern bool volleyRipple;
extern bool volleyLockAim;                   // keep the aim from the moment the volley started
extern bool fireHot; extern int fireKey, fireMs;   // hold-to-fire hotkey (hipfire)                    // fire the row in order (ripple) instead of random order
void volleyStart(int n);
void volleyPoll();                 // once per frame: hotkey edge -> volleyLeft (works with the menu closed)
struct VolleyDiag { volatile int failed = 0, failRun = 0, lastRet = -1, canShoot = -1; volatile unsigned exCode = 0, exRva = 0; volatile int exCount = 0; volatile int fired = 0, rejected = 0, noMatch = 0, entered = 0, badEnt = 0, notMine = 0; volatile unsigned shootRva = 0, owner = 0, local = 0; volatile unsigned long long ent = 0, held = 0; };
extern VolleyDiag volley;
void poll();                // once per frame: measures how long the weapon stays "reloading"
bool install();             // verifies the function prologue first; false if it does not match
void uninstall();
void restoreTables();       // put the original reload times back

struct Diag {
    const char* installResult = "not tried";
    volatile int calls = 0, localHits = 0, tablesScaled = 0, boosted = 0;
    volatile unsigned long long ownerId = 0, localId = 0, tablePtr = 0;
    float tableOrig[16] = {};              // last table, before scaling
    float tableNow[16] = {};               // after scaling
    unsigned char actDump[0x60] = {};      // reload action object after the original ran
    volatile unsigned long long weapon = 0, startTick = 0;
    volatile int busyMs = -1;              // time until the weapon's reloading flag (+0x7F) cleared; -1 = still reloading / unknown
    volatile int forced = 0;               // times force-finish ran
    char probeStatus[96] = "probe off";
    volatile int autoReloads = 0;
    volatile int secondRCount = 0, secondRBusy = -1, secondRKey = -1;   // automatic second R presses and what they saw
    volatile int clipBefore = -99, clipAfter = -99, clipCap = -99;   // loaded rounds around the forced completion
    volatile float timerMax = 0, timerNow = 0;
    volatile int entGet928 = -99, entGet928After = -99, wpn56After = -99;   // clip as read by the entity getter / the weapon's vfn[7]
    // live "can I shoot" gates of the last local weapon (CanShoot is vfn +168 = sub_1409AF790)
    struct Gate {
        volatile int canShoot = -1, b127 = -1, b418 = -1, e121 = -1, e120 = -1, v72 = -1, cap = -99, nComp = 0;
        volatile float timer = 0;
        volatile int compRes[6] = { -1, -1, -1, -1, -1, -1 };
        volatile unsigned compVt[6] = {};
    } gate;
    unsigned rva[10] = {};   // RVAs of: weapon vfn +56 +64 +168 +416 +520 and entity vfn +928 +936 +944 +952, weapon vtable                       // weapon +0x88 (fire/reload countdown) after the reload start
};
extern Diag diag;
}
