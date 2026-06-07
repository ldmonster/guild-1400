#pragma once
#include "guild/common/types.h"

// 24-bit RGB colour triple helpers recovered from gilde.exe. The colour record
// is three consecutive bytes. NOTE the original SetRgb stores its arguments in a
// non-obvious slot order (see below); the comparison routine compares all three
// bytes positionally, so any encoding is consistent as long as Set/Compare agree.
//
//   guild::util::ColorSetRgb      — gilde.exe 0x4226e0 — VIBE_Color_SetRgb
//   guild::util::ColorNotEqualRgb — gilde.exe 0x4226bc — VIBE_Color_NotEqualRgb
namespace guild::util {

// 0x4226e0 — VIBE_Color_SetRgb  (__usercall: dst@eax, a@dl, b@cl, c@bl).
// Stores positionally: dst[0]=a, dst[1]=c, dst[2]=b.  (Yes: slot 1 gets the
// THIRD argument and slot 2 gets the SECOND — faithful to the original register
// mapping where ebx(c) is written before ecx(b).)
void ColorSetRgb(u8* dst, u8 a, u8 b, u8 c);

// 0x4226bc — VIBE_Color_NotEqualRgb  (__usercall: a@eax, b@edx).
// Returns nonzero (BOOL) iff the two 3-byte colours differ in any byte.
int ColorNotEqualRgb(const u8* a, const u8* b);

} // namespace guild::util
