#pragma once
#include "guild/common/types.h"
#include <vector>

// =============================================================================
// guild::render — texture mip / blend-LUT / channel-LUT helpers (system memory).
// Faithful 1:1 reconstruction of the self-contained, vendor-free pieces of the
// gilde.exe ts_texture.c / gfx.c mip + colour-format machinery:
//
//   0x5b9230  VIBE_TextureCache_BuildBlendLut    (a*a*4 bilinear blend-weight LUT)
//   0x4358d8  VIBE_Render_ComputeChannelShifts   (R/G/B mask -> shift pairs)
//   0x435a3c  VIBE_Render_BuildChannelLUT        (256-entry per-channel remap LUT)
//   0x5db234  VIBE_Texture_UploadToSurface (tail) (mip-width derivation)
//   0x5b903c  VIBE_TextureCache_BuildMipmap (block-size pick)
//
// DEFERRED (see report): the 16-bit colour bilinear blit core of
// VIBE_TextureCache_ScaleBlitMip / BuildMipmap pixel loop and the DDraw-surface
// upload half of UploadToSurface — they fetch through the runtime 16-bit colour
// channel LUTs (dword_1404260/660/A60) built from the active DD surface format
// and dispatch through off_64A03C, which is vendor/surface-format coupled.
// =============================================================================
namespace guild::render {

// ---------------------------------------------------------------------------
// gilde.exe 0x4358d8 — VIBE_Render_ComputeChannelShifts. Given R/G/B channel
// masks, derive for each channel a (downShift, upShift) pair such that a packed
// native pixel's channel is `(value >> downShift) << upShift`-decodable into the
// 0..255 range and back. `downShift` = 8 - bitWidth(mask); `upShift` = number of
// trailing zero bits below the mask. The original returns them via out-params in
// the order (a3=downR? ...) — here grouped into Shifts.
// ---------------------------------------------------------------------------
struct ChannelShifts {
    u8 downR, upR;   // a3 = 8 - widthR ; a5 = trailing zeros of R mask
    u8 downG, upG;   // a6 = 8 - widthG ; a7 = trailing zeros of G mask
    u8 downB, upB;   // a8 = 8 - widthB ; a9 = trailing zeros of B mask
};
ChannelShifts MipChannelShifts(u32 maskR, u32 maskG, u32 maskB);

// ---------------------------------------------------------------------------
// gilde.exe 0x435a3c — VIBE_Render_BuildChannelLUT. Builds three contiguous
// 256-entry tables (R, G, B) in `out` (768 dwords): out[i] = (i>>down)<<up for
// each channel, mapping an 8-bit intensity straight into native bit position.
//   out[0..255]   = R remap
//   out[256..511] = G remap
//   out[512..767] = B remap
// ---------------------------------------------------------------------------
void BuildChannelLUT(u32 out[768], u32 maskR, u32 maskG, u32 maskB);

// ---------------------------------------------------------------------------
// gilde.exe 0x5b9230 — VIBE_TextureCache_BuildBlendLut. Builds an `n`*`n`*4 byte
// table of bilinear blend weights. For row r (0..n-1) and column c (0..n-1) the
// 4 bytes are the high bytes of a 16.16 lerp pair:
//   step = 256 / n
//   base2 = (step*r) << 8          ; weight of the "far" sample, 16.16
//   d2    = step * ((0xFFFF - base2) >> 8)   ; column decrement of (0xFFFF-base2)
//   col runs c=0..n-1, tracking v2 (=base2), v3 (=0xFFFF-base2), v5, v6:
//     out[+0] = BYTE1(v3)   ; (1-u)(1-v)-ish weight
//     out[+1] = BYTE1(v5)   ; v5 accumulates +d2 per col
//     out[+2] = BYTE1(v2)   ; u-weight
//     out[+3] = BYTE1(v6)   ; v6 accumulates +(step*r)*step per col
//   advancing v3 -= d2 ; v2 -= (step*r)*step each column.
// Writes n*n*4 bytes; returns 4*n (the per-row stride). `n` must divide 256.
// ---------------------------------------------------------------------------
u32 BuildBlendLut(u8* out, u32 n);

// ---------------------------------------------------------------------------
// gilde.exe 0x5b903c (head) — block size the mip builder picks from the source
// aspect (width/height): clamp width/height to [16,64].
// ---------------------------------------------------------------------------
int MipBlockSize(int width, int height);

// gilde.exe 0x5db234 (tail) — mip width derivation: mipWidth = baseWidth >> shift
// (byte_64A350). Bare unsigned shift (`shr eax,cl`); NO saturation in the binary.
int MipWidth(int baseWidth, int shift);

// Number of mip levels for a power-of-two square of side `width`:
//   levels = log2(width) + 1   (width=256 -> 9 levels: 256,128,...,1).
int MipLevelCount(int width);

// ---------------------------------------------------------------------------
// System-memory mip chain for an 8-bit palette-index texture. The engine's
// software path downsamples the index buffer by 2x2 box selection: each
// destination texel takes the source texel at (2u, 2v) (the top-left of the 2x2
// block), matching a nearest-of-block reduction that preserves palette indices
// (averaging indices would corrupt the palette). `BuildIndexMipChain` returns a
// vector of levels: levels[0] = full (width*width), levels[k] = width>>k square,
// down to 1x1. Each level is a copy reduced from the previous.
// ---------------------------------------------------------------------------
void DownsampleIndex2x(const u8* src, int srcWidth, u8* dst);
std::vector<std::vector<u8>> BuildIndexMipChain(const u8* base, int width);

} // namespace guild::render
