#pragma once
#include "guild/common/types.h"

// CRT locale ctype-table setup from gilde.exe, namespace guild::crt.
//
// The locale subsystem builds two runtime tables keyed by byte value:
//   byte_1464E01  — per-byte class bits (the locale _ctype overlay):
//                     0x10 == upper, 0x20 == lower (the bits this module sets)
//   byte_1464D00  — per-byte case-fold table (the locale lower/upper map)
// and reads them via VIBE_Locale_IsCType.
//
//   VIBE_Locale_BuildCTypeTables @0x1428a7f — fills the two tables. The MBCS
//     code-page branch (GetCPInfo + MB->WC adapter) is owned by another agent
//     and deferred; this module reimplements the C-locale fallback path (the
//     ASCII A-Z / a-z setup) which is exactly what runs in the static image and
//     every observed call.
//   VIBE_Locale_IsCType @0x142880f — (byte, ansiMask, localeMask): true if the
//     locale class bits OR the kPctype ANSI class bits match.
//   VIBE_Locale_IsSpace @0x14287fe — IsCType(byte, 0, 4): tests the locale
//     space bit (4) only.
//
// The ANSI class table (word_14529BA == kPctype) lives in ctype.{h,cpp} and is
// reused here (ODR: declared there, not redefined).
namespace guild::crt {

// Locale class bits set by the C-locale path of BuildCTypeTables.
constexpr u8 kLocaleUpper = 0x10; // _UPPER overlay
constexpr u8 kLocaleLower = 0x20; // _LOWER overlay

// The two runtime locale tables. Recovered as 256-byte arrays.
struct LocaleCType {
    u8 classBits[256] = {}; // byte_1464E01 — locale class overlay
    u8 caseFold[256] = {};  // byte_1464D00 — locale case-fold map (0 if none)
};

// Process-wide locale ctype state. Defaults to the C locale (built lazily).
LocaleCType& Locale();

// VIBE_Locale_BuildCTypeTables @0x1428a7f (C-locale path). Fills the class-bit
// and case-fold tables for ASCII A-Z (upper, +32 fold) and a-z (lower, -32
// fold); everything else gets caseFold 0. Returns the loop terminator (0x100),
// matching the original's `return result`.
unsigned BuildCTypeTablesC();

// VIBE_Locale_IsCType @0x142880f. ansiMask indexes the ctype.cpp kPctype table.
int LocaleIsCType(u8 ch, int ansiMask, u8 localeMask);

// VIBE_Locale_IsSpace @0x14287fe — LocaleIsCType(ch, 0, 4).
int LocaleIsSpace(u8 ch);

} // namespace guild::crt
