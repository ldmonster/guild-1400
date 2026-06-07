#pragma once
#include "guild/common/types.h"

// =============================================================================
// guild::render — bone keyframe position/pose samplers. Faithful 1:1
// reconstruction of two gilde.exe (d3_engine.c) skeletal-sampling leaves:
//
//   0x5cc850  VIBE_Anim_GetBonePosition    (frame translation + keyframe offset)
//   0x5ccea0  VIBE_Anim_GetBoneFramePose   (raw keyframe rotation + translation)
//
// Both index the bone's anim header: the frame array base is *(*(anim+104) + 348),
// each keyframe record is 192 bytes (48 floats). Within a keyframe:
//   +32/+36/+40 (float idx 8..10)  = rotation/orientation triple
//   +44/+48/+52 (float idx 11..13) = translation triple
// `anim` here is the bone record whose +104 points at the AnimHeader; `boneFrame`
// is the bone-frame block whose floats [33..35] hold the accumulated world
// translation that GetBonePosition adds to the keyframe offset.
// =============================================================================
namespace guild::render {

// gilde.exe 0x5cc850 — VIBE_Anim_GetBonePosition
//   (__userpurge eax=boneFrame, edx=bone, ebx=frameIdx, out)
//   out[0..2] = boneFrame[33..35] + keyframe[11..13] (the +44/+48/+52 translation
//   of keyframe `frameIdx`). The keyframe array base is *(*(bone+104) + 348),
//   stride 192 bytes. Returns `out`.
float* GetBonePosition(const float* boneFrame, const void* bone, int frameIdx, float* out);

// gilde.exe 0x5ccea0 — VIBE_Anim_GetBoneFramePose
//   (__userpurge edx=bone, ecx=outRot, ebx=frameIdx, outTrans)
//   outRot[0..2]   = keyframe[8..10]  (the +32/+36/+40 rotation triple)
//   outTrans[0..2] = keyframe[11..13] (the +44/+48/+52 translation triple)
//   Keyframe base/stride as above. Returns 192 * frameIdx (the original's eax).
int GetBoneFramePose(const void* bone, float* outRot, int frameIdx, float* outTrans);

} // namespace guild::render
