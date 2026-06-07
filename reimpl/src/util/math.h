#pragma once
#include "guild/common/types.h"

// guild::util numeric math reconstructed from gilde.exe (VIBE_Math_*).
//
// Fidelity notes:
//  - The original used the x87 FPU (80-bit long double) for intermediate
//    results. We use C++ `double`; where the binary kept an 80-bit intermediate
//    that we cannot reproduce exactly, the per-function comment says so. For the
//    libm-equivalent calls (atan2/acos/sqrt/fmod) the original *itself* dispatches
//    to the CRT (`atan2`, `fmod`, …) whenever the FPU-feature flag byte_64A958 is
//    clear (the normal runtime case), so std::<cmath> is bit-faithful there.
//  - Random-float helpers live in src/util/math_rng_float.cpp and build on
//    guild::crt::RandNext / a private MINSTD generator (see that file).
namespace guild::util {

// ---- Scalar / range -------------------------------------------------------

// VIBE_Math_ClampValueRange @0x552734 (__usercall eax=fn(lo@eax, val@edx, out@ecx, mul@ebx)).
// Clamps `value` into [lo, lo + 10000*mul]; writes result through `out`.
// Returns 0 (and writes nothing) if lo<1 or mul<1, else 1.
int ClampValueRange(int lo, int value, int* out, int mul);

// VIBE_Math_Distance2D @0x58525c (__usercall st0=fn(ax,dx,cx,bx)).
// Returns hypot(ax-bx, dx-cx) * 2.9081632653 (a fixed pixel->world scale).
double Distance2D(int ax, int dy, int cy, int bx);

// VIBE_Math_NormalizeAngle @0x5eef4c.
// Truncates `value` toward zero (via Coord::ConvertX) and, if `value` was
// negative, adds -1.0. (Original constant dbl_62BFFC == -1.0.)
double NormalizeAngle(double value);

// VIBE_Math_StoreAndZero @0x602968. Truncates st0, stores `value` through out,
// returns value-value (== 0.0, propagating NaN/Inf like the original).
double StoreAndZero(double value, double* out);

// VIBE_Math_DecrementAndAbs @0x5f0be5. Returns -NormalizeAngle(-value).
double DecrementAndAbs(double value);

// VIBE_Math_Atan2 @0x5f5701 (__usercall st0=fn(y@st1, x@st0)). atan2(y, x).
double Atan2(double y, double x);

// VIBE_Math_Atan2Unary @0x5f56ec. atan2(y, 1.0).
double Atan2Unary(double y);

// VIBE_Math_AcosGuarded @0x5f0b9c (__usercall st0=fn(x@st0)).
// acos(x) with clamping: if 1-x*x <= 0 returns 0 (x>=1) or pi (x<=-1).
double AcosGuarded(double x);

// ---- Interpolation --------------------------------------------------------

// VIBE_Math_CatmullRomInterp @0x5ca9d8 (__stdcall). Catmull-Rom blend of the 4
// control values p0,p1,p2,p3 at parameter t (a5).
double CatmullRomInterp(float p0, float p1, float p2, float p3, float t);

// VIBE_Math_CubicBezierPoint @0x59b3d8 (__userpurge). Evaluates a 2D cubic
// Bezier (p0,p1,p2,p3 are 2-float points) at t into out[2]; returns p0.
float* CubicBezierPoint(float* p0, float* p1, float* p2, float* p3, float t,
                        float* out);

// VIBE_Math_LerpClampedCoord @0x5d9d60 (__stdcall). Orders a/b, linearly maps
// d along [a,b]/span, rounds to nearest int.
int LerpClampedCoord(float a, float b, float span, float d);

// ---- Vector (3-float arrays; element [1] unused/zeroed in some callers) ----

// VIBE_Math_VectorLerp @0x5ca2fc. out = a + (b-a)*t (component-wise). Returns a.
float* VectorLerp(float* a, float* b, float t, float* out);

// VIBE_Math_VectorNormalize @0x5cb148. Normalizes v in place; zero-length -> 0.
float* VectorNormalize(float* v);

// VIBE_Math_VectorWithinTolerance @0x5caa4c. True if |a-b| <= tol per component.
bool VectorWithinTolerance(float* a, float* b, float tol);

// VIBE_Math_TriangleNormal @0x5cb824. Normalized cross of (b-a)x(c-a) into out.
float* TriangleNormal(float* a, float* b, float* c, float* out);

// VIBE_Math_VectorAngleBetween @0x5ca334. Signed angle (radians) between the
// XZ-projection of two direction vectors (Y is zeroed). Returns -pi for opposite.
double VectorAngleBetween(float* a, float* b);

// VIBE_Math_VectorAngleWrapped @0x5ca504. VectorAngleBetween, then if the result
// is < -pi adds 2*pi (single-step wrap to (-pi, pi]).
double VectorAngleWrapped(float* a, float* b);

// VIBE_Math_MaxVectorLength @0x5cffac. sqrt of the max squared-length over an
// array of `count` 4-float-stride vectors (start seed -1.0).
double MaxVectorLength(float* vecs, int count);

// ---- 64-bit integer helpers (compiler intrinsics in the original) ---------

// VIBE_Math_UInt64Multiply @0x14210c0 (__stdcall). Full 64x64 product.
u64 UInt64Multiply(u64 a, u64 b);

// VIBE_Math_Multiply64 @0x6068dc. Low 32 bits of (a@edx:eax) * (b@ebx:ecx);
// returns (i32)(a_lo * b_lo) regardless of the high words.
i32 Multiply64(i32 a_lo, i32 a_hi, i32 b_lo, i32 b_hi);

// VIBE_Math_UnsignedLongLongDivide @0x5e57e7 / VIBE_Math_LongLongDivide @0x5e5792.
// These are the MSVC __aulldiv/__alldiv intrinsics with a register signature.
// Faithful behavior == 64-bit integer division; exposed as normal C++ helpers.
u64 UnsignedLongLongDivide(u64 num, u64 den);
i64 LongLongDivide(i64 num, i64 den);

} // namespace guild::util
