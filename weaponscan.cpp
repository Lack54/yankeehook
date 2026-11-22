#include "weaponscan.h"
#include "buildflags.h"
#include <windows.h>
#include <vector>
#include <cmath>
#include <cstring>

namespace weaponscan {
std::atomic<int>  fireModes{ 0 };
std::atomic<int>  reloadTables{ 0 };
std::atomic<bool> scanning{ false };
std::atomic<int>  scanPercent{ 0 };
std::atomic<int>      heldFire{ 0 };
std::atomic<int>      heldReload{ 0 };
std::atomic<int>      heldSway{ 0 };
std::atomic<int>      heldRecoil{ 0 };
std::atomic<int>      heldDrop{ 0 };
std::atomic<uint32_t> heldItemId{ 0 };

float damageMul = 1.f, speedMul = 1.f, reloadMul = 1.f, suppressionMul = 1.f;
bool  apply = false;
bool  instantReload = false;
bool  heldOnly = true;
bool  noSway = false;
bool  noRecoil = false;
bool  noDrop = false;
bool  fastAds = false;
bool  gunMods = false;

namespace {
constexpr uintptr_t RVA_pLocalPlayer = 0x15E94A0;
constexpr uintptr_t RVA_ItemMapCount = 0x15CFFA0, RVA_ItemMapKeys = 0x15CFFA8, RVA_ItemMapVals = 0x15CFFB0;

struct FireRec { uintptr_t addr; int origDamage; float origSpeed; int lastDamage; float lastSpeed; bool wrote; };
struct ReloadRec { uintptr_t addr; float orig[16]; bool wrote; };
struct FloatRec { uintptr_t addr; float orig; bool wrote; };   // a single suppression float
struct SwayRec { uintptr_t addr; float orig; float target; bool wrote; };
struct DropRec { uintptr_t addr; int origBits; bool wrote; };    // raw bits: the original is a NaN "unset" marker
struct Set { std::vector<DropRec> drop; std::vector<FireRec> fire; std::vector<ReloadRec> rel; std::vector<FloatRec> flt; std::vector<SwayRec> sway; std::vector<SwayRec> recoil; std::vector<SwayRec> ads; };

SRWLOCK g_lock = SRWLOCK_INIT;
Set g_global, g_held;
uintptr_t g_heldItem = 0;

bool okPtr(uintptr_t v) { return v > 0x10000 && v < 0x00007FFFFFFF0000ull; }
bool okF(float f, float lo, float hi) { return std::isfinite(f) && f >= lo && f <= hi; }

// p points at a candidate FireModeParams (>= 0x90 readable bytes)
bool looksLikeFireMode(const uint8_t* p) {
    uint8_t hit = p[0x88];
    if (hit < 1 || hit > 13 || p[0x89] > 13) return false;
    int dmg = *(const int*)(p + 0x18);
    if (dmg < 1 || dmg > 1000000) return false;
    if (!okF(*(const float*)(p + 0x14), 0.f, 2500.f)) return false;
    if (!okF(*(const float*)(p + 0x1C), 0.f, 1000.f)) return false;
    uint16_t rate = *(const uint16_t*)(p + 0x38), clip = *(const uint16_t*)(p + 0x3E), maxClips = *(const uint16_t*)(p + 0x40);
    if (rate < 1 || rate > 3000 || clip < 1 || clip > 500 || maxClips > 1000) return false;
    if (!okF(*(const float*)(p + 0x58), 0.f, 1000.f)) return false;
    if (!okPtr(*(const uintptr_t*)(p + 0x00)) || !okPtr(*(const uintptr_t*)(p + 0x28)) || !okPtr(*(const uintptr_t*)(p + 0x30))) return false;
    return true;
}

// 16 floats: [0..7] standing steps, [8..15] kneeling steps. An unused slot is -1.0. Step 0 is 0 by default but the XML may
// set any value, so a slot is either -1.0 or a time in seconds; used slots rise, and once a half hits -1.0 it stays -1.0.
bool halfOk(const float* f, int& used) {
    used = 0; float prev = -0.001f; bool ended = false;
    for (int i = 0; i < 8; ++i) {
        if (f[i] == -1.f) { ended = true; continue; }
        if (ended || !okF(f[i], 0.f, 120.f) || f[i] <= prev) return false;
        prev = f[i]; ++used;
    }
    return true;
}
bool looksLikeReload(const float* f) {
    int a = 0, b = 0;
    if (!halfOk(f, a) || !halfOk(f + 8, b)) return false;
    if (a < 2 || b < 1) return false;                 // a real reload has several standing steps and at least one kneeling
    return f[a - 1] >= 0.2f;                          // the last standing step is the reload length
}

// The weapon's zoom block (item XML <zoom>, loaders sub_140813A80 / sub_140817AA0). Layout from the loaders:
//   +0x18 zoom_in_time, +0x1C zoom_out_time, +0x28 cameraShakeMultiplier, +0x2C zoom_fov (radians),
//   +0x40 zoomSway maxX, +0x44 maxY, +0x48 stabilizeTime, +0x4C destabilizeTime, +0x50 minScale, +0x54 crouchScale.
bool looksLikeZoom(const uint8_t* p) {
    const float* f = (const float*)p;
    float inT = f[0x18 / 4], outT = f[0x1C / 4], shake = f[0x28 / 4], fov = f[0x2C / 4];
    float mx = f[0x40 / 4], my = f[0x44 / 4], stab = f[0x48 / 4], destab = f[0x4C / 4], minS = f[0x50 / 4], crouch = f[0x54 / 4];
    return okF(inT, 0.f, 20.f) && okF(outT, 0.f, 20.f) && (inT > 0.f || outT > 0.f) &&
           okF(shake, 0.001f, 50.f) && okF(fov, 0.05f, 3.2f) &&
           okF(mx, 0.f, 100.f) && okF(my, 0.f, 100.f) &&
           okF(stab, 0.001f, 60.f) && okF(destab, 0.001f, 60.f) && okF(minS, 0.f, 1.5f) && okF(crouch, 0.01f, 2.f);
}

// Recoil block (loader sub_140991E50, 56 bytes): +0x00 max_recoil, +0x04 attack, +0x08 first_attack, +0x0C decay, +0x10 end_decay,
// +0x14 recoil_time, +0x18 maxX, +0x1C maxY, +0x20 randomness, +0x24 tilt, +0x28 impulse, +0x2C recoil_crouch_m, +0x30 recoil_jump_m,
// +0x34 recoil_holdBreathActive_m. The three multipliers are nonzero by default, which keeps all-zero memory from matching.
bool looksLikeRecoil(const uint8_t* p) {
    const float* f = (const float*)p;
    if (!okF(f[0], 0.f, 100.f) || !okF(f[1], 0.f, 100.f) || !okF(f[2], 0.f, 100.f) || !okF(f[3], 0.f, 100.f) || !okF(f[4], 0.f, 100.f)) return false;
    if (!okF(f[5], 0.f, 30.f) || !okF(f[6], 0.f, 100.f) || !okF(f[7], 0.f, 100.f) || !okF(f[8], 0.f, 30.f) || !okF(f[9], 0.f, 100.f) || !okF(f[10], 0.f, 100.f)) return false;
    if (!okF(f[11], 0.01f, 5.f) || !okF(f[12], 0.01f, 10.f) || !okF(f[13], 0.01f, 5.f)) return false;
    return f[0] > 0.f || f[6] > 0.f || f[7] > 0.f;
}

// Procedural recoil block (loader sub_140992870, 44 bytes): +0x00 duration, +0x04 strength, +0x08 kickIn, +0x0C arms (int),
// +0x10 enabled (byte), +0x14 dampStrength, +0x18 fireRecoilTime, +0x1C fireRecoilStrengthFirst, +0x20 fireRecoilStrength,
// +0x24 angleRecoilStrength, +0x28 randomness.
bool looksLikeProcRecoil(const uint8_t* p) {
    const float* f = (const float*)p;
    if (p[0x10] != 1) return false;
    return okF(f[0], 0.01f, 10.f) && okF(f[1], 0.f, 20.f) && okF(f[2], 0.f, 10.f) && okF(f[5], 0.01f, 100.f) && okF(f[6], 0.f, 5.f) &&
           okF(f[7], 0.f, 50.f) && okF(f[8], 0.001f, 50.f) && okF(f[9], 0.f, 50.f) && okF(f[10], 0.f, 10.f) &&
           *(const int*)(p + 0x0C) >= 0 && *(const int*)(p + 0x0C) <= 4;
}

// Plain arrays so the SEH functions have no C++ objects to unwind.
struct Hits { uintptr_t fire[64]; int nFire; uintptr_t rel[64]; int nRel; uintptr_t zoom[32]; int nZoom; uintptr_t rec[32]; int nRec; uintptr_t proc[32]; int nProc; };

void scanChunk(uintptr_t base, size_t size, Hits& h) {
    __try {
        const uint8_t* b = (const uint8_t*)base;
        for (size_t i = 0; i + 0x90 <= size && (h.nFire < 64 || h.nRel < 64 || h.nZoom < 32 || (false && (h.nRec < 32 || h.nProc < 32))); i += 4) {
            if (h.nZoom < 32 && looksLikeZoom(b + i)) h.zoom[h.nZoom++] = base + i;
            if (false && h.nRec < 32 && looksLikeRecoil(b + i)) h.rec[h.nRec++] = base + i;
            if (false && h.nProc < 32 && looksLikeProcRecoil(b + i)) h.proc[h.nProc++] = base + i;
            if (h.nFire < 64 && (i & 7) == 0 && looksLikeFireMode(b + i)) h.fire[h.nFire++] = base + i;
            if (h.nRel < 64 && looksLikeReload((const float*)(b + i))) h.rel[h.nRel++] = base + i;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
}

bool rdQ(uintptr_t a, uintptr_t& v) { __try { v = *(volatile uintptr_t*)a; return true; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; } }
bool rdU32(uintptr_t a, uint32_t& v) { __try { v = *(volatile uint32_t*)a; return true; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; } }
bool rdU8(uintptr_t a, uint8_t& v) { __try { v = *(volatile uint8_t*)a; return true; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; } }
bool readI(uintptr_t a, int& v)   { __try { v = *(volatile int*)a; return true; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; } }
bool readF(uintptr_t a, float& v) { __try { v = *(volatile float*)a; return true; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; } }
bool writeI(uintptr_t a, int v)   { __try { *(volatile int*)a = v; return true; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; } }
bool writeF(uintptr_t a, float v) { __try { *(volatile float*)a = v; return true; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; } }
bool stillFire(uintptr_t a) { __try { return looksLikeFireMode((const uint8_t*)a); } __except (EXCEPTION_EXECUTE_HANDLER) { return false; } }
bool stillReload(uintptr_t a) { __try { return looksLikeReload((const float*)a); } __except (EXCEPTION_EXECUTE_HANDLER) { return false; } }

static void addRecoilRecs(Set& s, const Hits& h);
void swayCvar(bool on);
void addHits(Set& s, const Hits& h) {
    for (int i = 0; i < h.nFire; ++i) {
        int d = 0; float sp = 0;
        if (!readI(h.fire[i] + 0x18, d) || !readF(h.fire[i] + 0x14, sp)) continue;
        bool dup = false; for (auto& r : s.fire) if (r.addr == h.fire[i]) dup = true;
        if (!dup) s.fire.push_back({ h.fire[i], d, sp, d, sp, false });
    }
    for (int i = 0; i < h.nRel; ++i) {
        bool dup = false; for (auto& r : s.rel) if (r.addr == h.rel[i]) dup = true;
        if (dup) continue;
        ReloadRec rr{}; rr.addr = h.rel[i]; rr.wrote = false;
        bool good = true;
        for (int k = 0; k < 16 && good; ++k) good = readF(h.rel[i] + k * 4, rr.orig[k]);
        if (good) s.rel.push_back(rr);
    }
    // sway: target values that remove aim sway, steadying time and camera shake
    static const struct { unsigned off; float target; } kSway[] = {
        { 0x28, 0.f }, { 0x40, 0.f }, { 0x44, 0.f }, { 0x48, 0.01f }, { 0x4C, 1000.f }, { 0x50, 0.f }, { 0x54, 0.f } };
    for (int i = 0; i < h.nZoom; ++i) {
        for (auto& k : kSway) {
            uintptr_t a = h.zoom[i] + k.off; float v = 0;
            if (!readF(a, v) || !std::isfinite(v)) continue;
            bool dup = false; for (auto& r : s.sway) if (r.addr == a) dup = true;
            if (!dup) s.sway.push_back({ a, v, k.target, false });
        }
    }
    // aim-down-sights speed: zoom_in_time (+0x18) and zoom_out_time (+0x1C) of the zoom block
    for (int i = 0; i < h.nZoom; ++i) {
        for (unsigned off : { 0x18u, 0x1Cu }) {
            uintptr_t a = h.zoom[i] + off; float v = 0;
            if (!readF(a, v) || !(v > 0.f)) continue;
            bool dup = false; for (auto& r : s.ads) if (r.addr == a) dup = true;
            float t = v * 0.08f; if (t < 0.02f) t = 0.02f; if (t > v) t = v;
            if (!dup) s.ads.push_back({ a, v, t, false });
        }
    }
    addRecoilRecs(s, h);
}

static void addRecoilRecs(Set& s, const Hits& h) {
    static const unsigned kRec[] = { 0x00, 0x04, 0x08, 0x18, 0x1C, 0x20, 0x24, 0x28 };   // max_recoil, attack, first_attack, maxX, maxY, randomness, tilt, impulse
    static const unsigned kProc[] = { 0x04, 0x1C, 0x20, 0x24 };                           // strength, first/regular fire recoil strength, angle recoil
    auto add = [&](uintptr_t a) {
        float v = 0;
        if (!readF(a, v) || !std::isfinite(v)) return;
        for (auto& r : s.recoil) if (r.addr == a) return;
        s.recoil.push_back({ a, v, 0.f, false });
    };
    for (int i = 0; i < h.nRec; ++i) for (unsigned o : kRec) add(h.rec[i] + o);
    for (int i = 0; i < h.nProc; ++i) for (unsigned o : kProc) add(h.proc[i] + o);
}

void restoreRecoil(Set& s) {
    for (auto& r : s.recoil) if (r.wrote) { writeF(r.addr, r.orig); r.wrote = false; }
}
void applyRecoil(Set& s) {
    for (auto& r : s.recoil) {
        float cur = 0;
        if (!readF(r.addr, cur) || !std::isfinite(cur)) continue;
        if (cur != r.orig && cur != r.target) continue;
        writeF(r.addr, r.target);
        r.wrote = true;
    }
}

void restoreDrop(Set& s) {
    for (auto& r : s.drop) if (r.wrote) { writeI(r.addr, r.origBits); r.wrote = false; }
}
void applyDrop(Set& s) {
    for (auto& r : s.drop) {
        int cur = 0;
        if (!readI(r.addr, cur)) continue;
        if (cur != r.origBits && cur != 0) continue;               // something else changed it
        writeI(r.addr, 0);
        r.wrote = true;
    }
}

void restoreAds(Set& s) {
    for (auto& r : s.ads) if (r.wrote) { writeF(r.addr, r.orig); r.wrote = false; }
}
void applyAds(Set& s) {
    for (auto& r : s.ads) {
        float cur = 0;
        if (!readF(r.addr, cur) || !std::isfinite(cur)) continue;
        if (cur != r.orig && cur != r.target) continue;
        writeF(r.addr, r.target);
        r.wrote = true;
    }
}

void restoreSway(Set& s) {
    for (auto& r : s.sway) if (r.wrote) { writeF(r.addr, r.orig); r.wrote = false; }
}
void applySway(Set& s) {
    for (auto& r : s.sway) {
        float cur = 0;
        if (!readF(r.addr, cur) || !std::isfinite(cur)) continue;
        if (cur != r.orig && cur != r.target) continue;      // something else changed it: leave alone
        writeF(r.addr, r.target);
        r.wrote = true;
    }
}

void restoreSet(Set& s) {
    for (auto& r : s.flt) if (r.wrote) { writeF(r.addr, r.orig); r.wrote = false; }
    for (auto& r : s.fire) if (r.wrote) {
        if (stillFire(r.addr)) { writeI(r.addr + 0x18, r.origDamage); writeF(r.addr + 0x14, r.origSpeed); }
        r.wrote = false; r.lastDamage = r.origDamage; r.lastSpeed = r.origSpeed;
    }
    for (auto& r : s.rel) if (r.wrote) {
        if (stillReload(r.addr)) for (int i = 0; i < 16; ++i) writeF(r.addr + i * 4, r.orig[i]);
        r.wrote = false;
    }
}

void restoreAll(Set& s) { restoreSet(s); restoreAds(s); restoreSway(s); restoreRecoil(s); restoreDrop(s); }

void applySet(Set& s, float dmgMul, float spdMul, float rlMul, float supMul) {
    for (auto& r : s.flt) {
        float cur = 0;
        if (!readF(r.addr, cur) || !std::isfinite(cur)) continue;
        writeF(r.addr, r.orig * supMul);
        r.wrote = true;
    }
    for (auto& r : s.fire) {
        if (!stillFire(r.addr) && !r.wrote) continue;          // freed / reused memory: leave alone
        int cd = 0; float cs = 0;
        if (!readI(r.addr + 0x18, cd) || !readF(r.addr + 0x14, cs)) continue;
        if (cd != r.lastDamage && cd != r.origDamage) continue; // changed by something else
        int nd = (int)(r.origDamage * dmgMul + 0.5f); if (nd < 1) nd = 1;
        float ns = r.origSpeed > 0.f ? r.origSpeed * spdMul : r.origSpeed;
        writeI(r.addr + 0x18, nd); writeF(r.addr + 0x14, ns);
        r.lastDamage = nd; r.lastSpeed = ns; r.wrote = true;
    }
    for (auto& r : s.rel) {
        if (!r.wrote && !stillReload(r.addr)) continue;
        for (int i = 0; i < 16; ++i) if (r.orig[i] > 0.f) writeF(r.addr + i * 4, r.orig[i] * rlMul);
        r.wrote = true;
    }
}

// ---- held weapon ----
// item object for the entity id, via the sorted map (count/keys/values)
uintptr_t findItem(uintptr_t base, uint32_t id) {
    uint32_t count = 0; uintptr_t keys = 0, vals = 0;
    if (!rdU32(base + RVA_ItemMapCount, count) || !rdQ(base + RVA_ItemMapKeys, keys) || !rdQ(base + RVA_ItemMapVals, vals)) return 0;
    if (count == 0 || count > 100000 || !okPtr(keys) || !okPtr(vals)) return 0;
    uint32_t lo = 0, n = count;
    while (n) {                                    // lower_bound over sorted u32 keys
        uint32_t half = n >> 1, k = 0;
        if (!rdU32(keys + 4ull * (lo + half), k)) return 0;
        if (k >= id) n = half; else { lo += half + 1; n -= half + 1; }
    }
    if (lo >= count) return 0;
    uint32_t k = 0; uintptr_t item = 0;
    if (!rdU32(keys + 4ull * lo, k) || k != id || !rdQ(vals + 8ull * lo, item) || !okPtr(item)) return 0;
    return item;
}

uintptr_t heldItemObject(uint32_t& idOut) {
    uintptr_t base = (uintptr_t)GetModuleHandleW(nullptr), pl = 0;
    idOut = 0;
    if (!rdQ(base + RVA_pLocalPlayer, pl) || !okPtr(pl)) return 0;
    uint8_t slot = 0; uint32_t id = 0;
    if (!rdU8(pl + 0x61, slot) || slot > 3 || !rdU32(pl + 0x64 + 4ull * slot, id) || id == 0) return 0;
    idOut = id;
    return findItem(base, id);
}

// bounded scan of the item's weapon parameters: the block at item+0x288 and the objects it points to (two levels)
void scanHeldItem(uintptr_t item, Set& out) {
    uintptr_t wp = item + 0x288;
    Hits h; h.nFire = 0; h.nRel = 0; h.nZoom = 0; h.nRec = 0; h.nProc = 0;
    memset(h.fire, 0, sizeof(h.fire)); memset(h.rel, 0, sizeof(h.rel)); memset(h.zoom, 0, sizeof(h.zoom));
    memset(h.rec, 0, sizeof(h.rec)); memset(h.proc, 0, sizeof(h.proc));
    scanChunk(wp, 0x400, h);                        // inline
    uintptr_t level1[64]; int n1 = 0;
    for (int i = 0; i < 0x400 / 8 && n1 < 64; ++i) {
        uintptr_t p = 0;
        if (rdQ(wp + i * 8, p) && okPtr(p)) level1[n1++] = p;
    }
    for (int i = 0; i < n1; ++i) {
        scanChunk(level1[i], 0x1000, h);
        for (int j = 0; j < 0x200 / 8; ++j) {       // second level, a smaller window
            uintptr_t p = 0;
            if (rdQ(level1[i] + j * 8, p) && okPtr(p)) scanChunk(p, 0x800, h);
            if (h.nFire >= 64 && h.nRel >= 64) break;
        }
    }
    // the reload table is a sibling member of the fire-mode block: look around every fire mode that was found
    for (int i = 0; i < h.nFire; ++i) scanChunk(h.fire[i] - 0x400, 0x1000, h);
    addHits(out, h);

    // direct route, as the reload hook uses it: weapon = item+0x288 (inline or a pointer; its +8 points back at the item),
    // fire-mode list P = *(weapon+0x10), reload table = *(P+0x40) -- a separate allocation, so a nearby scan never sees it
    uintptr_t cand[2] = { item + 0x288, 0 };
    rdQ(item + 0x288, cand[1]);
    for (int c = 0; c < 2; ++c) {
        uintptr_t w = cand[c], back = 0, P = 0, T = 0;
        if (!okPtr(w) || !rdQ(w + 8, back) || back != item) continue;
        if (!rdQ(w + 0x10, P) || !okPtr(P) || !rdQ(P + 0x40, T) || !okPtr(T)) continue;
        bool dup = false; for (auto& r : out.rel) if (r.addr == T) dup = true;
        if (dup) break;
        ReloadRec rr{}; rr.addr = T; rr.wrote = false;
        bool good = true; int positive = 0;
        for (int k = 0; k < 16 && good; ++k) { good = readF(T + k * 4, rr.orig[k]); if (rr.orig[k] > 0.f) ++positive; }
        if (good && positive >= 1) out.rel.push_back(rr);
        break;
    }

    // suppression: fire-mode suppressionAmount (+0x58 of every fire-mode block found) ...
    auto addFloat = [&](uintptr_t a) {
        float v = 0;
        if (!okPtr(a) || !readF(a, v) || !okF(v, 0.f, 1000.f)) return;
        for (auto& r : out.flt) if (r.addr == a) return;
        out.flt.push_back({ a, v, false });
    };
    for (auto& f : out.fire) addFloat(f.addr + 0x58);

    // ... and the ammo's own values: suppressionStrength (AmmoDef+0x18) and the bullet whiz strength (whiz struct +0x0C,
    // pointed to by AmmoDef+0xB0). The AmmoDef comes from the sorted {class, AmmoDef*} vector at
    // *(*(g_gameData 0x1415E8DF0)+48)+184..192, keyed by the fire mode's spawn class (**(weapon+0x10) + 0x30), as sub_1409AD620 does.
    uintptr_t wcand[2] = { item + 0x288, 0 };
    rdQ(item + 0x288, wcand[1]);
    for (int c = 0; c < 2; ++c) {
        uintptr_t w = wcand[c], back = 0, P = 0, params = 0, key = 0;
        if (!okPtr(w) || !rdQ(w + 8, back) || back != item) continue;
        if (!rdQ(w + 0x10, P) || !okPtr(P) || !rdQ(P, params) || !okPtr(params) || !rdQ(params + 0x30, key) || !okPtr(key)) break;
        uintptr_t mod = (uintptr_t)GetModuleHandleW(nullptr), gd = 0, reg = 0, vb = 0, ve = 0;
        if (!rdQ(mod + 0x15E8DF0, gd) || !okPtr(gd) || !rdQ(gd + 48, reg) || !okPtr(reg)) break;
        if (!rdQ(reg + 184, vb) || !rdQ(reg + 192, ve) || !okPtr(vb) || ve < vb || (ve - vb) / 16 > 100000) break;
        size_t lo = 0, n = (ve - vb) / 16;
        while (n) {                                    // lower_bound over 16-byte {key, value} records
            size_t half = n >> 1; uintptr_t k = 0;
            if (!rdQ(vb + 16 * (lo + half), k)) { n = 0; lo = (size_t)-1; break; }
            if (k < key) { lo += half + 1; n -= half + 1; } else n = half;
        }
        if (lo == (size_t)-1) break;
        uintptr_t k = 0, adef = 0;
        if (!rdQ(vb + 16 * lo, k) || k != key || !rdQ(vb + 16 * lo + 8, adef) || !okPtr(adef)) break;
        addFloat(adef + 0x18);
        uintptr_t whiz = 0;
        if (rdQ(adef + 0xB0, whiz) && okPtr(whiz)) addFloat(whiz + 0x0C);

        // bullet gravity: AmmoDef+0x70 points at the 168-byte physics particle block (AmmoDef_LoadPhysics 0x14098DA50). Its gravity
        // vector (+0x44, +0x48, +0x4C; the first component is initialised to the "unset" marker -1, so the world gravity applies)
        // is set to zero so the particle flies straight.
        uintptr_t blk = 0;
        if (rdQ(adef + 0x70, blk) && okPtr(blk)) {
            for (unsigned off : { 0x44u, 0x48u, 0x4Cu }) {
                int bits = 0;
                if (!readI(blk + off, bits)) continue;
                bool dup = false; for (auto& r : out.drop) if (r.addr == blk + off) dup = true;
                if (!dup) out.drop.push_back({ blk + off, bits, false });
            }
        }
        break;
    }
}

DWORD WINAPI scanThread(LPVOID) {
    Set res;
    SYSTEM_INFO si; GetSystemInfo(&si);
    uintptr_t addr = (uintptr_t)si.lpMinimumApplicationAddress, end = (uintptr_t)si.lpMaximumApplicationAddress;
    MEMORY_BASIC_INFORMATION mbi;
    const size_t kChunk = 1u << 20;
    while (addr < end && VirtualQuery((void*)addr, &mbi, sizeof(mbi))) {
        uintptr_t rb = (uintptr_t)mbi.BaseAddress, rs = mbi.RegionSize;
        bool ok = mbi.State == MEM_COMMIT && mbi.Type == MEM_PRIVATE && (mbi.Protect & PAGE_READWRITE) &&
                  !(mbi.Protect & (PAGE_GUARD | PAGE_NOACCESS));
        if (ok) {
            for (uintptr_t o = 0; o < rs; o += kChunk - 0x100) {
                size_t n = (size_t)((rs - o) < kChunk ? (rs - o) : kChunk);
                Hits h; h.nFire = 0; h.nRel = 0; h.nZoom = 0; h.nRec = 0; h.nProc = 0;
                scanChunk(rb + o, n, h);
                addHits(res, h);
            }
        }
        addr = rb + rs;
        scanPercent = (int)((addr * 100ull) / end);
    }
    AcquireSRWLockExclusive(&g_lock);
    restoreAll(g_global);
    g_global = std::move(res);
    fireModes = (int)g_global.fire.size(); reloadTables = (int)g_global.rel.size();
    ReleaseSRWLockExclusive(&g_lock);
    scanPercent = 100;
    scanning = false;
    return 0;
}
} // namespace

void restore() {
    swayCvar(false);
    AcquireSRWLockExclusive(&g_lock);
    restoreAll(g_global);
    restoreAll(g_held);
    ReleaseSRWLockExclusive(&g_lock);
}

void startScan() {
    bool exp = false;
    if (!scanning.compare_exchange_strong(exp, true)) return;
    restore();                       // never rescan while our own edits are in memory
    scanPercent = 0;
    HANDLE t = CreateThread(nullptr, 0, scanThread, nullptr, 0, nullptr);
    if (t) CloseHandle(t); else scanning = false;
}

void tick() {
    if (scanning) return;
    static int frame = 0;
    static bool lastHeldOnly = true;
    AcquireSRWLockExclusive(&g_lock);

    if (lastHeldOnly != heldOnly) { restoreAll(g_global); restoreAll(g_held); lastHeldOnly = heldOnly; }

    // follow the held weapon: re-resolve a few times a second, rescan its data only when it changes
    if ((frame++ % 15) == 0) {
        uint32_t id = 0;
        uintptr_t item = heldItemObject(id);
        heldItemId = id;
        if (item != g_heldItem) {
            restoreAll(g_held);
            g_held = Set();
            g_heldItem = item;
            if (item) scanHeldItem(item, g_held);
            heldFire = (int)g_held.fire.size(); heldReload = (int)g_held.rel.size(); heldSway = (int)(g_held.sway.size() / 7); heldRecoil = (int)g_held.recoil.size(); heldDrop = (int)g_held.drop.size();
        }
    }

    Set& active = heldOnly ? g_held : g_global;
    if (apply) applySet(active, damageMul, speedMul, reloadMul, suppressionMul);
    else restoreSet(active);
    if (noSway) applySway(active); else restoreSway(active);
    swayCvar(noSway);
    if (fastAds) applyAds(active); else restoreAds(active);
    restoreRecoil(active);                                   // recoil is now handled by norecoil.cpp (patches the per-shot kick calls); the block-zeroing broke aiming down sights
    if (noDrop) applyDrop(active); else restoreDrop(active);
    ReleaseSRWLockExclusive(&g_lock);
}

uintptr_t heldItemPtr() { return g_heldItem; }

// Actor.FirstPerson.Sway.WeaponIndependence (float, default 1.0, RVA 0x13210A8): how freely the held weapon lags/sways behind the view.
// 0 locks the weapon to the camera. Written together with the zoom-sway fields when "no sway" is on.
namespace {
bool g_swayCvarOn = false;
void swayCvar(bool on) {
    if (on == g_swayCvarOn) return;
    __try {
        volatile float* p = (volatile float*)((uintptr_t)GetModuleHandleW(nullptr) + 0x13210A8);
        if (on) { if (*p > 0.5f && *p < 1.5f) *p = 0.f; } else { if (*p == 0.f) *p = 1.f; }
        g_swayCvarOn = on;
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
}
}

float heldBulletSpeed() {
    float v = 0;
    AcquireSRWLockShared(&g_lock);
    for (auto& r : g_held.fire) {
        float s = 0;
        if (stillFire(r.addr) && readF(r.addr + 0x14, s) && s > 1.f && s < 3000.f) { v = s; break; }
    }
    ReleaseSRWLockShared(&g_lock);
    return v;
}
}
