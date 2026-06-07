#pragma once
// Money display formatting — the thousands-grouped Gulden string the trade,
// market and family-wealth panels render. Translated from gilde.exe:
//
//   VIBE_Money_FormatWithSeparators  0x58f798
//     (__usercall, eax = (amount@eax, currencyId@dl, dest@ebx))
//
// The original divides the raw amount by the per-currency rate
// (dword_649A88[ dword_13CD6F2[189*currencyId] >> 16 ]) — the SAME rate model the
// other money helpers use (see world/trade_player.h, world/exchange.h) — rounds
// half-up, then renders the rounded magnitude with '.' grouping every three
// digits and appends the currency glyph byte 0x11 (17). Negative amounts get a
// leading '-'; a rounded magnitude of 0 always renders "0" + glyph (no sign).
//
// FAITHFULNESS
//   * Rounding: v6 = (double)abs(amount)/rate + 0.5; (int)v6 after the FPU is put
//     in round-toward-zero mode (VIBE_Coord_ConvertX) == trunc(|amount|/rate+0.5)
//     == round-half-up of |amount|/rate.  (dbl_6269BC == 0.5.)
//   * Grouping: builds the string back-to-front, inserting a '.' (46) after every
//     third digit (the exact (v8+1)%3 cadence of the original loop).
//   * Glyph: the trailing byte is 0x11; we expose it as kCurrencyGlyph so tests
//     and callers can substitute a printable stand-in when needed.
#include <string>

#include "guild/common/types.h"

namespace guild::world {

// The in-game Gulden currency glyph the formatter appends (font code-point 17).
constexpr char kCurrencyGlyph = '\x11';

// Round-half-up bias added before the truncate-toward-zero (gilde.exe dbl_6269BC).
constexpr double kMoneyFormatRoundBias = 0.5;

// gilde.exe 0x58f798 — VIBE_Money_FormatWithSeparators.
// Formats `amount` (already converted to display units by dividing by `rate`)
// as a thousands-grouped string with a trailing currency glyph. `rate` is the
// per-currency scalar (dword_649A88[...]); pass 1 for "no conversion". The
// original always uses rate != 0; a rate of 0 is treated as 1 (identity) here to
// keep the helper total.
std::string MoneyFormatWithSeparators(i32 amount, i32 rate = 1);

// Lower-level core exposed for golden-vector testing: groups the decimal digits
// of `magnitude` (>= 0) with '.' separators every three digits, WITHOUT the sign
// or the currency glyph. Mirrors the back-to-front fill of the original's
// >= 1000 branch (and returns the plain decimal for magnitude < 1000).
std::string MoneyGroupThousands(u32 magnitude);

} // namespace guild::world
