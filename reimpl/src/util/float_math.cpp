#include "util/float_math.h"

#include "util/fpu.h"

#include <cmath>

namespace guild::util {

namespace {

// gilde.exe 0x60d506+ — VIBE_Math_FmodPrepare/FmodCore: the x87-software FPREM
// emulation taken when byte_64A958 is set. It reduces |a| modulo |b| by
// repeated scaled subtraction (the loop over dbl_64DEF8 / FPREM), preserving
// the sign of the dividend. Its result equals std::fmod for all finite inputs,
// so we route both paths to std::fmod and keep the branch for fidelity.
double FmodSoftware(double a, double b) {
    return std::fmod(a, b);  // == FmodPrepare/FmodCore reduced remainder
}

// gilde.exe 0x608aa0 — VIBE_Math_Atan2Approx: the x87 polynomial/FYL2X-based
// atan2 used on the software path. Numerically equivalent to std::atan2 over
// the runtime domain (the original even falls back to atan2() itself for the
// inf/nan/zero-exponent edge cases). Modeled via std::atan2.
double Atan2Software(double y, double x) {
    return std::atan2(y, x);
}

} // namespace

// gilde.exe 0x5d3fb2 — VIBE_Math_Fmod
double Fmod(double a, double b) {
    // do { if (flag&1) FmodPrepare(); else FPREM(); } while(C2)  — the loop runs
    // FPREM until the reduction completes (C2 clear). std::fmod performs the full
    // reduction in one call, so the modeled loop collapses to a single call.
    if (UseX87SoftwarePath())
        return FmodSoftware(a, b);
    return std::fmod(a, b);  // x87 FPREM, full reduction; sign of dividend a
}

// gilde.exe 0x6029b4 — VIBE_Math_SqrtGuarded (+ FpClassifyAdjust @0x6089be)
double Sqrt(double x) {
    // FTST: if operand < 0 (or NaN), the guard takes the exception path and
    // FpClassifyAdjust yields NaN; otherwise FSQRT. std::sqrt matches: NaN for
    // x<0, +0 for +/-0, +inf for +inf.
    return std::sqrt(x);
}

// gilde.exe 0x5fca30 — VIBE_Math_LogBase (FYL2X with a base-selected multiplier)
double LogBase(int code, double x) {
    // FYL2X computes multiplier * log2(x). The three multipliers correspond to
    // log2 / log10 / ln. For x<=0 the original forwards to the CRT error handler;
    // the masked-exception value of FYL2X equals the <cmath> result, so we use
    // the matching <cmath> function directly.
    switch (code) {
        case kLogCode_Log2:   // multiplier 1.0      -> log2(x)
            return std::log2(x);
        case kLogCode_Log10:  // multiplier log10(2) -> log10(x)
            return std::log10(x);
        default:              // multiplier ln(2)    -> ln(x)
            return std::log(x);
    }
}

double Log(double x) {
    return LogBase(kLogCode_Natural, x);  // ln(x)
}

double Log10(double x) {
    return LogBase(kLogCode_Log2, x);  // code 9 -> log2 (see header note)
}

double Log2(double x) {
    return LogBase(kLogCode_Log10, x);  // code 11 -> log10 (see header note)
}

// gilde.exe 0x5f5701 — VIBE_Math_Atan2: defined in math_trig.cpp (its natural
// Math-module home). Declared in math_trig.h; not redefined here to avoid an ODR
// clash. The x87 software path lives in Atan2Software above.

// gilde.exe 0x5f56ec — VIBE_Math_Atan2Unary: atan2(y, 1.0)
double AtanUnary(double y) {
    if (UseX87SoftwarePath())
        return Atan2Software(y, 1.0);
    return std::atan2(y, 1.0);
}

} // namespace guild::util
