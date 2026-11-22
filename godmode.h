#pragma once

// God mode for the local player.
// CPlayer_ApplyDamage (0x14096C450) returns without applying damage when Player+0x4A4 (byte) is non-zero, and the explosion
// path in CBullet_ApplyHit only kills when it is zero (re_notes/14_materials.md). Health (+0xA0, max +0xA4) is also kept full
// while alive as a backup for any damage path that doesn't test the flag.
namespace godmode {
extern bool enabled;
void tick();      // once per frame
void restore();   // put the original flag back
}
