#pragma once
#include "guild/common/types.h"

// 128-bit GUID record + canonical text format/parse, recovered from gilde.exe.
// Binary layout matches the Win32 GUID (little-endian fields), 16 bytes total.
// Canonical text form (lowercase hex):
//   "%08x-%04x-%04x-%02x%02x%02x%02x%02x%02x%02x%02x"
// e.g. "12345678-9abc-def0-0102030405060708".
//
//   guild::util::GuidFormat — gilde.exe 0x432a0c — VIBE_Guid_Format
//   guild::util::GuidParse  — gilde.exe 0x432a70 — VIBE_Guid_Parse
namespace guild::util {

GUILD_PACKED_BEGIN
struct GUILD_PACKED Guid {
    u32 data1;    // +0x00
    u16 data2;    // +0x04
    u16 data3;    // +0x06
    u8  data4[8]; // +0x08 .. +0x0F
};
GUILD_PACKED_END

// Minimum buffer size for GuidFormat output including the NUL terminator:
// 8 + 1 + 4 + 1 + 4 + 1 + 16 + 1(NUL) = 37.
inline constexpr int kGuidTextSize = 37;

// 0x432a0c — VIBE_Guid_Format  (__usercall: src@eax, out@edx).
// Writes the canonical text of `src` into `out`. No-op (returns 0) if either
// pointer is null; otherwise returns nonzero. `out` needs >= kGuidTextSize bytes.
int GuidFormat(const Guid* src, char* out);

// 0x432a70 — VIBE_Guid_Parse  (__usercall: str@eax, out@edx).
// Parses canonical text `str` into `out`. Returns 0 (false) if `str` is null,
// empty, or `out` is null; otherwise fills `out` and returns 1 (true).
// (Faithful to the original, which uses a single sscanf and does NOT validate
// the field count, so a short/garbled string leaves some fields unset.)
bool GuidParse(const char* str, Guid* out);

} // namespace guild::util
