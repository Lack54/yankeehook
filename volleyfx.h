#pragma once

// Muzzle flash / smoke at each volley position.
// The weapon's fire code (sub_1409901E0, called from every shoot function) picks a muzzle particle effect by camera distance and
// spawns it with sub_14028E0A0(particleManager, effect, const float pos[3], const float orient[4]). While a volley shot is being
// fired (begin()/end() around the shoot call, same thread) that spawn call gets the volley position instead of the gun's muzzle,
// so the smoke/flash appears along the line of bullets.
namespace volleyfx {
extern bool enabled;                                 // move the muzzle effect to the volley position
struct Diag { const char* installResult = "not tried"; volatile int spawns = 0, moved = 0; };
extern Diag diag;
bool install();
void uninstall();
void begin(const float pos[3], const float anchor[3]);   // pos = where this shot's effect goes; anchor = the aim origin (effects near it are muzzle effects)
void end();
}
