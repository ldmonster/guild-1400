#pragma once
#include "guild/common/types.h"
#include "render/raster.h"

// =============================================================================
// guild::render — raster blend / OR span variants (the translucent + raster-op
// inner spans of the software textured-triangle rasterizer, d3_engine.c).
//
// Faithful 1:1 reconstruction of gilde.exe's four remaining (formerly self-
// modifying) 16-bit textured inner span fillers:
//
//   0x5F728A  VIBE_Raster_FillSpanTexturedBlend        (50/50 alpha blend)
//   0x5F7310  VIBE_Raster_FillSpanTexturedBlendMasked  (blend + colour-key)
//   0x5F739C  VIBE_Raster_FillSpanTexturedOr           (OR raster-op)
//   0x5F740A  VIBE_Raster_FillSpanTexturedOrMasked     (OR + colour-key)
//
// These share the EXACT affine texel-fetch machinery of FillSpanTextured
// (render/raster.{h,cpp}) — same 16.16 U/V accumulators, same patched-immediate
// texture base / texel-index mask / palette LUT / U-step / V-step delivered via
// SpanTexParams. They differ only in how the fetched 16-bit colour is combined
// with the destination pixel:
//
//   Blend  : dst = ((src >> 1) & blendMask) + ((dst >> 1) & blendMask)
//   Or     : dst |= src
//   *Masked: as above but skip texels whose source index byte == 0 (colour key)
//
// In the original the blend mask was the patched immediate `word_1234567`
// (e.g. 0xF7DE for RGB565, 0x7BDE for RGB555 — it clears the LSB of each colour
// field so the >>1 average cannot carry between channels). We pass it as
// `blendMask` in SpanBlendParams. The original return value (the U accumulator
// after the span) is reproduced.
//
// The original spans walk a NEGATIVE byte index from -2*spanLen up to 0 over the
// row end pointer (a1 + 2*spanLen), i.e. they fill spanLen pixels left-to-right.
// We reproduce the identical pixel order and the identical fixed-point stepping
// (U via the carry-propagating high/low split, V via dword_13FC5E0).
// =============================================================================
namespace guild::render {

// The patched-immediate constants for a blend/OR span. Shares the texel-fetch
// fields of SpanTexParams (texBase/palBase/texelMask/uStepFrac/widthShift/vStep)
// and adds the blend mask (`word_1234567`) used by the two blend variants.
struct SpanBlendParams {
    const u8*  texBase;     // loc_5F72CB imm = dword_1406A8C  texel byte array
    const u16* palBase;     // word_1234567 (palette LUT base)  = dword_1406A78
    u32        texelMask;   // loc_5F72C0 imm = dword_1406A88   (V*W+U) wrap mask
    i32        uStepFrac;   // dword_13DCE54[] U fractional step (16.16)
    u8         widthShift;  // texture-width log2 shift folded into the V index
    i32        vStep;       // dword_13FC5E0  V step (16.16)
    i32        uStart;      // dword_13FC5A8  U accumulator start
    u16        blendMask;   // word_1234567   per-field LSB-clear mask (blend only)
};

// gilde.exe 0x5F728A — VIBE_Raster_FillSpanTexturedBlend.
// dst row = `dst` (16bpp); fills rs.spanLen pixels, blending each fetched texel
// 50/50 with the existing destination pixel. `u`/`v` are the 16.16 U/V starts.
i32 FillSpanTexturedBlend(RasterState& rs, u16* dst, i32 u, i32 v,
                          const SpanBlendParams& p);

// gilde.exe 0x5F7310 — VIBE_Raster_FillSpanTexturedBlendMasked. As blend but
// skips texels whose source index byte == 0.
i32 FillSpanTexturedBlendMasked(RasterState& rs, u16* dst, i32 u, i32 v,
                                const SpanBlendParams& p);

// gilde.exe 0x5F739C — VIBE_Raster_FillSpanTexturedOr. dst |= fetched texel.
i32 FillSpanTexturedOr(RasterState& rs, u16* dst, i32 u, i32 v,
                       const SpanBlendParams& p);

// gilde.exe 0x5F740A — VIBE_Raster_FillSpanTexturedOrMasked. OR + colour-key.
i32 FillSpanTexturedOrMasked(RasterState& rs, u16* dst, i32 u, i32 v,
                             const SpanBlendParams& p);

} // namespace guild::render
