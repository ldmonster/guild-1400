#pragma once
#include "guild/common/types.h"

// CRT x87 long-double -> decimal-digits engine from gilde.exe (namespace
// guild::crt). This is the MSVC `_$I10_OUTPUT` family — the *other* float
// formatter in gilde.exe, distinct from the IEEE-double dtoa core in dtoa.h /
// printf_float.h.
//
//   VIBE_Math_FloatToDigits    @0x5fa7fd — classify an 80-bit extended value,
//                                          generate its decimal digits, round to
//                                          the requested count, fill the output
//                                          control struct via the two emitters.
//   VIBE_Text_FormatFixedPoint @0x5fac3f — lay the rounded digits out as a fixed
//                                          (%f) field (integer part . fraction).
//   VIBE_Text_FormatExponent   @0x5fae5d — lay them out as scientific (%e) with a
//                                          signed exponent of 2..4 digits.
//   VIBE_Crt_FormatExponentFloat @0x6060be — the %e/%E/%g/%G entry that seeds the
//                                          control struct and drives FloatToDigits.
//
// The digit-pair leaf emitters (VIBE_Text_FormatDigitPair* @0x5faafe/87/d1) live
// in guild::gui::text (src/gui/text/text_format2.cpp); they are reused here, not
// redefined.
//
// The 80-bit big-int scaling (VIBE_Math_ScaleByPowerOfTen / PowerOfTenScale, and
// the 10^(2^k) extended-float table unk_64A9C0) was recovered byte-for-byte; per
// the agent guide we reproduce its *observable* result — the correctly-rounded
// significant-digit string plus the decimal exponent — using the host long double
// (80-bit extended on x86-64), then run the byte-faithful FormatFixedPoint /
// FormatExponent layout. The unk_64A9C0 power table is documented in i10_output.cpp.
namespace guild::crt {

using guild::i32;
using guild::u8;

// The output control struct FloatToDigits fills and the two layout routines read.
// Field offsets are byte-exact to the original (an array of 32-bit slots starting
// at the struct base, with the exponent marker char overlaid on slot 3).
struct I10Control {
    i32 precision;     // +0x00  field precision (# fractional / significant digits)
    i32 extraCount;    // +0x04  secondary digit count ('g' subtracts, %e leading)
    i32 flags;         // +0x08  low byte: 0x02 fixed(%f) 0x04 sci(%e/%g) 0x08 strip
                       //                   0x10 alt(#) 0x20 wide(L) 0x40 dwide
    i32 expMarker;     // +0x0C  exponent marker char ('e'/'E'); 0 => no marker
    i32 expDigits;     // +0x10  exponent digit-count seed (0,1,2,3 => min width)
    i32 sign;          // +0x14  -1 when the value is negative, else 0
    i32 outLen0;       // +0x18  produced run lengths (integer part / leading)
    i32 outLen1;       // +0x1C
    i32 outLen2;       // +0x20
    i32 outLen3;       // +0x24
    i32 outLen4;       // +0x28
};

// gilde.exe 0x5fa7fd — VIBE_Math_FloatToDigits  (__usercall ax=(value@eax,
// ctrl@edx, dst@ebx)). `value` points at a 10/12-byte x87 extended float; `ctrl`
// is the control struct (precision/flags pre-filled); `dst` is the output buffer.
// Returns 0 (the original's v40). Fills ctrl's sign + outLen fields and writes the
// formatted digit field into `dst`.
i32 FloatToDigits(const long double* value, I10Control* ctrl, char* dst);

// gilde.exe 0x5fac3f — VIBE_Text_FormatFixedPoint  (__userpurge eax=(ctrl@eax,
// digits@edx, decExp@ecx, ndigits@ebx, dst)). Lay the `ndigits` significant digits
// at `digits` out as a fixed-point field, decimal point after `decExp` integer
// digits. Returns the number of fraction digits emitted (the original's result).
i32 FormatFixedPoint(I10Control* ctrl, const char* digits, i32 decExp,
                     i32 ndigits, char* dst);

// gilde.exe 0x5fae5d — VIBE_Text_FormatExponent  (__userpurge (ctrl@eax,
// digits@edx, decExp@ecx, ndigits@ebx, dst)). Lay the digits out as scientific
// notation: mantissa, marker, sign, and a 2..4 digit exponent.
void FormatExponent(I10Control* ctrl, const char* digits, i32 decExp,
                    i32 ndigits, char* dst);

// gilde.exe 0x6060be — VIBE_Crt_FormatExponentFloat.  The %e/%E/%g/%G/%f driver:
// seeds an I10Control from the conversion letter `conv`, the `precision` (-1 => 6),
// and the '#' alt flag, runs FloatToDigits, then prepends the sign byte.
//   conv : one of 'e','E','f','F','g','G'.
//   dst  : output buffer; on return holds the NUL-terminated formatted number.
// Returns `dst`.
char* FormatExponentFloat(char conv, int precision, bool alt, long double value,
                          char* dst);

} // namespace guild::crt
