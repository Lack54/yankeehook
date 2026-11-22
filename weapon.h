#pragma once

// Weapon-side tweaks that are reachable through writable game CVars (holder 0x1415CF820, see re_notes/05b_cvars_full.md).
namespace weapon {
extern bool  noRespawnTimer;      // set the flag-deployment intervals (the deploy countdown) to ~0
bool  deployIntervals(float& a, float& b);
extern bool  noHeadBob;          // zero the Game.Player.Headbob.* widths and heights
extern bool  noSuppression;      // zero the three MaxSuppression* values
extern bool  reloadCancel;       // lower Game.Player.ReloadFractionAutoCancel
extern float reloadCancelValue;  // 0..1
bool read(float& maxInFormation, float& maxSkirmishing, float& maxOutOfLine, float& cancelFrac);
void tick();                     // once per frame
void restore();                  // put the original values back
}
