#pragma once
// Guild text module — leaf number / money / date formatters.
//
// These are the small, self-contained value formatters the rich-text engine
// (VIBE_Text_RenderRichString @0x59d6e8 / RenderFormattedMessage @0x59f99c)
// invokes inline to turn a numeric argument into its display string. The two
// KB-scale dispatchers do in-place buffer rewriting; the *value -> string*
// production lives here so it can be golden-tested in isolation.
//
// Recovered originals:
//   VIBE_Money_FormatWithSeparators @0x58f798 — amount -> "1.234<icon>" money string
//   VIBE_GameTime_PackToRecord      @0x583304 — minute-stamp -> date record
//   VIBE_GameTime_GetSeasonFromYear @0x583384 — (year>>16) % 4
//   VIBE_Math_RandomModulo          @0x58b89c — RandNext() % n  (reused from crt)
//   VIBE_Text_FormatDigitPair*      @0x5faafe/0x5faae7/0x5faad1 — pair/quad/oct digit emit
//   the inline thousands-separator loop in RenderRichString (the 'i' sub-specifier)
//
// The original money formatter divides the raw amount by a *runtime* per-currency
// exchange rate (dword_13CD6F2[189*currency]>>16 selects dword_649A88[...]). That
// rate table is populated by the economy/world cluster at run time, so to keep the
// formatter deterministic and testable we expose the divisor as an explicit
// argument (the original effectively passes divisor==1 for the player's home
// currency). All separator/rounding/icon-byte logic is byte-faithful.

#include "guild/common/types.h"
#include <cstddef>

namespace guild::gui::text {

using guild::i32;
using guild::u8;
using guild::u32;

// ---------------------------------------------------------------------------
// Icon / control bytes the engine embeds in produced strings (the rich-text
// renderer later turns these into inline glyph/icon words). Recovered as the
// literal integer operands of the sprintf calls in the originals.
// ---------------------------------------------------------------------------
inline constexpr char kIconMoney = 17;  // 0x11 — coin/"Gulden" icon (money 'm' code)
inline constexpr char kIconCount = 20;  // 0x14 — quantity icon ('c' code, "%i%c")
inline constexpr char kSepDot    = '.'; // 0x2E — thousands separator (German grouping)

// ---------------------------------------------------------------------------
// Thousands-separator integer formatter.
//
// Recovered from the inline loop in RenderRichString (the bare 'i' sub-specifier,
// e.g. "%i" with no '$'): format `value` as decimal, then walk it back-to-front
// inserting a '.' every 3 digits. The original computes the output length as
//   outLen = (len-1)/3 + len
// and fills from the end. Sign is preserved: a leading '-' is NOT grouped (the
// digit count used for grouping excludes it, matching `j && ...` guard).
// Returns the number of characters written (excluding the NUL).
int FormatGroupedInt(i32 value, char* out);

// ---------------------------------------------------------------------------
// Money formatter — VIBE_Money_FormatWithSeparators @0x58f798.
//
// amount/divisor is rounded to nearest (the +0.5 in dbl_6269BC), grouped with
// '.' separators when >=1000, and suffixed with the coin icon byte 0x11.
//   divisor : per-currency exchange rate (>=1; original reads it from the
//             runtime rate table; pass 1 for the home currency).
// Faithful cases:
//   0            -> "0\x11"
//   1..999       -> "<n>\x11"
//   <0, ==0      -> "0\x11"          (sign dropped when rounded magnitude is 0)
//   <0, 1..999   -> "-<n>\x11"
//   >=1000       -> "<grouped>\x11"  (e.g. 1234 -> "1.234\x11")
//   <=-1000      -> "-<grouped>\x11"
// Returns characters written (excluding NUL).
int FormatMoney(i32 amount, i32 divisor, char* out);

// ---------------------------------------------------------------------------
// Date record — the 12-byte struct VIBE_GameTime_PackToRecord @0x583304 writes.
// Field offsets are byte-exact to the original record.
//   +0 (u8)  day-marker      (always 1)
//   +1 (u8)  month           (3*(minuteStamp%4)+1  -> 1,4,7,10 : quarter month)
//   +2 (u16) year            (rawYear + 1400)
//   +4 (u8)  hour            (copied from source +4)
//   +5 (u8)  minute          (copied from source +6)
//   +8 (u32) extra           (copied from source +10)
// The packed "year" word at +2 (>>16 in the caller) is what the 'T'/date code
// prints; the season is (year) % 4.
struct DateRecord {
    u8  dayMarker;  // +0
    u8  month;      // +1
    u16 year;       // +2
    u8  hour;       // +4
    u8  pad5;       // (alignment to +5..)
    u8  minute;     // +5  (original stores at byte +5; pad5 covers the gap)
    u8  pad6;
    u8  pad7;
    u32 extra;      // +8
};

// Source layout the packer reads (VIBE_GameTime record):
//   +0 (i32) rawYearAndQuarter   (low: quarter via %4; high: year via >>16... see note)
// NOTE: the original reads `*(WORD*)src` for the +1400 year base and `*(i32*)src % 4`
// for the quarter. We model the two source fields the packer actually touches.
struct GameTimeSource {
    i32 yearQuarter;  // +0 : (WORD) base year ; (i32 % 4) quarter selector
    u8  hour;         // +4
    u8  pad5;
    u8  minute;       // +6
    u8  pad7[4];      // +7..+9
    i32 extra;        // +10 (unaligned in original; modeled aligned here)
};

// VIBE_GameTime_PackToRecord @0x583304.
void PackDateRecord(const GameTimeSource& src, DateRecord& out);

// VIBE_GameTime_GetSeasonFromYear @0x583384 — (yearField >> 16) % 4.
// Takes the raw packed dword whose high word is the year.
int SeasonFromYear(i32 packedYearDword);

} // namespace guild::gui::text
