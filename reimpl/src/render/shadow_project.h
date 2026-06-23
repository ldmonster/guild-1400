#pragma once
#include "guild/common/types.h"

// =============================================================================
// guild::render — projected-shadow GEOMETRY (the deferred half of the gilde.exe
// shadow cluster: caster-vertex projection onto the ground plane, caster-height
// resolution, and the per-object transform-state reset). Faithful 1:1
// reconstruction of d3_engine.c's shadow projection inner loops:
//
//   0x5f4880  VIBE_Shadow_ResetCasterTransforms (clear cached caster transforms)
//   0x5f4900  VIBE_Shadow_RetOne                (trivial 1-returning leaf)
//   0x5f4908  VIBE_Shadow_GetDefaultCallback    (returns &RetOne)
//   0x5f34c0  VIBE_Shadow_ComputeCasterHeight   (resolve ground-plane height)
//   0x5f363c  VIBE_Shadow_RenderMeshShadow      (projection inner loops, extracted)
//
// PROJECTION MATH (1:1 from VIBE_Shadow_RenderMeshShadow @ 0x5f363c):
//  - POINT light, with light POSITION L (ecx = L, edx = vertex v in the disasm):
//        d = L - v                           // 0x5f3ce3..0x5f3cf7
//        t = (L.y - groundY) / -d.y          // 0x5f3d08  (numerator uses L.y!)
//        v' = L + t*d                        // 0x5f3d18..0x5f3d39  (built off L!)
//    i.e. the projected point is L pushed by t*d. v'.y = L.y + t*(L.y - v.y);
//    with t = (L.y-groundY)/(v.y-L.y) this gives v'.y == groundY.
//  - DIRECTIONAL light, with light DIRECTION a5 == dir:
//        t = (v.y - groundY) / -dir.y        // v94 = -dir.y, 0x5f3721
//        v' = v + t*dir                      // 0x5f372f..0x5f3746
//    (the +529&0x10 branch). Same result: v'.y == groundY.
//  Both branches then accumulate the projected XZ/XY bounds (min/max) which feed
//  the surface-rect / ConvertX setup; ProjectMeshToGround returns those bounds.
//
// CASTER-HEIGHT (VIBE_Shadow_ComputeCasterHeight @ 0x5f34c0): the ground height
// the caster's shadow is flattened onto. Priority order from the original:
//   1. If any caster slot has a non-zero cached light-dir vector (+1804..+1812
//      != 0), use that slot's cached height (+1868).  [overrides everything]
//   2. Else, if (+529 & 0x20) "on explicit ground" flag: caster origin Y minus
//      the reference plane Y (+468 of the view) plus the object base offset
//      (+2292).  [PointThroughBoneChain gives the origin]
//   3. Else: scan the 8 view frustum/grid sample points (stride 80, field +4 is
//      the sample height) for the MINIMUM, add the object base offset (+2292).
// We model the engine pointers as small structs so the arithmetic stays verbatim.
//
// CONSTANTS (recovered via get_bytes):
//   flt_62C278 = 0.5     flt_62C27C =  5.0    flt_62C280 = -16.0
//   flt_62C284 = 16.0    dbl_62C288 = 16384.0 (bounds clamp in RenderMeshShadow)
// =============================================================================
namespace guild::render {

// A 3-float vector as the engine stores caster verts / light pos / light dir.
struct ShadowVec3 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

// Per-object caster slot fields used by ResetCasterTransforms / ComputeCasterHeight.
// Stride 128 bytes in the original; 4 slots per object at (*(obj+492))[0..512).
//   +1780  entry  (0 == empty slot)
//   +1804  cached light-dir vector (3 floats, +1804/+1808/+1812)
//   +1868  cached projected ground height
struct ShadowCasterSlot {
    u32       entry = 0;       // +1780  caster id/ptr (0 = empty)
    ShadowVec3 cachedDir;      // +1804  cached light-dir (== stateA/B/C as float)
    float     cachedHeight = 0.0f; // +1868 cached shadow ground height
};

// A node's caster table is exactly 4 slots (512 bytes / 128). Mirrors shadow.h.
constexpr int kCasterTableSlots = 4;

// 0.5 / 5.0 / -16.0 / 16.0 / 16384.0 — see header block.
constexpr float kShadowHalf      = 0.5f;     // flt_62C278
constexpr float kShadowHeightBias = 5.0f;    // flt_62C27C
constexpr float kShadowBoxMin    = -16.0f;   // flt_62C280
constexpr float kShadowBoxMax    = 16.0f;    // flt_62C284
constexpr double kShadowBoundsLimit = 16384.0; // dbl_62C288

// gilde.exe 0x5f4900 — VIBE_Shadow_RetOne. Returns 1.
int RetOne();

// gilde.exe 0x5f4908 — VIBE_Shadow_GetDefaultCallback. Returns &RetOne.
int (*GetDefaultCallback())();

// gilde.exe 0x5f4880 — VIBE_Shadow_ResetCasterTransforms (__usercall eax=fn(obj@eax)).
// For each of the 4 caster slots whose entry != 0, zero the cached transform
// state (+1804/+1808/+1812, here `cachedDir`). `enabled` mirrors (+529 & 4); the
// original early-outs unless the flag is set AND the object has a caster table
// (+492 != 0). Returns the loop terminator (512), matching the original `result`.
int ResetCasterTransforms(ShadowCasterSlot table[kCasterTableSlots], bool enabled);

// ---------------------------------------------------------------------------
// Vertex projection (the two RenderMeshShadow inner loops).
// ---------------------------------------------------------------------------

// gilde.exe 0x5f3721 — DIRECTIONAL projection (light DIRECTION `dir`).
//   t = (v.y - groundY) / -dir.y ;  v' = v + t*dir.
ShadowVec3 ProjectVertexDirectional(const ShadowVec3& v, const ShadowVec3& dir,
                                    float groundY);

// gilde.exe 0x5f3ce3 — POINT projection (light POSITION `lightPos`).
//   d = lightPos - v ;  t = (lightPos.y - groundY) / -d.y ;  v' = lightPos + t*d.
ShadowVec3 ProjectVertexPoint(const ShadowVec3& v, const ShadowVec3& lightPos,
                              float groundY);

// Axis-aligned XZ/Y bounds the engine accumulates while projecting (a8+72..88).
//   minX/maxX (a8+72/76), minZ/maxZ (a8+80/84 -> v85/v83), and the height a8+88.
struct ShadowBounds {
    float minX =  1e10f;   // v82
    float maxX = -1e10f;   // v84
    float minZ =  1e10f;   // v85
    float maxZ = -1e10f;   // v83
};

// Projects every vertex in `verts` (count `n`) onto the ground plane at
// `groundY` for the given light, writing the projected positions back into
// `out` (may alias `verts`) and accumulating the XZ bounds exactly as
// VIBE_Shadow_RenderMeshShadow does (min/max fold, original FPU order).
// `directional` selects the dir vs point-light loop. Returns the bounds.
ShadowBounds ProjectMeshToGround(const ShadowVec3* verts, int n, ShadowVec3* out,
                                 const ShadowVec3& light, bool directional,
                                 float groundY);

// ---------------------------------------------------------------------------
// Caster-height resolution (VIBE_Shadow_ComputeCasterHeight @ 0x5f34c0).
// ---------------------------------------------------------------------------

// One frustum/grid sample point (engine stride 80; only +4 = height is read).
struct ShadowSamplePoint {
    float height = 0.0f;   // +4
};

// Inputs modeled as a small struct so the priority logic reads cleanly.
struct CasterHeightQuery {
    float originY = 0.0f;       // PointThroughBoneChain(origin).y  (v15[1])
    float planeY  = 0.0f;       // view +468 reference-plane Y
    float baseOffset = 0.0f;    // object (+492) +2292 base height offset
    bool  onGround = false;     // (+529 & 0x20)
    const ShadowSamplePoint* samples = nullptr; // (+500)() -> 8 points (stride 80)
    int   sampleCount = 8;      // the original hardcodes 8
};

// gilde.exe 0x5f34c0 — VIBE_Shadow_ComputeCasterHeight. `table`/`enabled` model
// the caster-slot override scan (priority 1); `q` carries the rest. Returns the
// ground height the caster's shadow flattens onto.
float ComputeCasterHeight(const ShadowCasterSlot table[kCasterTableSlots],
                          bool enabled, const CasterHeightQuery& q);

// ---------------------------------------------------------------------------
// SHADOW-SURFACE UV MAPPING — the per-vertex map RenderMeshShadow applies after
// projection + bounds (gilde.exe 0x5f3bb6..0x5f3bf8):
//   v93  = surfW / (maxX - minX);              // 0x5f3bb6  (a8 width = v96+116)
//   v113 = surfW / (maxZ - minZ);              // 0x5f3bba
//   for each projected vertex:
//     v[4] = (v.x - minX) * v93;               // 0x5f3bea  -> +16 screen X
//     v[5] = (v.z - minZ) * v113;              // 0x5f3bef  -> +20 screen Y
// i.e. the projected ground XZ box maps onto the [0,surfW) shadow texture. The
// rasterizer then reads vertex +16/+20 as screen coordinates.
struct ShadowUv { float u = 0.0f; float v = 0.0f; };
inline ShadowUv MapShadowVertexToSurface(const ShadowVec3& p, const ShadowBounds& b,
                                         float surfW) {
    float sx = surfW / (b.maxX - b.minX);   // v93
    float sz = surfW / (b.maxZ - b.minZ);   // v113
    return ShadowUv{ (p.x - b.minX) * sx, (p.z - b.minZ) * sz };
}

// gilde.exe 0x5f3857 — the bounds-acceptance guard (after projection, before the
// surface setup): every accumulated bound and span must be within ±16384.
inline bool ShadowBoundsAcceptable(const ShadowBounds& b) {
    auto ok = [](double x) { return (x < 0 ? -x : x) <= kShadowBoundsLimit; };
    return ok(b.maxX) && ok(b.minX) && ok(b.maxZ) && ok(b.minZ) &&
           ok((double)b.maxX - b.minX) && ok((double)b.maxZ - b.minZ);
}

} // namespace guild::render
