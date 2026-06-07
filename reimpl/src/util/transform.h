#pragma once
#include "guild/common/types.h"

// Skeleton / bone-hierarchy transform routines reconstructed from gilde.exe's
// VIBE_Transform_* block. These walk an engine "frame/bone" entity record and use
// the linear-algebra primitives in util/matrix.{h,cpp} (MatrixTransformVectors,
// MatrixIdentity) to compose per-bone transforms up a parent chain.
//
// The entity record is a large engine struct; these routines touch it only via
// fixed byte offsets recovered from the decompilation. Rather than model the whole
// struct, we expose the original raw register signatures with the offset semantics
// documented per function (byte offsets from the base pointer). The frame "bone"
// matrix lives at +0x18C (float index 99) as a 4x4 (16 floats); the per-bone local
// 3x3 rotation lives at +0x18C..+0x1B4 (indices 99..110), translation parts at
// +0x4C/+0x6C/+0x78 (indices 19,27,30), and the parent link at +0x1F8 (index 126,
// byte 504). See each function for the exact offsets it uses.
//
// These are 1:1 translations of pointer-arithmetic-heavy code; the original used
// raw `int` base addresses and `float*` reinterpretation. We keep `Frame` as an
// opaque byte/float view (see FrameRef below) and index by the original float/byte
// offsets so the arithmetic stays verbatim.
namespace guild::util {

// A frame/bone entity record viewed as a flat float array. All offsets below are
// FLOAT indices unless a byte offset is noted; byte offset = 4 * float index.
//   +0x4C  (f[19])  pivot/origin A (3 floats)
//   +0x6C  (f[27])  pivot/origin B (3 floats)
//   +0x78  (f[30])  local translation (3 floats)
//   +0x18C (f[99])  4x4 frame matrix (16 floats) — also the 3x3 at f[99..110]
//   +0x210 (528)    sign/flags byte (f-relative byte 528)
//   +0x1F8 (504)    parent link pointer (byte 504 == f[126])
// The reconstruction passes the base as `float*`; functions that follow the parent
// chain take the next frame via the embedded pointer (modeled as FrameRef*).
struct FrameRef {
    // Opaque: callers supply a buffer large enough for the offsets each routine
    // touches. Provided as a typed alias so signatures read clearly.
    float* data;
};

// gilde.exe 0x5c8b38 — VIBE_Transform_PointThroughBoneChain
//   (__usercall eax=fn(frame@eax, point@edx, out@ebx)). Translates `point` by the
//   frame's local translation (frame[30..32]), then walks the parent chain (link at
//   byte 504): for each parent, offsets by its pivot (i+108), rotates by its 3x3
//   (i+396 block, indices 99..110 i.e. bytes 396/412/428...), un-offsets, and adds
//   two translation terms (i+76, i+120). Writes 3 floats to `out`. `frame` and the
//   chain links are raw base addresses; `next = *(frame_bytes + 504)`.
//   Returns a pointer into the scratch/out (original returns `result`).
float* PointThroughBoneChain(float* frame, const float* point, float* out);

// gilde.exe 0x5c8d0c — VIBE_Transform_PointThroughBoneChainPivot
//   (__usercall eax=fn(frame@eax, point@edx, out@ebx)). Like PointThroughBoneChain
//   but first maps `point` through the frame's own pivot block (frame[27..29] /
//   frame[99..109] 3x3 / frame[19..21]) before delegating to PointThroughBoneChain.
float* PointThroughBoneChainPivot(float* frame, const float* point, float* out);

// gilde.exe 0x5c8990 — VIBE_Transform_RotateVectorByHierarchy
//   (__usercall eax=fn(frame@eax, vec@edx, out@ebx)). Rotates direction `vec` by the
//   frame's 3x3 (bytes 396.. = indices 99..) then by each parent's 3x3 up the chain
//   (link at byte 504). No translation. Writes 3 floats to `out`.
float* RotateVectorByHierarchy(float* frame, const float* vec, float* out);

} // namespace guild::util
