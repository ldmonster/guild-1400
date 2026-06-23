#pragma once
#include "guild/common/types.h"
#include <cstddef>

// =============================================================================
// guild::render — software surface stretch / convert / block-copy (gilde.exe gfx.c).
//
// The bottom of the renderer's "grab a surface, resample/encode it into another
// surface" path. These are pure system-memory pixel routines (no DDraw/GDI) that
// the dispatchers VIBE_Render_StretchSurfaceDispatch / _BlitConvertDispatch fan
// out to based on the source/destination bit depth. Faithful 1:1 of:
//
//   0x435D88  VIBE_Render_StretchSurface8        (8bpp nearest-neighbour resample)
//   0x436488  VIBE_Render_StretchSurface8Up      (8bpp nearest, dst-driven loop)
//   0x435E00  VIBE_Render_StretchAverage16       (16bpp box-average down-sample)
//   0x437814  VIBE_Render_Convert24To16          (24bpp BGR block -> native 16bpp)
//   0x437530  VIBE_Render_StretchSurfaceDispatch (size/bpp dispatch + same-size copy)
//   0x437980  VIBE_Render_BlitConvertDispatch    (same-size convert/copy dispatch)
//   0x56D6D4  VIBE_Render_BlitThumbnailToSurface (copy a 160x(h) 16bpp block in)
//
// THE RAW SURFACE RECORD (recovered from *(type*)(base+off) accesses)
// -----------------------------------------------------------------------------
// The originals address two surface records by raw byte offset. The fields used
// by this family (DDSURFACEDESC-ish layout) are:
//
//   +0x08  i32  height (rows)                 (dword index [2])
//   +0x0C  i32  width  (pixels per row)       (dword index [3])
//   +0x10  i32  pitch  (BYTES per row)        (dword index [4])
//   +0x24  u8*  pixels (system-memory base)   (dword index [9])
//   +0x4C  u8   flags  (bit5 0x20 = 8bpp indexed)
//   +0x54  i32  bpp    (8/16/24/32)           (dword index [21])
//   +0x58  u32  rMask                         (dword index [22])
//   +0x5C  u32  gMask                         (dword index [23])
//   +0x60  u32  bMask                         (dword index [24])
//
// Pointer arithmetic below is byte-exact to the original: rows step by `pitch`
// BYTES; the 8bpp routines index bytes, the 16bpp routines index 16-bit words.
// =============================================================================
namespace guild::render {

// Minimal raw-surface descriptor mirroring the fields the stretch/convert leaves
// touch. `pitch` is the BYTE stride (the original's +0x10); `pixels` is the byte
// base (the original's +0x24). `indexed` mirrors the +0x4C bit5 (0x20) flag.
struct StretchSurfaceDesc {
    i32  height = 0;     // +0x08
    i32  width  = 0;     // +0x0C
    i32  pitch  = 0;     // +0x10  BYTES per row
    u8*  pixels = nullptr; // +0x24
    bool indexed = false;  // +0x4C & 0x20  (8bpp)
    i32  bpp    = 0;     // +0x54
    u32  rMask  = 0;     // +0x58
    u32  gMask  = 0;     // +0x5C
    u32  bMask  = 0;     // +0x60
};

// gilde.exe 0x435D88 — VIBE_Render_StretchSurface8 (eax=dst, edx=src).
//   Nearest-neighbour resample of an 8bpp surface from `src` into `dst`. The loop
//   is DST-driven: dst row r reads src row (srcH*r/dstH); dst col c reads src col
//   (c*srcW/dstW). Used by the dispatch's down-sample (dst smaller) branch.
//   Returns the last byte written (the original's `al`).
u8 StretchSurface8(const StretchSurfaceDesc& dst, const StretchSurfaceDesc& src);

// gilde.exe 0x436488 — VIBE_Render_StretchSurface8Up (eax=dst, edx=src).
//   8bpp up-sample. The SMALLER source (edx) row is read SEQUENTIALLY and each
//   byte is SCATTERED into the LARGER destination (eax): src col c writes dst col
//   (c*dstW/srcW); loop bounds are the SOURCE dimensions (esi[2]/esi[0xC]). First
//   positional arg is the eax DST (write target), second is the edx SRC (read).
//   Used by the dispatch's up-sample (dst larger) branch. Returns the last byte read.
u8 StretchSurface8Up(const StretchSurfaceDesc& dst, const StretchSurfaceDesc& src);

// gilde.exe 0x435E00 — VIBE_Render_StretchAverage16 (eax=dst, edx=src).
//   Box-average down-sample of a 16bpp surface. For each dst pixel it averages the
//   src pixels covering its footprint [rowSpan x colSpan), per channel, using the
//   src masks/shifts, then re-packs. `i,j,k` are the channel field positions of
//   the src R,G,B masks (trailing-zero counts). Returns dst pixel write cursor.
u16* StretchAverage16(const StretchSurfaceDesc& dst, const StretchSurfaceDesc& src);

// gilde.exe 0x437814 — VIBE_Render_Convert24To16 (eax=dst, edx=src).
//   Convert a 24bpp (B,G,R byte order in memory) block into native 16bpp using
//   the DEST masks (field position = trailing zeros, precision = 8 - setBits).
//   Packs (R>>(8-rBits)<<rPos)|(G>>..<<gPos)|(B>>..<<bPos). Returns last pixel.
u32 Convert24To16(const StretchSurfaceDesc& dst, const StretchSurfaceDesc& src);

// gilde.exe 0x437530 — VIBE_Render_StretchSurfaceDispatch (eax=dst, edx=src).
//   Dispatch on bpp + relative size:
//     - depths must match (src.bpp == dst.bpp), else no-op (returns 0).
//     - dst.width <  src.width  -> down-sample (Average16 / Surface8 for 8bpp)
//     - dst.width == src.width  -> straight row memcpy (per-row pitch-aware)
//     - dst.width >  src.width  -> up-sample (Interpolate / Surface8Up for 8bpp)
//   Only the 8bpp and 16bpp paths are implemented here; 24/32bpp average &
//   interpolate leaves are out of scope (see module report). Returns a status byte.
u8 StretchSurfaceDispatch(StretchSurfaceDesc& dst, const StretchSurfaceDesc& src);

// gilde.exe 0x437980 — VIBE_Render_BlitConvertDispatch (eax=dst, ecx=status, ebx=src).
//   Same-size convert/copy: requires equal width AND height. If the depths match
//   (or both indexed) it does a per-row pitch-aware memcpy; else it routes
//   8bpp->16bpp to Convert8To16Indexed (in shape_blit) or 24->16 to Convert24To16.
//   `status` is the passthrough return when nothing matches. Returns rows copied
//   or the convert result.
u32 BlitConvertDispatch(StretchSurfaceDesc& dst, u32 status,
                        const StretchSurfaceDesc& src, const u8* palette8);

// gilde.exe 0x56D6D4 — VIBE_Render_BlitThumbnailToSurface (eax=x, edx=y, ebx=surf).
//   Copies a fixed 160-pixel-wide, 120-row 16bpp block from the global thumbnail
//   word buffer (word_13CED78) into the surface at (x, y). The source is read
//   SEQUENTIALLY (a running index stepping by 2 bytes), so source pixel n =
//   thumb[r*160 + c] maps to surf pixel (x+c, y+r). The original brackets the copy
//   with a surface lock/unlock (VIBE_DecompressState_Blob / _Finalize); here the
//   destination pixel base/stride are passed explicitly (surf +0x1C / +0x10).
//   Returns the number of rows copied (120).
int BlitThumbnailToSurface(int x, int y, u16* surfPixels, int surfStridePx,
                           const u16* thumb);

} // namespace guild::render
