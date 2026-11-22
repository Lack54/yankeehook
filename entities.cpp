#include "entities.h"
#include <windows.h>
#include <cmath>
#include <cstring>

namespace entities {
// RVAs (VA - 0x140000000), from re_notes/09_players.md
static constexpr uintptr_t RVA_PlayerMap_count  = 0x1699040;  // u32
static constexpr uintptr_t RVA_PlayerMap_keys   = 0x1699048;  // u32*   (sorted EntityIds)
static constexpr uintptr_t RVA_PlayerMap_values = 0x1699050;  // Player_t**
static constexpr uintptr_t RVA_pLocalPlayer     = 0x15E94A0;

static constexpr uintptr_t PL_pEntity = 0x08, PL_state = 0x50, PL_flags2A = 0x2A,
                           PL_health = 0xA0, PL_maxHealth = 0xA4, PL_faction = 0x4AC;
static constexpr uintptr_t ENT_extFlags = 0x0C, ENT_name = 0x140, ENT_x = 0x4C, ENT_y = 0x5C, ENT_z = 0x6C;

template <class T> static bool rd(uintptr_t a, T& out) {
    __try { out = *(volatile T*)a; return true; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

static bool readStr(uintptr_t p, char* buf, size_t cap) {
    if (p < 0x10000) return false;
    __try {
        size_t i = 0;
        for (; i + 1 < cap; ++i) {
            char c = ((volatile char*)p)[i];
            if (!c) break;
            if ((unsigned char)c < 0x20) return false;
            buf[i] = c;
        }
        buf[i] = 0;
        return i > 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

// Stance tag: Player+0x528 -> vtbl[1](this) returns {count@+4, ptr@+8}; ptr[2] is the tag hash the game compares
// against 0xA5791FE6 (see PlayerMap_GetPlayerPos_candidate 0x1407f9160). Guarded call, run at scan rate only.
static uint32_t readStance(uintptr_t pl) {
    __try {
        uintptr_t si = *(volatile uintptr_t*)(pl + 0x528);
        if (si < 0x10000) return 0;
        uintptr_t vt = *(volatile uintptr_t*)si;
        if (vt < 0x10000) return 0;
        typedef uintptr_t(__fastcall* Fn)(uintptr_t);
        uintptr_t res = ((Fn)(*(volatile uintptr_t*)(vt + 8)))(si);
        if (res < 0x10000 || *(volatile uint32_t*)(res + 4) < 2) return 0;
        uintptr_t arr = *(volatile uintptr_t*)(res + 8);
        if (arr < 0x10000) return 0;
        return *(volatile uint32_t*)(arr + 8);
    } __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}

// Plain-old-data reads (no C++ objects) so SEH is safe.
struct Raw {
    uintptr_t ent;
    float x, y, z, hp, mhp;
    int faction;
    uint8_t state, f2a;
    uint32_t ext;
};

// fast fields: re-read every frame
static bool readFast(uintptr_t pl, uintptr_t ent, Raw& r) {
    r.ent = ent;
    if (!rd(ent + ENT_x, r.x) || !rd(ent + ENT_y, r.y) || !rd(ent + ENT_z, r.z)) return false;
    rd(pl + PL_state, r.state);
    rd(pl + PL_health, r.hp);
    rd(pl + PL_maxHealth, r.mhp);
    rd(pl + PL_faction, r.faction);
    return true;
}

static bool readSlow(uintptr_t pl, Raw& r, char* name, size_t cap) {
    memset(&r, 0, sizeof(r));
    if (pl < 0x10000) return false;
    uintptr_t ent = 0;
    if (!rd(pl + PL_pEntity, ent) || ent < 0x10000) return false;
    if (!readFast(pl, ent, r)) return false;
    rd(pl + PL_flags2A, r.f2a);
    rd(ent + ENT_extFlags, r.ext);
    name[0] = 0;
    uintptr_t np = 0;
    if (rd(ent + ENT_name, np)) readStr(np, name, cap);
    return true;
}

static bool readMapHeader(uintptr_t base, uint32_t& count, uintptr_t& keys, uintptr_t& vals, uintptr_t& local) {
    local = 0;
    if (!rd(base + RVA_PlayerMap_count, count) || !rd(base + RVA_PlayerMap_keys, keys) ||
        !rd(base + RVA_PlayerMap_values, vals)) return false;
    rd(base + RVA_pLocalPlayer, local);
    return true;
}

static bool readEntry(uintptr_t keys, uintptr_t vals, uint32_t i, uint32_t& id, uintptr_t& pl) {
    return rd(keys + i * 4, id) && rd(vals + (uintptr_t)i * 8, pl);
}

static Snapshot g_snap;
static ULONGLONG g_lastScan = 0;
const Snapshot& current() { return g_snap; }

static void scan() {
    Snapshot& out = g_snap;
    out.list.clear();
    out.mapReadable = false;
    out.localFaction = 0;
    out.haveLocal = false;
    uintptr_t base = (uintptr_t)GetModuleHandleW(nullptr);
    uint32_t count = 0; uintptr_t keys = 0, vals = 0, local = 0;
    if (!readMapHeader(base, count, keys, vals, local)) return;
    out.mapCount = count;
    if (count > 4096 || keys < 0x10000 || vals < 0x10000) return;
    out.mapReadable = true;

    out.list.reserve(count);
    for (uint32_t i = 0; i < count; ++i) {
        uint32_t id = 0; uintptr_t pl = 0;
        if (!readEntry(keys, vals, i, id, pl)) continue;
        Raw r; char name[64];
        if (!readSlow(pl, r, name, sizeof(name))) continue;
        Info e;
        e.id = id; e.player = pl; e.ent = r.ent; e.name = name;
        e.x = r.x; e.y = r.y; e.z = r.z;
        e.health = r.hp; e.maxHealth = r.mhp; e.faction = r.faction; e.state = r.state;
        e.extFlags = r.ext;
        e.isLocal = (r.ext & 0x400) != 0 || pl == local;
        e.isDummyFlag = (r.f2a & 0x10) != 0;
        e.stance = readStance(pl);
        out.list.push_back(std::move(e));
    }
}

// Per-frame: refresh the volatile fields through cached pointers and recompute distances.
static void fast() {
    Snapshot& s = g_snap;
    s.haveLocal = false;
    float lx = 0, ly = 0, lz = 0;
    for (auto& e : s.list) {
        Raw r;
        if (!readFast(e.player, e.ent, r)) continue;
        e.x = r.x; e.y = r.y; e.z = r.z;
        e.health = r.hp; e.maxHealth = r.mhp; e.faction = r.faction; e.state = r.state;
        if (e.isLocal) { lx = e.x; ly = e.y; lz = e.z; s.haveLocal = true; s.localFaction = e.faction; }
    }
    for (auto& e : s.list) {
        if (!s.haveLocal) { e.dist = 0; continue; }
        float dx = e.x - lx, dy = e.y - ly, dz = e.z - lz;
        e.dist = sqrtf(dx * dx + dy * dy + dz * dz);
    }
}

void update(bool forceScan) {
    ULONGLONG now = GetTickCount64();
    if (forceScan || now - g_lastScan >= 250) { g_lastScan = now; scan(); }
    fast();
}
}
