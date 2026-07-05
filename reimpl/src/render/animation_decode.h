#pragma once
#include "guild/common/types.h"
#include <cstddef>

// =============================================================================
// guild::render — animation/sprite FRAME decompress + blit (ts_texture.c).
//
// This is the real per-pixel work behind VIBE_Paintbox_DrawShape (0x41EF40) and
// VIBE_Animation_Basic (0x5D85B8 = "shp_ShowShapeFromBank"). A "frame" is one
// sprite of an animation bank; it is either an uncompressed 16bpp pixel block
// (VIBE_FrameData_Interpolate) or a per-row run-length stream
// (VIBE_FrameTable_* family). All variants clip against a global blit rectangle
// and support several raster "modes" (plain / 50%-darken / palette-remap / …).
//
// FUNCTIONS RECOVERED
// -----------------------------------------------------------------------------
//   0x5D781C  VIBE_FrameData_Process     — dispatcher: picks the blitter by the
//                                           frame's compression flag (+0x26==-1 =>
//                                           uncompressed Interpolate path) and the
//                                           global edge/HIBYTE selectors.
//   0x5D7420  VIBE_FrameData_Interpolate — UNCOMPRESSED block blit, X/Y clipped,
//                                           modes 0 (copy non-zero) / 2 (darken) /
//                                           3 (palette remap via remapTable).
//   0x5FC200  VIBE_FrameTable_Validate   — RLE blit, Y-clipped only, modes 0..5.
//   0x5FBC10  VIBE_FrameTable_Next       — RLE blit, X+Y clipped (per-pixel),
//                                           modes 0..5.
//   0x5FBFD4  VIBE_FrameTable_Bounds      — RLE blit, Y-clipped, modes 0/1/2/3 +
//                                           a default 8bpp-index→16 (dword_1406938).
//   0x5FBB24  VIBE_FrameTable_Index       — RLE blit, Y-clipped, light-table mode
//                                           (frame-offset table at +0x2A).
//   0x5D85B8  VIBE_Animation_Basic         — look up shape `n` in a shape bank and
//                                           dispatch FrameData_Process.
//
// THE FRAME RECORD (byte offsets, recovered from the blitters + GrabBit8 encoder)
// -----------------------------------------------------------------------------
//   +0x06  u16  width        (pixels per row)
//   +0x0A  u16  height       (rows)
//   +0x0C  u8   colorDepth   (0 = paletted/16bpp shape)
//   +0x0D  u8   mode         (raster op selector 0..5)
//   +0x26  i32  compFlag     (-1 => uncompressed; else RLE)
//   +0x2A  u32  rowTableOff  (byte offset, relative to the frame base, of the
//                             per-row u32 offset table; row R's RLE stream begins
//                             at frame + rowTable[R])
//   +0x32  …    payload      (uncompressed: width*height u16 pixels;
//                             RLE: per row {i32 runCount; runCount * run}, where a
//                             run is {i32 skipBytes; i32 nPixels; u16 px[nPixels]})
//
// THE GLOBAL BLIT STATE (originals were process-wide globals; modelled explicitly)
// -----------------------------------------------------------------------------
//   dword_64A1B4 clipX0  dword_64A1BC clipX1   (inclusive-exclusive screen X bounds,
//                                                in the X-clipped Next path these are
//                                                compared against a per-pixel X counter)
//   dword_64A1B8 clipY0  dword_64A1C0 clipY1   (screen Y bounds)
//   dword_64A1C8 destStridePx   (destination row stride, in PIXELS)
//   dword_64A1C4 remapTable      (u16* 65536-entry / 256-entry colour remap table)
//   word_1406944 / dword_1406930 darken masks  (per-pixel >>1 then AND; the engine
//                                                builds these at mode-set time)
//   dword_1406938 index→16 table  (8bpp index → 16bpp pixel; used by Bounds default)
//   dword_1406940 fillColor16    (solid-fill colour for mode 5)
// =============================================================================
namespace guild::render {

// Modelled blit-state (the original process-wide globals). All in screen space.
struct FrameBlitState {
    int  clipX0 = 0;        // dword_64A1B4
    int  clipX1 = 1 << 30;  // dword_64A1BC
    int  clipY0 = 0;        // dword_64A1B8
    int  clipY1 = 1 << 30;  // dword_64A1C0
    int  destStridePx = 0;  // dword_64A1C8  (dest row stride in pixels)
    u16* dest = nullptr;    // destination pixel buffer base (the +28 surface field)

