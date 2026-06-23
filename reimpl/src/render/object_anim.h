#pragma once
// =============================================================================
// guild::render — OBJECT keyframe animation: the Catmull-Rom tangent build +
// Hermite sample that drives VIBE_Camera_Flight / CameraFlightEnhanced and every
// object MoveObject/RotateObject track.
//
// gilde.exe builds an object anim (VIBE_Anim_CreateObjectAnim @0x5cef14) as an
// array of 88-byte keyframes; each frame stores a segment DURATION (+0), a
// position (+4) and rotation (+20), plus two tangent triples used by the Hermite
// segment that STARTS at that frame: the outgoing tangent at this frame (+32 pos,
// +64 rot) and the incoming tangent at the next frame (+48 pos, +76 rot).
//
// VIBE_Anim_ComputeFrameTangents @0x5ccf70 fills them with a Catmull-Rom tangent:
//   tangent_i = 0.5 * ( (P_i - P_{i-1})/dur_{i-1} + (P_{i+1} - P_i)/dur_i )   (flt_628D4C = 0.5)
//   frame[i].outTan (+32) = tangent_i   * dur_i            (M0 of segment [i,i+1])
//   frame[i].inTan  (+48) = tangent_{i+1} * dur_i          (M1 of segment [i,i+1])
// VIBE_Anim_BuildFrameTangents @0x5cec40 runs that over the interior frames and
// zeroes the boundary tangents (the non-looping ease-in/ease-out flight).
//
// The per-frame evaluator is embedded in the 6.6 KB VIBE_Anim_UpdateSkeletonPose
// @0x5cd1d8 (skeleton + object anims together); here we reconstruct the object
// path's interpolation directly with the canonical Hermite basis the recovered
// duration-scaled tangents define — a faithful-equivalent of that path (the
// keyframes, Catmull-Rom tangents and timing are exact; the final blend is the
// standard Hermite the tangent convention implies). The byte-exact in-engine
// evaluator (UpdateSkeletonPose) is the reference of record.
// =============================================================================
#include "guild/common/types.h"
#include <vector>

namespace guild::render {

// One object-anim keyframe. `dur` is the segment length to the NEXT frame (ticks).
struct ObjAnimFrame {
    float dur = 1.0f;          // +0   segment duration to the next frame
    float pos[3]    = {0, 0, 0};  // +4   position
    float rot[3]    = {0, 0, 0};  // +20  rotation (euler)
    float posOut[3] = {0, 0, 0};  // +32  pos tangent M0 of segment [i,i+1] (= tan_i * dur_i)
    float posIn[3]  = {0, 0, 0};  // +48  pos tangent M1 of segment [i,i+1] (= tan_{i+1} * dur_i)
    float rotOut[3] = {0, 0, 0};  // +64  rot tangent M0
    float rotIn[3]  = {0, 0, 0};  // +76  rot tangent M1
};

inline constexpr float kCatmullRomScale = 0.5f;   // flt_628D4C

// gilde.exe 0x5ccf70 — VIBE_Anim_ComputeFrameTangents: fill `mid`'s outgoing
// tangents (+32/+64, scaled by mid.dur) and `prev`'s incoming tangents
// (+48/+76, scaled by prev.dur) from the Catmull-Rom tangent at `mid`.
void ComputeFrameTangents(ObjAnimFrame& prev, ObjAnimFrame& mid, const ObjAnimFrame& next);

// gilde.exe 0x5cec40 — VIBE_Anim_BuildFrameTangents (non-looping path): compute
// interior-frame tangents and zero the boundary tangents (ease in/out).
void BuildFrameTangents(std::vector<ObjAnimFrame>& frames);

// Sample the position + rotation at global time `t` (ticks from 0) via the
// per-segment Hermite using the built tangents. Clamps to [0, total]. `frames`
// must have had BuildFrameTangents run on it.
void SampleObjectAnim(const std::vector<ObjAnimFrame>& frames, float t,
                      float outPos[3], float outRot[3]);

// Total duration = sum of segment durations (frames[0..n-2].dur).
float ObjectAnimDuration(const std::vector<ObjAnimFrame>& frames);

} // namespace guild::render
