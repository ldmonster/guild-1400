#include "render/falloff_lut.h"
#include <cmath>

// =============================================================================
// guild::render falloff LUT — implementation. See falloff_lut.h for provenance.
// =============================================================================
namespace guild::render {

// ---------------------------------------------------------------------------
// gilde.exe 0x5f0b9c — VIBE_Math_AcosGuarded. The x87 routine:
//   t = 1 - x*x
//   ftst t
//   if (t == 0): x==+/-1 -> fcompp picks 0 for x>=0, pi for x<0 (|x|==1 guard)
//   else: SqrtGuarded(t) -> st0 = sqrt(t), st1 = x; fxch;
//         VIBE_Math_Atan2 (0x5f5701: fxch st(1); fpatan) computes
//         atan2(st1=x, st0=sqrt(t)) == asin(x);
//         then pi/2 (tbyte_64A7D4) - asin(x) == ACOS(x).
// (Harden fix: a prior transcription dropped the inner fxch of Atan2 and read
// this as asin — the double fxch makes it acos, matching the IDA name and the
// |x|==1 guard values acos(1)=0 / acos(-1)=pi.)
// ---------------------------------------------------------------------------
double AcosGuarded(double x) {
    double t = 1.0 - x * x;
    if (t == 0.0) {
        // |x| == 1: 0 for x>=0 (acos(1)), pi for x<0 (acos(-1)).
        return (x >= 0.0) ? 0.0 : M_PI;
    }
    // pi/2 - atan2(x, sqrt(t)) == pi/2 - asin(x) == acos(x).
    double asin_x = std::atan2(x, std::sqrt(t));
    return (M_PI / 2.0) - asin_x;
}

// ---------------------------------------------------------------------------
// gilde.exe 0x5c88f8 — VIBE_Light_InitFalloffTable.
//   table[i] = 1.0 - acos(i * kFalloffStep) * kFalloffScale
// computed in double, stored as float (fstp dword). The curve RISES from ~0
// (i=0, grazing) toward ~0.97 (i=1023, facing) — matching the consumer
// (VIBE_Light_ApplyToCachedVertices) and light.cpp's BuildFalloffLUT.
// ---------------------------------------------------------------------------
void InitFalloffTable(float out[kFalloffEntries]) {
    const double B = kFalloffStep;   // flt_628CB4
    const double A = kFalloffScale;  // flt_628CB8
    for (int i = 0; i < kFalloffEntries; ++i) {
        double x = (double)i * B;       // fild i; fmul st,st(1) (B)
        double s = AcosGuarded(x);      // == acos(x)
        out[i] = (float)(1.0 - s * A);  // fmul st,st(2) (A); fld1; fsubrp; fstp dword
    }
}

} // namespace guild::render
