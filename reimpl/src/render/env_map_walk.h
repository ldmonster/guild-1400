#pragma once
#include "guild/common/types.h"
#include "render/geometry_types.h"   // Vertex

// =============================================================================
// guild::render — VIBE_Mesh_ComputeVertexLighting @0x5c9054, the env-map
// reflection-UV OBJECT WALK (the tail-call of VIBE_Mesh_InterpolateMorphVertices
// @0x5c953c, run for every drawn object after the vertex transform).
//
// The original gates on a REFLECTIVE material (a material whose texture record has
// +104 & 1) and, for such an object, builds the bone world 3x3
// (VIBE_Transform_ComputeBoneWorldMatrix @0x5c8fac) and finds the active morph layer
// (VIBE_Anim_FindHighestPriorityLayer @0x5d0e84). Then per vertex (those whose +77
// "reflective" byte is set) it sources the vertex normal and maps it to a spherical
// env-map reflection UV, written to the vertex's u/v (+32/+36):
//   * SKINNED  (active morph keyframe present): normal unpacked from the keyframe's
//     +184 skin-normal byte triples (UnpackSkinNormal), then the env-map reflect map;
//   * NON-SKINNED (no keyframe): normal read from the per-vertex source normal, then
//     the same env-map reflect map.
// Both call the reconstructed per-vertex kernel render::ComputeEnvMapReflectionUv,
// which applies the bone 3x3 internally (Nt = m3x3 * normal; R = pos - 2(Nt.pos)Nt;
// uv = 0.5*R + 0.5).
//
// This reconstructs the WALK over the existing kernels. Its object-walk INPUTS —
// the bone 3x3 (ComputeBoneWorldMatrix), the active-layer skin keyframe
// (FindHighestPriorityLayer), the per-vertex source normals, and the reflective-
// material gate (texRec +104 & 1) — are supplied by the caller, the named boundary
// (rule 8): without a reflective material the engine does not run this walk at all,
// so it must be gated by the caller exactly as ProcessSceneNode's path does.
// =============================================================================
namespace guild::render {

struct EnvMapWalkInputs {
    // The bone world 3x3 in the kernel's order {m0,m1,m2, m4,m5,m6, m8,m9,m10}
    // (VIBE_Transform_ComputeBoneWorldMatrix output). Required.
    const float* m3x3 = nullptr;
    // SKINNED path: the active keyframe's +184 skin-normal byte triples (3 per
    // vertex). When non-null the skinned branch runs (UnpackSkinNormal).
    const u8* skinNormalBytes = nullptr;
    // NON-SKINNED path: per-vertex source normals (3 floats each). Used when
    // skinNormalBytes is null. Required for the non-skinned branch.
    const float* vertexNormals = nullptr;
    // Optional per-vertex +77 gate (1 = compute this vertex). Null => every vertex.
    const u8* reflectiveFlags = nullptr;
};

// gilde.exe 0x5c9054 — the env-map reflection-UV walk. For each vertex (gated by
// reflectiveFlags when given), source the normal (skin bytes -> UnpackSkinNormal,
// else vertexNormals) and write render::ComputeEnvMapReflectionUv(pos, normal, m3x3)
// into the vertex u/v (+32/+36). Returns the number of vertices whose UV was set.
// A no-op (returns 0) when m3x3 is null or neither normal source is supplied — the
// engine's "no reflective material / no layer" early-outs.
int ComputeEnvMapVertexUvs(Vertex* verts, int count, const EnvMapWalkInputs& in);

} // namespace guild::render
