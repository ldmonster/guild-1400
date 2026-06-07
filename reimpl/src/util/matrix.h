#pragma once
#include "guild/common/types.h"

// 4x4 matrix / quaternion / Euler math reconstructed from gilde.exe's Math
// block (VIBE_Math_Matrix*/Quat*/Snap*/BuildBasis*). Companion to util/math.{h,cpp}
// (which owns the scalar/vector helpers VectorNormalize, Atan2, AcosGuarded that
// these routines call) and util/transform.{h,cpp} (the skeleton/bone Transform_*
// routines that consume MatrixTransformVectors/MatrixIdentity).
//
// Split rationale (documented per the agent guide):
//   matrix.{h,cpp}    -> pure linear-algebra primitives on a flat 16-float matrix
//                        and 4-float quaternion: identity/copy/inverse/from-euler/
//                        to-euler/decompose/transform-vectors + quat rotate/normalize
//                        + snap-to-axis + build-basis-from-angle.
//   transform.{h,cpp} -> the VIBE_Transform_* hierarchy walkers that operate on the
//                        engine "bone/frame" entity record and call into matrix.h.
//
// Fidelity notes:
//  - The original used the x87 FPU (80-bit long double) for intermediate results.
//    We model intermediates as C++ `double`; where the binary kept an 80-bit
//    temporary that double cannot reproduce bit-exactly, the comment says so.
//  - Matrix storage is a flat row of 16 floats, indexed [0..15] exactly as the
//    Hex-Rays pseudocode does. Conceptually this is a 4x4 column layout used as a
//    3x3 rotation block (indices 0,1,2 / 4,5,6 / 8,9,10) plus a translation column
//    (indices 12,13,14) and a homogeneous tail (index 15 == 1.0). We keep the raw
//    flat indexing to stay 1:1 with the decompiled arithmetic; see Mat4 below.
//  - Fmod/Sqrt are the guild::util x87 helpers (util/float_math.h); we call them
//    where the original did. Trig (sin/cos/atan2/acos) matches the original's CRT
//    dispatch and is modeled with <cmath> via util/math.h's Atan2/AcosGuarded.
namespace guild::util {

// Flat 16-float 4x4 matrix as the engine stores it. The reconstruction keeps the
// raw flat layout (no row/col accessors) so the index arithmetic matches the
// decompiled pseudocode 1:1. Offsets are float indices (byte offset = 4*index).
//
//   m[ 0] m[ 1] m[ 2] m[ 3]      // +0x00  rotation row 0 (x basis), m[3] pad
//   m[ 4] m[ 5] m[ 6] m[ 7]      // +0x10  rotation row 1 (y basis), m[7] pad
//   m[ 8] m[ 9] m[10] m[11]      // +0x20  rotation row 2 (z basis), m[11] pad
//   m[12] m[13] m[14] m[15]      // +0x30  translation + homogeneous (m[15]==1)
struct Mat4 {
    float m[16];
};

// gilde.exe 0x5cb100 — VIBE_Math_MatrixIdentity (__usercall eax=fn(dst@eax)).
// Zeroes all 16 floats then sets m[0]=m[5]=m[10]=m[15]=1.0 (the diagonal).
void MatrixIdentity(float* dst);

// gilde.exe 0x5cabf0 — VIBE_Math_MatrixCopy (__usercall eax=fn(src@eax, dst@edx)).
// NOTE: despite the name this writes the TRANSPOSE: dst[4*i + j] = src[i + 4*j].
// It is the fallback path of MatrixInverse for a (near-)singular matrix, where the
// transpose approximates the inverse of an orthonormal rotation. Faithful to the
// strided copy loop in the original.
void MatrixCopy(const float* src, float* dst);

// gilde.exe 0x5cac3c — VIBE_Math_MatrixInverse (__usercall fn(src@eax, dst@edx)).
// Full 4x4 cofactor inverse. If |det| < ~1e-7 (dbl_628D38) falls back to
// MatrixCopy (transpose). Writes 16 floats to dst.
void MatrixInverse(const float* src, float* dst);

// gilde.exe 0x5caaa4 — VIBE_Math_MatrixTransformVectors
//   (__usercall eax=fn(a@eax, b@edx, out@ebx)). Multiplies the 3x3 rotation block
// of `a` (rows a[0..2],a[4..6],a[8..10]) by each of the 4 rows of `b`, adding b's
// translation column to the last row. Writes 16 floats to `out` (out[3]=out[7]=
// out[11]=0, out[15]=1.0). Returns `a`. This is `out = a (*) b` for the engine's
// bone-matrix accumulation. Models the original's flat indexing exactly.
float* MatrixTransformVectors(float* a, const float* b, float* out);

// gilde.exe 0x5cb1bc — VIBE_Math_MatrixFromEuler
//   (__usercall eax=fn(angles@eax, dst@edx)). Builds a rotation matrix from the
// 3 Euler angles angles[0..2] (radians). Sets the homogeneous tail (dst[3,7,11]=0,
// dst[12,13,14]=0, dst[15]=1).
void MatrixFromEuler(const float* angles, float* dst);

// gilde.exe 0x5cb2cc — VIBE_Math_MatrixToEuler (__usercall fn(m@eax)).
// Extracts Euler angles back into m[0..2] (IN PLACE — overwrites the first three
// floats of the matrix), using Atan2. Gimbal-lock branch when the xy magnitude
// <= ~1.9e-6 (dbl_628D40).
void MatrixToEuler(float* m);

// gilde.exe 0x5cb354 — VIBE_Math_MatrixDecompose (__usercall eax=fn(m@eax, out@edx)).
// Recovers an averaged rotation (via three normalized basis-difference vectors fed
// through MatrixToEuler) and a translation = 0.125 * sum of the 4 row origins,
// written as out[0..2]. Returns the past-the-end source pointer (m+16), as the
// original does. See cpp for the full reconstruction.
float* MatrixDecompose(float* m, float* out);

// gilde.exe 0x5ca798 — VIBE_Math_QuatRotateVector
//   (__usercall eax=fn(q@eax, v@edx, out@ebx)). Rotates 3-vector `v` by the
// quaternion q[0..3] (x,y,z,w order; see cpp) by building the 3x3 rotation matrix
// (scale flt_628D00=2.0) and applying it. Writes 3 floats to out.
void QuatRotateVector(const float* q, const float* v, float* out);

// gilde.exe 0x5ca8c8 — VIBE_Math_QuatNormalizeAxis (__usercall fn(q@eax)).
// If q[3] (w) is in [-1, 1], rescales the xyz axis so the quaternion is unit:
// xyz *= sqrt((1 - w*w) / |xyz|^2). No-op if |xyz|^2 <= 0 or w outside [-1,1].
void QuatNormalizeAxis(float* q);

// gilde.exe 0x5ca940 — VIBE_Math_SnapVectorToAxis.
// Recovered register signature: eax=src(3 floats), edx=out(3 floats),
// ecx=ref-fmod scratch(3 floats), and a second source from the original's `edx`
// register (here `ref`). For each of 3 components: out[i] = fmod(src[i], 2pi),
// refmod[i] = fmod(ref[i], 2pi); then out[i] is shifted by +/-2pi (or left as-is)
// to minimize |refmod[i] - out[i]| — an angle-unwrap toward `ref`. Returns nothing
// observable (original returns the stale FPU status word in ax).
void SnapVectorToAxis(const float* src, const float* ref, float* out, float* refmod);

// gilde.exe 0x5ca544 — VIBE_Math_BuildBasisFromAngle (__userpurge eax=fn(dir@eax,
//   angle, out@<stack>)). Builds an orthonormal 4x4 basis (16 floats at `out`)
// whose "up"/forward column comes from normalized `dir`, with a roll of `angle`
// applied to a reference up-vector (rotated in the XZ plane by sin/cos angle).
// Falls back through two alternate reference axes if the cross is degenerate, and
// to identity if all are degenerate. Returns `out` cast to int in the original.
void BuildBasisFromAngle(const float* dir, float angle, float* out);

} // namespace guild::util
