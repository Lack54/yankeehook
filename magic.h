#pragma once

// Magic bullet: redirects the bullets you fire at the best bot target, with lead and drop compensation, no spread.
// Hook: CBullet_SetPhase (0x140982C20). Phase 1 starts the flight and copies the bullet's stored direction (floats at
// bullet + 0x7C / +0x80 / +0x84) into the particle's physics heading. For a bullet whose shooter (bullet + 0xA0, an entity id) is
// the local player, those three floats are replaced by the direction from the bullet to the predicted target point.
namespace magic {
struct Diag {
    const char* installResult = "not tried";
    volatile int phase1 = 0;         // flights started (all shooters)
    volatile int mine = 0;           // of those: yours
    volatile int redirected = 0;     // yours that were redirected
    volatile int noTarget = 0;       // yours with no usable target
    const char* shootInstall = "not tried";
    volatile int queued = 0, magicFired = 0, magicFailed = 0;   // shots queued from your real bullets / fired / refused
    volatile int shootCalls = 0, shootLocal = 0, shootRedirected = 0;   // shoot-hook counters (all / yours / redirected)
    float lastDotWithOld = 0;        // cosine between the original and the new direction (how big the correction was)
};
extern Diag diag;
// Volley line: while a volley runs, each of your bullets gets its start position shifted sideways (camera right axis) so the
// bullets leave from a row of positions instead of one muzzle. Test feature: it rewrites the bullet entity's position just
// before its flight starts.
extern volatile int pendingShots;                       // magic shots waiting for the weapon's next update
extern volatile unsigned long long magicIgnoreUntil;    // GetTickCount64() until which new local bullets are not turned into magic shots
extern bool  volleyDamage;        // clear the 'predicted' bullet flag so volley bullets can deal damage
extern bool  lineOn;
extern float lineSpacing;          // metres between neighbours
extern int   lineCount;            // positions in the row
extern volatile unsigned long long lineUntil;   // GetTickCount64() deadline, refreshed by each volley shot
extern volatile int lineApplied;
bool install();      // verifies the first 16 bytes of the function; false if the game version differs
void uninstall();
}
