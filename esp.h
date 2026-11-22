#pragma once
#include <map>
#include <cstdint>

namespace esp {
struct Settings {
    bool  enabled = true;
    bool  box = true, name = false, health = false, distance = false, snaplines = false;   // default look: boxes only
    bool  showDead = false;
    bool  teamCheck = true;              // on: enemies only, off: everyone
    bool  teamColors = true;             // (unused: boxes are always coloured by enemy / teammate)
    float colEnemy[4] = { 1.00f, 0.25f, 0.25f, 1.0f };   // box colour for enemies
    float colTeam[4]  = { 0.30f, 1.00f, 0.40f, 1.0f };   // box colour for teammates (visible when team check is off)
    float maxDistance = 500.f;
    float boxHeight = 1.8f;              // meters, standing
    float lowHeight = 0.9f;              // meters, any non-standing stance (crouch/prone)
    float fov = 70.f;                    // projection FOV (vertical) used when the FOV changer is off
    float thickness = 1.5f;
};
extern Settings s;

// Per-stance box height (meters), keyed by stance tag hash. New hashes get a default the first time they are seen:
// the most common stance among living players = standing height, anything else = lowHeight. Editable in the menu.
struct StanceEntry { float height; int liveCount; };
std::map<uint32_t, StanceEntry>& stanceTable();
float boxHeightFor(uint32_t stance, unsigned char state);   // box height used for a player (stance table aware)
int  botsOnScreenCount();   // living non-local players on screen within ~200 m (drives chams learning)
bool anyBotOnScreen();   // any living non-local player on screen within ~250 m (drives chams learning)
void draw();   // draws into the foreground list; call every frame even if menu is closed
}
