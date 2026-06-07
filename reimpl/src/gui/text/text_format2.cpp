#include "gui/text/text_format2.h"

#include <cstring>

// The trailing-class trim consults the recovered 256-byte character-class table
// byte_64A208. It is real data already reconstructed once in guild::sim; reuse it
// (extern) rather than redefining (ODR).
namespace guild::sim {
extern const guild::u8 kCharClass[256];
}

namespace guild::gui::text {

// gilde.exe word_649D48 — current inline-icon word. BSS init 0xFFFF (= none).
i16 g_currentIconWord = static_cast<i16>(0xFFFF);

// ===========================================================================
// gilde.exe 0x5faafe — VIBE_Text_FormatDigitPair  (__usercall, al = (a1@ax, out@ebx))
// Emit two fixed-width decimal digits for a 0..99 value. The original is an 8-bit
// path: only when the low byte is >= 10 does it DIV by 10; otherwise the high byte
// (normally 0) becomes the tens digit. The destination cursor (EBX) is advanced
// by two; we mirror that by taking the cursor by reference.
// ===========================================================================
void FormatDigitPair(u16 value, char*& out) {
    u8 al = static_cast<u8>(value);        // low byte
    u8 ah = static_cast<u8>(value >> 8);   // high byte
    u8 tens, units;
    if (al < 10u) {
        // jb taken: no DIV. al(low) stays as low byte -> units; high byte -> tens.
        tens  = ah;
        units = al;
    } else {
        tens  = static_cast<u8>(al / 10u);
        units = static_cast<u8>(al % 10u);
    }
    out[0] = static_cast<char>(tens + '0');
    out[1] = static_cast<char>(units + '0');
    out += 2;
}

// ===========================================================================
// gilde.exe 0x5faae7 — VIBE_Text_FormatDigitPair2  (__usercall, al = (a1@eax, out@ebx))
// Emit four fixed-width decimal digits for a 0..9999 value: split by 100 with a
// 16-bit DIV (only when >= 100), then the hundreds pair followed by the units pair.
// ===========================================================================
void FormatDigitPair2(u32 value, char*& out) {
    u16 hi, lo;
    if (value < 100u) {
        hi = 0;
        lo = static_cast<u16>(value);
    } else {
        // 16-bit DIV cx: operands are the low 16 bits.
        u16 v = static_cast<u16>(value);
        hi = static_cast<u16>(v / 100u);
        lo = static_cast<u16>(v % 100u);
    }
    FormatDigitPair(hi, out);
    FormatDigitPair(lo, out);
}

// ===========================================================================
// gilde.exe 0x5faad1 — VIBE_Text_FormatDigitPair3  (__usercall, al = (a1@eax, out@ebx))
// Emit eight fixed-width decimal digits for a 0..99999999 value: split by 10000
// with a 32-bit DIV (only when >= 10000), then the high four followed by the low.
// ===========================================================================
void FormatDigitPair3(u32 value, char*& out) {
    u32 hi, lo;
    if (value < 0x2710u) {  // 10000
        hi = 0;
        lo = value;
    } else {
        hi = value / 0x2710u;
        lo = value % 0x2710u;
    }
    FormatDigitPair2(hi, out);
    FormatDigitPair2(lo, out);
}

// ===========================================================================
// gilde.exe 0x4f8d98 — VIBE_Text_StripNameTokens
//   (__usercall, eax = (src@eax, dst@edx, outTailLen@ecx, outConsumed@ebx))
// Copy `src` into `dst`, stopping after two '-' separators are consumed. '%' codes
// are folded: a '%' while the running marker is still a space is copied verbatim
// and consumed; otherwise a separating space is emitted and the '%' copied,
// recording it as the new marker. Reports source bytes consumed + remaining tail
// length. dst is left un-terminated (caller terminates), matching the original.
// ===========================================================================
void StripNameTokens(const char* src, char* dst, i32& consumed, i32& tailLen) {
    char marker = 32;          // v6: running class marker (space initially)
    const char* p = src;       // v7
    int dashes = 0;            // v8
    while (*p) {
        if (dashes >= 2)
            break;
        if (*p == '-') {       // 45
            ++dashes;
            ++p;
        } else {
            char ch = *p;      // v10
            char* dstNext = dst + 1;  // v11
            if (*p == '%') {   // 37
                if (marker == 32) {
                    ++p;
                    *dst++ = '%';
                } else {
                    *dst = 32;          // separating space
                    dst += 2;
                    marker = *p++;      // consume the '%' byte; record as marker
                    *dstNext = marker;  // place '%' at dst[1]
                }
            } else {
                marker = *p++;
                *dst++ = ch;
            }
        }
    }
    consumed = static_cast<i32>(p - src);
    tailLen  = static_cast<i32>(std::strlen(p));
}

// ===========================================================================
// gilde.exe 0x59c270 — VIBE_Text_TrimTrailingSpace
//   (__usercall, eax = (src@eax, len@edx, dst@ebx))
// Walk backward from src[len] over a trailing run of class-0x20 bytes, then copy
// the trailing run (src[split..len]) into dst NUL-terminated and return `split`,
// the length of the kept prefix. The class test is byte_64A208[(u8)(c+1)] & 0x20.
// ===========================================================================
i32 TrimTrailingSpace(const char* src, i32 len, char* dst) {
    i32 idx = len;                 // a2/edx
    if (idx >= 0) {
        const char* q = src + idx; // v5/eax
        do {
            u8 cls = guild::sim::kCharClass[static_cast<u8>(*q + 1)];
            if ((cls & 0x20) == 0)
                break;
            --idx;
            --q;
        } while (idx >= 0);
    }
    i32 split = idx + 1;                 // v6 = a2 + 1 (kept-prefix length)
    i32 tailCount = len - split + 1;     // bytes copied (trailing run + 1)
    std::memcpy(dst, src + split, static_cast<std::size_t>(tailCount));
    dst[tailCount] = 0;
    return split;
}

// ===========================================================================
// gilde.exe 0x5a3418 — VIBE_Text_GetCurrentIconWord
// Returns word_649D48, the inline-icon glyph word the engine last selected.
// ===========================================================================
i16 GetCurrentIconWord() {
    return g_currentIconWord;
}

// ===========================================================================
// gilde.exe 0x411ee4 — VIBE_Text_AppendWideLines  (__cdecl)
// Clears the caption buffer (widget+216) then appends the byte string `line`
// followed by a '|' separator (unk_610E3C = "|") `count` times. The original
// appends with an unrolled two-bytes-at-a-time strcat (it copies byte0; if NUL,
// stop; else copies byte1; if NUL, stop), which is byte-faithful to strcat of a
// NUL-terminated string. Returns the last byte processed (0 after the final
// terminator copy), preserved for fidelity with the original `return result`.
//
// `caption` points at the widget's +216 caption buffer; `line` is the per-line
// text. These are 8-bit char strings in the original (the unrolled copy is not
// UTF-16 — byte1==0 terminates).
// ===========================================================================
char AppendWideLines(char* caption, int count, const char* line) {
    static const char kSeparator[] = "|";  // unk_610E3C -> "|"

    caption[0] = 0;    // *(BYTE*)(rec+216) = 0 clears the caption string
    char result = 0;
    int produced = 0;  // v4
    if (count > 0) {
        do {
            // strcat(caption, line) via the original's 2-byte unrolled copy.
            char* end = caption + std::strlen(caption);
            const char* s = line;
            for (;;) {
                char b0 = *s;
                *end = b0;
                if (b0 == 0) break;
                char b1 = s[1];
                s += 2;
                end[1] = b1;
                end += 2;
                if (b1 == 0) break;
            }
            ++produced;
            // strcat(caption, "|").
            char* end2 = caption + std::strlen(caption);
            const char* sep = kSeparator;
            for (;;) {
                char b0 = *sep;
                *end2 = b0;
                result = b0;
                if (b0 == 0) break;
                char b1 = sep[1];
                sep += 2;
                end2[1] = b1;
                end2 += 2;
                result = b1;
                if (b1 == 0) break;
            }
        } while (produced < count);
    }
    return result;
}

} // namespace guild::gui::text
