#pragma once
#include "guild/common/types.h"
#include <cstddef>

// =============================================================================
// guild::render — 2D sprite ("shape") RLE pixel blit.
//
// Faithful 1:1 reconstruction of the row-RLE sprite codec in gilde.exe
// (ts_texture.c):
//   0x5D70CC  VIBE_Shape_DecodeRle      (decode + blit a 16bpp RLE shape)
//   0x5D7164  VIBE_Shape_BlitColored16  (same, with a per-pixel luma recolour)
//
// THE SHAPE RLE LAYOUT (recovered byte-for-byte)
// -----------------------------------------------------------------------------
// A shape's pixel payload is row-RLE. Within the shape blob:
//   +0x0A  u16  height (rows)
//   +0x32  the first row's RLE stream begins here.
// The stream is a sequence of rows; each row is:
//   u32  runCount                      (number of runs in this row)
//   runCount * {                       (one run)
//     u32 skip      ; leading transparent pixels = (skip >> 1)
//     u32 nPixels   ; opaque pixel count
//     u16 px[nPixels]                  (16-bit native pixels)
//   }
// After a row's runs, the NEXT row's runCount dword follows immediately. The
// decoder advances a destination pointer by `skip>>1` pixels before each run,
// writes `nPixels` pixels, and at the end of a row jumps the destination to the
// start of the next scanline (dst += surface widthPx).
//
// The blit target is a software surface accessed by raw offsets in the original:
//   *(surface + 0x10) = widthPx (row stride in pixels)
//   *(surface + 0x1C) = pixel buffer base (u16* for 16bpp)
// matching render/types.h Surface (+0x10 widthPx, +0x1C pixels).
// =============================================================================
namespace guild::render {

// Shape RLE field offsets within a shape blob.
namespace shape_rle_off {
constexpr size_t kHeight    = 0x0A;  // u16 rows
constexpr size_t kRleStream = 0x32;  // first row's runCount dword
}

// Minimal 16bpp blit destination mirroring the engine's surface offsets used by
// the RLE blit (+0x10 widthPx, +0x1C pixels). Kept tiny so the codec is testable
// without the full Surface record.
struct BlitTarget16 {
    int  widthPx;   // +0x10  row stride in pixels
    u16* pixels;    // +0x1C  pixel buffer base
};

// gilde.exe 0x5D70CC — VIBE_Shape_DecodeRle (__usercall eax=fn(x@eax, y@edx,
//   shape@ecx, surface@ebx)). Blits the row-RLE shape `shape` at (x, y) into the
//   16bpp `dst`. Honours the per-run skip (transparent gap = skip>>1 pixels) and
//   walks scanlines via dst.widthPx. Returns the past-the-end destination pixel
//   pointer (as the original returns `result`).
//
// NOTE: the original performs NO clipping — callers pre-clip. This reconstruction
// is byte-faithful; pass a destination large enough for x + shapeWidth and
// y + shapeHeight, mirroring the engine's contract.
u16* ShapeDecodeRle(int x, int y, const u8* shape, const BlitTarget16& dst);

} // namespace guild::render