    const u16* remapTable = nullptr;   // dword_64A1C4 (mode 3)
    const u16* indexTable = nullptr;   // dword_1406938 (Bounds default mode)
    u16  darkMask16  = 0x3DEF;         // word_1406944  (mode 2 odd-pixel mask)
    u32  darkMask32  = 0x3DEFBDEF;     // dword_1406930 (mode 2 dword-pair mask)
    u16  fillColor16 = 0;              // dword_1406940 (mode 5; the binary stores
                                       //   the colour duplicated in both halves:
                                       //   0x5d88dd `result | (result << 16)`)
    bool disabled  = false;            // byte_140694B  (Process global gate)
    bool index16Sel = false;           // HIBYTE(dword_1406947): depth-0 RLE frames
                                       //   go to Bounds (index->16) when set,
                                       //   Index (8bpp dest) when clear
};

// Frame header field byte offsets.
namespace frame_off {
constexpr size_t kWidth      = 0x06;  // u16
constexpr size_t kHeight     = 0x0A;  // u16
constexpr size_t kColorDepth = 0x0C;  // u8
constexpr size_t kMode       = 0x0D;  // u8
constexpr size_t kCompFlag   = 0x26;  // i32 (-1 => uncompressed)
constexpr size_t kRowTableOff= 0x2A;  // u32
constexpr size_t kPayload    = 0x32;  // first row / pixel block
}

// gilde.exe 0x5D7420 — VIBE_FrameData_Interpolate (__usercall ax=fn(x@eax, y@edx,
//   frame@ecx, surface@ebx)). Blit the uncompressed `width`x`height` block at +0x32
//   (a flat u16 array) to (x, y), clipping to [clipX0,clipX1) x [clipY0,clipY1).
//   The original skips pixels whose source value is 0 (transparent). Modes:
//     0  copy  : dst = src              (when src != 0)
//     2  darken: dst = (dst>>1) & darkMask16   (when src != 0)
//     3  remap : dst = remapTable[dst]         (when src != 0)
//   Returns the frame height (the original returns `ax`).
u16 FrameDataInterpolate(int x, int y, const u8* frame, const FrameBlitState& st);

// gilde.exe 0x5FC200 — VIBE_FrameTable_Validate (__usercall fn(x@eax, y@edx,
//   frame@ecx, surface@ebx)). RLE blit clipped in Y only (X assumed in-range).
//   Modes 0..5; see the .cpp for each. The per-row offset table is at +0x2A.
void FrameTableValidate(int x, int y, const u8* frame, const FrameBlitState& st);

// gilde.exe 0x5FBC10 — VIBE_FrameTable_Next. RLE blit clipped in both X (per-pixel
//   against [clipX0,clipX1)) and Y. Modes 0..5.
void FrameTableNext(int x, int y, const u8* frame, const FrameBlitState& st);

// gilde.exe 0x5FBFD4 — VIBE_FrameTable_Bounds. RLE blit clipped in Y; modes 0/1/2/3
//   plus a "default" path that reads 8bpp indices and looks them up in indexTable.
void FrameTableBounds(int x, int y, const u8* frame, const FrameBlitState& st);

// gilde.exe 0x5FBB24 — VIBE_FrameTable_Index. RLE blit clipped in Y; the simple
//   non-X-clipped copy path with the frame-offset table at +0x2A.
void FrameTableIndex(int x, int y, const u8* frame, const FrameBlitState& st);

// gilde.exe 0x5D781C — VIBE_FrameData_Process. Dispatcher. `uncompressed` mirrors
//   the original's compFlag==-1 test; `xClipped`/`bounds` mirror the global edge
//   selectors that pick between the Index / Next / Validate / Bounds RLE blitters.
// Returns true if a blit was issued.
bool FrameDataProcess(int x, int y, const u8* frame, const FrameBlitState& st);

// gilde.exe 0x5D85B8 — VIBE_Animation_Basic ("shp_ShowShapeFromBank", __userpurge
//   eax=fn(x@eax, y@edx, bank@ecx, surface@ebx, n@a5)). Validates `n` against the
//   bank's shape count (+0x2A) and dispatches FrameDataProcess on the shape at
//   bank + offsetTable[n] (offset table at +0x45). Returns 1 on success, 0 if the
//   bank is null or `n` is out of range.
int AnimationBasic(int x, int y, const u8* bank, int n, const FrameBlitState& st);

} // namespace guild::render
