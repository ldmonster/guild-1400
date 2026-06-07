#pragma once
#include "guild/common/types.h"

// Per-vertex environment-map lighting + morph-vertex interpolation cores for the
// guild::render mesh stage (d3_engine.c).
//
// Reconstructed here (1:1 math cores):
//   VIBE_Mesh_ComputeVertexLighting       @0x5c9054  (env-map reflection UV)
//   VIBE_Mesh_InterpolateMorphVertices    @0x5c953c  (morph-target vertex blend)
//
// SCOPE NOTE: the production functions walk the object record (the +4 mesh block,
// the +5 light array, the bone/skin palette, VIBE_Anim_FindHighestPriorityLayer,
// VIBE_Transform_ComputeBoneWorldMatrix) and write back into the 80-byte engine
// vertex records. Those object-walk shells are DEFERRED (see report). This module
// reconstructs the two self-contained per-vertex math kernels they apply, so they
// can be golden-tested in isolation.
namespace guild::render {

// ---------------------------------------------------------------------------
// Recovered constants (exact bytes via get_bytes):
//   flt_628CBC = 0xC0000000 = -2.0   (reflection coefficient: R = P - 2(N.P)N)
//   flt_628CC0 = 0x3F000000 =  0.5   (env-map UV scale/bias: uv = 0.5*R + 0.5)
//   flt_628CC4 = 0x3C008081 ≈ 0.0078431373 = 2/255 (skin normal byte -> unit)
//   flt_628CC8 = 0xC3000000 = -128.0 (skin normal byte bias)
// ---------------------------------------------------------------------------

// gilde.exe 0x5c9054 — VIBE_Mesh_ComputeVertexLighting (env-map reflection UV core)
//
// For one vertex, given:
//   pos    : the vertex model/world position (3 floats; *(vert+0..8))
//   normal : the vertex normal (3 floats)
//   m3x3   : the bone/world rotation matrix's 3x3, in the engine's flat layout
//            (the v20..v28 block: indices 0,1,2 / 3,4,5 / 6,7,8 of a 9-float row,
//            which is what ComputeBoneWorldMatrix's MatrixTransformVectors produces
//            as out[0],out[4],out[8] | out[1],out[5],out[9] | out[2],out[6],out[10]).
//            Pass the 9 floats in the order {m0,m1,m2, m4,m5,m6, m8,m9,m10}.
// Computes the reflection of the position vector about the matrix-rotated normal and
// maps it to a spherical env-map UV:
//   Nt = m3x3 * normal
//   d  = (Nt . pos) * (-2.0)
//   R  = d*Nt + pos
//   normalize(R)
//   u  = 0.5 * R.x + 0.5;   v = 0.5 * R.y + 0.5;
// Writes {u, v} to outUv. Mirrors the non-skinned branch of the original verbatim
// (the skinned branch only differs in how `normal` is unpacked from palette bytes —
// see UnpackSkinNormal below).
void ComputeEnvMapReflectionUv(const float* pos, const float* normal,
                               const float* m3x3, float* outUv);

// Unpacks a skinned-vertex normal byte triple to a unit-ish float vector, exactly as
// the original's skin branch: n[i] = (byte[i] - 128) * (2/255).
//   v29 = ((double)(__int16)b0 + (-128.0)) * (2/255), etc.
void UnpackSkinNormal(const u8* bytes, float* outNormal);

// gilde.exe 0x5c953c — VIBE_Mesh_InterpolateMorphVertices (morph blend core)
//
// One morph vertex blended between two keyframe samples with two weights, then
// transformed by a 4x4 world matrix. The original packs each frame's per-axis scale
// (frame[20/24/28]) and bias (frame[8/12/16] etc.) and reads the morph point as a
// quantized i16 byte triple; this reconstruction exposes the decoded blend:
//
//   blended[a] = (p0[a]*scale0[a] + bias0[a]) * w0 + (p1[a]*scale1[a] + bias1[a]) * w1
//
// where p0/p1 are the (signed) quantized point components for the two frames, scale0/
// bias0 and scale1/bias1 are the two frames' per-axis dequant terms, and w0/w1 are
// the morph weights (VIBE_Anim_ComputeMorphWeights). Then the result is transformed
// by the 4x4 `world` matrix (flat 16-float, the v6/v67 block) into outPos:
//   outPos = world.rotate(blended) + world.translation
// Writes 3 floats to outPos. This is the per-vertex kernel of the morph loop.
void InterpolateMorphVertex(const i16* p0, const float* scale0, const float* bias0,
                            const i16* p1, const float* scale1, const float* bias1,
                            float w0, float w1, const float* world, float* outPos);

// Transforms a model-space point by a flat 16-float world matrix (the non-morph
// vertex path of VIBE_Mesh_InterpolateMorphVertices and the static fallbacks):
//   out.x = p.x*m[0] + p.y*m[4] + p.z*m[8]  + m[12];  (and y/z via m[1,5,9,13] / m[2,6,10,14])
// Matches the `*v7 * *v6 + ...` flat-index multiply used throughout the function.
void TransformPointByWorldMatrix(const float* p, const float* world, float* out);

} // namespace guild::render
