#pragma once

// Third-person and freecam. Hook: sub_1403CDAC0 (RVA 0x3CDAC0), the per-frame "set view camera" routine. Its third argument holds a
// pointer (at +0x18) to the frame's CCamera (Matrix34 at +0: rows of right/forward/up/translation columns). The detour edits that
// matrix before the original copies it into the camera object (this + 0x6C0) and the renderer, then puts the original values back.
namespace camhook {
extern bool  thirdPerson;
extern float tpDistance;       // metres behind the head
extern float tpHeight;         // metres above
extern bool  freecam;
extern float fcSpeed;          // m/s
extern int   tpKey, fcKey;     // index into kKeyNames (F1..F12, Insert, Home, End)
extern const char* const kKeyNames[];
extern const int kKeyCount;
struct Diag { const char* installResult = "not tried"; volatile int calls = 0, applied = 0, rejected = 0; };
extern Diag diag;
bool install();
void uninstall();
bool blockKey(int vk);          // true while freecam is on for the keys it uses (the game must not see them go down)
void poll();                    // once per frame: hotkeys
}
