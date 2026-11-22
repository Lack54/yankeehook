#pragma once

// No recoil without touching any weapon data (so aiming down sights is unaffected).
// The six fire-mode shoot functions call two helpers after every shot: sub_1409901E0 (on the weapon's +0x150 sub-object) and
// sub_140990950 (+0x178 sub-object). They are the per-shot recoil/kick triggers and are called from nowhere else.
// With the option on, their first byte is replaced by `ret` so the kick is never started; off restores the original byte.
namespace norecoil {
struct Diag { const char* state = "off"; };
extern Diag diag;
void set(bool on);     // call every frame; patches/unpatches on change
void restore();
}
