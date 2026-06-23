#pragma once
#include "guild/common/types.h"

// Shared structs for the guild::render software-surface / image-I/O bottom of
// the renderer. All offsets are byte offsets in the ORIGINAL 32-bit x86 record
// (recovered from raw *(type*)(base+off) access in gilde.exe — the IDB had no
// named UDTs). Pixel buffers live in system memory (8/16/24/32 bpp); none of
// this touches DDraw/GDI (that is the present layer, another module).
namespace guild::render {

// ---------------------------------------------------------------------------
// Native pixel colour format — the per-channel shift table the original kept in
// the global present-state block (gilde.exe byte_762719..76271E). Recovered
// from VIBE_Result_Handler_Final (pack) @0x434f30 and VIBE_Render_UnpackColor
// @0x434f7c. Derived from RGB masks by VIBE_Render_ComputeChannelShifts @0x4358d8.
//
//   pack(r,g,b) = (g >> gPrec << gPos) | (r >> rPrec << rPos) | (b >> bPrec << bPos)
//
//   gPos  = byte_76271A   gPrec = byte_76271B
//   rPos  = byte_76271E   rPrec = byte_76271C
//   bPos  = byte_76271D   bPrec = byte_762719
// ---------------------------------------------------------------------------
struct ColorFormat {
    u8 gPos;   // byte_76271A  green field position (left-shift)
    u8 gPrec;  // byte_76271B  green precision drop (right-shift of 8-bit channel)
    u8 rPrec;  // byte_76271C  red precision drop
    u8 bPos;   // byte_76271D  blue field position
    u8 rPos;   // byte_76271E  red field position
    u8 bPrec;  // byte_762719  blue precision drop
};

// RGB565: R=5@11, G=6@5, B=5@0.
inline ColorFormat Format565() { return ColorFormat{5, 2, 3, 0, 11, 3}; }
// RGB555: R=5@10, G=5@5, B=5@0.
inline ColorFormat Format555() { return ColorFormat{5, 3, 3, 0, 10, 3}; }
// XRGB8888: R=8@16, G=8@8, B=8@0, no precision drop — the byte order the 32 bpp
// software surfaces actually store (0xAARRGGBB), for packing text/primitives onto them.
inline ColorFormat Format8888() { return ColorFormat{8, 0, 0, 0, 16, 0}; }

// ---------------------------------------------------------------------------
// Surface record — gilde.exe gfx.c "gfx_CreateSurface" (0x40 = 64 bytes).
// VIBE_Surface_Create memcpy's a 64-byte template, then fills the fields below.
// The DDraw-backed path stored a vendor surface ptr at +0x20; in this software
// reconstruction we only model the system-memory framebuffer (pixels at +0x1C).
// ---------------------------------------------------------------------------
struct Surface {
    u32  caps;        // +0x00  surface caps/flags from the creation template
    i32  width;       // +0x04  width in pixels
    i32  height;      // +0x08  height in pixels
    i32  pitch;       // +0x0C  bytes per row  ((bpp>>3) * width)
    i32  widthPx;     // +0x10  row stride in pixels (pitch / (bpp>>3))
    u8   bpp;         // +0x14  bits per pixel (8/15/16/24/32)
    u8   _pad15[3];   // +0x15  (alignment)
    i32  _r18;        // +0x18  (reserved template dword, index 6)
    u8*  pixels;      // +0x1C  pixel buffer (system memory)
    void* ddSurface;  // +0x20  DDraw surface ptr in original; null here (sw path)
    i32  clipX0;      // +0x24  clip rect left   (index 9;  Create sets 0)
    i32  clipY0;      // +0x28  clip rect top    (index 10; Create sets 0)
    i32  clipX1;      // +0x2C  clip rect right  (index 11; Create sets template[1])
    i32  clipY1;      // +0x30  clip rect bottom (index 12; Create sets template[2])
    i32  decompState; // +0x34  decompression handle (index 13); 0 = none
    i32  shared;      // +0x38  reference/shared flag (index 14)
    i32  _r3c;        // +0x3C  (reserved, index 15)
    // VIBE_Surface_SetPixelRgb bounds-checks x in [clipX0,clipX1) y in [clipY0,clipY1)
    // and addresses pixels at base + (widthPx*y + x) * (bpp>>3).

    // Reconstruction-only: the original read channel shifts from a single global
    // present-state block (byte_762719..). We attach the format per surface so the
    // pixel I/O is re-entrant/testable. Not part of the original 64-byte record.
    ColorFormat fmt;
};

#pragma pack(push, 1)
// 14-byte BITMAPFILEHEADER as written by VIBE_Bmp_Save* (gilde.exe).
struct BmpFileHeader {
    u16 type;        // +0x00  "BM" = 0x4D42 (19778)
    u32 fileSize;    // +0x02
    u16 reserved1;   // +0x06
    u16 reserved2;   // +0x08
    u32 dataOffset;  // +0x0A  (1078 for 8-bit, 54 for 24-bit)
};
// 40-byte BITMAPINFOHEADER.
struct BmpInfoHeader {
    u32 size;            // +0x00  40
    i32 width;           // +0x04
    i32 height;          // +0x08  (positive => bottom-up rows)
    u16 planes;          // +0x0C  1
    u16 bitCount;        // +0x0E  8 or 24
    u32 compression;     // +0x10  0 = BI_RGB, 1 = BI_RLE8
    u32 imageSize;       // +0x14
    i32 xPelsPerMeter;   // +0x18
    i32 yPelsPerMeter;   // +0x1C
    u32 clrUsed;         // +0x20
    u32 clrImportant;    // +0x24
};
#pragma pack(pop)

} // namespace guild::render
