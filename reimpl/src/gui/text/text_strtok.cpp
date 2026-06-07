#include "gui/text/text_strtok.h"

namespace guild::gui::text {

namespace {
// byte_62CEC0 @0x62cec0 — the single-bit-per-index mask table.
constexpr u8 kBitMask[8] = {1, 2, 4, 8, 16, 32, 64, 128};
} // namespace

// gilde.exe 0x5fe5a0 — VIBE_Util_SetBitmapBits  (__usercall, al = (out@eax, delims@ecx))
//
//   VIBE_Light_SetGrayColorThunk(0, 32, out);   // memset(out, 0, 32)
//   for (result = *p; *p; result = *p) {
//       ++p;
//       out[result >> 3] |= byte_62CEC0[result & 7];
//   }
//   return result;
//
// Note the original reads `result` (the current char) BEFORE the increment but
// indexes AFTER, so the bit set corresponds to the just-read character; the
// loop stops once a NUL is seen (and the NUL's own bit, index 0, is never set
// because the loop body runs only while *p != 0).
u8 SetBitmapBits(u8 out[32], const char* delims) {
    for (int i = 0; i < 32; ++i)
        out[i] = 0;

    const unsigned char* p = reinterpret_cast<const unsigned char*>(delims);
    unsigned char result = *p;
    while (*p) {
        ++p;
        out[result >> 3] |= kBitMask[result & 7];
        result = *p;
    }
    return result;
}

// gilde.exe 0x5e9cd0 — VIBE_Text_StrtokWhitespace  (__usercall, eax = (str@eax, delims@ecx))
//
// Faithful re-entrant strtok. Pseudocode flow:
//   v2 = str;
//   if (!str) { v2 = ctx.saved; if (!v2) return 0; }
//   SetBitmapBits(bitmap, delims);
//   // skip leading delimiters
//   while (*v2 && (byte_62CEC0[*v2 & 7] & bitmap[*v2 >> 3]))  ++v2;
//   if (!*v2) return 0;            // nothing but delimiters / empty
//   for (i = v2; *i; ++i) {
//       if (byte_62CEC0[*i & 7] & bitmap[*i >> 3]) {   // hit a delimiter
//           *i = 0;
//           ctx.saved = i + 1;     // resume after the NUL
//           return v2;
//       }
//   }
//   ctx.saved = 0;                 // ran to end of string
//   return v2;
char* StrtokWhitespace(StrtokContext& ctx, char* str, const char* delims) {
    char* v2 = str;
    if (!str) {
        v2 = ctx.saved;
        if (!v2)
            return nullptr;
    }

    u8 bitmap[32];
    SetBitmapBits(bitmap, delims);

    auto in_set = [&bitmap](unsigned char c) -> bool {
        return (kBitMask[c & 7] & bitmap[c >> 3]) != 0;
    };

    // Skip leading delimiters.
    while (*v2 && in_set(static_cast<unsigned char>(*v2)))
        ++v2;

    if (!*v2)
        return nullptr;

    for (char* i = v2; *i; ++i) {
        if (in_set(static_cast<unsigned char>(*i))) {
            *i = 0;
            ctx.saved = i + 1;
            return v2;
        }
    }

    // Reached the end without another delimiter: this is the last token.
    ctx.saved = nullptr;
    return v2;
}

} // namespace guild::gui::text
