#pragma once
// guild::gui::text — the engine's re-entrant string tokenizer.
//
// Recovered original:
//   VIBE_Text_StrtokWhitespace @0x5e9cd0 — a faithful `strtok(str, delims)`:
//     it builds a 256-bit delimiter-membership bitmap from `delims`
//     (VIBE_Util_SetBitmapBits @0x5fe5a0, using the byte_62CEC0 bit-mask
//     table {1,2,4,8,...,128}), skips leading delimiters, finds the next
//     delimiter, NUL-terminates the token there, and stashes the resume
//     pointer in a per-context slot ([off_64A90C()+16]). A null `str`
//     resumes from that saved pointer, exactly like the C runtime strtok.
//
// The label-definition parser (VIBE_Text_ParseLabelDefinition @0x44b2e0)
// drives this with `StrtokWhitespace(line, "\n")` then `StrtokWhitespace(0, ...)`
// to walk a multi-line `#define` block, so the tokenizer is the deterministic
// front-end of the whole text-DB authoring path. It is bit-exact and tested
// against std libc's strtok as the oracle.

#include "guild/common/types.h"

namespace guild::gui::text {

using guild::u8;

// VIBE_Util_SetBitmapBits @0x5fe5a0 — set, in the 32-byte (256-bit) bitmap
// `out`, one bit per character present in the NUL-terminated `delims` string:
//   out[c >> 3] |= byte_62CEC0[c & 7];     // byte_62CEC0 = {1,2,4,...,128}
// `out` must be 32 bytes and is zero-filled first (the original memset-via-
// VIBE_Light_SetGrayColorThunk(0, 32, out)). Returns the last byte scanned
// (0 at the terminator), matching the original's `al` return.
u8 SetBitmapBits(u8 out[32], const char* delims);

// Re-entrant tokenizer context. The original keeps the saved resume pointer in
// a per-thread CRT slot ([off_64A90C()+16]); we model it explicitly so the
// tokenizer stays testable and free of hidden global state.
struct StrtokContext {
    char* saved = nullptr;   // [off_64A90C()+16]
};

// gilde.exe 0x5e9cd0 — VIBE_Text_StrtokWhitespace  (__usercall, eax = strtok)
//   eax = str (a1), ecx = delims (a2).
// Faithful strtok: on the first call pass the string in `str`; on subsequent
// calls pass nullptr to continue from where the previous call stopped. `delims`
// is the active delimiter set. Returns the next token (NUL-terminated in place)
// or nullptr when exhausted. The resume pointer lives in `ctx`.
char* StrtokWhitespace(StrtokContext& ctx, char* str, const char* delims);

} // namespace guild::gui::text
