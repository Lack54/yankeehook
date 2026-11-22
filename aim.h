#pragma once

// Aim assist for bots (offline). Moves the mouse toward the best enemy while a key is held, leading moving targets and
// compensating bullet drop.
//
// Ballistics (from REA/IDA of CBullet_SetPhase 0x140982C20 and the fire function 0x1409AD620): a bullet is launched along the
// aim direction at the fire mode's speed_override (FireModeParams+0x14, ammo default 289 m/s) and then flies under gravity as a
// CryPhysics particle. Bullet speed is read live from the held weapon; gravity is measured from observed bullet trails
// (tracers::measured) because the XML value (-19.62, "doubled for half gravity") is not trustworthy as an effective value.
namespace aim {
struct Settings {
    bool  enabled = false;
    int   key = 0;                // index into the key list in the menu
    float fovPx = 180.f;          // only targets within this many pixels of the crosshair
    float strength = 0.35f;       // mouse counts per pixel of error, scaled (see aim.cpp)
    float maxStep = 40.f;         // max mouse counts per frame
    float aimHeight = 0.88f;      // fraction of the box height to aim at (0.9 = head/neck)
    float maxDistance = 300.f;
    bool  predict = true;         // lead moving targets
    bool  drop = true;            // compensate bullet drop
    bool  useMeasured = true;     // use gravity measured from tracers when available
    float gravity = 9.81f;        // manual gravity (m/s^2) when not measured
    bool  drawFov = true;
    bool  visibleOnly = true;     // only lock to bots with a clear line of sight (no walls/terrain between)
    bool  magic = false;          // magic bullet: your shots are redirected at the best bot in the magic FOV
    float magicFovPx = 300.f;
    float magicSpawnDist = 1.0f;  // magic bullet starts this many metres in front of the target (on the line from you)     // pixels from the crosshair
};
extern Settings s;

struct Stats {
    bool  active = false;         // key held and a target selected
    bool  haveTarget = false;
    float bulletSpeed = 0;        // m/s in use
    float gravityUsed = 0;        // m/s^2 in use
    float flightTime = 0;         // s to the current target
    float targetDist = 0;
    int   moves = 0;              // mouse moves sent
};

extern const char* const kKeyNames[];
extern const int kKeyCount;
void update();     // once per frame, after entities::update
void draw();       // FOV circle and predicted aim point
Stats stats();
// Called from the game thread (bullet hook). Direction from origin to the published magic target, with lead and drop compensation.
bool magicTarget(float out[3]);   // the current magic target's aim point (lead applied), world space
bool magicDirection(float ox, float oy, float oz, float v0, float out[3]);
}
