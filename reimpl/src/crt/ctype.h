#pragma once
#include "guild/common/types.h"

// CRT character classification and case-conversion primitives from gilde.exe,
// namespace guild::crt.
//
// This is the "ctype" half of the numeric/character module (the parsing half lives
// in strtol.{h,cpp}). It recovers the two locale tables byte-for-byte and the
// classification/case functions that key off them:
//
//   off_14529B0 -> the 256-entry MSVC `__pctype` table (16-bit class bits per byte),
//                  recovered here as kPctype[]. Bit meanings (MSVC _ctype masks):
//                      0x001 _UPPER   0x002 _LOWER  0x004 _DIGIT  0x008 _SPACE
//                      0x010 _PUNCT   0x020 _CONTROL 0x040 _BLANK 0x080 _HEX
//                      0x100 _LEADBYTE 0x200 _ALPHA
//   dword_1452BBC — the "code-page > 1 byte / MBCS active" flag. In the static image
//                  it is 1, so the fast path `kPctype[c] & mask` is always taken;
//                  modelled as MbcsCodePageActive() (default false == fast path).
//   byte_64A208  — a separate 256-byte helper table used only by VIBE_Crt_StrToLong
//                  (strtol's C-runtime variant). Recovered here as kStrtolCtype[].
//   dword_145A3C4 — the active "wide-case code page" used by the locale ToUpper/ToLower
//                  and the String case ops. 0 in the static image (C locale); modelled
//                  as CaseCodePage() (default 0 == ASCII path).
//
// All four globals are runtime locale state; in the C locale (the static image and
// every observed call in gilde.exe) the ASCII fast paths are exact. We expose the
// state via small accessors so a test could flip it, but default to the C locale.
namespace guild::crt {

// off_14529B0[0] — MSVC __pctype class-bit table (256 x u16). Recovered byte-for-byte.
extern const u16 kPctype[256];

// byte_64A208 — strtol's private whitespace(0x02)/... helper table (256 x u8).
// Bit 0x02 marks the bytes treated as leading whitespace by VIBE_Crt_StrToLong
// (it indexes the table at (*p + 1), i.e. with a +1 bias). Recovered byte-for-byte.
extern const u8 kStrtolCtype[256];

// dword_1452BBC > 1  — true when a multibyte code page is active (slow CharTypeQuery
// path). Default false == the static-image C locale (fast path).
bool MbcsCodePageActive();
void SetMbcsCodePageActive(bool active);

// dword_145A3C4 — active code page for the locale ToUpper/ToLower / String case ops.
// 0 == C locale (ASCII case folding). Default 0.
int CaseCodePage();
void SetCaseCodePage(int cp);

// gilde.exe 0x1421c9b — VIBE_Crt_CharTypeQuery  (a1 = char, a2 = mask).
// Returns `mask & kPctype[a1]` for single bytes. The original's multibyte branch
// (a1 outside 0..0x100) calls into the locale MB->WC adapter; that path is only
// reachable when an MBCS code page is active and is deferred (see ctype.cpp).
int CharTypeQuery(int ch, int mask);

// gilde.exe 0x60b4c0 — VIBE_Crt_DigitValue  (__usercall, eax = (c@al)).
// Maps a base-36 digit character to its value 0..35; returns 37 for non-digits
// (so a caller comparing against `base` always rejects it).
int DigitValue(u8 c);

// gilde.exe 0x5f0c00 — VIBE_Util_ToLower  (__usercall, eax = c@eax).
// ASCII tolower: 'A'..'Z' -> +32, else unchanged.
int ToLowerAscii(int c);

// gilde.exe 0x5ea650 — VIBE_Util_CharToUpper (__usercall, eax = c@eax).
// ASCII toupper: 'a'..'z' -> -32, else unchanged.
int ToUpperAscii(int c);

// gilde.exe 0x1425fd1 — VIBE_Locale_ToUpper.  In the C locale (CaseCodePage()==0)
// this is ASCII toupper. The MBCS code-page path is deferred (see ctype.cpp).
int LocaleToUpper(int c);

// gilde.exe 0x1426d1e — VIBE_Locale_ToLower.  In the C locale this is ASCII tolower.
// The MBCS code-page path is deferred (see ctype.cpp).
int LocaleToLower(int c);

} // namespace guild::crt
