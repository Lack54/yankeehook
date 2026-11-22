#include "visible.h"
#include <windows.h>
#include <cmath>
#include <cstring>

namespace visible {
Diag diag;

namespace {
constexpr uintptr_t RVA_RWI = 0x76F1D0;        // sub_14076F1D0(world, req, tag, callerId)
constexpr uintptr_t RVA_World = 0x14D1F40;     // qword_1414D1F40 (the physical world object, passed by address)
using RwiFn = int(__fastcall*)(void* world, void* req, const char* tag, unsigned callerId);

// Request layout (from the wrapper and two of the game's own callers):
//  +0x18 origin xyz, +0x24 direction xyz (full vector, length = ray length), +0x30 objtypes, +0x34 flags,
//  +0x38 hits*, +0x40 max hits, everything else 0. ray_hit is 0x50 bytes: dist at +0.
struct alignas(32) Req { unsigned char b[0x80]; };
struct alignas(32) Hit { unsigned char b[0x80]; };

int callRwi(float ox, float oy, float oz, float dx, float dy, float dz) {
    Req req; Hit hit;
    memset(&req, 0, sizeof(req)); memset(&hit, 0, sizeof(hit));
    float* f = (float*)req.b;
    f[0x18 / 4] = ox; f[0x1C / 4] = oy; f[0x20 / 4] = oz;
    f[0x24 / 4] = dx; f[0x28 / 4] = dy; f[0x2C / 4] = dz;
    *(unsigned*)(req.b + 0x30) = 0x101;                    // ent_static | ent_terrain
    *(unsigned*)(req.b + 0x34) = 0x0F;                     // stop at pierceable
    *(void**)(req.b + 0x38) = hit.b;
    *(int*)(req.b + 0x40) = 1;
    uintptr_t base = (uintptr_t)GetModuleHandleW(nullptr);
    RwiFn fn = (RwiFn)(base + RVA_RWI);
    return fn((void*)(base + RVA_World), req.b, "RayWorldIntersection(Physics)", 4);
}

bool guardedCall(float ox, float oy, float oz, float dx, float dy, float dz, int& hits) {
    __try { hits = callRwi(ox, oy, oz, dx, dy, dz); return true; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
}

bool lineClear(float ox, float oy, float oz, float tx, float ty, float tz) {
    if (diag.disabled) return true;
    float dx = tx - ox, dy = ty - oy, dz = tz - oz;
    float len = sqrtf(dx * dx + dy * dy + dz * dz);
    if (!(len > 1.f) || !std::isfinite(len)) return true;
    float k = (len - 0.4f) / len;                          // stop just short of the target
    int hits = 0;
    ++diag.rays;
    if (!guardedCall(ox, oy, oz, dx * k, dy * k, dz * k, hits)) {
        if (++diag.faults >= 3) diag.disabled = true;
        return true;
    }
    if (hits > 0) { ++diag.blocked; return false; }
    return true;
}
}
