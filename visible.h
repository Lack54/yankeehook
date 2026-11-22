#pragma once

// Line-of-sight test through the game's own physics ray wrapper (sub_14076F1D0, RayWorldIntersection(Physics)).
// Ray = static world + terrain only (objtypes 0x101, flags 0x0F, one hit), the same setup the game uses for its own
// world-only rays, so bots/players never block the ray; only walls, buildings, trees and terrain do.
namespace visible {
struct Diag { volatile int rays = 0; volatile int blocked = 0; volatile int faults = 0; bool disabled = false; };
extern Diag diag;
// true = nothing solid between the two points. On any fault the check disables itself and reports "visible" (never blocks).
bool lineClear(float ox, float oy, float oz, float tx, float ty, float tz);
}
