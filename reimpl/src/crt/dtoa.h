#pragma once
#include "guild/common/types.h"

// CRT IEEE-double -> decimal-digits conversion (dtoa) from gilde.exe,
// namespace guild::crt.
//
// This is the digit-generation core behind the C-runtime float formatter
// (_cfltcvt / _fptostr family) used by sprintf("%f"/"%e"/"%g", double). It is
// DISTINCT from the printf engine's 16.16 fixed-point %f path
// (VIBE_Crt_FormatFixedFloat @0x605848 — see printf.h / printf_float.h).
//
// Original pipeline (all __cdecl unless noted):
//   VIBE_Math_DoubleToExtended80   @0x14272b0 — IEEE double -> x87 80-bit extended
//   VIBE_Float_FormatToDigits      @0x1429420 — 80-bit extended -> decimal digits
//                                                using a 96-bit big-int engine and
//                                                a power-of-ten table of 80-bit floats
//   VIBE_Math_DoubleToLongDoubleCvt@0x142724c — driver: fills the `StrFlt` struct
//   VIBE_Math_FormatDigitsRound    @0x14271d5 — copy + round `ndigits` of mantissa
//
// The 80-bit big-int engine (VIBE_Math_BigInt* @0x1426e88..0x1429a96) and its two
// power-of-ten tables (unk_1455510 positive, unk_1455670 negative; arrays of
// 12-byte slots holding 80-bit extended floats) were recovered byte-for-byte; see
// dtoa.cpp for the table provenance. Per the agent guide we model the x87 80-bit
// arithmetic with the host `long double` (which is 80-bit extended on x86-64),
// reproducing the same 17-significant-digit decimal mantissa + decimal exponent
// that the original big-int conversion yields. The result feeds the byte-identical
// formatting in printf_float.cpp, which is what the C-library oracle is matched to.
namespace guild::crt {

// byte_1452BC0 @0x1452bc0 — the locale decimal-point character ('.' in the C
// locale / the static image). Used by the formatters.
extern char kDecimalPoint;

// The `StrFlt` record produced by VIBE_Math_DoubleToLongDoubleCvt. Mirrors the
// MSVC `strflt`: enough mantissa digits for 17 significant figures plus rounding
// slack. `decpt` is the position of the decimal point relative to the first
// mantissa digit (decpt==1 means d.dddd...). For a value of 0 the mantissa is
// "0" and decpt is 1.
struct StrFlt {
    char sign;        // +0x00 in the original: '-' (0x2D) if negative else ' ' (0x20)
    int  decpt;       // +0x04: decimal exponent (decimal-point position)
    int  error;       // +0x08: 0 ok, 5/6 = inf/nan code, 1 = zero
    // +0x0C: NUL-terminated significant digits (no sign/point). The original
    // generates the exact decimal expansion as needed; we size the buffer to hold
    // the full integer-part expansion of any normal-range double (~309 digits)
    // plus rounding/precision slack so %f of large magnitudes matches C exactly.
    char mantissa[800];
};

// gilde.exe 0x142724c — VIBE_Math_DoubleToLongDoubleCvt.  Convert `value` to its
// decimal mantissa / exponent (17 significant digits), filling `out`. This is the
// shared first stage of every %f/%e/%g formatting.
void DoubleToStrFlt(double value, StrFlt* out);

// gilde.exe 0x14271d5 — VIBE_Math_FormatDigitsRound.  Copy `ndigits` mantissa
// digits of `sf` into `dst` (a leading scratch byte is reserved at dst[-0]),
// rounding the last digit up if the following digit is >= '5'. A carry that
// reaches the leading position increments sf->decpt. `dst` must have room for
// ndigits+2 bytes. Returns the (possibly carried) digit string start.
char* FormatDigitsRound(char* dst, int ndigits, StrFlt* sf);

} // namespace guild::crt
