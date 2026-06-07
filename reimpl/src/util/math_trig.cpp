#include "util/math_trig.h"
#include <cmath>

namespace guild::util {

// gilde.exe 0x5f5701 — VIBE_Math_Atan2 (__usercall st0=fn(y@st1, x@st0))
//   if ((byte_64A958 & 1) == 0) return atan2(y, x);  else <x87 fallback>
// byte_64A958 is the FPU-feature/exception flag; under the normal runtime it is
// clear, so the routine forwards to the CRT atan2. The set-flag path computes the
// identical value via VIBE_Math_Atan2Approx (a software polynomial). We model the
// common (and mathematically equivalent) path with std::atan2.
double Atan2(double y, double x) {
    return std::atan2(y, x);
}

// gilde.exe 0x5f56ec — VIBE_Math_Atan2Unary. atan2(y, 1.0).
double Atan2Unary(double y) {
    return std::atan2(y, 1.0);
}

// gilde.exe 0x5f0b9c — VIBE_Math_AcosGuarded (__usercall st0=fn(x@st0))
//   s = 1 - x*x;
//   if (s <= 0) {            // |x| >= 1
//       if (x >= 1) return 0; else return pi;   // ftst on x picks the branch
//   }
//   // else: atan2(sqrt(s), x) == acos(x), formed as pi/2-ish via the
//   //       SqrtGuarded + Atan2 + (tbyte_64A7D4 - r) sequence.
//   return acos(x);
double AcosGuarded(double x) {
    double s = 1.0 - x * x;
    if (s <= 0.0) {
        // ftst compares x against 0 in the original; the guard returns 0 for the
        // positive-saturated root and pi for the negative one.
        return (x >= 0.0) ? 0.0 : 3.14159265358979323846;
    }
    return std::acos(x);
}

} // namespace guild::util
