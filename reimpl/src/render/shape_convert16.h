#pragma once
#include "guild/common/types.h"
#include "render/colorformat.h"   // ColorFormat / PackColor (0x434f30)

#include <cstddef>

// =============================================================================
// guild::render — the 16bpp SHAPE CONVERTERS (the depth-2/-0 -> depth-1 chain).
//
// 1:1 reconstruction of the lazy load-time conversion gilde.exe runs on every
// SHAPBANK before its shapes can reach the depth-1-only blitters
// (VIBE_Shape_ShowFromBank @0x5d861c -> VIBE_Shape_BlitColored16 @0x5d7164):
//
//   VIBE_State_Helper @0x40e014 (d2_LoadObj)
//     -> VIBE_ShapeBank_ConvertNew  @0x5d80a8   ShapeBankConvertNew   (HERE)
//        -> VIBE_Shape_ConvertToNew @0x5d8080   render_leaves9 Shape_ConvertToNew
//           -> VIBE_Shape_ConvertRgbTo16 @0x5d7c0c  ShapeConvertRgbTo16 (HERE)
//           -> VIBE_Shape_Convert8To16   @0x5d7924  ShapeConvert8To16   (HERE)
//        -> VIBE_ShapeBank_AddShape  @0x5d8330   render/shapebank ShapeBankAddShape
//
// This closes the named gap documented in progress/session-hud.md: every
// gilde.gfx bank ships as pixel-format 2 (24bpp RLE) and was a proven no-op in
// the depth-1 blitter until converted.
//
// GLOBAL-STATE MODEL (struct ShapeConvertState)
// -----------------------------------------------------------------------------
// The originals read process globals; we pass them explicitly (repo style,
// cf. ShapeColorState in shape_recon_cluster.h) plus keep ONE process-global
// instance (ActiveShapeConvertState) mirroring the engine's single global set,
// for the RenderLeaves9Hooks trampolines:
//   byte_762719..76271E  per-channel shift table  -> ColorFormat fmt
//                        (PackColor == VIBE_Result_Handler_Final @0x434f30)
//   word_1406944         the half-bright AND mask -> u16 darkMask
//                        (built by VIBE_Shape_InitColorMasks @0x5d4ad4:
//                         ((1<<(7-prec))-1)<<pos per channel; 0x7BEF for 565)
//   byte_1406530[1024]   the active 8bpp palette (RGBX quads, stride 4)
//                        -> palette1024 (zero-filled default == the .bss image)
//   VIBE_ErrorLog_ReportMessage @0x438da8 -> reportMessage hook (inert default)
//
// SHAPE RECORD OFFSETS (shared with shape_recon_cluster.h shp_hdr):
//   +0x00 u32 size  +0x06 u16 width  +0x0A u16 height  +0x0C u8 depth
//   +0x0D u8 typeFlag (1 => half-bright)  +0x26 u32 spanFlag (-1 == raw bitmap)
//   +0x2A u32 rowTableOffset  +0x2E u32 opaquePixelCount  +0x32 payload
//
// ALLOCATION: the originals return VIBE_Memory_AllocDebug blocks ("d2:shp:
// NewShape" / "d2:shp:NewBank"); here the converters return std::malloc blocks
// the caller releases with std::free (ShapeBankConvertNew frees its temporaries
// with std::free exactly where the original calls VIBE_Memory_FreeDebug).
//
// STACK-BUFFER BOUNDS (faithful): the originals stage one row of packed pixels
// in a WORD[1152] stack array and the row-offset table in a DWORD[864] stack
// array; shapes wider than 1152 px or taller than 864 rows overran the stack in
// gilde.exe and are equally out of contract here (the real assets max 800x600).
// =============================================================================
namespace guild::render {

// The converter global-state model (see above).
struct ShapeConvertState {
    ColorFormat fmt{};                  // byte_762719..76271E
    u16 darkMask = 0;                   // word_1406944
    const u8* palette1024 = nullptr;    // byte_1406530 (RGBX quads); null == zeros
    void (*reportMessage)(const char* msg) = nullptr;  // VIBE_ErrorLog_ReportMessage
};

// word_1406944 as VIBE_Shape_InitColorMasks @0x5d4ad4 derives it from the shift
// table:  ((1<<(7-bPrec))-1)<<bPos | ((1<<(7-gPrec))-1)<<gPos |
//         ((1<<(7-rPrec))-1)<<rPos.   (565 -> 0x7BEF, 555 -> 0x3DEF.)
u16 ShapeConvertDarkMask(const ColorFormat& f);

// The process-global state the leaves9 trampolines read (the engine's single
// global set). Defaults: Format565 + matching darkMask, zero palette, inert log.
ShapeConvertState& ActiveShapeConvertState();
void SetActiveShapeConvertState(const ShapeConvertState& st);

// gilde.exe 0x5d7c0c — VIBE_Shape_ConvertRgbTo16 (__usercall eax = fn(shape@eax)).
// Convert a 24bpp shape to a 16bpp one. Returns a malloc'd new shape blob.
//   * RLE branch (spanFlag @+38 != -1): per row { u32 runCount; runs { u32 skip;
//     u32 n; px[n] } }: skip is re-encoded skip*2/3 (unsigned; 3-byte px -> 2-byte
//     px gap), each pixel packs via PackColor(fmt, p[0], p[1], p[2]); a pixel that
//     packs to 0 is replaced by PackColor(fmt,5,5,5) (0 is the transparent slot);
//     when typeFlag @+13 == 1 the pixel is then halved: (px>>1) & darkMask.
//     Header: size@0 rebuilt, depth@12=1, spanFlag@38 = running run count,
//     opaque@46 = running pixel count; the row-offset table (u32 per row,
//     shape-relative) is appended at the end and rowTableOffset@42 points at it.
//   * RAW branch (spanFlag == -1, full bitmap): width*height 3-byte pixels pack
//     row-by-row (NO 0 -> (5,5,5) replacement; half-bright still applies);
//     spanFlag stays -1, rowTableOffset@42 = 0, opaque@46 = 0.
u8* ShapeConvertRgbTo16(const ShapeConvertState& st, const u8* shape);

// gilde.exe 0x5d7924 — VIBE_Shape_Convert8To16 (__usercall eax = fn(shape@eax)).
// Convert an 8bpp (palette-indexed) RLE shape to 16bpp. Builds a 256-entry LUT
// from the palette quads (lut[i] = PackColor(fmt, pal[4i], pal[4i+1], pal[4i+2])),
// re-encodes each run with skip*2 (1-byte px -> 2-byte px gap) and px = lut[idx]
// (no 0-replacement; half-bright as above). A RAW (spanFlag == -1) shape is NOT
// supported: reports "shp_Convert8To16: Converting of NoReadAndSkip Shapes not
// surported..." through st.reportMessage and returns null, like the original.
u8* ShapeConvert8To16(const ShapeConvertState& st, const u8* shape);

// gilde.exe 0x5d80a8 — VIBE_ShapeBank_ConvertNew (__usercall eax = fn(bank@eax,
// target@dl, keepSource@ebx)). Convert a whole SHAPBANK to pixel format 1:
//   * returns null when the bank's write cursor (+48) is 0;
//   * returns `bank` itself unchanged unless (format@52 == 2 or 0) && target == 1;
//   * else allocates writeCursor - opaqueSum (fmt 2) / + opaqueSum (fmt 0) bytes,
//     copies the 0x845-byte header region, resets cursor/count, stamps format 1,
//     then per shape runs render_leaves9 Shape_ConvertToNew(shape, 1) (@0x5d8080,
//     whose hooks InstallShapeConvertersIntoLeaves9 points at the two converters
//     above) and appends via the real ShapeBankAddShape (@0x5d8330), freeing each
//     temporary; trailing sequence data (+62 offset / +67 count, 8-byte records)
//     is re-appended at the new cursor.
//   * when !keepSource (original ebx == 0) the source bank is std::free'd, so
//     pass keepSource=true for banks you do not own through malloc.
// The returned bank is malloc'd (free with std::free) unless it == `bank`.
u8* ShapeBankConvertNew(u8* bank, i8 target, bool keepSource);

// Rule-13 wiring: point RenderLeaves9Hooks::ConvertRgbTo16 / ::Convert8To16 at
// the two real converters above (reading ActiveShapeConvertState), leaving the
// other hook slots untouched. Idempotent. After this, render_leaves9's
// Shape_ConvertToNew (the 1:1 0x5d8080 driver) and ShapeBankConvertNew run the
// REAL conversion chain end-to-end.
void InstallShapeConvertersIntoLeaves9();

} // namespace guild::render
