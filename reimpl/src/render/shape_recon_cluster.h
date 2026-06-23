#pragma once
#include "guild/common/types.h"
#include <cstddef>

// =============================================================================
// guild::render — shape / shape-bank / shape-anim reconstruction cluster.
//
// 1:1 translation of the 2D "shape" (sprite/blit primitive) cluster of
// gilde.exe (the ts_texture.c / shp_t / d2:shp subsystem). A "shape" is the
// engine's RLE-compressible 2D sprite record; a "shape bank" is a packed
// container of shapes (see render/shapebank.h for the bank append/insert side);
// "shape anim" is the 16-slot animated-shape draw table.
//
// FUNCTIONS RECONSTRUCTED HERE (addr -> symbol):
//   0x41F4F4  VIBE_Shape_ClassifyType        ShapeClassifyType
//   0x5D49A0  VIBE_Shape_BuildLightTable     ShapeBuildLightTable
//   0x5D4AD4  VIBE_Shape_InitColorMasks      ShapeInitColorMasks
//   0x5D4BCC  VIBE_Shape_FreeLightTable      ShapeFreeLightTable
//   0x5D88C0  VIBE_Shape_SetMaskColor        ShapeSetMaskColor
//   0x5D68C4  VIBE_Shape_GrabByDepth         ShapeGrabByDepth (dispatcher)
//   0x5D4C20  VIBE_Shape_GrabBit24           ShapeGrabBit24
//   0x5D547C  VIBE_Shape_GrabBit24NoRle      ShapeGrabBit24NoRle
//   0x5D5908  VIBE_Shape_GrabBit16           ShapeGrabBit16
//   0x5D5E7C  VIBE_Shape_GrabBit16NoRle      ShapeGrabBit16NoRle
//   0x5D6160  VIBE_Shape_GrabBit8            ShapeGrabBit8
//   0x5D8294  VIBE_ShapeBank_SetPalette      ShapeBankSetPalette
//   0x5D80A8  VIBE_ShapeBank_ConvertNew      ShapeBankConvertNew (driver hook)
//   0x5D8A10  VIBE_ShapeBank_GrabFromFile    ShapeBankGrabFromFile (driver hook)
//   0x5D8F54  VIBE_ShapeBank_SetSequenceData ShapeBankSetSequenceData
//   0x5D8E30  VIBE_ShapeAnim_DrawAllSlots    ShapeAnimDrawAllSlots
//   0x5D8F1C  VIBE_ShapeAnim_GetSlot         ShapeAnimGetSlot
//   0x5D8F3C  VIBE_ShapeAnim_GetSlotTable    ShapeAnimGetSlotTable
//
// Helper leaves used by the cluster (translated 1:1 alongside, kept local to
// avoid ODR clashes with the existing color/light code):
//   0x434F30  VIBE_Result_Handler_Final  -> ColorPack   (rgb -> packed 16/15bpp)
//   0x434F7C  VIBE_Render_UnpackColor    -> ColorUnpack (packed -> rgb)
//   0x4226BC  VIBE_Color_NotEqualRgb     -> ColorNotEqualRgb
//   0x4226E0  VIBE_Color_SetRgb          -> ColorSetRgb
//
// PROVENANCE OF THE GLOBAL STATE (modelled by struct ShapeColorState)
// -----------------------------------------------------------------------------
//   byte_762719 .. byte_76271E  : per-channel depth (shift-down) + position
//                                 (shift-up) amounts for the active pixel format.
//                                 Set by display-mode init; zero in the static
//                                 image (so the default state is identity).
//      0x762719 bShiftDown   (b >> byte_762719)
//      0x76271A gShiftUp     (<< byte_76271A)
//      0x76271B gShiftDown   (g >> byte_76271B)
//      0x76271C rShiftDown   (r >> byte_76271C)
//      0x76271D bShiftUp     (<< byte_76271D)
//      0x76271E rShiftUp     (<< byte_76271E)
//   byte_1406946 / dword_1406947 : the transparent "mask" colour (R = _946,
//                                 G = lo byte of _947, B = byte1 of _947).
//   byte_64A1AC..AE : anchor colour A (default {0,0,0xFF}) — hotspot marker.
//   byte_64A1AF..B1 : anchor colour B (default {0xFF,0,0}) — hotspot marker.
//   byte_1406530[1024] : the active 8bpp palette (RGBA quads).
//   dword_64A1C4 : the 0x20000-byte light table (depth-0 only).
//   byte_140694B : nonzero => high-colour mode (skip the light table).
//   word_1406944/dword_1406930 : high-bit mask; dword_1406940 : packed mask col.
// =============================================================================

