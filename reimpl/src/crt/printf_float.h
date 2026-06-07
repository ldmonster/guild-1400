#pragma once
#include "guild/common/types.h"

// CRT IEEE-double %f / %e / %g formatting from gilde.exe, namespace guild::crt.
//
// This is the C-runtime float formatter family (the `_cftof` / `_cftoe` / `_cftog`
// `_cfltcvt` group) that turns a `double` into its printf string via the dtoa
// digit core (see dtoa.h). It is the path C's snprintf("%f"/"%e"/"%g", d) uses.
//
//   VIBE_FloatFormat_Fixed       @0x1422906 — %f  (FormatFixed)
//   VIBE_FloatFormat_Exponential @0x1422802 — %e/%E (FormatExponential)
//   VIBE_FloatFormat_General     @0x14229e4 — %g/%G (FormatGeneral)
//   VIBE_FloatFormat_Dispatch    @0x1422ac9 — pick by conversion letter
//   VIBE_FloatFormat_InsertDecimal @0x1422704 / TrimZeros @0x142275e — %g helpers
//   VIBE_String_ShiftRight       @0x1422b1a — make room for an inserted char/zeros
//
// IMPORTANT — two unrelated "%f" paths in gilde.exe:
//   * This module formats IEEE doubles (matches C snprintf).
//   * The integer-printf engine's own float branch
//     (VIBE_Crt_FormatFixedFloat @0x605848) formats 16.16 FIXED-POINT values
//     (game animation/UI numbers) via VIBE_AnimationState_Update, NOT doubles.
//     That one is reproduced as FormatFixed16_16 below and documented as a
//     separate, non-C-compatible converter.
namespace guild::crt {

// FormatFixed/Exponential/General write a NUL-terminated string into `dst`
// (caller-sized) and return `dst`. `precision` is the printf precision; for %g it
// is the significant-digit count (>=1). `upper` selects 'E' vs 'e' for the
// exponent marker. They mirror the original byte-for-byte over the StrFlt digits.

// gilde.exe 0x1422906 — VIBE_FloatFormat_Fixed.
char* FormatFixed(double value, char* dst, int precision);

// gilde.exe 0x1422802 — VIBE_FloatFormat_Exponential.  `upper` => 'E'.
char* FormatExponential(double value, char* dst, int precision, bool upper);

// gilde.exe 0x14229e4 — VIBE_FloatFormat_General.  `upper` => 'E'/'G'.
char* FormatGeneral(double value, char* dst, int precision, bool upper);

// gilde.exe 0x1422ac9 — VIBE_FloatFormat_Dispatch.  `conv` is one of e/E/f/g/G.
char* FormatDispatch(double value, char* dst, int conv, int precision);

// gilde.exe 0x605848 — VIBE_Crt_FormatFixedFloat.  The integer-printf engine's
// %f branch: formats a 16.16 fixed-point value (NOT an IEEE double). `fixed` is
// the raw 32-bit 16.16 value; `precision` (-1 => 4) fractional digits. Writes a
// NUL-terminated string into `dst`. Kept here for completeness/parity; it is the
// game's fixed-point %f and does not match C's double %f.
void FormatFixed16_16(char* dst, i32 fixed, int precision);

} // namespace guild::crt
