#pragma once
// guild::render — per-frame animation NORMAL -> per-frame LIGHTING glue.
//
// THE GAP THIS CLOSES
// -------------------------------------------------------------------------
// VIBE_Anim_CalculateAnimNormals @0x5d0020 (render/anim_normals.h:
// CalculateAnimNormals / CalculateClipNormals) recomputes, for every frame of a
// morph/skeletal clip, the per-vertex UNIT NORMALS of the DEFORMED mesh — because
// after a frame's morph deltas move the vertices, the rest-pose normals are stale.
// VIBE_Mesh_ComputeVertexLighting @0x5c9054 (render/skeleton_pose.h:
// ComputeMeshVertexLightingNonSkinned) is the object env-map lighting walk: for each
// lit vertex it reflects the position about the matrix-rotated vertex NORMAL (read
// from the +72 source block's +12) and writes the spherical env-map UV to +32/+36.
//
// In the engine the lighting walk reads whatever normal currently sits at the
// vertex's +12 source slot. For a STATIC mesh that is the (correct) rest normal; for
// an ANIMATED/posed mesh the deform pass must first push the frame's freshly computed
// normal into +12, otherwise the env-map UV is lit with the stale rest-pose normal
// (the documented "normals stay static after deformation" gap). This module is the
// glue: it takes one frame's normals (the outNormals[f] CalculateAnimNormals produces)
// and applies them to the mesh's per-vertex VertexSource.normal, then runs the
// lighting walk so the posed frame is lit with the CORRECT per-frame normals.
//
// Pure (no scene-graph / OS): the input is a MorphMeshBlock (the posed-frame vertex
// view) plus the flat per-frame normal array; the output is the lit +32/+36 UV the
// lighting walk writes, exactly as ComputeMeshVertexLightingNonSkinned computes it.
#include "render/skeleton_pose.h"  // MorphMeshBlock, VertexSource, ComputeMeshVertexLightingNonSkinned

#include <vector>

namespace guild::render {

// gilde.exe 0x5d0020 -> 0x5c9054 glue — feed CalculateAnimNormals output into
// VIBE_Mesh_ComputeVertexLighting for an animated/posed frame.
//
//   mesh         : the posed-frame mesh block (its `sources[i].normal` slots are the
//                  +72/+12 lighting inputs the walk reads; positions in `vertices[i]`
//                  are the already-posed world/model positions).
//   frameNormals : the frame's per-vertex unit normals, flat vertexCount*3 floats —
//                  i.e. outNormals[f] from CalculateAnimNormals/CalculateClipNormals.
//   m3x3         : the bone/world 3x3 rotation in the engine's flat
//                  {m0,m1,m2, m4,m5,m6, m8,m9,m10} order (same as the lighting walk).
//
// For each vertex i it writes frameNormals[3*i .. 3*i+2] into mesh.sources[i].normal
// (the engine's per-frame +12 normal update after deformation), then calls
// ComputeMeshVertexLightingNonSkinned(mesh, m3x3) once so the whole frame is relit.
// Only as many normals as are supplied are copied (frameNormals shorter than
// vertexCount*3 leaves the trailing source normals untouched); the relight still runs.
void RelightPosedFrame(MorphMeshBlock& mesh, const std::vector<float>& frameNormals,
                       const float* m3x3);

} // namespace guild::render