namespace guild::render {

// -----------------------------------------------------------------------------
// Shape record header layout (the per-sprite blob produced by the Grab* funcs).
// Byte offsets, recovered directly from the Grab* stores:
//   +0x00  u32  dataSize       (running write cursor; final = total blob size)
//   +0x04  u16  minX           (a4[2])  left bound
//   +0x06  u16  width          (a4[3])  = maxX - minX + 1
//   +0x08  u16  maxX           (a4[4])
//   +0x0A  u16  height         (a4[5])  = maxY - minY + 1   (row/line count)
//   +0x0C  u8   colorDepthCode (set by GrabByDepth: 0=8bpp,1=16bpp,2=24bpp)
//   +0x0D  u8   typeFlag       (a6, the RLE/no-RLE caller flag)
//   +0x0E  u16  minX2          (a4[7])  duplicate bounds (hotspot baseline)
//   +0x10  u16  minY2          (a4[8])
//   +0x12  u16  width2         (a4[9])
//   +0x14  u16  height2        (a4[10])
//   +0x16  u16  hotA_x         (a4[11]) anchor-A x
//   +0x18  u16  hotA_y         (a4[12]) anchor-A y
//   +0x1A  u16  hotB_x         (a4[13]) anchor-B x
//   +0x1C  u16  hotB_y         (a4[14]) anchor-B y
//   +0x26  u32  rowTableOffset (+42) offset to per-row u32 offset table
//   +0x2E  u32  opaquePixels   (+46) running count of copied opaque pixels
//   +0x26  u32  spanCount?     (+38) total span count (-1 for No-Rle variants)
// (the +38/+42/+46 dwords overlap the u16 view above; see the .cpp comments.)
//
// We model the header by explicit field accessors (offsets) instead of a struct
// because the original writes through raw offset stores and the dword/word
// views alias.  The constants below are the authoritative offsets.
namespace shp_hdr {
constexpr size_t kDataSize   = 0;     // u32
constexpr size_t kMinX       = 4;     // u16  (word[2])
constexpr size_t kWidth      = 6;     // u16  (word[3])
constexpr size_t kMaxX       = 8;     // u16  (word[4])
constexpr size_t kHeight     = 10;    // u16  (word[5])
constexpr size_t kDepthCode  = 12;    // u8
constexpr size_t kTypeFlag   = 13;    // u8
constexpr size_t kSpanCount  = 38;    // u32  (-1 for NoRle)
constexpr size_t kRowTableOff= 42;    // u32
constexpr size_t kOpaqueCnt  = 46;    // u32
constexpr size_t kPixelBase  = 50;    // first byte of pixel/RLE payload
}

// Depth codes stored at shp_hdr::kDepthCode by ShapeGrabByDepth.
constexpr u8 kDepthCode8  = 0;
constexpr u8 kDepthCode16 = 1;
constexpr u8 kDepthCode24 = 2;

// -----------------------------------------------------------------------------
// Colour-format state (the byte_762719.. shift globals + mask + anchors).
// All zero by default = identity (matches the static image of gilde.exe).
struct ShapeColorState {
    // gilde.exe byte_762719..76271E — per-channel shift amounts.
    u8 bShiftDown = 0;   // 0x762719
    u8 gShiftUp   = 0;   // 0x76271A
    u8 gShiftDown = 0;   // 0x76271B
    u8 rShiftDown = 0;   // 0x76271C
    u8 bShiftUp   = 0;   // 0x76271D
    u8 rShiftUp   = 0;   // 0x76271E

