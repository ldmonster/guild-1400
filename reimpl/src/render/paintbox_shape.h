#pragma once
#include "guild/common/types.h"
#include "render/types.h"
#include "render/colorformat.h"

// =============================================================================
// guild::render — Paintbox / Surface shape-blit leaves (gilde.exe gfx.c).
//
// The 2D "shape blit" that paints an indexed or RGB bitmap into the engine's
// system-memory framebuffer / surface. Faithful 1:1 reconstruction of:
//
//   0x422EE4  VIBE_Surface_BlitRgbToPixels       (RGB triples -> surface, packed)
//   0x422F80  VIBE_Surface_BlitPaletteToPixels   (8bpp indices -> framebuffer)
//   0x423050  VIBE_Surface_CopyRegionRgb         (framebuffer 565 -> RGB triples)
//
// VIBE_Paintbox_DrawShape / DrawShapeDirect / DrawShapeClipped (0x41EF40/EFC4/
// F00C) are thin dispatchers into the sprite/animation decompression module
// (VIBE_Animation_Basic @0x5D85B8 + VIBE_DecompressState_Blob); their pixel work
// lives there, NOT in this module — they are DEFERRED (see module report). The
// real per-pixel shape blit is BlitPaletteToPixels, reconstructed here.
//
// As elsewhere the original read the framebuffer width from the global
// dword_7626F8; we pass it explicitly. Colour packing uses the active
// ColorFormat (RGB565 by default) instead of the global channel-shift table.
// =============================================================================
namespace guild::render {

// gilde.exe 0x422F80 — VIBE_Surface_BlitPaletteToPixels (ecx=height, ebx=width,
// arg0=srcIndices, argC=dstFb, arg10=palette).
// Build a 256-entry index->native LUT from `palette` (256 * 4-byte R,G,B,X
// entries, packed via the ColorFormat), then blit a `width`x`height` block of
// 8-bit indices from `src` (row stride = width) into `dstFb` (row stride
// `fbWidth`) at the top-left, writing the LUT colour per pixel.
void Surface_BlitPaletteToPixels(u16* dstFb, int fbWidth,
                                 const u8* src, int width, int height,
                                 const u8* palette, const ColorFormat& fmt);

// gilde.exe 0x422EE4 — VIBE_Surface_BlitRgbToPixels (ecx=height, ebx=width,
// arg0=srcRgb, argC unused, a6=surface).
// Blit a `width`x`height` block of RGB triples (`src`, 3 bytes/pixel, row stride
// = 3*width) into the surface's pixel buffer, packing each triple via the
// surface's ColorFormat. Destination addressed at surface.widthPx (the +16 field)
// stride; writes 16-bit packed pixels.
void Surface_BlitRgbToPixels(Surface* surf, const u8* src, int width, int height);

// gilde.exe 0x423050 — VIBE_Surface_CopyRegionRgb (ecx=height, ebx=width,
// arg0=srcFb, a6=dstRgb).
// Copy a `width`x`height` block from the 16-bit framebuffer `srcFb` (row stride
// `fbWidth`) out to an RGB-triple buffer `dstRgb` (3 bytes/pixel, row stride
// 3*width), unpacking each pixel via the ColorFormat.
void Surface_CopyRegionRgb(const u16* srcFb, int fbWidth, u8* dstRgb,
                           int width, int height, const ColorFormat& fmt);

} // namespace guild::render
