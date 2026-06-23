#pragma once
#include "guild/common/types.h"
#include "render/geometry_types.h"   // Vertex, Polygon

// =============================================================================
// guild::render — VIBE_Render_ProjectObjectVertices @0x5ac970 (IDA-named
// "UpdateBillboards", a misnomer): the per-object PERSPECTIVE projection +
// backface cull VIBE_Render_ProcessSceneNode @0x5add1c runs after the view
// transform (InterpolateMorphVertices) and the clip-flag pass (ComputeVertexClipFlags),
// before the draw-list append.
//
// PROJECTION (the software branch, byte_649D70). For each vertex whose +76 byte has
// its sign bit set (0x80 — marked "used by a visible poly" by ComputeVertexClipFlags),
// perspective-divide the VIEW-space position (+0/+4/+8) to screen:
//   invZ = 1.0 / z
//   screenX = xScale * x * invZ + xOffset      (-> +16, flt_13FCD0C / flt_13FCD18)
//   screenY = yScale * y * invZ + yOffset      (-> +20, flt_13FCAF8 / flt_13FCD10)
//   (+28 = invZ ; +64 = +68 ; these per-vertex dwords are not modelled by the affine
//    raster, which reads only +16/+20 and the UVs, so they are omitted.)
// This is the genuine universe-object projection — a real perspective divide, NOT the
// affine model->screen map of VIBE_Mesh_ProjectVerticesToScreen @0x5c5120.
//
// BACKFACE CULL (LABEL_16). For each polygon whose +36 byte has its sign bit set
// (front-candidate), the signed screen-space area of the PROJECTED triangle decides:
//   if ((v0.sx-v2.sx)*(v0.sy-v1.sy) > (v0.sx-v1.sx)*(v0.sy-v2.sy)) && !(flags38 & 4)
//       -> clear +36 bit7 (cull, back-facing) ;  the (+36 & 0x10) case sets +36 |= 0x40.
//
// The xScale/xOffset/yScale/yOffset are the engine's per-frame VIEW scalars
// (flt_13FCD0C / flt_13FCD18 / flt_13FCAF8 / flt_13FCD10) — caller-supplied (the same
// ProjectScalars the rasterizer's clip reproject uses). The fog/distance shade byte
// (+79, the byte_649DD8 sub-branch over flt_13FC544/5AC/58C + dbl_628074) is the named
// boundary (rule 8) — runtime fog globals, not reconstructed here.
// =============================================================================
namespace guild::render {

struct ObjectProjectScalars {
    float xScale  = 1.0f;   // flt_13FCD0C
    float xOffset = 0.0f;   // flt_13FCD18
    float yScale  = 1.0f;   // flt_13FCAF8
    float yOffset = 0.0f;   // flt_13FCD10
};

// gilde.exe 0x5ac970 — perspective-project the +76&0x80 vertices (z divide) into the
// vertex screen x/y (+16/+20) and backface-cull the +36&0x80 polygons by projected
// signed area. Returns the number of vertices projected. `projectAll` ignores the
// +76 gate (projects every vertex) for callers that have not run ComputeVertexClipFlags.
int ProjectObjectVertices(Vertex* verts, int count, Polygon* polys, int polyCount,
                          const ObjectProjectScalars& s, bool projectAll = false);

} // namespace guild::render
