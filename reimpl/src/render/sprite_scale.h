#pragma once
#include "guild/common/types.h"
#include "render/shape_blit.h"        // ColorBlitTarget16 (+0x10 widthPx, +0x1C pixels)
#include "render/animation_decode.h"  // FrameBlitState (clip rect + remap table globals)
#include <cstddef>

// =============================================================================
// guild::render — scaled 2D sprite/shape compositors (ts_texture.c / shp.c).
//
// The screen-space sprite draw path that blits one shape of an animation/shape
// bank at a fractional/integer scale. Three per-pixel leaves plus their bank
// dispatcher, distinct from the unscaled shape_blit.cpp / animation_decode.cpp
// blitters already translated:
//
//   0x5D72F0  VIBE_Shape_BlitScaled16    — UNCOMPRESSED 16bpp sprite, sampled
//                                          through 16.16-style fixed-point X/Y
//                                          step deltas (nearest-neighbour scale).
//   0x5D6A08  VIBE_Shape_BlitRleScaled   — RLE sprite, integer DOWN-scale by `scale`
//                                          (every `scale`-th source column/row kept),
//                                          Y/X clipped against the global blit rect.
//   0x5D6D74  VIBE_Shape_BlitRleLightTable — as BlitRleScaled, but each opaque pixel
//                                          is recoloured through remapTable (the
//                                          dword_64A1C4 light/colour table).
//   0x5D86C4  VIBE_Shape_ShowFromBankScaled ("shp_ShowShapeFromBank" scaled variant)
//                                          — validate the shape index against the
//                                          bank, install the dest stride, and dispatch
//                                          the RLE scaled or light-table blitter.
//   0x559D60  VIBE_Render_EncodeSpriteDrawFlags — pack a sprite's draw flags.
//
// THE SHAPE / FRAME RECORD (byte offsets; same family as animation_decode.h)
// -----------------------------------------------------------------------------
//   +0x06  u16  width        (pixels per row)
//   +0x0A  u16  height       (rows)
//   +0x0C  u8   colorDepth   (0 / 1 = RLE shape, 2 = uncompressed marker)
//   +0x32  …    for the SCALED-16 path: a flat u16 pixel block (width*height)
//   +0x32  u32  for the RLE paths: row[0] runCount; then runs of
//                {u32 skip; u32 nPixels; u16 px[nPixels]}; the next row's runCount
//                follows immediately (skip>>1 = transparent pixels to advance).
//
// THE SHAPE BANK RECORD (for ShowFromBankScaled)
// -----------------------------------------------------------------------------
//   +0x0A  char name[]        (bank name, for the error sprintf)
//   +0x2A  u16  shapeCount    (max valid index)
//   +0x45  u32  offset[i]     (bank-relative byte offset of shape i)
//
// THE GLOBAL BLIT STATE — reused from animation_decode.h FrameBlitState:
//   clipX0 dword_64A1B4  clipX1 dword_64A1BC  clipY0 dword_64A1B8  clipY1 dword_64A1C0
//   remapTable dword_64A1C4   destStridePx dword_64A1C8
// =============================================================================
namespace guild::render {

// gilde.exe 0x5D72F0 — VIBE_Shape_BlitScaled16 (__userpurge edx:eax = fn(x@eax,
//   y@edx, shape@ecx, surface@ebx, xStep, yStep, useColorKey)). Blits the
//   uncompressed 16bpp block at shape+0x32 to (x, y), advancing the source by the
//   fixed-point `xStep`/`yStep` per dest pixel/row (sampled via the high byte of a
//   running fixed-point accumulator). When `useColorKey` is true, source value 0 is
//   transparent; otherwise every sample is copied. No screen clipping (callers
//   pre-clip). Returns the last computed source row offset (the original's edx:eax).
u64 ShapeBlitScaled16(int x, int y, const u8* shape, const ColorBlitTarget16& dst,
                      int xStep, int yStep, bool useColorKey);

// gilde.exe 0x5D6A08 — VIBE_Shape_BlitRleScaled (__userpurge eax = fn(x@eax, y@edx,
//   shape@ecx, surface@ebx, scale@bl)). Integer down-scale of the row-RLE shape:
//   only every `scale`-th source row/column survives. Clips against the global blit
//   rect (clipX0/X1/Y0/Y1). Returns 1 if drawn, 0 if fully clipped.
int ShapeBlitRleScaled(int x, int y, const u8* shape, const ColorBlitTarget16& dst,
                       u8 scale, const FrameBlitState& st);

// gilde.exe 0x5D6D74 — VIBE_Shape_BlitRleLightTable. As BlitRleScaled, but each
//   opaque source index is remapped: dst = remapTable[srcPixel] (st.remapTable =
//   dword_64A1C4). Returns 1 if drawn, 0 if fully clipped.
int ShapeBlitRleLightTable(int x, int y, const u8* shape, const ColorBlitTarget16& dst,
                           u8 scale, const FrameBlitState& st);

// gilde.exe 0x5D86C4 — VIBE_Shape_ShowFromBankScaled. Looks up shape `shapeIndex`
//   in `bank` (count @+0x2A, offset table @+0x45). `scale` is the integer down-scale
//   factor (the original derives it as abs(BYTE4 of the packed 64-bit arg)); when
//   `doScaledBlit` is set the RLE blitter runs (light-table form when `lightTable`
//   is non-zero), otherwise only the clip rect is recomputed. `bankStridePx` is the
//   dest surface stride installed into st.destStridePx (original: *(surface+16)).
//   Returns 1 on success / valid, 0 when the index is out of range or depth==0.
int ShapeShowFromBankScaled(int x, int y, const u8* bank, int shapeIndex, u8 scale,
                            bool doScaledBlit, bool lightTable,
                            const ColorBlitTarget16& dst, FrameBlitState& st);

// gilde.exe 0x5D861C — VIBE_Shape_ShowFromBank ("shp_ShowShapeFromBank", unscaled).
//   Validates `shapeIndex` against the bank's shape count (+0x2A), then, when the
//   global scaled-draw gate is clear, installs the dest stride and — for an RLE
//   shape (depth in 1..) — runs the recolouring blit VIBE_Shape_BlitColored16
//   (reused from shape_blit.cpp). depth 0 -> fail, depth 2 -> no-op success,
//   depth > 2 -> restore stride, success. Returns 1 on success, 0 on failure.
//   `fmt` is the destination colour format the BlitColored16 (un)pack needs.
int ShapeShowFromBank(int x, int y, const u8* bank, int shapeIndex,
                      const ColorBlitTarget16& dst, const ColorFormat& fmt,
                      FrameBlitState& st);

// gilde.exe 0x559D60 — VIBE_Render_EncodeSpriteDrawFlags (__usercall eax=fn(mode@edx,
//   value@ebx)). Returns 0 when mode==1, else (value<<22)|0x8000000.
int RenderEncodeSpriteDrawFlags(int mode, int value);

} // namespace guild::render
