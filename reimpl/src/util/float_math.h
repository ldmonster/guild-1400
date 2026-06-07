#pragma once
#include "guild/common/types.h"

// Floating-point math entry points (guild::util) — the x87 / libm dispatch.
//
// gilde.exe's math helpers branch on the CPU-feature flag byte_64A958
// (see fpu.h: UseX87SoftwarePath). On a normal CPU the flag is 0 and the
// originals execute the plain x87 instruction (FPREM for fmod, FYL2X for the
// logs, the FPU's own atan2), which is bit-for-bit the same value the libm /
// <cmath> functions return for the runtime range these are called with. The
// flag is only set on a faulty Pentium (FDIV bug) to take a software emulation
// path. We therefore implement the normal path via <cmath> and DOCUMENT the
// equivalence per function, while preserving the flag-based dispatch so the
// software-path branch is still reachable and observable.
//
// Split rationale: fpu.{h,cpp} owns the control/status-word state machine and
// the feature flag; float_math.{h,cpp} owns the value-producing math routines
// that consume that state. Each can be tested in isolation.
namespace guild::util {

// Base codes the original LogBase used to pick the FYL2X multiplier. The
// numeric mapping is reproduced exactly from gilde.exe 0x5fca30:
//   code 9   -> multiplier 1.0           -> log2(x)
//   code 11  -> multiplier log10(2)      -> log10(x)
//   other    -> multiplier ln(2)         -> ln(x)
// (Note: IDA's thunk names Log10Thunk/Log2Thunk pass 9/11 respectively, which
// is swapped relative to those names; we key on the code value, not the name.)
enum LogBaseCode : int {
    kLogCode_Log2   = 9,
    kLogCode_Log10  = 11,
    kLogCode_Natural = 10,
};

// gilde.exe 0x5d3fb2 — VIBE_Math_Fmod  (st0 = fmod(st1, st0))
// a = dividend, b = divisor. Result has the sign of the dividend (a), matching
// x87 FPREM / std::fmod. With byte_64A958==0 this is exactly std::fmod; the set
// flag routes through FmodPrepare (software FPREM emulation) which produces the
// same reduced remainder. Documented equivalence: std::fmod.
double Fmod(double a, double b);

// gilde.exe 0x6029b4 / 0x6089be — VIBE_Math_SqrtGuarded + FpClassifyAdjust.
// The original guards FSQRT: if the operand is negative (FTST sets C0) it
// raises a domain exception and returns a NaN/0 per FpClassifyAdjust; otherwise
// it executes FSQRT. Equivalent to std::sqrt (which returns NaN for x<0).
double Sqrt(double x);

// gilde.exe 0x5fca30 — VIBE_Math_LogBase. Raw base-coded log (see LogBaseCode).
// For x<=0 the original forwards to the CRT domain-error handler and returns x;
// std::log/log2/log10 return -inf at 0 and NaN for x<0, which is the value the
// FYL2X path yields once the (masked) exception is taken. Documented as the
// <cmath> equivalent of the selected base.
double LogBase(int code, double x);

// gilde.exe natural-log path (code 10) — ln(x). Equivalent: std::log.
double Log(double x);
// gilde.exe 0x5fca91/0x5fca71 path (code 9) — log2(x). Equivalent: std::log2.
double Log10(double x);  // see LogBaseCode note: code 9 -> log2
// gilde.exe 0x5fca75 path (code 11) — log10(x). Equivalent: std::log10.
double Log2(double x);   // see LogBaseCode note: code 11 -> log10

// gilde.exe 0x608aa0 / 0x60429c / 0x5f5701 — VIBE_Math_Atan2 dispatch.
// byte_64A958==0 -> atan2(y, x); set -> the x87 polynomial approximation
// (Atan2Approx) reached through CallWithFpuState. Documented equivalence:
// std::atan2. Argument order matches the original: Atan2(y, x).
double Atan2(double y, double x);
// gilde.exe 0x5f56ec — VIBE_Math_Atan2Unary: atan2(y, 1.0) == atan(y).
double AtanUnary(double y);

} // namespace guild::util
