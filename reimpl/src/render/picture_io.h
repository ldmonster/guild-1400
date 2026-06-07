#pragma once
#include "guild/common/types.h"
#include <vector>

// =============================================================================
// guild::render — Picture image I/O (gilde.exe gfx.c "Picture/Bmp" leaves).
//
// These load/save 16-bit (RGB555) pictures into the engine's *system-memory*
// framebuffer. The originals went through the VFS file shim (VIBE_File_Open /
// Read / Seek, VIBE_Vfs_WriteBuffered) and a single global framebuffer width
// (dword_7626F8) / height (cy); here the same byte stream is modelled in memory
// and the framebuffer is passed explicitly (ptr + width) so the codecs are
// testable without I/O or globals. The pixel maths is byte-for-byte the original.
//
// Faithful 1:1 reconstruction of:
//   0x421B58  VIBE_Picture_SwapRowBytes   (in-place byte swap of a u16 run)
//   0x421B88  VIBE_Picture_LoadTga        (16bpp TGA load into the framebuffer)
//   0x421CA4  VIBE_Picture_SaveTga        (16bpp TGA save from the framebuffer)
//   0x421DC4  VIBE_Picture_LoadBmp24      (24bpp picture -> RGB555 framebuffer)
//   0x421F2C  VIBE_Picture_LoadBmp32      (32bpp picture -> RGB555 framebuffer)
//   0x422094  VIBE_Picture_BlitRegion     (rect copy within/between framebuffers)
//   0x42210C  VIBE_Picture_FillRows       (clear a run of full rows)
//   0x422148  VIBE_Picture_DrawBorder     (white outline of a rect)
//   0x422B58  VIBE_Picture_SaveBmp24      (24bpp BMP save from an RGB buffer)
//
// PIXEL FORMAT: the framebuffer is RGB555 little-endian:
//   pix = ((b>>3)&0x1F) | (((g>>3)&0x1F)<<5) | (((r>>3)&0x1F)<<10)
// (the original builds exactly this: blue in bits 0..4, green via `32*`, red via
// `<<10`). The on-disk 18-byte header is a TGA image header (see the .cpp): the
// "Bmp" entry points dispatch to LoadTga when the header bpp/depth field is 16.
// =============================================================================
namespace guild::render {

// An in-memory picture file: the raw bytes the original streamed through the VFS.
using PictureFile = std::vector<u8>;

// gilde.exe 0x421B58 — VIBE_Picture_SwapRowBytes. Swap the two bytes of each of
// `pixelCount` consecutive u16s in place (lo<->hi). Used by SaveTga to convert
// the framebuffer's little-endian pixels to the on-disk byte order and back.
void PictureSwapRowBytes(u16* pixels, int pixelCount);

// gilde.exe 0x421B88 — VIBE_Picture_LoadTga.
// Parse the 18-byte TGA header in `file`; copy the 16bpp pixel rows into `fb`
// (row stride `fbWidth` u16s). Descriptor byte 17 == 32 => top-down rows, else
// bottom-up (row r -> fb row height-1-r). Returns the image width/height via
// outW/outH. Returns true on success (false if the file is too short).
bool PictureLoadTga(const PictureFile& file, u16* fb, int fbWidth,
                    int& outW, int& outH);

// gilde.exe 0x421CA4 — VIBE_Picture_SaveTga.
// Emit a 16bpp TGA (imageType=2, depth=16, descriptor=32) of `w`x`h` pixels read
// from `fb` (row stride `fbWidth`). Reproduces the original's pre/post byte-swap
// of the framebuffer (SwapRowBytes over w*h, write, swap back). The header fixes
// the on-disk width to 320 and height to 240 exactly as the original does (it
// hard-codes HIWORD(width)=320, height=240); pass w/h to control how many rows
// are emitted from the framebuffer.
PictureFile PictureSaveTga(const u16* fb, int fbWidth, int w, int h);

// gilde.exe 0x421DC4 — VIBE_Picture_LoadBmp24.
// Load a 24bpp TGA-style picture from `file` into the RGB555 framebuffer `fb`.
// On-disk pixels are B,G,R (3 bytes); packed to RGB555. Descriptor byte 17 == 32
// => top-down. Returns width/height. (If the header depth is 16 the original
// dispatches to LoadTga; pass such files to PictureLoadTga directly.)
bool PictureLoadBmp24(const PictureFile& file, u16* fb, int fbWidth,
                      int& outW, int& outH);

// gilde.exe 0x421F2C — VIBE_Picture_LoadBmp32. As LoadBmp24 but 4 bytes/pixel
// (B,G,R,X). NOTE: the original iterates rows in [0, height-1) (it stops one row
// short — `< (height>>16) - 1`); this faithful port preserves that off-by-one.
bool PictureLoadBmp32(const PictureFile& file, u16* fb, int fbWidth,
                      int& outW, int& outH);

// gilde.exe 0x422094 — VIBE_Picture_BlitRegion (result@eax=srcX, edx=srcY,
// ecx=h, ebx=w, a5=srcFb, a6=dstX, a7=dstY, a8=dstFb).
// Copy a `w`x`h` (pixels) rectangle from src at (srcX,srcY) to dst at
// (dstX,dstY). Both framebuffers share the row stride `fbWidth`. Each row is a
// straight memcpy of 2*w bytes. No-op when w<1 or h<1.
void PictureBlitRegion(const u16* srcFb, int srcX, int srcY, int w, int h,
                       u16* dstFb, int dstX, int dstY, int fbWidth);

// gilde.exe 0x42210C — VIBE_Picture_FillRows (ecx=rows, edx=y, eax=x, ebx=w,
// a5=fb). Zero-fill `w` pixels starting at (x,y) for `rows` consecutive rows.
void PictureFillRows(u16* fb, int x, int y, int w, int rows, int fbWidth);

// gilde.exe 0x422148 — VIBE_Picture_DrawBorder (result@eax=x, edx=y, ecx=h,
// ebx=w, a5=fb). Draw a white (0x00FF) rectangle outline: left & right vertical
// edges (clipped to row count `fbHeight`), then the top & bottom horizontal
// edges (each `w` pixels), all in framebuffer `fb` of stride `fbWidth`.
void PictureDrawBorder(u16* fb, int x, int y, int w, int h,
                       int fbWidth, int fbHeight);

// gilde.exe 0x422B58 — VIBE_Picture_SaveBmp24 (edx=width, ecx=rgb, ebx=height).
// Write a top-down 24bpp Windows BMP (height stored negative) from `rgb`
// (width*height*3 bytes, R,G,B order). Each pixel is emitted B,G,R; the data
// offset is 58 (a 4-byte gap after the 54-byte header, as the original lays out:
// fileSize = 3*w*h + 58, dataOffset = 58). No row padding (the original copies
// exactly 3*width bytes per row).
PictureFile PictureSaveBmp24(int width, int height, const u8* rgb);

} // namespace guild::render
