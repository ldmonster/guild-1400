#pragma once
#include "guild/common/types.h"
#include "render/surface.h"

// =============================================================================
// guild::render — software triangle rasterizer (the heart of d3_engine.c).
//
// Faithful 1:1 reconstruction of gilde.exe's software rasterizer cluster:
//   0x5F7D58  VIBE_Raster_RasterizeTexturedTriangle  (edge-walk + span setup)
//   0x5F7960  VIBE_Raster_FillTexturedSpansShaded    (scanline span loop)
//   0x5F7840  VIBE_Raster_InterpolateEdgeZTex        (long-edge interpolator)
//   0x5F6A8C  VIBE_Raster_InterpolateEdgeZ           (short-edge X interpolator)
//   0x5F71AD  VIBE_Raster_FillSpanTextured           (16-bit textured inner span)
//   0x5F721A  VIBE_Raster_FillSpanTexturedMasked     (16-bit, colour-key)
//   0x5F7500.. VIBE_Raster_PatchSpanConstants*       (self-modifying patchers)
//   0x603DA8  VIBE_Raster_FillSpans / 0x603D00 ComputeEdgeSlope  (flat fill)
//
// ALL interpolation is 16.16 FIXED-POINT (u/v/x/light/z). The original kept the
// edge-walk and span accumulators in a block of file-scope globals at 0x13FC5xx
// (see ORIGINAL GLOBAL MAP below). To keep this reconstruction re-entrant and
// bit-exactly testable we gather those globals into one explicit RasterState
// record; every field carries the original global symbol it mirrors and the
// arithmetic is otherwise verbatim (same 64-bit intermediate widening, same
// `(x+0xFFFF)>>16` ceil-to-pixel rounding, same wraparound).
//
// ---------------------------------------------------------------------------
// SELF-MODIFYING SPAN CODE (documented; reconstructed as a parameterised loop)
// ---------------------------------------------------------------------------
// The original 16-bit textured inner spans (FillSpanTextured & 5 siblings) are
// *patched at run time*: before each batch the PatchSpanConstants* routines
// (0x5F7500..) overwrite the immediate operands embedded in the span machine
// code with the live texture base, texel-index mask, palette/light-table base,
// the U-step low word and the texture-width shift count, e.g.
//     loc_5F71EF:  mov dl, [ebx+12345678h]   ; 12345678h <- texture base ptr
//     loc_5F7202:  mov cx,  word ds:1234567h[edx*2] ; <- palette/light table
//     loc_5F71E4:  and ebx, 12345678h        ; <- texel-index mask
//     loc_5F71EA:  add eax, 12345678h         ; <- U fractional step
//     loc_5F71C7:  shl ebx, 12h               ; 12h  <- texture-width log2 shift
// Self-modification was a 1990s speed hack: hoist per-triangle constants out of
// the per-pixel loop into immediates (no register pressure, no memory loads).
// We translate it to an equivalent *parameterised* inner loop: the patched
// immediates become the fields of SpanTexParams, passed by reference. Behaviour
// is identical bit-for-bit; only the delivery mechanism (struct field vs. code
// immediate) differs. See FillSpanTextured() in raster.cpp.
//
// ---------------------------------------------------------------------------
// MMX BILINEAR BLOCK @0x5F76E2 (VIBE_Raster_BilinearBlendBlockMmx)
// ---------------------------------------------------------------------------
// A *texture-block magnifier*, NOT part of the per-triangle span path: it takes
// 4 source row pointers and bilinearly interpolates a rectangular block into a
// 16-bit destination, using three precomputed channel-spread LUTs
// (dword_1404260/1404660/1404A60 — R/G/B field expansion tables) and per-step
// fractional weights packed in mm6. It has a scalar fast path (taken when all 4
// source rows are identical -> point sample) and an MMX path (pmaddwd of the
// 4 corner texels by the bilinear weight vector, >>8 per channel, recombined
// via the channel LUTs). We provide a portable *scalar* reconstruction
// (BilinearBlendBlock) that reproduces the same per-channel maths; it is
// documented and unit-tested but the engine only uses it for texture upload, so
// the triangle rasterizer never calls it.
// =============================================================================
namespace guild::render {

// ---------------------------------------------------------------------------
// __ROR4__ — 32-bit rotate-right, the intrinsic Hex-Rays emits for the x86
// `ror reg, n`. Used in FillTexturedSpansShaded to keep the 16.16 U accumulator
// in "value-in-high-word, fraction-in-low-word" form so an `adc`-based add can
// carry-propagate the sub-pixel step into the integer part for free.
// ---------------------------------------------------------------------------
inline u32 Ror4(u32 v, unsigned n) {
    n &= 31u;
    return n ? ((v >> n) | (v << (32u - n))) : v;
}

// ---------------------------------------------------------------------------
// RasterState — the 16.16 edge-walk / span accumulator block. Mirrors the
// file-scope globals at gilde.exe 0x13FC5xx (the "d3:raster" scratch area).
// Per-vertex input arrays are indexed by the 0..2 triangle vertex index.
// ---------------------------------------------------------------------------
struct RasterState {
    // -- per-vertex inputs (set up by RasterizeTexturedTriangle) -------------
    i32 vx[3];      // dword_13FC5B0[]  screen X, 16.16
    i32 vy[3];      // dword_13FC59C[]  screen Y, 16.16
    i32 vlight[3];  // dword_13FC578[]  light/shade value, 16.16  (<<16 of byte)
    i32 vg[3];      // dword_13FC55C[]  extra channel (Rgbz variant)
    i32 vb[3];      // dword_13FC550[]  extra channel (Rgbz variant)

