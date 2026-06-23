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
//   s = 1 - x*x;                       // st0=s, st1=x
//   ftst s;                            // FTST compares s against 0.0
//   if (s != 0) goto sqrt_path;        // jnz: any s != 0 (incl. s < 0)
//   // s == 0  (x == +/-1): fcompp s,x -> ja => x<0 => fldpi(pi) else fldz(0)
//   if (x >= 0) return 0; else return pi;
// sqrt_path:
//   if (SqrtGuarded(s) != 0) return st0;        // s < 0 -> NaN via FpClassifyAdjust
//   // s > 0: pi/2 - atan2(sqrt(s), x) == acos(x)   (tbyte_64A7D4 == pi/2)
//   return acos(x);
//
// NOTE (FIXED): the original's fldz/fldpi saturation branch is taken ONLY when
// s == 0 exactly (x == +/-1) — the FTST jnz routes every s != 0 (including the
// s < 0 / |x| > 1 case) into SqrtGuarded, whose FTST(s<0) guard yields a NaN
// (domain error) via FpClassifyAdjust. std::acos(x) returns NaN for |x| > 1, so
// it matches that path. A prior `s <= 0.0` guard wrongly returned 0/pi for
// |x| > 1 instead of NaN; the correct guard is `s == 0.0`.
// (Disasm: 0x5f0ba6 ftst on s; 0x5f0bab jnz -> SqrtGuarded; the second compare
//  at 0x5f0bad/fcompp tests s vs x, selecting pi for x<0 and 0 for x>=0.)
double AcosGuarded(double x) {
    double s = 1.0 - x * x;
    if (s == 0.0) {
        // x == +/-1: fcompp(s, x) picks pi for the negative root, 0 for positive.
        return (x >= 0.0) ? 0.0 : 3.14159265358979323846;
    }
    // s > 0: normal acos; s < 0 (|x|>1): SqrtGuarded -> NaN, matched by std::acos.
    return std::acos(x);
}

} // namespace guild::util
