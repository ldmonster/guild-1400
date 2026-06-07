#include "render/raster_blend.h"

// =============================================================================
// guild::render — raster blend / OR span variants (implementation).
//
// Each variant shares the affine texel-fetch of FillSpanTextured (raster.cpp):
//   uInt = u >> 16 ; vInt = v >> 16
//   addr = ((vInt << widthShift) + uInt) & texelMask
//   idx  = texBase[addr] ; src = palBase[idx]
//   u += uStepFrac ; v += vStep
// and differ only in the destination combine. The original kept U in a ROR'd
// high/low split so an `adc` carried the sub-pixel fraction into the integer
// part for free; we reproduce the value with a plain 16.16 accumulator (the
// emitted pixels are bit-identical). The original returns the U accumulator
// after the span (result = a2<<16 advanced by uStepFrac per pixel) — we return
// the same final value.
// =============================================================================
namespace guild::render {

// gilde.exe 0x5F728A — VIBE_Raster_FillSpanTexturedBlend.
i32 FillSpanTexturedBlend(RasterState& rs, u16* dst, i32 u, i32 v,
                          const SpanBlendParams& p) {
    int n = rs.spanLen;
    for (int i = 0; i < n; ++i) {
        i32 uInt = u >> 16;
        i32 vInt = v >> 16;
        u32 addr = ((((u32)vInt) << p.widthShift) + (u32)uInt) & p.texelMask;
        u8  idx  = p.texBase[addr];
        u16 src  = p.palBase[idx];
        // dst = ((src>>1) & mask) + ((dst>>1) & mask)  (no inter-channel carry)
        u16 cur  = dst[i];
        dst[i] = (u16)((u16)((src >> 1) & p.blendMask) + (u16)((cur >> 1) & p.blendMask));
        u += p.uStepFrac;
        v += p.vStep;
    }
    return u;
}

// gilde.exe 0x5F7310 — VIBE_Raster_FillSpanTexturedBlendMasked.
i32 FillSpanTexturedBlendMasked(RasterState& rs, u16* dst, i32 u, i32 v,
                                const SpanBlendParams& p) {
    int n = rs.spanLen;
    for (int i = 0; i < n; ++i) {
        i32 uInt = u >> 16;
        i32 vInt = v >> 16;
        u32 addr = ((((u32)vInt) << p.widthShift) + (u32)uInt) & p.texelMask;
        u8  idx  = p.texBase[addr];
        if (idx != 0) {                                  // colour key (test dl,dl)
            u16 src = p.palBase[idx];
            u16 cur = dst[i];
            dst[i] = (u16)((u16)((src >> 1) & p.blendMask) + (u16)((cur >> 1) & p.blendMask));
        }
        u += p.uStepFrac;
        v += p.vStep;
    }
    return u;
}

// gilde.exe 0x5F739C — VIBE_Raster_FillSpanTexturedOr.
i32 FillSpanTexturedOr(RasterState& rs, u16* dst, i32 u, i32 v,
                       const SpanBlendParams& p) {
    int n = rs.spanLen;
    for (int i = 0; i < n; ++i) {
        i32 uInt = u >> 16;
        i32 vInt = v >> 16;
        u32 addr = ((((u32)vInt) << p.widthShift) + (u32)uInt) & p.texelMask;
        u8  idx  = p.texBase[addr];
        dst[i] |= p.palBase[idx];                        // *(_WORD*)... |= v9
        u += p.uStepFrac;
        v += p.vStep;
    }
    return u;
}

// gilde.exe 0x5F740A — VIBE_Raster_FillSpanTexturedOrMasked.
i32 FillSpanTexturedOrMasked(RasterState& rs, u16* dst, i32 u, i32 v,
                             const SpanBlendParams& p) {
    int n = rs.spanLen;
    for (int i = 0; i < n; ++i) {
        i32 uInt = u >> 16;
        i32 vInt = v >> 16;
        u32 addr = ((((u32)vInt) << p.widthShift) + (u32)uInt) & p.texelMask;
        u8  idx  = p.texBase[addr];
        if (idx != 0)                                    // colour key
            dst[i] |= p.palBase[idx];
        u += p.uStepFrac;
        v += p.vStep;
    }
    return u;
}

} // namespace guild::render
