#pragma once
// Guild text engine — low-level string helpers feeding the rich-text dispatchers.
//
// Namespace: guild::gui::text. These are leaf helpers the two KB-scale rich-text
// dispatchers (VIBE_Text_RenderRichString @0x59d6e8 / RenderFormattedMessage
// @0x59f99c) and the float-to-digits machinery lean on. They are small and
// deterministic, so they are split out here for golden-vector testing.
//
// Recovered originals:
//   VIBE_Text_FormatDigitPair    @0x5faafe — emit 2 fixed-width decimal digits
//   VIBE_Text_FormatDigitPair2   @0x5faae7 — emit 4 fixed-width decimal digits
//   VIBE_Text_FormatDigitPair3   @0x5faad1 — emit 8 fixed-width decimal digits
//   VIBE_Text_StripNameTokens    @0x4f8d98 — strip up to two '-' tokens, fold '%' codes
//   VIBE_Text_TrimTrailingSpace  @0x59c270 — trim a trailing char-class run, copy out
//   VIBE_Text_GetCurrentIconWord @0x5a3418 — read the current inline-icon word
//   VIBE_Text_AppendWideLines    @0x411ee4 — append a wide-char run N times to a widget
//
// The character-class table the trim helper consults (byte_64A208) is real data
// already reconstructed once, in guild::sim::kCharClass (src/sim/script_lexer.cpp);
// we reuse it rather than duplicating the 256 bytes (ODR).

#include "guild/common/types.h"

namespace guild::gui::text {

using guild::i16;
using guild::i32;
using guild::u8;
using guild::u16;

// ---------------------------------------------------------------------------
// Fixed-width decimal digit emitters (leaves of VIBE_Math_FloatToDigits).
//
// In the original these are __usercall and pass the destination cursor in EBX;
// each call advances EBX past the digits it wrote, and the cursor state persists
// across the (register-threaded) recursive calls. We model that by taking the
// cursor by reference and advancing it.
//
//   FormatDigitPair (@0x5faafe): value in 0..99    -> writes exactly 2 digits.
//   FormatDigitPair2(@0x5faae7): value in 0..9999  -> writes exactly 4 digits.
//   FormatDigitPair3(@0x5faad1): value in 0..1e8-1 -> writes exactly 8 digits.
//
// No NUL is written; the caller terminates. Out-of-range values wrap exactly as
// the original 8/16/32-bit div instructions do (documented per function).
// ---------------------------------------------------------------------------

// @0x5faafe — `a1` is read as a 16-bit value; only the low byte's high/low decimal
// pair is meaningful. For 0..99 emits "00".."99". The original does an 8-bit DIV by
// 10 only when al >= 10 (else it treats ah as the tens digit, normally 0).
void FormatDigitPair(u16 value, char*& out);

// @0x5faae7 — splits `value` by 100 (16-bit DIV when >= 100), then emits the
// hundreds pair followed by the units pair. Covers 0..9999.
void FormatDigitPair2(u32 value, char*& out);

// @0x5faad1 — splits `value` by 10000 (32-bit DIV when >= 10000), then emits the
// high four digits followed by the low four. Covers 0..99999999.
void FormatDigitPair3(u32 value, char*& out);

// ---------------------------------------------------------------------------
// VIBE_Text_StripNameTokens @0x4f8d98.
//
// Walks `src` copying it into `dst`, stopping after it has consumed two '-'
// separators (the dash-delimited name tokens) or hit end-of-string. A '%' code
// is special-cased: the first '%' at the start of a "format group" (when the
// running marker is still a space) is copied verbatim and consumed; a later '%'
// emits a separating space then copies the '%' AND the following byte (the format
// letter), recording that letter as the new marker. dst is NOT NUL-terminated by
// this routine (the original leaves termination to the caller's later writes).
//
// Outputs:
//   consumed  : number of source bytes consumed before stopping (v7 - src).
//   tailLen   : strlen of the remaining (unconsumed) source tail.
// Mirrors the original's `*a4 = v7 - a1; *a3 = strlen(v7);`.
void StripNameTokens(const char* src, char* dst, i32& consumed, i32& tailLen);

// ---------------------------------------------------------------------------
// VIBE_Text_TrimTrailingSpace @0x59c270.
//
// Despite the IDA name, this SPLITS the string at the boundary of a trailing run
// of characters whose char-class byte has bit 0x20 set (byte_64A208[(u8)(c+1)] &
// 0x20 — the DIGIT class in the recovered table). It walks backward from src[len]
// over that run, then copies the trailing run (src[split..len]) into `dst`,
// NUL-terminated, and returns `split` = the length of the kept prefix (which stays
// in `src`). With no trailing class-run, split == len+1 and dst receives "".
//   src : source buffer base.
//   len : index of the LAST character to consider (i.e. length-1).
//   dst : output buffer (receives the trailing class-run, NUL-terminated).
// Returns `split` (== the original's returned v6 = a2 + 1).
i32 TrimTrailingSpace(const char* src, i32 len, char* dst);

// ---------------------------------------------------------------------------
// VIBE_Text_GetCurrentIconWord @0x5a3418 — returns the current inline-icon word
// (word_649D48), the icon-glyph code the rich-text engine last selected.
// ---------------------------------------------------------------------------
i16 GetCurrentIconWord();

// gilde.exe word_649D48 — current inline-icon word (initial 0xFFFF = none).
extern i16 g_currentIconWord;

// ---------------------------------------------------------------------------
// VIBE_Text_AppendWideLines @0x411ee4.
//
// Clears the caption buffer at widget+216, then appends the byte string `line`
// followed by a '|' separator (unk_610E3C = "|") `count` times. `caption` points
// at the widget's +216 caption buffer (a NUL-terminated char string); `line` is
// the per-line text. The original appends via an unrolled two-bytes-at-a-time
// strcat (byte0; if NUL stop; byte1; if NUL stop) — equivalent to strcat.
//
// Returns the last byte processed (the original's `return result`), which is 0
// after a normal terminator copy — preserved for fidelity.
char AppendWideLines(char* caption, int count, const char* line);

} // namespace guild::gui::text
