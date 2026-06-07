#pragma once
#include "guild/common/types.h"
#include "render/types.h"

// Native pixel colour (un)packing for the software surface. The original kept a
// single global channel-shift table (byte_762719..76271E) set up at display-mode
// init; here the shifts live in a ColorFormat passed explicitly so the code is
// re-entrant and testable. The arithmetic is byte-for-byte the original's.
namespace guild::render {

// gilde.exe 0x4358d8 — VIBE_Render_ComputeChannelShifts
// Derive a ColorFormat from 32-bit R/G/B channel masks (e.g. 0xF800/0x07E0/0x001F
// for RGB565). Counts trailing zero bits (field position) and set bits (precision)
// of each mask; precision drop = 8 - setBits.
ColorFormat ComputeChannelShifts(u32 rMask, u32 gMask, u32 bMask);

// gilde.exe 0x434f30 — VIBE_Result_Handler_Final  (__usercall: r@al, g@dl, b@bl)
// Pack an 8-bit-per-channel RGB triple into a native pixel value.
u32 PackColor(const ColorFormat& f, u8 r, u8 g, u8 b);

// gilde.exe 0x434f7c — VIBE_Render_UnpackColor  (pixel@eax, r@edx, g@ecx, b@ebx)
// Unpack a native pixel value back to an 8-bit RGB triple.
void UnpackColor(const ColorFormat& f, u32 pixel, u8& r, u8& g, u8& b);

} // namespace guild::render
