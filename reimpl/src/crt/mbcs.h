#pragma once
#include "guild/common/types.h"

// Multibyte-character classification primitives from gilde.exe. These mirror the
// MSVC CRT _ismbb* / _mbclen / _mbsinc family, all keying off two pieces of
// runtime locale state:
//
//   dword_140A9C0  — "MBCS code page active" flag (set by _setmbcp). When zero
//                    every byte is a single-byte char.   -> MbcsState::active
//   byte_140A9D1   — the 256-entry _mbctype table indexed by byte value. The
//                    relevant bits used by this module are:
//                        bit 0 (0x01) = lead byte   (_M1)
//                        bit 3 (0x08) = kanji/_MS    (via byte_64C305 for CP932)
//                    In the static image the table is all-zero (C locale); it is
//                    populated at runtime per code page.   -> MbcsState::ctype
//
// To keep this self-contained and testable we expose an MbcsState the caller can
// populate (e.g. with the standard CP932 Shift-JIS lead-byte ranges) instead of
// reaching into the OS. A default global state starts inactive (C locale), which
// reproduces the all-zero static table behaviour exactly.
//
// Functions translated:
//   VIBE_Mbcs_IsLeadByte      @0x6067d0
//   VIBE_Locale_CharByteLength@0x60a100  (a.k.a. _mbclen)
//   VIBE_Mbcs_IsStringEnd     @0x60b050
//   VIBE_Mbcs_AdvanceChar     @0x60b0f0  (_mbsinc)
namespace guild::crt {

// Lead-byte bit in the _mbctype table (byte_140A9D1).
constexpr u8 kMbLeadBit = 0x01;  // _M1
// Kanji bit (used by VIBE_Mbcs_IsKanjiByte for CP932 via byte_64C305).
constexpr u8 kMbKanjiBit = 0x08; // _MS

struct MbcsState {
    bool active = false; // dword_140A9C0
    u8   ctype[256] = {}; // byte_140A9D1 — _mbctype lead-byte/class table

    // Helper: mark the standard Shift-JIS (CP932) lead-byte ranges
    // 0x81..0x9F and 0xE0..0xFC, matching what _setmbcp(932) installs.
    void SetCp932LeadBytes();
};

// The process-wide locale state (mirrors the original's globals). Default is the
// inactive C locale.
MbcsState& Mbcs();

// gilde.exe 0x6067d0 — VIBE_Mbcs_IsLeadByte  (__usercall, eax = (byte@eax))
// Returns the lead-byte bit of byte_140A9D1[byte] (0 or 1). Note: the original
// does NOT gate this on the active flag — it indexes the table directly.
int IsLeadByte(int byte);

// gilde.exe 0x60a100 — VIBE_Locale_CharByteLength (_mbclen)
//   (__usercall, eax = (s@eax)).  Returns 2 if *s is an active lead byte, else 1.
int CharByteLength(const u8* s);

// gilde.exe 0x60b050 — VIBE_Mbcs_IsStringEnd  (__usercall, eax = (s@eax)).
// Returns 1 at a normal NUL terminator, 2 if a lead byte is immediately followed
// by NUL (a truncated multibyte char), else 0.
int IsStringEnd(const u8* s);

// gilde.exe 0x60b0f0 — VIBE_Mbcs_AdvanceChar (_mbsinc)  (__usercall, eax = (s@eax)).
// Advance past one logical character: 2 bytes for a complete lead+trail pair,
// otherwise 1.
const u8* AdvanceChar(const u8* s);

} // namespace guild::crt
