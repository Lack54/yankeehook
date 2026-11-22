#pragma once
#include <vector>
#include <string>
#include <cstdint>

// Reads the game's player map (see re_notes/09_players.md). Read-only.
// Two-stage cache: a slow scan of the player map (list of players, names, flags) a few times
// per second, plus a cheap per-frame refresh of position/health/state through the cached pointers,
// so ESP boxes move in step with the camera instead of at the scan rate.
namespace entities {
struct Info {
    uint32_t id = 0;
    uintptr_t player = 0;
    uintptr_t ent = 0;        // CEntity*
    std::string name;
    float x = 0, y = 0, z = 0;
    float health = 0, maxHealth = 0;
    int faction = 0;          // 1=USA 2=CSA
    uint8_t state = 0;        // 1=alive 2=dead
    bool isLocal = false;     // entity extFlags & 0x400
    bool isDummyFlag = false; // Player+0x2A & 0x10 (flag used by DummyPlayers)
    uint32_t extFlags = 0;
    uint32_t stance = 0;      // stance tag hash (Player+0x528 info, dword #2); 0 = unknown
    float dist = 0;           // from local player
};

struct Snapshot {
    std::vector<Info> list;
    uint32_t mapCount = 0;
    bool mapReadable = false;
    int localFaction = 0;     // 0 = unknown
    bool haveLocal = false;
};

// Shared cache. Call update() exactly once per frame.
const Snapshot& current();
void update(bool forceScan = false);
}
