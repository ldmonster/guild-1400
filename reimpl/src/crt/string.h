#pragma once
#include "guild/common/types.h"

// CRT / app string and memory routines, namespace guild::crt.
//
// The four hot routines (Length/Concat/Compare/FindChar) are MSVC's word-at-a-time
// optimized strlen/strcat/strcmp/strchr. They are bit-for-bit faithful clones of the
// standard C library functions (the magic constants 0x7EFEFEFF / 0x81010100 are the
// classic "has-zero-byte" word trick), so we reimplement them with the same observable
// behavior and verify against <cstring> in the unit tests.
//
// Several neighboring VIBE_String_* / VIBE_Crt_* routines depend on other modules
// (Locale, Mbcs, Mem, Math, Runtime) and are intentionally left to those agents; see
// string.cpp / the agent report for the deferred address list.
namespace guild::crt {

// VIBE_String_Length @0x1421d10 — strlen. Returns the number of bytes before the NUL.
u32 StringLength(const char* s);

// VIBE_String_Concat @0x1421ef0 (copy body also entered via the thunk at 0x1421ee0
// -> 0x1421f51) — strcat. Appends src to the NUL-terminated dst and returns dst.
char* StringConcat(char* dst, const char* src);

// VIBE_String_Compare @0x14220c0 — strcmp. Returns <0 / 0 / >0; the original yields
// exactly -1 / 0 / +1 (sign of the first differing unsigned byte).
int StringCompare(const char* a, const char* b);

// VIBE_String_FindChar @0x1422490 — strchr. Returns a pointer to the first occurrence
// of c (matched as unsigned char) in s, or nullptr. c==0 matches the terminating NUL.
char* StringFindChar(char* s, unsigned char c);

// VIBE_String_MatchPrefix @0x4418ec — returns the length of the common prefix of a and
// b, but only while b is non-empty and capped at 128; returns 0 if b is empty or the
// very first byte differs.
int StringMatchPrefix(const char* a, const char* b);

// VIBE_String_GetDelimitedField @0x44ad48 — walks a buffer of consecutive NUL-terminated
// fields (records). Given the start, a field count `n`, and a mode byte, returns a pointer
// to a selected field. Faithful 1:1 of the original record walker; see string.cpp.
char* StringGetDelimitedField(char* start, int n, char mode, char* fallback);

// VIBE_String_SkipLeadingSpaces @0x44b268 — returns a pointer to the first non-space
// (0x20) character, or nullptr if the string is all spaces / empty.
const char* StringSkipLeadingSpaces(const char* s);

// VIBE_String_TrimTrailingSpaces @0x44b2ac — trims trailing spaces in place by writing a
// NUL after the last non-space; returns a pointer into the string (see string.cpp for the
// exact return semantics, which match the original byte-for-byte).
const char* StringTrimTrailingSpaces(char* s);

// VIBE_String_IntToRadix @0x142a511 — core itoa: formats `value` in `radix` (2..36) into
// `buf` (NUL-terminated), with an optional leading '-' when `negative` is set (value is
// then treated as already negated, i.e. -value is formatted). Returns the last char
// written by the in-place reversal (matches original al return).
char StringIntToRadix(u32 value, char* buf, u32 radix, int negative);

// VIBE_String_IntToAscii @0x142a4e4 — itoa: signed for radix 10, unsigned otherwise.
char* StringIntToAscii(int value, char* buf, u32 radix);

// VIBE_String_UIntToString @0x609640 — ultoa core. Formats unsigned `value` in `radix`
// into `buf` (digits 0-9a-z) and returns `buf`.
char* StringUIntToString(u32 value, char* buf, u32 radix);

// VIBE_String_WideEnvLength @0x609040 — length (in u16 units) of a wide string. NOTE the
// original probes a1[1] in its loop, so an empty wide string (*s==0) returns 0 and any
// non-empty string returns the index of its terminator; reproduced 1:1.
int StringWideEnvLength(const u16* s);

// VIBE_Crt_WcsChr @0x60c000 — wcschr. First occurrence of c in the wide string, or null;
// c==0 matches the terminator.
u16* WcsChr(u16* s, u16 c);

} // namespace guild::crt
