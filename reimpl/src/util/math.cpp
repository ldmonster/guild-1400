#include "util/math.h"
#include "util/coord.h"   // ConvertX (truncate toward zero)
#include "util/math_trig.h" // Atan2/AcosGuarded helpers (libm-faithful)
#include <cmath>

namespace guild::util {

// gilde.exe 0x552734 — VIBE_Math_ClampValueRange
//   if (lo<1 || mul<1) return 0;
//   hi = lo + 10000*mul; v = (val>=hi)? hi : val;
//   if (v<=lo) *out=lo; else *out = (val>=hi? hi : val);  return 1;
int ClampValueRange(int lo, int value, int* out, int mul) {
    if (lo < 1 || mul < 1)
        return 0;
    int hi = lo + 10000 * mul;
    int clamped = (value >= hi) ? hi : value;
    if (clamped <= lo)
        *out = lo;
    else
        *out = (value >= hi) ? hi : value;
    return 1;
}

// gilde.exe 0x58525c — VIBE_Math_Distance2D
//   sqrt((ax-bx)^2 + (dy-cy)^2) * dbl_6264DC(=2.9081632653)
// Original computes fabs() of each delta first; the squares make the abs moot.
double Distance2D(int ax, int dy, int cy, int bx) {
    double dx = std::fabs(static_cast<double>(ax) - static_cast<double>(bx));
    double dyv = std::fabs(static_cast<double>(dy) - static_cast<double>(cy));
    return std::sqrt(dx * dx + dyv * dyv) * 2.9081632653;
}

// gilde.exe 0x602968 — VIBE_Math_StoreAndZero (__stdcall)
//   ConvertX();  *out = value;  return value - value;
// The leading ConvertX truncates whatever is on st0 (the caller's scratch); the
// observable result is the store plus a NaN-preserving zero.
double StoreAndZero(double value, double* out) {
    *out = value;
    return value - value;
}

// gilde.exe 0x5eef4c — VIBE_Math_NormalizeAngle
//   StoreAndZero(value -> tmp); if (value < 0) tmp += -1.0; return tmp;
// (dbl_62BFFC == -1.0.) Net effect: floor-toward-zero then bias by -1 when
// negative == std::floor for the integral-stepping callers.
double NormalizeAngle(double value) {
    double tmp = value;
    if (value < 0.0)
        tmp += -1.0;
    return tmp;
}

// gilde.exe 0x5f0be5 — VIBE_Math_DecrementAndAbs (__fastcall)
//   return -NormalizeAngle(-value);
double DecrementAndAbs(double value) {
    return -NormalizeAngle(-value);
}

// gilde.exe 0x5ca9d8 — VIBE_Math_CatmullRomInterp (__stdcall)
//   constants: flt_628D28=2.0, flt_628D2C=3.0, flt_628D30=-2.0
double CatmullRomInterp(float p0, float p1, float p2, float p3, float t) {
    double tt = t;
    double t3 = tt * tt * tt;
    return (tt + t3 - tt * tt * 2.0) * p2
         + (tt * tt * 3.0 + t3 * -2.0) * p1
         + (2.0 * t3 - tt * tt * 3.0 + 1.0) * p0
         + (t3 - tt * tt) * p3;
}

// gilde.exe 0x59b3d8 — VIBE_Math_CubicBezierPoint (__userpurge)
//   flt_627D08 == 3.0. Standard cubic Bezier with basis (u^3, 3u^2 t, 3u t^2, t^3).
float* CubicBezierPoint(float* p0, float* p1, float* p2, float* p3, float t,
                        float* out) {
    double u = 1.0 - t;
    double b0 = u * u * u;            // (1-t)^3
    double b3 = static_cast<double>(t) * t * t; // t^3
    double k = static_cast<double>(t) * 3.0;
    double b1 = k * u * u;            // 3 t (1-t)^2
    double b2 = u * (static_cast<double>(t) * k); // 3 t^2 (1-t)
    out[0] = static_cast<float>(b1 * p1[0] + b0 * p0[0] + b2 * p3[0] + b3 * p2[0]);
    out[1] = static_cast<float>(b1 * p1[1] + b0 * p0[1] + b2 * p3[1] + b3 * p2[1]);
    return p0;
}

// gilde.exe 0x5d9d60 — VIBE_Math_LerpClampedCoord (__stdcall)
//   order a<=b; v = a + (b-a)/span*d + 0.5(dbl_6295E0); ConvertX(); return (int)v.
//   The +0.5 then truncate-toward-zero == round-half-up for non-negative results.
int LerpClampedCoord(float a, float b, float span, float d) {
    if (a > static_cast<double>(b)) {
        float t = a;
        a = b;
        b = t;
    }
    double v = a + (b - a) / span * d + 0.5;
    v = ConvertX(v);
    return static_cast<int>(v);
}

// gilde.exe 0x5ca2fc — VIBE_Math_VectorLerp (__userpurge)
//   out = a + (b-a)*t  (component-wise, 3 floats). Returns a.
float* VectorLerp(float* a, float* b, float t, float* out) {
    out[0] = (b[0] - a[0]) * t + a[0];
    out[1] = (b[1] - a[1]) * t + a[1];
    out[2] = t * (b[2] - a[2]) + a[2];
    return a;
}

// gilde.exe 0x5cb148 — VIBE_Math_VectorNormalize (__usercall)
//   len = sqrt(x^2+y^2+z^2); if (len bits & 0x7FFFFFFF) v *= 1/len; else v = 0.
float* VectorNormalize(float* v) {
    float len = static_cast<float>(
        std::sqrt(static_cast<double>(v[0]) * v[0] + static_cast<double>(v[1]) * v[1]
                  + static_cast<double>(v[2]) * v[2]));
    u32 bits;
    __builtin_memcpy(&bits, &len, 4);
    if ((bits & 0x7FFFFFFFu) != 0) {
        float inv = 1.0f / len;
        float y = v[1] * inv;
        float z = v[2] * inv;
        v[0] = v[0] * inv;
        v[1] = y;
        v[2] = z;
    } else {
        v[1] = 0.0f;
        v[2] = 0.0f;
        v[0] = 0.0f;
    }
    return v;
}

// gilde.exe 0x5caa4c — VIBE_Math_VectorWithinTolerance (__userpurge)
//   |a-b| <= tol per component.
bool VectorWithinTolerance(float* a, float* b, float tol) {
    return std::fabs(b[0] - a[0]) <= tol
        && std::fabs(b[1] - a[1]) <= static_cast<double>(tol)
        && std::fabs(b[2] - a[2]) <= static_cast<double>(tol);
}

// gilde.exe 0x5cb824 — VIBE_Math_TriangleNormal (__usercall)
//   e1 = b-a; e2 = c-a; out = e1 x e2 (with the original's component order); normalize.
float* TriangleNormal(float* a, float* b, float* c, float* out) {
    float e1x = b[0] - a[0];
    float e1y = b[1] - a[1];
    float e1z = b[2] - a[2];
    float e2x = c[0] - a[0];
    float e2y = c[1] - a[1];
    float e2z = c[2] - a[2];
    out[0] = e2z * e1y - e2y * e1z;
    out[1] = e2x * e1z - e2z * e1x;
    out[2] = e2y * e1x - e2x * e1y;
    return VectorNormalize(out);
}

// gilde.exe 0x5ca334 — VIBE_Math_VectorAngleBetween (__usercall)
//   Copies a,b; zeroes Y of both; normalizes; if nearly equal -> 0; if nearly
//   opposite -> -pi; else acos(dot) signed by the sign of the cross-product
//   projected onto the reference axis (flt_5CA2D0..8 == {0,1,0}).
//   dbl_628CE0 = 1e-7 (tolerance); dbl_628CE8 = -2*pi (wrap for the +acos branch).
double VectorAngleBetween(float* a, float* b) {
    const double kTol = 1e-07;
    float ax = a[0], az = a[2];
    float bx = b[0], bz = b[2];
    float ay = 0.0f, by = 0.0f;
    float va[3] = {ax, ay, az};
    float vb[3] = {bx, by, bz};
    VectorNormalize(va);
    VectorNormalize(vb);
    ax = va[0]; ay = va[1]; az = va[2];
    bx = vb[0]; by = vb[1]; bz = vb[2];
    // Nearly equal -> 0 (original returns the uninitialised st register == 0 on
    // this path; faithful callers treat coincident directions as zero angle).
    if (std::fabs(bx - ax) <= kTol && std::fabs(by - ay) <= kTol
        && std::fabs(bz - az) <= kTol)
        return 0.0;
    if (std::fabs(-bx - ax) <= kTol && std::fabs(-by - ay) <= kTol
        && std::fabs(-bz - az) <= kTol)
        return -3.1415927;
    float crossX = bz * ay - by * az;
    float crossY = bx * az - bz * ax;
    float crossZ = by * ax - bx * ay;
    double dot = ax * bx + ay * by + az * bz;
    // reference axis = {0,1,0}: pick crossY.
    if (0.0f * crossX + 1.0f * crossY + 0.0f * crossZ > 0.0f)
        return static_cast<float>(-AcosGuarded(dot));
    return static_cast<float>(AcosGuarded(dot) + -6.28318530718);
}

// gilde.exe 0x5ca504 — VIBE_Math_VectorAngleWrapped (__usercall)
//   a = VectorAngleBetween; if (a < dbl_628CF0(=-pi)) a += flt_628CF8(=2pi).
double VectorAngleWrapped(float* a, float* b) {
    double ang = VectorAngleBetween(a, b);
    float angf = static_cast<float>(ang);
    if (ang >= -3.14159265359)
        return angf;
    return static_cast<float>(angf + 6.2831854820251465f);
}

// gilde.exe 0x5cffac — VIBE_Math_MaxVectorLength (__usercall)
//   best = -1.0; for each of `count` vectors (stride 4 floats) best = max(best, x^2+y^2+z^2);
//   return sqrt(best).
double MaxVectorLength(float* vecs, int count) {
    float best = -1.0f;
    float* p = vecs;
    for (int i = 0; i < count; ++i) {
        float sq = p[0] * p[0] + p[1] * p[1] + p[2] * p[2];
        if (best > static_cast<double>(sq))
            ; // keep best
        else
            best = sq;
        p += 4;
    }
    return std::sqrt(static_cast<double>(best));
}

// gilde.exe 0x14210c0 — VIBE_Math_UInt64Multiply (__stdcall, MSVC __allmul).
u64 UInt64Multiply(u64 a, u64 b) {
    return a * b;
}

// gilde.exe 0x6068dc — VIBE_Math_Multiply64 (__usercall)
//   Both arms of the original return (a_lo * b_lo); the high words are ignored.
i32 Multiply64(i32 a_lo, i32 a_hi, i32 b_lo, i32 b_hi) {
    (void)a_hi;
    (void)b_hi;
    return b_lo * a_lo;
}

// gilde.exe 0x5e57e7 — VIBE_Math_UnsignedLongLongDivide (MSVC __aulldiv intrinsic).
// The original is the register-calling shift/subtract long-division routine; its
// observable result is the unsigned 64-bit quotient. Implemented as such.
u64 UnsignedLongLongDivide(u64 num, u64 den) {
    return den ? (num / den) : 0;
}

// gilde.exe 0x5e5792 — VIBE_Math_LongLongDivide (MSVC __alldiv intrinsic).
// Signed 64-bit quotient (sign handled exactly as the original's sign folding).
i64 LongLongDivide(i64 num, i64 den) {
    return den ? (num / den) : 0;
}

} // namespace guild::util
