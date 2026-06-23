#pragma once
#include "guild/common/types.h"
#include <vector>
#include <functional>

// =============================================================================
// guild::render — Picture BMP I/O cluster (gilde.exe gfx.c "Bmp" leaves).
//
// Faithful 1:1 reconstruction of the Windows-BMP / TGA-headed picture codecs
// that live at 0x4222bc..0x422d98 in gilde.exe. These are DISTINCT from the
// 0x421xxx TGA-style codecs already in render/picture_io.{h,cpp}; there is no
// symbol overlap (verified: those reconstruct 0x421B58..0x422B58 SwapRowBytes /
// LoadTga / SaveTga / LoadBmp24(0x421DC4) / LoadBmp32 / BlitRegion / FillRows /
// DrawBorder / SaveBmp24(0x422B58); this file reconstructs the 8bpp-palette /
// RLE / 24bpp loaders and the palette BMP saver, none of which exist yet).
//
// The originals streamed through the VFS file shim:
//     VIBE_File_OpenStream(name,"rb") / VIBE_File_Seek(off,whence) /
//     VIBE_File_Read(fd,buf,size,count) -> returns element count (fread-like) /
//     VIBE_Vfs_CloseAndFreeEntry(fd) / VIBE_Vfs_WriteBuffered(buf,len,fd,1)
// To keep the *pixel/header math* (rule 1) testable without I/O or DDraw, the
// raw byte stream is modelled in memory (PictureFile = vector<u8>) and the
// destination pixel buffers are passed explicitly. The header parsing, RLE span
// decode, channel ordering, row flips, integer wraparound and edge cases are
// transcribed byte-for-byte from the Hex-Rays pseudocode.
//
// HEADER FORMAT. Despite the "Bmp" names these picture files carry an 18-byte
// header read after Seek(14,0) (i.e. the engine skips a 14-byte prefix and reads
// a 40-... no: it reads 40 *elements* of size 1 == 40 bytes, but only the first
// 18-ish are used). Field offsets WITHIN the 40-byte block read at file+14
// (see ReadHeader 0x4222bc, fields v4..v9 on the stack):
//     +0  biSize (4)      +4  biWidth (i32, v5)    +8  biHeight (i32, v6)
//     +12 biPlanes (i16,v7)  +14 biBitCount (i16,v8)  +16 biCompression (i32,v9)
// LoadBmp24_226ec instead does Seek(0,0) + Read(18) into a flat buffer and reads
// width @+12 (i16), height @+14 (i16), bpp @+16, descriptor @+17 — a TGA layout.
// Both variants are reproduced exactly as written.
// =============================================================================
namespace guild::render {

// In-memory picture file: the raw bytes the original streamed through the VFS.
using PictureBmpFile = std::vector<u8>;

// ---------------------------------------------------------------------------
// gilde.exe 0x4222bc — VIBE_Picture_ReadHeader (__usercall, eax=result).
// Parse the 40-byte block at file offset 14. Returns -1 if the file can't be
// opened, -2 unless (planes==1 && bitcount==8 && compression<2), else
//     width | (abs(height) << 16).
// (8bpp palettised picture validity probe.)
// ---------------------------------------------------------------------------
i32 PictureReadHeader(const PictureBmpFile& file);

// ---------------------------------------------------------------------------
// gilde.exe 0x4228a0 — VIBE_Picture_ReadBmpType (__usercall, ax=result).
// Open, Seek(14,0), read 40 bytes into a 14-byte stack buffer v4[14] + i16 v5
// (so v5 is the WORD at +14 of the read == biBitCount). Returns -1 if the file
// can't be opened, else v5 (the bit-count field). Faithful note: the original
// reads 40 bytes into a 14-byte buffer; v5 lands at read-offset 14.
// ---------------------------------------------------------------------------
i16 PictureReadBmpType(const PictureBmpFile& file);

// ---------------------------------------------------------------------------
// gilde.exe 0x4228fc — VIBE_Picture_ReadBmpDimensions (__usercall, eax=result).
// As ReadHeader but the validity gate is (planes==1 && bitcount==24 &&
// compression==0); returns width | (height << 16) [height NOT abs'd here],
// -2 on bad header, -1 if unopenable. (24bpp dimensions probe.)
// ---------------------------------------------------------------------------
i32 PictureReadBmpDimensions(const PictureBmpFile& file);

// ---------------------------------------------------------------------------
// gilde.exe 0x42234c — VIBE_Picture_LoadBmpPalette (__fastcall).
// Zero-fill the 768-byte (256*3) RGB palette `pal`, then read `clrUsed` BGRA
// quads from file offset 14+40 (the original reads the 40-byte info header, then
// reads clrUsed*4 bytes; clrUsed==0 is treated as 256) and unpack each quad
// [B,G,R,_] into pal as [R,G,B]. FAITHFUL DETAIL: the original clears only 768
// bytes but writes entries with a 4-byte stride (pal + 4*i), so for 256 colours
// up to 1024 bytes are touched. `pal` must hold >= 4*clrUsed bytes (>=1024 for a
// full 256-colour palette).
// ---------------------------------------------------------------------------
void PictureLoadBmpPalette(const PictureBmpFile& file, u8* pal);

// ---------------------------------------------------------------------------
// gilde.exe 0x422418 — VIBE_Picture_LoadBmpRle (__usercall, al=result).
// Decode an 8bpp BMP into `dst` (row stride `dstStride` bytes, `flipVertical`
// = original `a2`/v25). Reads the 40-byte info header (compression v19 @+8 of
// the header read; width v17 @ +0?, see .cpp for exact field offsets matching
// Hex-Rays). compression==1 => BI_RLE8 span decode; else a single flat read of
// height*stride bytes. If `flipVertical` and height>0, rows are reversed in
// place via a stride-sized scratch buffer (uses `alloc`/`free` hooks). Returns 1
// on success, 0 if the file can't be opened. `dst` must be >= abs(height)*stride.
// ---------------------------------------------------------------------------
char PictureLoadBmpRle(const PictureBmpFile& file, u8* dst, int dstStride,
                       char flipVertical);

// ---------------------------------------------------------------------------
// gilde.exe 0x422980 — VIBE_Picture_LoadBmpUncompressed (__usercall, eax=res).
// Load an uncompressed 24bpp BMP (planes==1, bitcount==24, compression==0) into
// `dst` as packed 24-bit. Reads 3*width bytes/row with 4-byte DWORD row padding
// (the original seeks 4-((3*width)&3) when (3*width)&3 != 0). Negative biHeight
// => top-down (v21). After loading, every pixel's B and R channels are swapped
// in place (the BGR<->RGB pass at 0x422a8e). Returns the open result (the fd
// value, nonzero on success) — 0 if the file can't be opened. `dst` must be
// >= width*abs(height)*3 bytes.
// ---------------------------------------------------------------------------
int PictureLoadBmpUncompressed(const PictureBmpFile& file, u8* dst);

// ---------------------------------------------------------------------------
// gilde.exe 0x4226ec — VIBE_Picture_LoadBmp24_226ec (__fastcall).
// Load a TGA-headed 24bpp picture into a flat RGB buffer `dst`, ROW STRIDE in
// pixels given by `fbWidth` (the original's global framebuffer width
// dword_7626F8). Reads an 18-byte header (Seek(0,0)+Read(18)); requires bpp
// (header+16) == 24, else closes and returns 1 (success, no-op). width@+12 (i16),
// height@+14 (i16), descriptor@+17. Per pixel stores R,G,B (3 bytes) at
// 3*(x + fbWidth*row); descriptor==32 => top-down rows, else bottom-up
// (height-1-i). Returns 1 on success/short-bpp, 0 if the file can't be opened.
// `dst` must be >= 3*fbWidth*height bytes.
// ---------------------------------------------------------------------------
int PictureLoadBmp24_226ec(const PictureBmpFile& file, u8* dst, int fbWidth);

// ---------------------------------------------------------------------------
// gilde.exe 0x422d98 — VIBE_Picture_SaveBmpPalette (__userpurge).
// Emit an 8bpp palettised Windows BMP (BITMAPFILEHEADER + 40-byte info header +
// 256-entry BGRA palette + pixel rows). `width`,`height`, `pixels` = w*h 8bpp
// indices, `pal` = 256*3 RGB triples. height is stored NEGATIVE (top-down) and
// dataOffset/fileSize match the original (1078-byte header+palette). The palette
// emit loop writes entries 1..255 as [B,G,R,0] from pal and leaves entry 0 zero,
// reproducing the original's `for(i=0;i!=255;...)` off-by-one exactly.
// ---------------------------------------------------------------------------
PictureBmpFile PictureSaveBmpPalette(int width, int height,
                                     const u8* pixels, const u8* pal);

// ---------------------------------------------------------------------------
// gilde.exe 0x422ae4 — VIBE_Picture_CreateSurfaceFromBmp (__thiscall).
// Orchestrator: probe 24bpp dimensions (ReadBmpDimensions), bail (return null)
// if <0, take width=lo16, height=abs(hi16), create a 24bpp surface via the
// injected `createSurface(w,h,bpp=24)` hook, then fill it via
// PictureLoadBmpUncompressed(file, surface.dataPtr). The original used the DDraw
// surface allocator VIBE_Surface_Create (rule-3 boundary) and read the BMP off
// the same path; here the surface allocation is injected so the control flow is
// faithful without the GPU/VFS internals. `out` receives the created surface
// handle (whatever the hook returns) on success. Returns the surface handle, or
// the SurfaceHandle{} default on any failure, mirroring the original's `return 0`.
// ---------------------------------------------------------------------------
struct BmpSurface {
    int   width  = 0;   // surface +4
    int   height = 0;   // surface +8
    int   bpp    = 0;   // surface +20 (byte, here widened)
    u8*   data   = nullptr;   // surface +28 (pixel buffer)
    void* handle = nullptr;   // opaque handle the create-hook returns
};
// createSurface(width,height,bpp) -> BmpSurface (data!=null on success).
using BmpSurfaceCreate = std::function<BmpSurface(int, int, int)>;

BmpSurface PictureCreateSurfaceFromBmp(const PictureBmpFile& file,
                                       const BmpSurfaceCreate& createSurface);

} // namespace guild::render
