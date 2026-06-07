#pragma once
#include "render/geometry_types.h"

// Skeletal / keyframe animation blending for the guild::render pipeline
// (d3_engine.c anim block). This module owns the interpolation math that turns a
// pair of keyframes + a blend parameter into a transform delta.
//
//   VIBE_Math_VectorLerp            @0x5ca2fc  (3-vector linear blend)
//   VIBE_Anim_InterpolateBoneFrame  @0x5cbc10  (keyframe translation accumulation)
//
// The full skeleton pose update (VIBE_Anim_UpdateSkeletonPose @0x5cd1d8, 0x1A25
// bytes) and bone-matrix palette (VIBE_Anim_ComputeBoneMatrices @0x5cc0d0) are the
// large consumers of these primitives and are NOT reconstructed here (see report).
namespace guild::render {

// gilde.exe 0x5ca2fc — VIBE_Math_VectorLerp
//   (__userpurge fn(a@eax, b@edx, t, out)). Component-wise out = a + (b-a)*t for a
// 3-vector. Note the original mixes the multiply order per component (b-a)*t vs
// t*(b-a); for IEEE floats this is commutative so the result is identical. Returns a.
const float* VectorLerp(const float* a, const float* b, float t, float* out);

// A single keyframe's translation sample (the +32/+36/+40 float triple the original
// read from each 192-byte bone-frame record). The full record carries rotation and
// flags too; the translation blend below only needs these three.
struct BoneKeyframe {
    float tx;  // +0x20 (frame+32)
    float ty;  // +0x24 (frame+36)
    float tz;  // +0x28 (frame+40)
};

// gilde.exe 0x5cbc10 — VIBE_Anim_InterpolateBoneFrame (translation core)
//
// The original walks the keyframe range [fromFrame .. toFrame] accumulating the
// per-segment translation deltas, prorating the first and last partial segments by
// the sub-frame phase, then rotates the accumulated delta by the bone's 3x3 and
// adds the bone's base translation. This function reconstructs that accumulation in
// isolation, given the keyframe array and the phase parameters:
//
//   keys      : array of per-frame translation samples (192-byte stride in the
//               original; here a flat BoneKeyframe array indexed by frame).
//   fromFrame : current frame index (v35 / v27 in the original)
//   toFrame   : target frame index
//   phaseNum  : numerator of the sub-frame phase (v29 .. v31 progression)
//   segLen    : per-segment length (v28 = frames[from].duration)
//   out       : receives the accumulated translation delta (3 floats), BEFORE the
//               bone-matrix rotation and base-translation add (those are applied by
//               the caller; see UpdateSkeletonPose, not reconstructed here).
//
// At from==to with phaseNum==segLen the delta equals the full from->to translation
// difference; intermediate phases prorate linearly. Matches the original's
// (delta * phase/segLen) accumulation.
void AccumulateBoneTranslation(const BoneKeyframe* keys, int fromFrame, int toFrame,
                               int phaseNum, int segLen, float* out);

} // namespace guild::render
