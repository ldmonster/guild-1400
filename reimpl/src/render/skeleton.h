#pragma once
#include "guild/common/types.h"

// Skeletal-animation consumers for the guild::render pipeline (d3_engine.c anim
// block). This module owns the bone-hierarchy matrix accumulation and the keyframe
// interpolation core that turn an animation's per-frame translation samples into a
// posed bone transform, plus the frame-advance state primitive that drives loop /
// ping-pong playback.
//
// Reconstructed here (1:1):
//   VIBE_Transform_RotateVectorWithFrame   @0x5c8ab4
//   VIBE_Transform_AccumulateBoneMatrices  @0x5c8eb4
//   VIBE_Transform_ComputeBoneWorldMatrix  @0x5c8fac
//   VIBE_Anim_AdvanceFrameIndex            @0x5ccf18
//   VIBE_Anim_InterpolateBoneFrame         @0x5cbc10  (translation-accumulation core)
//
// The bone records are large engine "frame" entities addressed by raw byte offsets
// in the original; rather than model the whole entity we view the base as float*
// and index by the ORIGINAL float index (byte offset = 4 * index). Parent links are
// pointers embedded at byte 504 (float index 126). See util/transform.h for the
// companion RotateVectorByHierarchy / PointThroughBoneChain walkers that these reuse.
//
// SCOPE NOTE: the full pose driver VIBE_Anim_UpdateSkeletonPose (@0x5cd1d8) and the
// bone-matrix palette VIBE_Anim_ComputeBoneMatrices (@0x5cc0d0) are object/scene-
// graph state machines (they walk the object's draw-data block, attachment lists,
// and call VIBE_Object_SetPosition / VIBE_Texture_AdvanceAnimFrames /
// VIBE_Light_BuildVegetationCache). They are DEFERRED (see report); this module
// reconstructs the self-contained transform / interpolation math they are built on.
namespace guild::render {

// ===========================================================================
// Animation / keyframe record layouts (recovered byte-for-byte from
// VIBE_ModelIo_LoadBinaryAnimation @0x5e450c and VIBE_Anim_InterpolateBoneFrame
// @0x5cbc10 / VIBE_Anim_ComputeBoneMatrices @0x5cc0d0).
// ===========================================================================

// Per-frame keyframe record — 192-byte stride (iterated `+= 192` / `192 * frame`
// throughout the anim code). Only the fields the reconstructed routines touch are
// named; the remainder is reserved padding so sizeof == 192 exactly.
struct AnimFrame {
    i32   timeOrIndex;  // +0x00  frame time / index (token 24 in the loader)
    i32   duration;     // +0x04  segment length to next frame (loader post-pass; *=3)
    float bbMinX;       // +0x08  per-frame point AABB min x
    float bbMinY;       // +0x0C  per-frame point AABB min y
    float bbMinZ;       // +0x10  per-frame point AABB min z
    float bbExtX;       // +0x14  AABB extent x * (1/255)
    float bbExtY;       // +0x18  AABB extent y * (1/255)
    float bbExtZ;       // +0x1C  AABB extent z * (1/255)
    float tx;           // +0x20  (+32) root translation x  (lerped per InterpolateBoneFrame)
    float ty;           // +0x24  (+36) root translation y
    float tz;           // +0x28  (+40) root translation z
    float rx;           // +0x2C  (+44) root rotation/aux x
    float ry;           // +0x30  (+48) root rotation/aux y
    float rz;           // +0x34  (+52) root rotation/aux z
    // Reserved tail to the 192-byte stride. In the original this carries the
    // attachment vec3 pairs (+84, 24*i), the wpoints byte buffer ptr (+180), and the
    // points float ptr (+188). The reconstruction surfaces points/attachments via the
    // Animation container, so this is opaque padding sized to the engine stride.
    u8    _tail[192 - 56]; // +0x38 .. +0xBF  (56 bytes of named fields precede)
};
static_assert(sizeof(AnimFrame) == 192, "AnimFrame must be the 192-byte engine stride");

// Animation header — 0x16C (364) bytes (VIBE_Memory_AllocDebug((char*)0x16C, ...)).
// Field offsets are the ORIGINAL byte offsets; *((_DWORD*)v3 + N) == byte 4*N.
struct AnimHeader {
    char  name[64];     // +0x00  name (StrNCopyPad 63)
    u8    _pad40[256];  // +0x40 .. +0x13F  reserved (mesh/lod/material book-keeping)
    i32   vertexCount;  // +0x140 (idx 80)  number of morph points per frame
    i32   loopAttach;   // +0x144 (idx 81)  attachment / loop sub-count (token 55)
    i32   frameCount;   // +0x148 (idx 82)  number of frames (token 35)
    i32   _pad14C;      // +0x14C (idx 83)
    i32   startFrame;   // +0x150 (idx 84)  token 41 (clamped into [0, frameCount-1])
    i32   endFrame;     // +0x154 (idx 85)  token 42 (clamped into [0, frameCount-1])
    i32   _pad158;      // +0x158 (idx 86)
    AnimFrame* frames;  // +0x15C (idx 87)  frame data array (192 * frameCount bytes)
    u8    _pad160[8];   // +0x160 .. +0x167
    u8    flag360;      // +0x168 (byte 360) cleared to 0 by the loader
    u8    flag361;      // +0x169 (byte 361) = the `a3` load flag (delta-encode mode)
    u8    _pad16A[2];   // +0x16A .. +0x16B
};
// The original 32-bit record is exactly 0x16C (364) bytes; the comment offsets above
// are those ORIGINAL byte offsets. With a native 64-bit `frames` pointer the in-memory
// reconstruction is larger, but AnimHeader is never serialised (the loader fills it by
// field), so the field order + documented offsets are what matter — see geometry_types.h
// for the same convention applied to Polygon.

// ===========================================================================
// Frame-advance state primitive.
// ===========================================================================

// gilde.exe 0x5ccf18 — VIBE_Anim_AdvanceFrameIndex
//   (__userpurge eax=fn(flags@ah, cur@edx, last@ecx, first@ebx, count)). Computes
//   the *next* frame index for a playing track given its mode flags:
//     bit1 (0x2)  = reverse (ping-pong active): step toward `first`, clamp at first
//     bit4 (0x10) = clamp-to-count (one-shot to count-1) vs hold/loop
//     bit0 (0x1)  = loop (wrap), else hold
//   Faithful translation of the 1:1 branch ladder. `flags` is *(track+109)>>8 in
//   the caller (the high byte of the dword at +108), passed as a small integer.
i32 AdvanceFrameIndex(u8 flags, i32 cur, i32 last, i32 first, i32 count);

// ===========================================================================
// Keyframe translation interpolation (the core of VIBE_Anim_InterpolateBoneFrame).
// ===========================================================================

// gilde.exe 0x5cbc10 — VIBE_Anim_InterpolateBoneFrame (translation-accumulation core)
//
// The original walks the keyframe segments [fromFrame .. toFrame] accumulating the
// per-segment translation delta (frame.tx/ty/tz at +32/+36/+40), prorating the
// leading and trailing partial segments by the sub-frame phase, then rotates the
// accumulated delta by the bone's local 3x3 (frame indices 99/103/107 | 100/104/108
// | 101/105/109) and adds the bone's base translation (frame indices 19/20/21).
//
// This reconstruction takes the keyframe array + phase parameters directly (the
// caller in the engine derives them from the playing track state) and returns the
// final translation that the original feeds to VIBE_Object_SetPosition:
//
//   frames     : the AnimFrame array (192-byte stride).
//   bone       : the bone/frame float-record base (for the 3x3 + base translation).
//   fromFrame  : current frame index   (*(track) in the original)
//   toFrame    : target frame index    (*(track+4))
//   phaseNum   : sub-frame phase numerator (*(track+8))
//   out        : receives the posed translation (3 floats).
//
// `seg(k)` length is frames[k].duration (the original reads *(frame+4)); the leading
// term is prorated by (1 - phaseNum/seg(from)) when fromFrame<toFrame, whole inner
// segments add in full, and the trailing term by phaseNum/seg(to) (or, when
// fromFrame==toFrame, by (phaseEnd-phaseNum)/seg). Matches the original exactly.
void InterpolateBoneFrame(const AnimFrame* frames, const float* bone,
                          i32 fromFrame, i32 toFrame, i32 phaseNum, i32 phaseEnd,
                          float* out);

// ===========================================================================
// Bone-hierarchy world-matrix accumulation.
// ===========================================================================

// gilde.exe 0x5c8eb4 — VIBE_Transform_AccumulateBoneMatrices
//   (__usercall eax=fn(frame@eax, isRoot@dl, out@ecx, acc@ebx)). Recursively walks
// the bone parent chain (link at byte 504 / float index 126), composing each bone's
// local 4x4 (the 3x3 at byte 396 == float index 99, i.e. frame[99..110]) into the
// accumulator via MatrixTransformVectors. On a non-root bone it pre-offsets the
// accumulator's translation column by the bone's pivot/translation terms (bytes
// 108/112/116 = idx 27/28/29, 120/124/128 = idx 30/31/32, 76/80/84 = idx 19/20/21);
// on the root it zeroes the +444 scratch triple. `acc` is the 16-float accumulator
// (identity on entry). Writes the composed 16-float matrix to `out` at the leaf.
//
//   frame : the bone float-record base (raw `int` base in the original).
//   isRoot: nonzero on the first (deepest) call; clears the +444 scratch.
//   out   : receives the final 16-float bone matrix.
//   acc   : the running 16-float accumulator (caller passes identity).
float* AccumulateBoneMatrices(float* frame, u8 isRoot, float* out, float* acc);

// gilde.exe 0x5c8fac — VIBE_Transform_ComputeBoneWorldMatrix
//   (__usercall eax=fn(frame@eax, pivot@edx, isRoot@bl, out@ecx)). Builds the bone's
// world matrix: starts from identity, accumulates the parent chain via
// AccumulateBoneMatrices, then (when `pivot` != null) subtracts the pivot's base
// (pivot[19..21]) and local (pivot[30..32]) translations from the accumulated
// translation column and re-applies the pivot's own 3x3 (pivot+99) via
// MatrixTransformVectors. With pivot==null it returns the raw accumulated matrix.
// Writes 16 floats to `out`. `pivot` is the world/camera frame in the original
// (dword_13FCD1C) or null when the bone's +528 sign byte is set.
float* ComputeBoneWorldMatrix(float* frame, const float* pivot, u8 isRoot, float* out);

// gilde.exe 0x5c8ab4 — VIBE_Transform_RotateVectorWithFrame
//   (__usercall eax=fn(frame@eax, basis@edx, out@ecx, vec@ebx)). Rotates direction
// `vec` up the bone hierarchy (RotateVectorByHierarchy), then — when the frame's
// +528 sign byte is clear and `basis` is non-null — additionally rotates the result
// by `basis`'s 3x3 (basis indices 99/103/107 | 100/104/108 | 101/105/109). With the
// sign byte set or basis==null it returns the plain hierarchy rotation. Writes 3
// floats to `out`. `signByte` is the frame's *(frame+528) value (negative => skip).
float* RotateVectorWithFrame(float* frame, i8 signByte, const float* basis,
                             const float* vec, float* out);

} // namespace guild::render
