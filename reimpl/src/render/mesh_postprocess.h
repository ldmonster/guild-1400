#pragma once
#include "render/mesh_load.h"

// =============================================================================
// guild::render — mesh post-load geometry passes.
//
//   0x5D1B54  VIBE_Mesh_ComputeBoundingExtents
//   0x5D1A6C  VIBE_Mesh_ComputeVertexNormals
//
// Both operate on the loaded Mesh's raw 24-byte vertex / 56-byte poly arrays.
// =============================================================================
namespace guild::render {

// gilde.exe 0x5D1B54 — VIBE_Mesh_ComputeBoundingExtents (__usercall eax=fn(a1)).
//   1. radius = max over all verts of sqrt(x^2+y^2+z^2)  (stored at +472, +468).
//   2. zero the centroid (+104/+108/+112).
//   3. min/max each axis over all verts; write the 8 AABB corner positions into
//      the 8 slack vertex slots (indices vertexCount..vertexCount+7).
//   4. radius2 (+472) = length of the AABB diagonal (max-min).
//   5. centroid = (1/8) * sum of the 8 corner positions  (flt_628FC0 = 0.125).
// Requires the vertex array to have at least vertexCount+8 slots.
void ComputeBoundingExtents(Mesh& m);

// gilde.exe 0x5D1A6C — VIBE_Mesh_ComputeVertexNormals (__usercall eax=fn(result)).
//   1. per polygon: face normal = TriangleNormal(v0, v1, v2) stored at poly +44.
//   2. per vertex: sum the face normals of every polygon referencing it (by
//      index match against vtx[0/1/2]), normalize, store at vertex +12.
void ComputeVertexNormals(Mesh& m);

} // namespace guild::render
