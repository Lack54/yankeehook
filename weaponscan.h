#pragma once
#include <atomic>
#include <cstdint>

// Scales the game's fire-mode parameter blocks and reload-time tables.
// Layouts come from FireMode_LoadParams 0x1409A7550 / FireMode_LoadReloadTimings 0x1409A69A0 (re_notes/12_weapons.md):
//   FireModeParams: +0x14 float speed_override, +0x18 int damage, +0x1C float falloff, +0x20 int horse_damage,
//                   +0x38 u16 rate, +0x3E u16 clip_size, +0x40 u16 max_clips, +0x58 float suppression, +0x60 float penetration cap,
//                   +0x88 u8 hit type id.
//   Reload table:   16 floats; [0..7] standing steps, [8..15] kneeling steps (unused = -1.0).
// Held-weapon lookup: heldId = *(u32*)(player + 0x64 + 4 * *(u8*)(player + 0x61)); item objects are in the sorted id map
// (count 0x1415CFFA0, keys 0x1415CFFA8, values 0x1415CFFB0); the item's weapon parameters are at item + 0x288.
// Weapon data is shared per weapon type, so bots holding the same weapon type are affected too.
namespace weaponscan {
extern std::atomic<int>  fireModes;      // found by the global scan
extern std::atomic<int>  reloadTables;
extern std::atomic<bool> scanning;
extern std::atomic<int>  scanPercent;

extern std::atomic<int>      heldFire;    // found in the held weapon
extern std::atomic<int>      heldReload;
extern std::atomic<uint32_t> heldItemId;  // 0 = none

extern bool  noDrop;            // zero the gravity of the held weapon's bullets (physics block of its ammo)
extern std::atomic<int> heldDrop;   // gravity fields found for the held ammo
extern bool  gunMods;           // master switch: no sway, no drop, fast ADS, instant reload, infinite ammo/clip, no suppression/head bob, value multipliers
extern bool  fastAds;           // aim-down-sights in/out times cut to ~8%
extern bool  noRecoil;          // zero the held weapon's recoil blocks (recoil + procedural recoil)
extern std::atomic<int> heldRecoil; // recoil blocks found in the held weapon
extern bool  noSway;           // zero the held weapon's aim sway, steadying time and camera shake (zoom block)
extern std::atomic<int> heldSway;   // zoom blocks found in the held weapon
extern float suppressionMul;  // scales the suppression your shots cause (fire-mode suppressionAmount, ammo suppressionStrength, bullet whiz)
extern float damageMul;       // 1 = unchanged
extern float speedMul;
extern float reloadMul;       // <1 = faster reload
extern bool  apply;
extern bool  instantReload;   // reload steps collapse to ~0 s (independent of the Apply toggle)
extern bool  heldOnly;        // true: only the held weapon's data; false: everything the global scan found

uintptr_t heldItemPtr();      // item object of the held weapon (0 if unknown)
float heldBulletSpeed();      // m/s from the held weapon's fire mode (0 if unknown)
void startScan();             // global scan, background thread
void tick();                  // once per frame
void restore();
}
