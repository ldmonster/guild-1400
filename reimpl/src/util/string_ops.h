#pragma once
#include "guild/common/types.h"
#include <cstddef>

// guild::util — the engine's hand-written C string primitives recovered from
// gilde.exe. These are the MSVC-CRT-derived ASCII string routines the game uses
// pervasively (file-name handling, text DB parsing, config). They are OS-free
// leaves and bit-for-bit faithful to the originals; the unit tests cross-check
// them against <cstring> / <cctype> as oracles where the behaviour coincides.
//
// NOTE: several of these have deliberately non-libc-standard semantics (e.g.
// StrChrLast records the LAST match, like strrchr; StrnLen returns the count of
// non-NUL bytes within `cap`, but the raw `cap` when no terminator is in range).
// Those quirks are reproduced exactly; see string_ops.cpp for the line-by-line
// mapping to the original pseudocode.
namespace guild::util {

// VIBE_Util_StrStr @0x1427fd0 — strstr. Returns a pointer to the first occurrence
// of the (NUL-terminated) substring `needle` in `hay`, or nullptr. An empty
// needle returns `hay` (matches the original's `if (!*a2) return a1`).
char* StrStr(char* hay, const char* needle);

// VIBE_Util_StrnLen @0x14287d3 — bounded length probe. Scans up to `cap` bytes of
// `s`. If a NUL is found within the first `cap` bytes, returns the index of that
// NUL (i.e. the string length); otherwise returns `cap`. Faithful to the original,
// which returns `(v - s)` when terminated and the raw `cap` when not.
std::size_t StrnLen(const char* s, int cap);

// VIBE_Util_StrnCpy @0x1428cb0 — strncpy. Copies up to `n` bytes from `src` into
// `dst`; if `src` is shorter than `n`, the remainder of `dst` is NUL-padded; if
// `src` is `n` or longer, `dst` is NOT NUL-terminated. Returns `dst`. The original
// is the word-at-a-time MSVC strncpy; this is the behaviour-identical byte form.
char* StrnCpy(char* dst, const char* src, std::size_t n);

// VIBE_Util_StrChr @0x5d3ef0 — returns a pointer to the LAST occurrence of `c`
// (matched as a raw char) in `s`, or nullptr. This is strrchr semantics: the
// original keeps overwriting the result on every match. `c == 0` matches the
// terminating NUL.
char* StrChrLast(char* s, char c);

// VIBE_Util_StrToUpper @0x5e9f50 — in-place ASCII upper-case of `s` (only 'a'..'z'
// are affected). Returns `s`.
char* StrToUpper(char* s);

// VIBE_Util_StrncmpN @0x5e9ee0 — strncmp over at most `n` bytes. Returns the signed
// difference of the first differing unsigned byte, or 0 if equal within `n`.
int StrncmpN(const char* a, const char* b, int n);

// VIBE_Util_StrCmpNoCase @0x5cb8f0 — stricmp. ASCII case-insensitive (folds 'A'..'Z'
// to lower case). Returns the difference of the first differing folded byte.
int StrCmpNoCase(const char* a, const char* b);

// VIBE_Util_StrCmpNoCaseN @0x5e0db0 — strnicmp. Case-insensitive (folds to lower)
// over at most `n` bytes. Returns the folded-byte difference, or 0.
int StrCmpNoCaseN(const char* a, const char* b, int n);

// VIBE_Util_StrCmpNoCaseInline @0x5ea788 — an alternate case-insensitive compare
// that folds the OTHER way ('a'..'z' -> upper) and normalises its return to
// exactly -1 / 0 / +1 (not the raw byte difference). Reproduced 1:1.
int StrCmpNoCaseSign(const char* a, const char* b);

} // namespace guild::util
