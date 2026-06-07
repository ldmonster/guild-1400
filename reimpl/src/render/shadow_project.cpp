#include "render/shadow_project.h"

// =============================================================================
// guild::render — projected-shadow geometry implementation. Faithful 1:1
// translations of the gilde.exe shadow projection / caster-height / transform
// routines listed in shadow_project.h. Pointer-arithmetic-heavy code is modeled
// with small structs whose fields carry the original byte offsets so the
// arithmetic stays verbatim. No OS calls here (the surface-lock / rasterize tail
// of VIBE_Shadow_RenderMeshShadow stays in shadow_render.cpp via the shim).
// =============================================================================
namespace guild::render {

// ---------------------------------------------------------------------------
// gilde.exe 0x5f4900 — VIBE_Shadow_RetOne.  return 1;
// ---------------------------------------------------------------------------
int RetOne() {
    return 1;
}

// ---------------------------------------------------------------------------
// gilde.exe 0x5f4908 — VIBE_Shadow_GetDefaultCallback.  return VIBE_Shadow_RetOne;
// ---------------------------------------------------------------------------
int (*GetDefaultCallback())() {
    return &RetOne;
}

// ---------------------------------------------------------------------------
// gilde.exe 0x5f4880 — VIBE_Shadow_ResetCasterTransforms.
//   if ((obj+529 & 4) && (obj+492)) {
//     for (result = 0; result != 512; result += 128) {
//       while (1) {                       // skip empty slots
//         v2 = result + *(obj+492);
//         if (*(v2+1780)) break;          // found an occupied slot
//         result += 128;
//         if (result == 512) return result;
//       }
//       *(v2+1804) = 0;                    // clear cached transform state
//       *(*(obj+492)+result+1808) = 0;
//       *(*(obj+492)+result+1812) = 0;
//     }
//   }
//   return result;
// The inner while/outer for is just "for each occupied slot, clear +1804..+1812".
// We preserve the original return value (512 when the table was processed; the
// loop terminator), which the engine ignores but we keep for fidelity.
// ---------------------------------------------------------------------------
int ResetCasterTransforms(ShadowCasterSlot table[kCasterTableSlots], bool enabled) {
    int result = 0;
    if (enabled) {
        for (result = 0; result != 512; result += 128) {
            int idx = result / 128;
            if (idx >= kCasterTableSlots)
                break;
            if (table[idx].entry == 0)
                continue;               // mirrors the inner skip-empty while-loop
            // *(+1804) = *(+1808) = *(+1812) = 0  (the cached light-dir vector)
            table[idx].cachedDir.x = 0.0f;
            table[idx].cachedDir.y = 0.0f;
            table[idx].cachedDir.z = 0.0f;
        }
    }
    return result;
}

// ---------------------------------------------------------------------------
// gilde.exe 0x5f3721 — DIRECTIONAL vertex projection (RenderMeshShadow).
//   v94 = -dir.y;                               // 0x5f370b
//   t   = (v.y - groundY) / v94;                // 0x5f3721  (=(v.y-g)/-dir.y)
//   v'.x = t*dir.x + v.x;                        // 0x5f372f
//   v'.y = t*dir.y + v.y;                        // 0x5f3737
//   v'.z = t*dir.z + v.z;                        // 0x5f3746
// (The original stores groundY in v95; here it is the parameter.)
// ---------------------------------------------------------------------------
ShadowVec3 ProjectVertexDirectional(const ShadowVec3& v, const ShadowVec3& dir,
                                    float groundY) {
    float t = (v.y - groundY) / -dir.y;
    ShadowVec3 r;
    r.x = t * dir.x + v.x;
    r.y = t * dir.y + v.y;
    r.z = t * dir.z + v.z;
    return r;
}

// ---------------------------------------------------------------------------
// gilde.exe 0x5f3ce3 — POINT vertex projection (RenderMeshShadow).
//   v86 = L.x - v.x;  v87 = L.y - v.y;  v88 = L.z - v.z;   // 0x5f3ce3..0x5f3cf7
//   t   = (v.y - groundY) / -v87;                          // 0x5f3d08
//   v'.x = t*v86 + v.x;                                     // 0x5f3d18
//   v'.y = t*v87 + v.y;                                     // 0x5f3d27
//   v'.z = t*v88 + v.z;                                     // 0x5f3d39
// ---------------------------------------------------------------------------
ShadowVec3 ProjectVertexPoint(const ShadowVec3& v, const ShadowVec3& lightPos,
                              float groundY) {
    float dx = lightPos.x - v.x;
    float dy = lightPos.y - v.y;
    float dz = lightPos.z - v.z;
    float t = (v.y - groundY) / -dy;
    ShadowVec3 r;
    r.x = t * dx + v.x;
    r.y = t * dy + v.y;
    r.z = t * dz + v.z;
    return r;
}

// ---------------------------------------------------------------------------
// Bounds accumulation folded into both projection loops of RenderMeshShadow.
// The original keeps running min/max in v82 (minX), v84 (maxX), v85 (minZ),
// v83 (maxZ). The fold is, per vertex, in the exact branch form the decompiler
// produced (e.g. 0x5f3d42): `if (minZ >= v.z) minZ = v.z;` etc. We replicate
// that (>= / <= as written, not std::min/max) so wraparound/NaN/tie behavior is
// bit-identical to the original FPU compares.
// ---------------------------------------------------------------------------
static void AccumulateBounds(ShadowBounds& b, const ShadowVec3& p) {
    if (b.minZ >= p.z) b.minZ = p.z;   // v85 (0x5f3d42 / 0x5f374f)
    if (b.maxZ <= p.z) b.maxZ = p.z;   // v83 (0x5f3d62 / 0x5f376f)
    if (b.minX >= p.x) b.minX = p.x;   // v82 (0x5f3d7c / 0x5f3793)
    if (b.maxX <= p.x) b.maxX = p.x;   // v84 (0x5f3d95 / 0x5f37b0)
}

// ---------------------------------------------------------------------------
// gilde.exe 0x5f363c (extracted projection body) — VIBE_Shadow_RenderMeshShadow.
// The original seeds v82=v85=1e10, v83=v84=-1e10 (0x5f366e..0x5f36ab), projects
// every caster vertex into the engine's vertex buffer (stride 20 floats), and
// folds the XZ bounds. We expose that vertex+bounds pass; the surface-lock /
// triangle-rasterize tail (0x5f3f0b..) is kept in shadow_render.cpp behind the
// shim. `out` may alias `verts` (the original projects in place).
// ---------------------------------------------------------------------------
ShadowBounds ProjectMeshToGround(const ShadowVec3* verts, int n, ShadowVec3* out,
                                 const ShadowVec3& light, bool directional,
                                 float groundY) {
    ShadowBounds b;   // v82=v85=1e10, v83=v84=-1e10 via in-class defaults
    for (int i = 0; i < n; ++i) {
        ShadowVec3 p = directional
                           ? ProjectVertexDirectional(verts[i], light, groundY)
                           : ProjectVertexPoint(verts[i], light, groundY);
        out[i] = p;
        AccumulateBounds(b, p);
    }
    return b;
}

// ---------------------------------------------------------------------------
// gilde.exe 0x5f34c0 — VIBE_Shadow_ComputeCasterHeight.
//
//   v13 = 0.0;  v18 = 0;  PointThroughBoneChain(origin) -> v15  (caller-supplied)
//   for (i = *(a1+504); i; i = *(i+504))                 // node sibling chain
//     if ((i+529 & 4) && (i+492) && !v18)
//       for (v5 = 0; v5 < 512 && !v18; v5 += 128)        // 4 caster slots
//         if (*(*(i+492)+v5+1780))                        // occupied slot
//           if (dir·dir > 0.0)                            // cached light-dir set
//             { v13 = *(slot+1868); v18 = 1; }            // -> cached height
//   if (v18 || !(a1+492))             return v13;         // override / no table
//   if (a1+529 & 0x20)               return v15.y - view+468 + (a1+492)+2292;
//   v10 = (*(view+500))();  v17 = 1e10;                   // 8 sample points
//   for (j = 0; j < 8; ++j) { v16 = min(v17, *(v10+4)); v10 += 80; v17 = v16; }
//   return v16 + (a1+492)+2292;
//
// The node-sibling traversal (priority 1) is the caller's loop over the scene;
// this routine implements the per-node kernel: given THIS node's caster table,
// find the first occupied slot with a non-zero cached light-dir and take its
// cached height. (Faithful: same break-on-first-hit, same dir·dir>0 test.)
// ---------------------------------------------------------------------------
float ComputeCasterHeight(const ShadowCasterSlot table[kCasterTableSlots],
                          bool enabled, const CasterHeightQuery& q) {
    float v13 = 0.0f;   // result accumulator
    bool v18 = false;   // "override found" flag

    // Priority 1: cached-dir override scan over the caster table.
    if (enabled) {
        for (int v5 = 0; v5 < 512 && !v18; v5 += 128) {
            const ShadowCasterSlot& s = table[v5 / 128];
            if (s.entry) {
                const ShadowVec3& d = s.cachedDir;   // slot +1804 (1780+24)
                if (d.x * d.x + d.y * d.y + d.z * d.z > 0.0f) {
                    v13 = s.cachedHeight;            // slot +1868
                    v18 = true;
                }
            }
        }
    }

    // if (v18 || !(a1+492)) return v13;  — q.samples==nullptr models no table.
    if (v18 || q.samples == nullptr)
        return v13;

    // Priority 2: explicit-ground flag (+529 & 0x20).
    //   return origin.y - view+468 + (a1+492)+2292;
    if (q.onGround)
        return q.originY - q.planeY + q.baseOffset;

    // Priority 3: minimum over the 8 sample points (+4 = height), stride 80.
    float v16 = 1.0e10f;
    float v17 = 1.0e10f;
    for (int j = 0; j < q.sampleCount; ++j) {
        // v16 = (v17 >= sample) ? sample : v17;  (0x5f35fd)
        v16 = (v17 >= q.samples[j].height) ? q.samples[j].height : v17;
        v17 = v16;
    }
    return v16 + q.baseOffset;
}

} // namespace guild::render