    // gilde.exe byte_1406946 / dword_1406947 — transparent mask colour.
    u8 maskR = 0;        // 0x1406946
    u8 maskG = 0;        // low byte of dword_1406947
    u8 maskB = 0;        // byte1 of dword_1406947

    // gilde.exe byte_64A1AC..AE — anchor A (hotspot). Default {0,0,0xFF}.
    u8 anchorA_r = 0x00, anchorA_g = 0x00, anchorA_b = 0xFF;
    // gilde.exe byte_64A1AF..B1 — anchor B. Default {0xFF,0,0}.
    u8 anchorB_r = 0xFF, anchorB_g = 0x00, anchorB_b = 0x00;
};

// 0x434F30 — VIBE_Result_Handler_Final. Packs an (r,g,b) triple into the active
// hi-colour pixel using the per-channel shift state.
u16 ColorPack(const ShapeColorState& st, u8 r, u8 g, u8 b);

// 0x434F7C — VIBE_Render_UnpackColor. Unpacks a packed hi-colour pixel back into
// (r,g,b) using the per-channel shift state.
void ColorUnpack(const ShapeColorState& st, u16 packed, u8& r, u8& g, u8& b);

// 0x4226BC — VIBE_Color_NotEqualRgb.
bool ColorNotEqualRgb(const u8* a, const u8* b);
// 0x4226E0 — VIBE_Color_SetRgb. Stores {dl, bl, cl} i.e. {r, b, g} order.
void ColorSetRgb(u8* dst, u8 r, u8 g, u8 b);

// -----------------------------------------------------------------------------
// 0x5D49A0 — VIBE_Shape_BuildLightTable. Builds a 0x20000-byte (65536-entry,
// u16) brighten LUT: each 16bpp colour, with every channel saturating-added by
// `amount`, repacked. Writes into `out` (must be 65536 u16). Returns 1.
//   Faithful to the saturating-add edge cases (the 255-a1 comparisons).
int ShapeBuildLightTable(const ShapeColorState& st, u8 amount, u16* out /*[65536]*/);

// 0x5D4BCC — VIBE_Shape_FreeLightTable. Pure predicate form: returns whether the
// light table should be freed (true when not in high-colour mode and a table is
// present). The real function frees dword_64A1C4; here freeing is the caller's
// (allocator is a coupled leaf).
bool ShapeShouldFreeLightTable(bool highColorMode, bool tablePresent);

// 0x5D88C0 — VIBE_Shape_SetMaskColor. Packs (a1=r? , a2) — fastcall (r in a1,
// g/b combined in a2) — into the mask register dword_1406940 (lo|hi<<16).
// Returns the packed colour. We expose the pure pack: given r and the second
// fastcall byte, returns ColorPack(st, a1, a2, 0)-equivalent. The original
// passes only two args to VIBE_Result_Handler_Final, so bl=0.
u32 ShapeSetMaskColor(const ShapeColorState& st, u8 a1, u8 a2, u32& outMaskReg);

// -----------------------------------------------------------------------------
// 0x41F4F4 — VIBE_Shape_ClassifyType. Classifies a loaded shape/bank blob.
//   VIBE_Util_StrCmp(aShapbank, blob) is a real strcmp (0 on equality); the
//   original branches on the NONZERO (mismatch) result first:
//   - returns 0 for null;
//   - if the first 8 bytes are NOT "SHAPBANK" (a single shape/image): returns
//     17 iff blob[2]==2 (new-format image), else 0;
//   - if the blob IS a "SHAPBANK": inspect blob[9] (subtype) and blob[42..43]
//     (shape count):
//       if blob[9]!=0 OR shapeCount<=1:
//          switch(blob[9]) 1->4, 2->7, 3->8, default->1
//       else -> 5  (multi-frame animation).
// `blob` must point to at least 44 readable bytes.
u8 ShapeClassifyType(const u8* blob);

// -----------------------------------------------------------------------------
// 0x5D8294 — VIBE_ShapeBank_SetPalette. Copies 1024 palette bytes into `palette`
// (the active byte_1406530), and builds the 256-entry packed-colour table
// `packedOut` from the RGB of each palette quad. Returns 1 (0 if src null).
//   NOTE: the original writes packedOut[1..256] (1-based, off-by-one quirk
//   preserved): the first entry written is index 1, the last index 256.
int ShapeBankSetPalette(const ShapeColorState& st, const u8* src1024,
                        u8 palette1024[1024], u16 packedOut[257]);

// -----------------------------------------------------------------------------
// 0x5D8F54 — VIBE_ShapeBank_SetSequenceData. Appends `count` 8-byte sequence
// records to a bank buffer. Bank field offsets (bytes):
//   +48 (u32) writeCursor (total used bytes)
//   +62 (u32) seqDataOffset (lazily = writeCursor on first call)
//   +67 (u16) prevSeqCount
// Copies 8*count bytes from `src` to bank+seqOffset, rewinds writeCursor by
// 8*prevCount, sets prevCount=count, advances writeCursor by 8*count. Returns
// 8*count. `bank` must have room.  (qmemcpy length = 4*((8*count)>>2) == 8*count
// for any count, preserved exactly.)
int ShapeBankSetSequenceData(u8* bank, const u8* src, u16 count);

// -----------------------------------------------------------------------------
// The Grab* family. Each scans an input pixel rectangle, computes the tight
// opaque bounding box, optionally relocates hotspot anchor pixels, then writes
// a shape blob (header @ shp_hdr + per-row RLE/raw payload) into `out`.
//
// Parameters mirror the originals:
//   x0,y0     : top-left of the source sub-rectangle (a1=x in src cols, a2=y row)
//   w,h       : sub-rectangle width (a4) and height (a3)   [NB: a3=h, a4=w!]
//   src       : source pixel buffer
//   typeFlag  : a6, stored at +0x0D
//   srcStride : a7, source row stride in PIXELS
//   st        : colour state (mask/anchor/shift)
//   out       : caller-provided output buffer (size with ShapeGrab*MaxSize).
// Returns the total blob byte size written (the original returns the realloc'd
// header pointer; we return the size and fill `out`). 0 on degenerate input.
//
// The four bound-scan probes and the saturating bounding-box collapse, the
// 8bpp palette mask test, the 16bpp packed-mask test, the 24bpp anchor
// relocation, and the per-row RLE span encoding are all reproduced 1:1.

// Worst-case output size used by the originals' VIBE_Memory_AllocDebug calls.
size_t ShapeGrabBit8MaxSize (int w, int h);   // 4*h + 8*w*h + 50
size_t ShapeGrabBit16MaxSize(int w, int h);   // 8*w*h + 4*h + 50  (h=cols here)
size_t ShapeGrabBit24MaxSize(int w, int h);   // 12*w*h + 4*h + 50

// 0x5D6160 — VIBE_Shape_GrabBit8 (8bpp, RLE). src is u8 palette indices.
size_t ShapeGrabBit8(const ShapeColorState& st, const u8* palette1024,
                     int x0, int y0, int w, int h, const u8* src,
                     u8 typeFlag, u16 srcStride, u8* out);

// 0x5D5908 — VIBE_Shape_GrabBit16 (16bpp, RLE). src is u16 packed pixels.
size_t ShapeGrabBit16(const ShapeColorState& st,
                      int x0, int y0, int w, int h, const u16* src,
                      u8 typeFlag, u16 srcStride, u8* out);

// 0x5D5E7C — VIBE_Shape_GrabBit16NoRle (16bpp, raw rows).
size_t ShapeGrabBit16NoRle(const ShapeColorState& st,
                           int x0, int y0, int w, int h, const u16* src,
                           u8 typeFlag, u16 srcStride, u8* out);

// 0x5D4C20 — VIBE_Shape_GrabBit24 (24bpp, RLE). src is 3-byte RGB pixels.
size_t ShapeGrabBit24(const ShapeColorState& st,
                      int x0, int y0, int w, int h, u8* src,
                      u8 typeFlag, u16 srcStride, u8* out);

// 0x5D547C — VIBE_Shape_GrabBit24NoRle (24bpp, raw rows).
size_t ShapeGrabBit24NoRle(const ShapeColorState& st,
                           int x0, int y0, int w, int h, u8* src,
                           u8 typeFlag, u16 srcStride, u8* out);

// -----------------------------------------------------------------------------
// 0x5D68C4 — VIBE_Shape_GrabByDepth. Dispatch over a source descriptor's depth
// (`srcDepth`) and the `useRle` flag, calls the matching Grab*, then stamps the
// depth code (+0x0C) and typeFlag (+0x0D) into the result. Returns the blob size
// (0 on failure). The descriptor fields used by the original:
//   desc+20 (u8)  bitDepth (8/16/24)
//   desc+28 (ptr) pixels
//   desc+4  (u16) stride
// We pass them explicitly to keep the function self-contained.
size_t ShapeGrabByDepth(const ShapeColorState& st, const u8* palette1024,
                        int x0, int y0, int w, int h,
                        u8 srcDepth, const void* pixels, u16 stride,
                        u8 typeFlag, bool useRle, u8* out);

// -----------------------------------------------------------------------------
// Shape-anim slot table (gilde.exe dword_1406420, 16 slots x 17 bytes = 272).
// Slot byte layout, from VIBE_ShapeAnim_DrawAllSlots / RegisterSlot:
//   +0x00 (u32) objectId/bankPtr (nonzero => active)
//   +0x06 (u8)  shapeNr (passed as a5 to VIBE_Animation_Basic)
//   +0x0D (i16) x  (word_140642D)
//   +0x0F (i16) y  (word_140642F)
constexpr int kShapeAnimSlotCountR  = 16;
constexpr int kShapeAnimSlotStrideR = 17;
constexpr int kShapeAnimTableBytesR = kShapeAnimSlotCountR * kShapeAnimSlotStrideR; // 272

namespace anim_slot {
constexpr size_t kObject  = 0x00;  // u32
constexpr size_t kShapeNr = 0x06;  // u8   (word_1406426 low byte)
constexpr size_t kX       = 0x0D;  // i16  (word_140642D)
constexpr size_t kY       = 0x0F;  // i16  (word_140642F)
}

// 0x5D8E30 — VIBE_ShapeAnim_DrawAllSlots. For each active slot, calls the draw
// callback with (x, y, object, drawArg, shapeNr). `result` is the running
// accumulator passed through (the original returns the last callback result, or
// the initial value if no slot fired). Pure control-flow; the per-slot draw is
// a coupled leaf supplied via `drawSlot`.
using ShapeAnimDrawFn = int (*)(i16 x, i16 y, u32 object, int drawArg,
                                u8 shapeNr, void* ctx);
int ShapeAnimDrawAllSlots(const u8 table[272], int drawArg,
                          ShapeAnimDrawFn drawSlot, void* ctx);

// 0x5D8F1C — VIBE_ShapeAnim_GetSlot. Returns a pointer to slot `n`'s 17 bytes.
const u8* ShapeAnimGetSlot(const u8 table[272], int n);
u8*       ShapeAnimGetSlot(u8 table[272], int n);

// 0x5D8F3C — VIBE_ShapeAnim_GetSlotTable. Returns the table base (the original
// memcpy'd the whole 272-byte table out via SetGrayColorThunk; here the table
// IS the state, so we just return the base pointer).
const u8* ShapeAnimGetSlotTable(const u8 table[272]);

} // namespace guild::render
