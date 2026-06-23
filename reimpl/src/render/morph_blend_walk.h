#pragma once
#include "guild/common/types.h"
#include "render/geometry_types.h"   // Vertex

// =============================================================================
// guild::render — VIBE_Mesh_InterpolateMorphVertices @0x5c953c, the MORPH-ACTIVE
// (animated) object-vertex blend WALK (the branch taken when a2+380 != 0). This is
// the per-frame vertex deformation for animated character meshes.
//
// The original walks up to three morph LAYERS (records at a2+28, +116 stride, to
// a2+376). For each layer with a clip (+104) and a non-zero weight (+64, tested
// `& 0x7FFFFFFF`), it picks the two keyframes (clip+348 base, 192-byte stride;
// frame0 = layer+0, frame1 = layer+4), scales each keyframe's per-axis dequant
// scale (+20/+24/+28) and bias (+8/+12/+16) by the LAYER weight, gets the two blend
// weights (VIBE_Anim_ComputeMorphWeights @0x5c9394 -> w0,w1), and per vertex blends
// the two keyframes' quantized point bytes (+180, 3 per vertex):
//   blended[a] = (p0[a]*scale0[a]*lw + bias0[a]*lw)*w0
//              + (p1[a]*scale1[a]*lw + bias1[a]*lw)*w1
// The FIRST active layer SETS each vertex position (+0/+4/+8); subsequent layers
// ACCUMULATE (+=). After all layers the function world-transforms the vertices (the
// static path, render/mesh_transform_walk.h) and tail-calls ComputeVertexLighting.
//
// The per-vertex blend is the reconstructed kernel render::InterpolateMorphVertex.
// This module reconstructs the multi-layer set/accumulate WALK over it. The layer +
// keyframe RECORD parsing (the a2+28 / clip+348 layouts) and ComputeMorphWeights are
// the caller-supplied inputs (the named boundary, rule 8): the walk takes already-
// parsed MorphLayer inputs so it is golden-testable without the live anim records.
// =============================================================================
namespace guild::render {

// One morph keyframe (parsed from a clip+348 + 192*frame record): the per-axis
// dequant scale (+20/+24/+28) and bias (+8/+12/+16), and the quantized point bytes
// (+180; 3 unsigned bytes per vertex, promoted to i16 0..255 as the engine does).
struct MorphKeyframe {
    float     scale[3] = {0, 0, 0};
    float     bias[3]  = {0, 0, 0};
    const u8* points   = nullptr;   // 3 bytes per vertex
};

// One active morph layer: two keyframes, the layer weight (scales the keyframe
// scale/bias), and the two blend weights (VIBE_Anim_ComputeMorphWeights: w0 = v93
// for keyframe 0, w1 = v94 for keyframe 1).
struct MorphLayer {
    MorphKeyframe kf0;
    MorphKeyframe kf1;
    float layerWeight = 1.0f;   // layer+64 (gate: != 0 after masking the sign bit)
    float w0 = 0.5f;            // ComputeMorphWeights v93 (keyframe-0 weight)
    float w1 = 0.5f;            // ComputeMorphWeights v94 (keyframe-1 weight)
};

// gilde.exe 0x5c953c (morph-active branch) — accumulate the morph-blended model
// positions across `layers` into each Vertex position (+0/+4/+8): the first active
// layer SETS, the rest ACCUMULATE. A layer is skipped when it has no keyframe points
// or a zero weight (the engine's clip/weight gate). Returns the number of layers
// applied. The caller then world-transforms (TransformMeshVerticesByMatrix) and runs
// ComputeVertexLighting — exactly the static tail of the same function.
int AccumulateMorphBlend(Vertex* verts, int count, const MorphLayer* layers, int layerCount);

} // namespace guild::render
