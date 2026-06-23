#pragma once
#include "guild/common/types.h"
#include "render/geometry_types.h"   // Vertex

// =============================================================================
// guild::render — VIBE_Mesh_InterpolateMorphVertices @0x5c953c, the STATIC
// (non-morph) object-vertex transform walk. This is the per-object vertex
// transform VIBE_Render_ProcessSceneNode @0x5add1c calls before clip/append:
// it transforms every vertex's model position by the object's world matrix
// (the engine's drawData+72, a3).
//
// The function has three branches:
//   * morph-active (a2+380 != 0): blends two morph keyframes per vertex
//     (VIBE_Anim_ComputeMorphWeights + quantized i16 points) then world-transforms
//     — the ANIMATED character path; the per-vertex blend kernel is reconstructed in
//     render/vertex_lighting.cpp (InterpolateMorphVertex), the walk is DEFERRED here.
//   * static + bounds (a1+529 & 0x20): world-transform every vertex AND accumulate
//     the running near/far z bounds (flt_13FD168[0] / flt_13FCF3C).
//   * static (the common universe-object case): world-transform every vertex.
// All three tail-call VIBE_Mesh_ComputeVertexLighting (0x5c9054) — the env-map /
// lighting pass, the NAMED boundary here (rule 8; its object-walk shell is deferred).
//
// The static transform per vertex (verbatim from 0x5c9bdb/0x5c95d6) is exactly
// render::TransformPointByWorldMatrix (render/vertex_lighting.h): the 16-float
// column-major world matrix `m` maps the source model pos -> the transformed pos:
//   out.x = x*m[0] + y*m[4] + z*m[8]  + m[12]
//   out.y = x*m[1] + y*m[5] + z*m[9]  + m[13]
//   out.z = x*m[2] + y*m[6] + z*m[10] + m[14]
// The engine reads the SOURCE pos from each vertex's +72 ptr and writes the result
// to the vertex's +0/+4/+8; this reconstruction transforms the Vertex position
// (+0/+4/+8) in place (the caller seeds those with the source model pos).
// =============================================================================
namespace guild::render {

struct MeshTransformResult {
    int   count = 0;            // vertices transformed
    float nearZ = 1e30f;        // running min transformed-z (flt_13FD168[0]) when trackDepth
    float farZ  = -1e30f;       // running max transformed-z (flt_13FCF3C)  when trackDepth
};

// gilde.exe 0x5c953c (static / static+bounds branches) — transform `count` Vertex
// positions in place by the column-major 16-float world matrix `world`. When
// `trackDepth` (the original's a1+529 & 0x20 path) the running near/far z bounds are
// updated from the transformed z, seeded by nearSeed/farSeed (the engine's 1e10 / 0).
MeshTransformResult TransformMeshVerticesByMatrix(
    Vertex* verts, int count, const float world[16],
    bool trackDepth = false, float nearSeed = 1e30f, float farSeed = -1e30f);

} // namespace guild::render
