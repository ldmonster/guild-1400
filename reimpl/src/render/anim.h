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

// A single keyframe's duration + translation sample (the +0x04 dword and the
// +32/+36/+40 float triple the original reads from each 192-byte bone-frame
// record: durations at record+4 @0x5cbc39/0x5cbc6b, translation @+0x20/24/28).
// The full record carries rotation and flags too; the blend needs only these.
struct BoneKeyframe {
    int   dur; // +0x04 — per-frame segment duration (phase denominator)
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
//   keys        : array of per-frame duration+translation samples (192-byte stride
//                 in the original; here a flat BoneKeyframe array indexed by frame).
//   fromFrame   : current frame index (v35 = obj+0x0C in the original)
//   toFrame     : target frame index (v27 = ecx arg)
//   curPhase    : current sub-frame phase at fromFrame (v29 = obj+0x10)
//   targetPhase : target sub-frame phase at toFrame (v31 = ebx arg)
//   out         : receives the accumulated translation delta (3 floats), BEFORE the
//                 bone-matrix rotation and base-translation add (those are applied
//                 by the caller; see UpdateSkeletonPose, not reconstructed here).
//
// Leading segment prorated by 1 - curPhase/keys[fromFrame].dur; trailing segment by
// targetPhase/keys[toFrame].dur — or (targetPhase-curPhase)/keys[toFrame].dur when
// fromFrame == toFrame (branch @0x5cbe08). No zero guards on the durations.
void AccumulateBoneTranslation(const BoneKeyframe* keys, int fromFrame, int toFrame,
                               int curPhase, int targetPhase, float* out);

} // namespace guild::render