    // -- edge accumulators (output of the InterpolateEdge* helpers) ----------
    i32 xLeft;      // dword_13FC5D8  left edge X, 16.16
    i32 xLeftStep;  // dword_13FC5E8  d(xLeft)/dy, 16.16
    i32 xRight;     // dword_13FC5BC  right edge X, 16.16
    i32 xRightStep; // dword_13FC5C8  d(xRight)/dy, 16.16
    i32 uLeft;      // dword_13FC5F4  left edge U (light/shade), 16.16
    i32 uLeftStep;  // dword_13FC5C4  d(uLeft)/dy, 16.16
    i32 uGrad;      // dword_13FC590  horizontal dU/dx across a span, 16.16

    // -- Rgbz-variant extra edge accumulators --------------------------------
    i32 gLeft;      // dword_13FC5F0
    i32 gLeftStep;  // dword_13FC5CC
    i32 bLeft;      // dword_13FC5EC
    i32 bLeftStep;  // dword_13FC5D0

    // -- per-span scratch ----------------------------------------------------
    i32 spanLen;    // dword_13FC588  pixel count of the current span

    // -- destination -----------------------------------------------------------
    u8* fbBase;     // dword_13FC5DC  framebuffer cursor (start-of-frame row 0)
    i32 fbPitch;    // a2 / stride in pixels (passed to span fillers)
    u8* shadeBase;  // dword_1408AA4  24-byte/pixel parallel shading buffer
    i32 blurRadius; // dword_1408AA0  shade-buffer blur spread radius
};

// ---------------------------------------------------------------------------
// SpanTexParams — the constants the original PATCHED into the span machine code
// (see "SELF-MODIFYING SPAN CODE" above). Reconstructed as plain parameters.
// ---------------------------------------------------------------------------
struct SpanTexParams {
    const u8*  texBase;     // loc_5F71EF imm   = dword_1406A8C  texel byte array
    const u16* palBase;     // loc_5F7202 imm   = dword_1406A78  index->16bpp LUT
    u32        texelMask;   // loc_5F71E4 imm   = dword_1406A88  (V*W+U) wrap mask
    i32        uStepFrac;   // loc_5F71EA imm   = dword_13FC594  U step (16.16)
    u8         widthShift;  // loc_5F71C7 imm   = byte_1407A91   log2(texWidth)+?
    i32        lightStart;  // dword_13FC5A8     light accumulator start
    i32        vStep;       // dword_13FC5E0     V step (16.16) (added via dword_13DCE54[])
};

// Per-vertex source for one triangle: screen x/y (float), light byte, and (for
// the textured path) the U/V/light fixed-point start + gradients. The original
// read these from the Vertex records + global edge tables; we pass them in.
struct RasterVertex {
    float x;     // screen X (pixels) — scaled by 65536 into 16.16 internally
    float y;     // screen Y (pixels)
    u8    light; // light/shade value -> <<16 into vlight[]
};

// ---------------------------------------------------------------------------
// Edge interpolators.
// ---------------------------------------------------------------------------

// gilde.exe 0x5F7840 — VIBE_Raster_InterpolateEdgeZTex (a@eax, b@edx).
// Sets up xLeft/xLeftStep AND uLeft/uLeftStep for the long left edge a->b.
void InterpolateEdgeZTex(RasterState& rs, int a, int b);

// gilde.exe 0x5F6A8C — VIBE_Raster_InterpolateEdgeZ (a@eax, b@edx).
// Sets up xRight/xRightStep for the short right edge a->b.
void InterpolateEdgeZ(RasterState& rs, int a, int b);

// ---------------------------------------------------------------------------
// Span fillers.
// ---------------------------------------------------------------------------

// gilde.exe 0x5F71AD — VIBE_Raster_FillSpanTextured. The (formerly self-
// modifying) 16-bit textured inner span. dst = pixel row pointer (16bpp),
// u = U accumulator start (16.16), v = V accumulator start (16.16). Writes
// rs.spanLen texels. Parameterised by `p` (see SpanTexParams).
void FillSpanTextured(RasterState& rs, u16* dst, i32 u, i32 v,
                      const SpanTexParams& p);

// gilde.exe 0x5F721A — VIBE_Raster_FillSpanTexturedMasked. As above but skips
// texels whose source index is 0 (colour-key transparency).
void FillSpanTexturedMasked(RasterState& rs, u16* dst, i32 u, i32 v,
                            const SpanTexParams& p);

// ---------------------------------------------------------------------------
// Top-level triangle rasterizers.
// ---------------------------------------------------------------------------

// gilde.exe 0x5F7960 (core) — VIBE_Raster_FillTexturedSpansShaded.
// Walks `rowCount` scanlines from screen row y0, emitting the 8-bit shaded
// affine span: each pixel byte = integer part of the U/shade accumulator
// (uLeft + uGrad*subpixel), stepped by uGrad across the span. `fb` is the
// destination 8-bit surface; writes are clipped to fb's clip rect.
// Returns the last filled row index, or -1 if nothing was drawn.
int FillTexturedSpansShaded(RasterState& rs, Surface* fb, int rowCount, int y0);

// gilde.exe 0x5F7D58 — VIBE_Raster_RasterizeTexturedTriangle.
// Full pipeline: sort the 3 vertices by Y, pick the long edge, set up both
// edge interpolators and the horizontal U gradient (dword_13FC590), then fill
// the top and bottom sub-triangles via FillTexturedSpansShaded. `v[3]` are the
// screen-space vertices; writes the affine-shaded triangle into `fb`.
// Returns nonzero if any span was drawn. Back-face (signed area) handling and
// the vertex Y-sort exactly mirror the original.
int RasterizeTexturedTriangle(Surface* fb, const RasterVertex v[3]);

// ---------------------------------------------------------------------------
// Flat-shaded triangle (gilde.exe 0x603DA8/0x603D00 family — used for shadows /
// solid fills). Fills the triangle with a single 8-bit colour index.
// ---------------------------------------------------------------------------
int RasterizeFlatTriangle(Surface* fb, const RasterVertex v[3], u8 color);

// ---------------------------------------------------------------------------
// Portable scalar reconstruction of the MMX bilinear block (0x5F76E2).
// Bilinearly samples a `w`x`h` destination block from 4 source row pointers,
// writing 16-bit packed colours. Documented; not on the triangle path.
// ---------------------------------------------------------------------------
void BilinearBlendBlock(u16* dst, const u8* const srcRows[4], int w, int h,
                        int srcStep, int dstStep,
                        const u32 rLut[256], const u32 gLut[256],
                        const u32 bLut[256], u16 weightLow, u16 weightHigh);

} // namespace guild::render
