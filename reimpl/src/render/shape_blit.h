#pragma once
#include "guild/common/types.h"
#include "render/colorformat.h"
#include <cstddef>

// =============================================================================
// guild::render — 16-bit colored sprite blit + 8->16 indexed surface convert.
//
// Two self-contained leaves from gilde.exe (ts_texture.c / gfx.c) that were
// deferred out of the earlier shape/surface translation:
//
//   0x5D7164  VIBE_Shape_BlitColored16   — blit a row-RLE 16bpp shape, recolouring
//                                          every opaque pixel to a desaturated grey
//   0x43768C  VIBE_Render_Convert8To16Indexed — convert an 8bpp-indexed surface
//                                          block to native 16bpp through a palette
//
// THE SHAPE RLE LAYOUT (identical to render/shape.h ShapeDecodeRle)
// -----------------------------------------------------------------------------
//   +0x0A  u16  height
//   +0x32  u32  row[0] runCount, then runCount runs of { u32 skip; u32 nPixels;
//               u16 px[nPixels] }; the next row's runCount follows immediately.
//   skip>>1 = transparent pixels to advance before the run.
// The destination is the engine surface (+0x10 widthPx stride, +0x1C u16* pixels).
// =============================================================================
namespace guild::render {

// Minimal 16bpp blit destination mirroring the engine surface offsets used by the
// RLE blit (+0x10 widthPx, +0x1C pixels). Same shape as render/shape.h BlitTarget16
// but kept here to avoid a cross-header dependency; identical semantics.
struct ColorBlitTarget16 {
    int  widthPx;   // +0x10  row stride in pixels
    u16* pixels;    // +0x1C  pixel buffer base
};

// gilde.exe 0x5D7164 — VIBE_Shape_BlitColored16 (__usercall ax=fn(x@eax, y@edx,
//   shape@ecx, surface@ebx)). Blits the row-RLE shape at (x, y), but instead of
//   copying the source pixel it UNPACKS each opaque pixel to RGB, computes a grey
//   luma  L = r*0.2 + b*0.59 + g*0.2  (the original's exact, asymmetric weighting:
//   dbl_629478=0.2 applied to BOTH r and g, dbl_629480=0.59 to b), rounds via the
//   FPU truncate-after-+0.5 helper, then re-packs (L,L,L) and writes that grey.
//   No clipping (callers pre-clip). Returns the shape height (the original's `ax`).
//
//   The (un)pack uses the destination format; pass the surface's ColorFormat.
u16 ShapeBlitColored16(int x, int y, const u8* shape, const ColorBlitTarget16& dst,
                       const ColorFormat& fmt);

// Source/destination surface descriptors for the indexed convert. The original
// reads these as raw dwords off two surface records; we name the fields used:
//   src: pitch(+0x10 idx4), widthPx(+0x24 idx9 base? ) ... see notes in the .cpp.
struct ConvertSurf8 {
    int   widthPx;   // a2[4]  source row stride in PIXELS (1 byte each)
    int   width;     // a2[3]  pixels per row to convert
    int   height;    // a2[2]  rows to convert
    const u8* pixels;// a2[9]  8bpp index buffer base
};
struct ConvertSurf16 {
    int   widthPx;   // a1[4]  dest row stride in PIXELS
    int   base;      // a1[9]  dest pixel offset (added to widthPx*row); in pixels
    u32   rMask;     // a1[22] dest red   channel mask
    u32   gMask;     // a1[23] dest green channel mask
    u32   bMask;     // a1[24] dest blue  channel mask
    u16*  pixels;    // dest pixel buffer base (the +base/+widthPx are relative)
};

// gilde.exe 0x43768C — VIBE_Render_Convert8To16Indexed (eax=destSurf, edx=srcSurf,
//   ebx=palette). Derives per-channel (position, precision) from the dest masks
//   (count trailing zeros = field position; count set bits = precision), then for
//   each source index `i` reads palette[4*i+0..2] = R,G,B and writes
//     (G>>(8-gBits)<<gPos) | (R>>(8-rBits)<<rPos) | (B>>(8-bBits)<<bPos)
//   into the destination 16bpp block. `palette` is 256 * 4-byte R,G,B,X entries.
//   Returns the last packed pixel value (the original's `eax`).
u32 Convert8To16Indexed(const ConvertSurf16& dst, const ConvertSurf8& src,
                        const u8* palette);

} // namespace guild::render
