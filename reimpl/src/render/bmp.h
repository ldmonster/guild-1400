#pragma once
#include "guild/common/types.h"
#include <vector>

// Windows BMP load/save from gilde.exe (gfx.c "bmp:*"). The originals went
// through the VFS file shim (VIBE_Vfs_OpenFile/Read/Write/Seek/Close); here the
// same byte stream is modelled in memory so the codec is testable without I/O.
//
// Supported on disk: 8-bit paletted (BI_RGB and BI_RLE8) and 24-bit, both
// stored bottom-up (positive height). Magic = "BM", 14-byte file header +
// 40-byte info header; 8-bit has a 256-entry (1024-byte) BGRA palette at 0x36.
namespace guild::render {

// Result of VIBE_Bmp_ReadHeaderInfo (0x5f0c10): width, height(abs), bpp(8/24).
struct BmpInfo {
    int width = 0;
    int height = 0;
    int bitCount = 0;
    bool ok = false;
};

// gilde.exe 0x5f0c10 — VIBE_Bmp_ReadHeaderInfo. Parse just the dimensions/bpp
// from a BMP byte buffer. Validates planes==1, bpp in {8,24}, compression<=1.
BmpInfo BmpReadHeaderInfo(const std::vector<u8>& file);

// gilde.exe 0x5f1664 — VIBE_Bmp_SaveIndexed. Write an 8-bit paletted BMP.
//   pixels  : width*height bytes, top-down (row 0 = top); rows are emitted
//             bottom-up to disk. `width` is also the row stride (no padding in
//             the original — it writes exactly `width` bytes per row).
//   palette : optional 256*3 RGB triples; nullptr => identity grayscale ramp.
// Faithfully reproduces the original's palette byte layout (see bmp.cpp).
std::vector<u8> BmpSaveIndexed(int width, int height, const u8* pixels,
                               const u8* palette = nullptr);

// gilde.exe 0x5f18f4 — VIBE_Bmp_Save24Bit. Write a 24-bit BMP.
//   pixels : width*height*3 bytes, R,G,B order, top-down; emitted bottom-up as
//            on-disk B,G,R (the original swaps R/B per pixel).
std::vector<u8> BmpSave24Bit(int width, int height, const u8* pixels);

// gilde.exe 0x5f0ce4 — VIBE_Bmp_LoadBuffer (core paths).
// Decode a BMP byte buffer. `wantBpp` selects the desired output:
//   8  => returns width*height palette indices; fills `outPalette` (256*3 RGB)
//         when the source is 8-bit.
//   24 => returns width*height*3 RGB bytes (expands 8-bit via its palette).
// Output rows are top-down. Handles BI_RGB and BI_RLE8 for 8-bit sources.
// Returns empty on failure.
std::vector<u8> BmpLoadBuffer(const std::vector<u8>& file, int wantBpp,
                              int& outWidth, int& outHeight,
                              u8* outPalette = nullptr);

} // namespace guild::render
