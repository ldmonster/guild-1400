#include "util/matrix.h"

#include "util/math.h"        // VectorNormalize, Atan2, AcosGuarded (Math module)
#include "util/float_math.h"  // Fmod, Sqrt (x87 helpers, guild::util)

#include <cmath>   // sin, cos, fabs (the original's CRT trig dispatch, see math.h)
#include <cstring> // memset / memcpy

namespace guild::util {

// ---- Recovered constants (exact bytes via get_global_value) ----------------
namespace {
constexpr float  kTwo          = 2.0f;        // flt_628D00 = 0x40000000
constexpr double kQuatWLo      = -1.0;        // dbl_628D08 = 0xBFF0000000000000
constexpr float  kTwoPiF       = 6.2831855f;  // flt_628D10 = 0x40C90FDB (+2pi, float)
constexpr float  kNegTwoPiF    = -6.2831855f; // flt_628D14 = 0xC0C90FDB (-2pi, float)
// NOTE: the binary's 2pi double is the slightly-imprecise 0x401921FB54442EEA
// (mantissa ...EEA, NOT the correctly-rounded ...D18). The decimal below is the
// shortest literal that round-trips to those exact bytes.
constexpr double kTwoPi        = 6.28318530718;       // dbl_628D18 = 0x401921FB54442EEA (+2pi)
constexpr double kNegTwoPi     = -6.28318530718;      // dbl_628D20 = 0xC01921FB54442EEA (-2pi)
constexpr double kDetEpsilon   = 1e-07;               // dbl_628D38 = 0x3E7AD7F29ABCAF48 (det floor)
constexpr double kGimbalEps    = 1.9073486336e-06;    // dbl_628D40 = 0x3EC00000001C5F68 (toEuler)
constexpr float  kQuarter      = 0.125f;      // flt_628D48 = 0x3E000000
constexpr float  kBasisEps     = 1.0000000116860974e-07f; // flt_628CFC = 0x33D6BF95 ~1e-7
// Reference up axis used by BuildBasisFromAngle: {flt_5CA2D0,D4,D8} = {0,1,0}.
constexpr float kRefUp[3]   = {0.0f, 1.0f, 0.0f};
// Alternate reference axes if the primary cross is degenerate.
// {flt_5CA2D0,D4,D8} = {0,1,0} (X-elim), {flt_5CA2B0,B4,B8} = {0,0,1} (Y-elim).
constexpr float kRefAxisA[3] = {0.0f, 1.0f, 0.0f}; // 5CA2D0..D8
constexpr float kRefAxisB[3] = {0.0f, 0.0f, 1.0f}; // 5CA2B0..B8
} // namespace

// gilde.exe 0x5cb100 — VIBE_Math_MatrixIdentity
//   memset 64 bytes to 0; m[15]=m[10]=m[5]=m[0]=1.0 (0x3F800000 == 1065353216).
void MatrixIdentity(float* dst) {
    std::memset(dst, 0, 16 * sizeof(float));
    dst[0] = 1.0f;
    dst[5] = 1.0f;
    dst[10] = 1.0f;
    dst[15] = 1.0f;
}

// gilde.exe 0x5cabf0 — VIBE_Math_MatrixCopy
//   Strided "copy": for i in 0..3, for j in 0..3: dst[4*i + j] = src[i + 4*j].
//   This is a TRANSPOSE (used as the singular-matrix fallback of MatrixInverse).
void MatrixCopy(const float* src, float* dst) {
    for (int i = 0; i < 4; ++i) {
        for (int j = 0; j < 4; ++j)
            dst[4 * i + j] = src[i + 4 * j];
    }
}

// gilde.exe 0x5cac3c — VIBE_Math_MatrixInverse
//   Full 4x4 cofactor inverse over the flat 16-float matrix. The determinant is
//   computed by Laplace expansion along the first column using the same 2x2 minor
//   groupings as the original; if |det| < kDetEpsilon it falls back to the
//   transpose (MatrixCopy). 80-bit x87 intermediates modeled as double.
void MatrixInverse(const float* a1, float* a2) {
    double v2 = (double)a1[10] * a1[15] - (double)a1[11] * a1[14];
    double v3 = (double)a1[2] * a1[15] - (double)a1[3] * a1[14];
    double v4 = (double)a1[2] * a1[11] - (double)a1[3] * a1[10];
    double v14 = v2 * a1[5];
    double v5 = (double)a1[6] * a1[15] - (double)a1[7] * a1[14];
    double v12 = (double)a1[9] * v5;
    double v15 = v14 - v12;
    double v6 = (double)a1[6] * a1[11] - (double)a1[7] * a1[10];
    double v13 = (double)a1[13] * v6;
    double v16 = v15 + v13;
    double v8 = (double)a1[2] * a1[7] - (double)a1[3] * a1[6];
    double v9 = (double)a1[0] * v16
              - (a1[1] * v2 - a1[9] * v3 + a1[13] * v4) * a1[4]
              + (v5 * a1[1] - v3 * a1[5] + a1[13] * v8) * a1[8]
              - (v8 * a1[9] + v6 * a1[1] - v4 * a1[5]) * a1[12];

    if (std::fabs(v9) < kDetEpsilon) {
        MatrixCopy(a1, a2);
        return;
    }
    double inv = 1.0 / (double)(float)v9;  // original truncates det to float first
    a2[0] = (float)(v16 * inv);
    a2[1] = (float)(-(((double)a1[10] * a1[15] - (double)a1[11] * a1[14]) * a1[1]
                    - ((double)a1[2] * a1[15] - (double)a1[3] * a1[14]) * a1[9]
                    + ((double)a1[2] * a1[11] - (double)a1[3] * a1[10]) * a1[13]) * inv);
    a2[2] = (float)((((double)a1[6] * a1[15] - (double)a1[7] * a1[14]) * a1[1]
                   - ((double)a1[2] * a1[15] - (double)a1[3] * a1[14]) * a1[5]
                   + ((double)a1[2] * a1[7] - (double)a1[3] * a1[6]) * a1[13]) * inv);
    a2[3] = (float)(-(((double)a1[6] * a1[11] - (double)a1[7] * a1[10]) * a1[1]
                    - ((double)a1[2] * a1[11] - (double)a1[3] * a1[10]) * a1[5]
                    + ((double)a1[2] * a1[7] - (double)a1[3] * a1[6]) * a1[9]) * inv);
    a2[4] = (float)(-(((double)a1[10] * a1[15] - (double)a1[11] * a1[14]) * a1[4]
                    - ((double)a1[6] * a1[15] - (double)a1[7] * a1[14]) * a1[8]
                    + ((double)a1[6] * a1[11] - (double)a1[7] * a1[10]) * a1[12]) * inv);
    a2[5] = (float)((((double)a1[10] * a1[15] - (double)a1[11] * a1[14]) * a1[0]
                   - ((double)a1[2] * a1[15] - (double)a1[3] * a1[14]) * a1[8]
                   + ((double)a1[2] * a1[11] - (double)a1[3] * a1[10]) * a1[12]) * inv);
    a2[6] = (float)(-(((double)a1[6] * a1[15] - (double)a1[7] * a1[14]) * a1[0]
                    - ((double)a1[2] * a1[15] - (double)a1[3] * a1[14]) * a1[4]
                    + ((double)a1[2] * a1[7] - (double)a1[3] * a1[6]) * a1[12]) * inv);
    a2[7] = (float)((((double)a1[6] * a1[11] - (double)a1[7] * a1[10]) * a1[0]
                   - ((double)a1[2] * a1[11] - (double)a1[3] * a1[10]) * a1[4]
                   + ((double)a1[2] * a1[7] - (double)a1[3] * a1[6]) * a1[8]) * inv);
    a2[8] = (float)((((double)a1[9] * a1[15] - (double)a1[11] * a1[13]) * a1[4]
                   - ((double)a1[5] * a1[15] - (double)a1[7] * a1[13]) * a1[8]
                   + ((double)a1[5] * a1[11] - (double)a1[7] * a1[9]) * a1[12]) * inv);
    a2[9] = (float)(inv * -(((double)a1[9] * a1[15] - (double)a1[11] * a1[13]) * a1[0]
                          - ((double)a1[1] * a1[15] - (double)a1[3] * a1[13]) * a1[8]
                          + ((double)a1[1] * a1[11] - (double)a1[3] * a1[9]) * a1[12]));
    a2[10] = (float)((((double)a1[5] * a1[15] - (double)a1[7] * a1[13]) * a1[0]
                    - ((double)a1[1] * a1[15] - (double)a1[3] * a1[13]) * a1[4]
                    + ((double)a1[1] * a1[7] - (double)a1[3] * a1[5]) * a1[12]) * inv);
    a2[11] = (float)(-(((double)a1[5] * a1[11] - (double)a1[7] * a1[9]) * a1[0]
                     - ((double)a1[1] * a1[11] - (double)a1[3] * a1[9]) * a1[4]
                     + ((double)a1[1] * a1[7] - (double)a1[3] * a1[5]) * a1[8]) * inv);
    a2[12] = (float)(-(((double)a1[9] * a1[14] - (double)a1[10] * a1[13]) * a1[4]
                     - ((double)a1[5] * a1[14] - (double)a1[6] * a1[13]) * a1[8]
                     + ((double)a1[5] * a1[10] - (double)a1[6] * a1[9]) * a1[12]) * inv);
    a2[13] = (float)((((double)a1[9] * a1[14] - (double)a1[10] * a1[13]) * a1[0]
                    - ((double)a1[1] * a1[14] - (double)a1[2] * a1[13]) * a1[8]
                    + ((double)a1[1] * a1[10] - (double)a1[2] * a1[9]) * a1[12]) * inv);
    a2[14] = (float)(inv * -(((double)a1[5] * a1[14] - (double)a1[6] * a1[13]) * a1[0]
                           - ((double)a1[1] * a1[14] - (double)a1[2] * a1[13]) * a1[4]
                           + ((double)a1[1] * a1[6] - (double)a1[2] * a1[5]) * a1[12]));
    a2[15] = (float)((((double)a1[5] * a1[10] - (double)a1[6] * a1[9]) * a1[0]
                    - ((double)a1[1] * a1[10] - (double)a1[2] * a1[9]) * a1[4]
                    + ((double)a1[1] * a1[6] - (double)a1[2] * a1[5]) * a1[8]) * inv);
}

// gilde.exe 0x5caaa4 — VIBE_Math_MatrixTransformVectors
//   out = a (*) b: the 3x3 rotation block of `a` times each row of `b`, plus b's
//   translation column on the last output row. out[3]=out[7]=out[11]=0,out[15]=1.
float* MatrixTransformVectors(float* a, const float* b, float* out) {
    out[0] = (float)((double)a[1] * b[4] + (double)a[0] * b[0] + (double)a[2] * b[8]);
    out[1] = (float)((double)a[1] * b[5] + (double)a[0] * b[1] + (double)a[2] * b[9]);
    out[2] = (float)((double)a[1] * b[6] + (double)a[0] * b[2] + (double)a[2] * b[10]);
    out[3] = 0.0f;
    out[4] = (float)((double)a[5] * b[4] + (double)a[4] * b[0] + (double)a[6] * b[8]);
    out[5] = (float)((double)a[4] * b[1] + (double)a[5] * b[5] + (double)a[6] * b[9]);
    out[6] = (float)((double)a[4] * b[2] + (double)a[5] * b[6] + (double)a[6] * b[10]);
    out[7] = 0.0f;
    out[8] = (float)((double)a[9] * b[4] + (double)a[8] * b[0] + (double)a[10] * b[8]);
    out[9] = (float)((double)a[8] * b[1] + (double)a[9] * b[5] + (double)a[10] * b[9]);
    out[10] = (float)((double)a[8] * b[2] + (double)a[9] * b[6] + (double)a[10] * b[10]);
    out[11] = 0.0f;
    out[12] = (float)((double)a[13] * b[4] + (double)a[12] * b[0] + (double)a[14] * b[8] + b[12]);
    out[13] = (float)((double)a[12] * b[1] + (double)a[13] * b[5] + (double)a[14] * b[9] + b[13]);
    out[14] = (float)((double)a[12] * b[2] + (double)a[13] * b[6] + (double)a[14] * b[10] + b[14]);
    out[15] = 1.0f;  // 1065353216 == 1.0f
    return a;
}

// gilde.exe 0x5cb1bc — VIBE_Math_MatrixFromEuler
//   angles[0]=rot about (call it X), angles[1]=Y(pitch), angles[2]=Z. Builds the
//   rotation block; sets translation 0 and m[15]=1. x87 80-bit intermediates -> double.
void MatrixFromEuler(const float* a1, float* m) {
    double sy = std::sin(a1[1]);    // v2 = sin(angles[1])
    double cx = std::cos(a1[0]);    // v3 = cos(angles[0])
    double cy = std::cos(a1[1]);    // v4 = cos(angles[1])
    double cz = std::cos(a1[2]);    // v10
    double sz = std::sin(a1[2]);    // v12
    double v5 = sy * cz;            // v5
    double v6 = sy * sz;            // v6
    double sx = std::sin(a1[0]);    // v8

    m[0] = (float)(cy * cz);                 // v4*v10
    m[11] = 0.0f;
    m[4] = (float)(cy * sz);                 // v9(=cy)*v12
    m[8] = (float)(-sy);                     // -v11(=sy)
    m[14] = 0.0f;
    m[9] = (float)(sx * cy);                 // v8*v9
    m[15] = 1.0f;                            // 1065353216
    m[10] = (float)(cx * cy);                // v13(=cx)*v9
    m[7] = 0.0f;                             // a2+28 = a2+44
    m[3] = 0.0f;                             // a2+12 = a2+28
    m[13] = 0.0f;                            // a2+52 = a2+56
    m[12] = 0.0f;                            // a2+48
    m[1] = (float)(sx * v5 - cx * sz);       // v8*v5 - v13*v12
    m[2] = (float)(v5 * cx + sx * sz);       // v5*v3 + v8*v12
    m[5] = (float)(cx * cz + sx * v6);       // v13*v10 + v8*v6
    m[6] = (float)(v6 * cx - sx * cz);       // v6*v13 - v8*v10
}

// gilde.exe 0x5cb2cc — VIBE_Math_MatrixToEuler
//   Writes Euler angles back into m[0..2] IN PLACE. v14 = hypot(m[4], m[0]).
//   If v14 <= kGimbalEps -> gimbal-lock branch (uses m[6],m[5]); else standard.
void MatrixToEuler(float* m) {
    double v14 = std::sqrt((double)m[4] * m[4] + (double)m[0] * m[0]);
    if (v14 <= kGimbalEps) {
        double e0 = Atan2(-m[6], m[5]);
        double e1 = Atan2(-m[8], v14);
        m[0] = (float)e0;
        m[1] = (float)e1;
        m[2] = 0.0f;
    } else {
        double e0 = Atan2(m[9], m[10]);
        double e1 = Atan2(-m[8], v14);
        double e2 = Atan2(m[4], m[0]);
        m[0] = (float)e0;
        m[1] = (float)e1;
        m[2] = (float)e2;
    }
}

// gilde.exe 0x5cb354 — VIBE_Math_MatrixDecompose
//   Recovers rotation by averaging three "basis difference" vectors derived from
//   the four 4-float rows of `a1` (rows at indices 0,4,8 plus the translation row
//   at 12, and the pivot/extra blocks at 16..30), normalizing each and feeding the
//   first through MatrixToEuler (writes Euler into a1[0..2]); the translation is
//   0.125 * componentwise sum of EIGHT row origins (float offsets 0,4,..,28),
//   stored to out[0..2]. Returns the past-the-end source pointer (a1+32). 80-bit
//   temps -> double.
//
//   This routine indexes a1 well past the 16-float matrix (up to a1[30] for the
//   basis vectors, and reads rows 16..28 in the translation sum); in the original
//   these are adjacent engine fields. We reproduce the exact index arithmetic;
//   callers must pass a buffer large enough (>=32 floats). It has 0 callers in
//   gilde.exe, so this is a best-effort faithful translation.
float* MatrixDecompose(float* a1, float* a2) {
    float v95[16];
    std::memset(v95, 0, sizeof(v95));
    v95[15] = 1.0f; v95[10] = 1.0f; v95[5] = 1.0f; v95[0] = 1.0f;

    // First basis-difference vector -> v95[0..2].
    {
        float x = (a1[16] - a1[20]) + (a1[0] - a1[4]) + (a1[24] - a1[28]) + (a1[8] - a1[12]);
        float y = (a1[17] - a1[21]) + (a1[1] - a1[5]) + (a1[25] - a1[29]) + (a1[9] - a1[13]);
        float z = (a1[18] - a1[22]) + (a1[2] - a1[6]) + (a1[26] - a1[30]) + (a1[10] - a1[14]);
        v95[0] = x; v95[1] = y; v95[2] = z;
        VectorNormalize(v95);
    }
    // Second basis-difference vector -> v96[0..2] (a 3-float scratch we name v96).
    float v96[3];
    {
        float x = (a1[28] - a1[20]) + (a1[12] - a1[4]) + (a1[24] - a1[16]) + (a1[8] - a1[0]);
        float y = (a1[29] - a1[21]) + (a1[13] - a1[5]) + (a1[25] - a1[17]) + (a1[9] - a1[1]);
        float z = (a1[30] - a1[22]) + (a1[14] - a1[6]) + (a1[26] - a1[18]) + (a1[10] - a1[2]);
        v96[0] = x; v96[1] = y; v96[2] = z;
        VectorNormalize(v96);
    }
    // Third basis-difference vector -> v99[0..2].
    float v99[3];
    {
        float x = (a1[4] - a1[20]) + (a1[12] - a1[28]) + (a1[0] - a1[16]) + (a1[8] - a1[24]);
        float y = (a1[5] - a1[21]) + (a1[13] - a1[29]) + (a1[1] - a1[17]) + (a1[9] - a1[25]);
        float z = (a1[6] - a1[22]) + (a1[14] - a1[30]) + (a1[2] - a1[18]) + (a1[10] - a1[26]);
        v99[0] = x; v99[1] = y; v99[2] = z;
        VectorNormalize(v99);
    }

    MatrixToEuler(v95);  // writes Euler angles into v95[0..2] (rotation result)

    // Translation = 0.125 * componentwise sum of the EIGHT 4-float rows of a1.
    // Disasm @0x5cb7b6: `add esi, 80h` -> the loop end is a1 + 0x80 bytes = a1 + 32
    // floats, and `add eax, 10h` advances one 4-float row per iteration, so it sums
    // rows at float offsets 0,4,8,12,16,20,24,28 (8 rows, NOT 4). The 0.125 scale
    // (flt_628D48) therefore averages 8 row origins. The function returns the
    // past-the-end pointer eax == a1 + 32 (mov eax,esi before the loop holds a1;
    // eax ends equal to esi = a1+0x80).
    float sx = 0.0f, sy = 0.0f, sz = 0.0f;
    float* p = a1;
    float* end = a1 + 32;
    do {
        sx += p[0];
        sy += p[1];
        sz += p[2];
        p += 4;
    } while (p != end);
    a2[0] = sx * kQuarter;
    a2[1] = sy * kQuarter;
    a2[2] = sz * kQuarter;
    return p;  // == a1 + 32
}

// gilde.exe 0x5ca798 — VIBE_Math_QuatRotateVector
//   Quaternion q = (q[0],q[1],q[2],q[3]) = (x,y,z,w). Builds the standard 3x3
//   rotation matrix (scale 2.0 == flt_628D00) and applies it to v -> out.
//
//   FIDELITY NOTE / KNOWN ORIGINAL BUG (verified @0x5ca798 disasm): the binary
//   lays out its scratch as +0x00 m00, +0x04 m01, +0x08 m02, +0x10 m11, +0x14 m12,
//   +0x18 (a *buggy* "m10" = 2(x*y - w*y), not 2(wz+xy)), +0x20 m22; slots +0x0C
//   and +0x1C are never written. The apply step then reads the +0x24/+0x28 slots
//   (uninitialized stack, aliased to the output temporaries v14/v16) for two of the
//   nine matrix*vector terms, and the diagonal-only first output row. So two
//   off-diagonal entries are wrong/garbage and the result is unusable. The slot
//   claims here are confirmed against the disasm. This routine has 0 callers in
//   gilde.exe (dead code), so deviating from the binary bug is justified: we
//   implement the INTENDED, correct rotation (all 9 standard quaternion->matrix
//   entries in their natural 3x3 slots, full dot-product rows) so the function is
//   usable and testable. Entries m00,m01,m02,m11,m12,m22 below match the binary's
//   computed (pre-misread) values; m10 and m20 are the corrected intended values
//   (the binary's v12/m10 is the 2(x*y - w*y) typo and its m20 slot is never even
//   stored). The binary's uninitialized-read behavior is documented, not reproduced.
void QuatRotateVector(const float* q, const float* v, float* out) {
    double x = q[0], y = q[1], z = q[2], w = q[3];
    // Matrix entries exactly as the original computes them (constant 2.0):
    double m00 = 1.0 - kTwo * (y * y + z * z);   // var_3C
    double m01 = (x * y - w * z) * kTwo;         // var_38 = 2(xy - wz)
    double m02 = (w * y + x * z) * kTwo;         // var_34 = 2(wy + xz)
    double m10 = (w * z + x * y) * kTwo;         // CORRECTED: binary +0x18 is the typo 2(xy - wy)
    double m11 = 1.0 - kTwo * (z * z + x * x);   // var_2C = +0x10
    double m12 = (y * z - w * x) * kTwo;         // var_28 = +0x14 = 2(yz - wx)
    double m20 = (x * z - w * y) * kTwo;         // CORRECTED: binary never stores this slot
    double m21 = (y * z + w * x) * kTwo;         // 2(yz + wx)
    double m22 = 1.0 - kTwo * (y * y + x * x);   // var_1C
    out[0] = (float)(v[0] * m00 + v[1] * m01 + v[2] * m02);
    out[1] = (float)(v[0] * m10 + v[1] * m11 + v[2] * m12);
    out[2] = (float)(v[0] * m20 + v[1] * m21 + v[2] * m22);
}

// gilde.exe 0x5ca8c8 — VIBE_Math_QuatNormalizeAxis
//   if (q[3] <= 1.0 (compared as raw int <= 0x3F800000) && q[3] >= -1.0):
//     s2 = x^2 + y^2 + z^2; if (s2 > 0): xyz *= sqrt((1 - w^2) / s2).
//   The int comparison q[3]<=1065353216 is the original's `<= 1.0` guard.
void QuatNormalizeAxis(float* q) {
    u32 wbits;
    std::memcpy(&wbits, &q[3], 4);
    if ((i32)wbits <= 0x3F800000 && q[3] >= (float)kQuatWLo) {
        float s2 = q[1] * q[1] + q[0] * q[0] + q[2] * q[2];
        if (s2 > 0.0f) {
            float scale = (float)std::sqrt((1.0 - (double)q[3] * q[3]) / s2);
            float ny = q[1] * scale;
            float nz = q[2] * scale;
            q[0] = q[0] * scale;
            q[1] = ny;
            q[2] = nz;
        }
    }
}

// gilde.exe 0x5ca940 — VIBE_Math_SnapVectorToAxis
//   For each of 3 components i: out[i] = Fmod(src[i], 2pi); refmod[i] = Fmod(ref[i], 2pi).
//   Then choose the representative of out[i] (out[i], out[i]+2pi, or out[i]-2pi)
//   that is closest to refmod[i] — an angle-unwrap of `src` toward `ref`.
//   Recovered from disasm: eax=src loop base, edx=out, ecx=refmod, esi=ref.
void SnapVectorToAxis(const float* src, const float* ref, float* out, float* refmod) {
    for (int i = 0; i < 3; ++i) {
        out[i] = (float)Fmod(src[i], kTwoPi);
        refmod[i] = (float)Fmod(ref[i], kTwoPi);
        double base = out[i];
        double dCur = std::fabs((double)refmod[i] - base);
        // distance if we add +2pi (flt_628D10) to out[i]
        double dPlus = std::fabs((double)refmod[i] - (base + kTwoPiF));
        if (dPlus < dCur) {
            out[i] = (float)(base + kTwoPi);          // dbl_628D18 (+2pi)
        } else {
            // distance if we add -2pi (flt_628D14) to out[i]
            double dMinus = std::fabs((double)refmod[i] - (base + kNegTwoPiF));
            if (dMinus < dCur)
                out[i] = (float)(base + kNegTwoPi);   // dbl_628D20 (-2pi)
        }
    }
}

// gilde.exe 0x5ca544 — VIBE_Math_BuildBasisFromAngle (__userpurge)
//   dir@eax (3 floats), angle (float), out@stack (16 floats). Builds an orthonormal
//   basis: out[8..10] = normalize(dir) (the "primary" axis). A reference up-vector
//   {0,1,0} is rolled by `angle` in the XZ plane -> r = (cos*0 - 1*0... ) per the
//   constants kRefUp/flt_5CA2D0..D8 ({0,1,0}). r is then Gram-Schmidt'd against the
//   primary axis to form out[4..6]; if that is too short (< kBasisEps) it retries
//   with reference axis {0,1,0} then {0,0,1}; if all degenerate -> identity-ish.
//   out[0..2] = cross(out[8..10], out[4..6]). out[3]=out[7]=out[11]=0, out[15]=1.
void BuildBasisFromAngle(const float* a1, float a2, float* a3) {
    double s = std::sin(a2);
    double c = std::cos(a2);
    // Roll the reference up {kRefUp} by angle in the XZ plane.
    // v23 = c*ref.x - ref.y*s; v24 = ref.y*c + s*ref.x; v25 = ref.z.
    float v23 = (float)(c * kRefUp[0] - kRefUp[1] * s);
    float v24 = (float)(kRefUp[1] * c + s * kRefUp[0]);
    float v25 = kRefUp[2];

    // Primary axis = normalize(dir) -> a3[8..10].
    a3[8] = a1[0];
    a3[9] = a1[1];
    a3[10] = a1[2];
    VectorNormalize(a3 + 8);

    // Gram-Schmidt rolled reference against the primary axis -> a3[4..6].
    double d = -(v23 * (double)a3[8] + v24 * (double)a3[9] + v25 * (double)a3[10]);
    a3[4] = (float)(d * a3[8] + v23);
    a3[5] = (float)(d * a3[9] + v24);
    a3[6] = (float)(d * a3[10] + v25);

    bool ok = std::sqrt((double)a3[4] * a3[4] + (double)a3[5] * a3[5] + (double)a3[6] * a3[6])
              >= kBasisEps;
    if (!ok) {
        // Retry with reference axis A = {0,1,0}: project it out of the primary axis.
        a3[4] = (float)(-(double)a3[9] * a3[8] + kRefAxisA[0]);
        a3[5] = (float)(-(double)a3[9] * a3[9] + kRefAxisA[1]);
        a3[6] = (float)(-(double)a3[9] * a3[10] + kRefAxisA[2]);
        ok = std::sqrt((double)a3[4] * a3[4] + (double)a3[5] * a3[5] + (double)a3[6] * a3[6])
             >= kBasisEps;
        if (!ok) {
            // Retry with reference axis B = {0,0,1}.
            a3[4] = (float)(-(double)a3[10] * a3[8] + kRefAxisB[0]);
            a3[5] = (float)(-(double)a3[10] * a3[9] + kRefAxisB[1]);
            a3[6] = (float)(-(double)a3[10] * a3[10] + kRefAxisB[2]);
            ok = std::sqrt((double)a3[4] * a3[4] + (double)a3[5] * a3[5] + (double)a3[6] * a3[6])
                 >= kBasisEps;
        }
    }

    if (ok) {
        VectorNormalize(a3 + 4);
        // a3[0..2] = cross(a3[8..10], a3[4..6])  (original component order).
        float c0 = (float)((double)a3[10] * a3[5] - (double)a3[9] * a3[6]);
        float c1 = (float)((double)a3[8] * a3[6] - (double)a3[10] * a3[4]);
        float c2 = (float)((double)a3[9] * a3[4] - (double)a3[8] * a3[5]);
        a3[0] = c0;
        a3[1] = c1;
        a3[2] = c2;
        a3[11] = 0.0f;
        a3[15] = 1.0f;  // 1065353216
        a3[7] = 0.0f;   // = a3[11]
        a3[3] = 0.0f;   // = a3[7]
    } else {
        // All references degenerate -> a near-identity fallback.
        std::memset(a3, 0, 16 * sizeof(float));
        a3[15] = 1.0f;
        a3[10] = 1.0f;
        a3[5] = 1.0f;
        a3[0] = 1.0f;
    }
}

} // namespace guild::util
