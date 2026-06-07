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
//   if (t != 0): r = atan2(sqrt(t), x) (== acos(x)); return pi/2 - r (== asin(x))
//   if (t == 0): if (x>0/upper) return 0 else return pi   (the |x|==1 guard)
// We reproduce the value semantics. For the falloff table x is in [0, 1) so the
// guard is never taken; the guard is included for faithfulness.
// ---------------------------------------------------------------------------
double AcosGuarded(double x) {
    double t = 1.0 - x * x;
    if (t == 0.0) {
        // |x| == 1. fcompp compares (1-x^2) against 0 already popped; the
        // original picks 0 for x>=1, pi for x<=-1.
        return (x >= 0.0) ? 0.0 : M_PI;
    }
    // (pi/2) - acos(x) == asin(x). Computed via atan2 exactly as the original.
    double acos_x = std::atan2(std::sqrt(t), x);
    return (M_PI / 2.0) - acos_x;
}

// ---------------------------------------------------------------------------
// gilde.exe 0x5c88f8 — VIBE_Light_InitFalloffTable.
//   table[i] = 1.0 - asin(i * kFalloffStep) * kFalloffScale
// computed in double, stored as float (truncating round, matching fstp dword).
// ---------------------------------------------------------------------------
void InitFalloffTable(float out[kFalloffEntries]) {
    const double B = kFalloffStep;   // flt_628CB4
    const double A = kFalloffScale;  // flt_628CB8
    for (int i = 0; i < kFalloffEntries; ++i) {
        double x = (double)i * B;       // fild i; fmul B
        double s = AcosGuarded(x);      // == asin(x)
        out[i] = (float)(1.0 - s * A);  // fmul A; fld1; fsubr; fstp dword
    }
}

} // namespace guild::render
