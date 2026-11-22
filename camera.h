#pragma once

// Active camera + local player position (read-only). See re_notes/13_offline_modding_leads.md.
namespace camera {
struct V3 { float x = 0, y = 0, z = 0; };
struct Cam {
    bool valid = false;
    V3 pos, right, fwd, up;   // world-space; CryEngine: +Y forward, +Z up
};
struct LocalPos { bool valid = false; V3 pos; };

Cam read();                 // camera world matrix from the game's camera struct
LocalPos readLocal();       // local player entity position

// Perspective projection. fovDeg is vertical unless fovIsHorizontal. Returns false if behind camera.
bool worldToScreen(const Cam& c, const V3& p, float screenW, float screenH,
                   float fovDeg, bool fovIsHorizontal, float& sx, float& sy);
}
