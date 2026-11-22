#include "camera.h"
#include <windows.h>
#include <cmath>

namespace camera {
// RVAs (VA - 0x140000000)
static constexpr uintptr_t RVA_CameraStruct = 0x15E4438;  // qword -> struct; Matrix34 rows at +0x6C0/+0x6D0/+0x6E0 (translation at +0x6CC/+0x6DC/+0x6EC)
static constexpr uintptr_t RVA_pLocalPlayer = 0x15E94A0;

template <class T> static bool rd(uintptr_t a, T& out) {
    __try { out = *(volatile T*)a; return true; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

static bool fin(float f) { return std::isfinite(f) && fabsf(f) < 1e7f; }

Cam read() {
    Cam c;
    uintptr_t base = (uintptr_t)GetModuleHandleW(nullptr), s = 0;
    if (!rd(base + RVA_CameraStruct, s) || s < 0x10000) return c;
    float m[3][4];
    for (int r = 0; r < 3; ++r)
        for (int k = 0; k < 4; ++k)
            if (!rd(s + 0x6C0 + r * 0x10 + k * 4, m[r][k]) || !fin(m[r][k])) return c;
    // Matrix34 rows; basis vectors are columns.
    c.right = { m[0][0], m[1][0], m[2][0] };
    c.fwd   = { m[0][1], m[1][1], m[2][1] };
    c.up    = { m[0][2], m[1][2], m[2][2] };
    c.pos   = { m[0][3], m[1][3], m[2][3] };
    c.valid = true;
    return c;
}

LocalPos readLocal() {
    LocalPos l;
    uintptr_t base = (uintptr_t)GetModuleHandleW(nullptr), pl = 0, ent = 0;
    if (!rd(base + RVA_pLocalPlayer, pl) || pl < 0x10000) return l;
    if (!rd(pl + 0x08, ent) || ent < 0x10000) return l;
    if (rd(ent + 0x4C, l.pos.x) && rd(ent + 0x5C, l.pos.y) && rd(ent + 0x6C, l.pos.z)) l.valid = true;
    return l;
}

static float dot(const V3& a, const V3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }

bool worldToScreen(const Cam& c, const V3& p, float w, float h, float fovDeg, bool fovIsH, float& sx, float& sy) {
    if (!c.valid || w < 1 || h < 1) return false;
    V3 d{ p.x - c.pos.x, p.y - c.pos.y, p.z - c.pos.z };
    float depth = dot(d, c.fwd);
    if (depth < 0.1f) return false;
    float aspect = w / h;
    float t = tanf(fovDeg * 0.5f * 3.14159265f / 180.f);
    float tanH = fovIsH ? t : t * aspect;
    float tanV = fovIsH ? t / aspect : t;
    float nx = (dot(d, c.right) / depth) / tanH;
    float ny = (dot(d, c.up) / depth) / tanV;
    sx = (nx * 0.5f + 0.5f) * w;
    sy = (0.5f - ny * 0.5f) * h;
    return true;
}
}
