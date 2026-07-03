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
// WrapAddI32 — the 32-bit two's-complement `add` the x86 span loops use to step
// the 16.16 U/V/fog accumulators. Plain `i32 += i32` is signed-overflow UB in
// C++ when the accumulator approaches INT_MAX (e.g. a steep gradient near the
// edge of the fixed-point range); the original `add reg, step` simply wraps mod
// 2^32. Doing the add in the unsigned domain reproduces that wrap bit-for-bit
// with no UB. HARDENING wave-10 (UBSAN: signed integer overflow in the span
// accumulator advance).
inline i32 WrapAddI32(i32 a, i32 b) { return (i32)((u32)a + (u32)b); }

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

    // -- PER-PIXEL FOG channel (wave-7 W7-FOGPIX) ----------------------------
    // The D3D fixed-function VERTEX fog factor (the FVF specular byte stored at
    // vertex+79 by ComputeFogFactor @0x5ac9aa/@0x5beb0b) is interpolated LINEARLY
    // across the triangle by D3D and blended per pixel toward the fog colour.
    // The faithful software realisation carries the factor as a THIRD 16.16 span
    // channel alongside U/V: `fStart` is the left-edge factor accumulator (16.16,
    // the factor*65536 of the covered scanline's left pixel) and `fGrad` is the
    // horizontal dF/dx step. When `fPerPixel` is set the textured span body steps
    // fStart per pixel and uses (clamped) fStart>>16 as the per-pixel fog factor;
    // when it is clear the span falls back to the per-triangle constant
    // SpanFog().factor (the wave-6 first-cut path — kept byte-identical for direct
    // FillSpanTextured callers that do not seed the channel). All three are
    // zero/false on a default RasterState, so non-fog spans are unaffected.
    i32  fStart;    // left-edge interpolated fog factor, 16.16
    i32  fGrad;     // horizontal dF/dx, 16.16
    bool fPerPixel; // drive the per-pixel factor from fStart/fGrad

    // -- PER-PIXEL GOURAUD RGB shade channel (D3D diffuse modulate) -----------
    // The per-vertex RGB diffuse shade (vertex +70/+69/+68, the software COLOUR
    // branch of FinalizeVertexShade @0x5c8218) interpolated across the span and
    // MULTIPLIED into each resolved texel — the hardware path's Gouraud texture
    // modulate realised in the software span (lantern pools / tinted night
    // ambient). All zero/false on a default RasterState -> byte-identical spans.
    i32  rStart, gStart, bStart;   // left-edge shade accumulators, 16.16
    i32  rGrad,  gGrad,  bGrad;    // horizontal gradients, 16.16
    bool gPerPixel;                // arm the per-pixel modulate

    // -- destination -----------------------------------------------------------
    u8* fbBase;     // dword_13FC5DC  framebuffer cursor (start-of-frame row 0)
    i32 fbPitch;    // a2 / stride in pixels (passed to span fillers)
    u8* shadeBase;  // dword_1408AA4  tile-type stamp cursor: the 24-BYTE-STRIDE
                    //   terrain tile-record array (BuildTerrainMesh's [esi+24h];
                    //   one type byte per 0x18-byte tile cell). Set to
                    //   tileBase + 24*y0*fbPitch before a fill; advanced by
                    //   24*fbPitch per row exactly like the original cursor.
    i32 blurRadius; // dword_1408AA0  tile-stamp halo spread radius (a7)
};

// ---------------------------------------------------------------------------
// SpanTexParams — the constants the original PATCHED into the span machine code
// (see "SELF-MODIFYING SPAN CODE" above). Reconstructed as plain parameters.
// ---------------------------------------------------------------------------
struct SpanTexParams {
    const u8*  texBase;     // loc_5F71EF imm   = dword_1406A8C  texel byte array
    const u16* palBase;     // loc_5F7202 imm   = dword_1406A78  the bound HiColTab
                            //   data block: u16 entry = palBase[lightRow8 | texel]
    u32        texelMask;   // loc_5F71E4 imm   = dword_1406A88  (V<<shift)+U wrap mask
    i32        uStepFrac;   // FULL 16.16 U step. Original split: frac<<16 in the
                            //   loc_5F71EA imm (= dword_13FC594 = uGrad<<16) +
                            //   integer part inside the combined texel-index delta
                            //   dword_13DCE54[0] (with the frac carry fed through
                            //   the `adc`). Proven bit-equal (see raster.cpp note).
    u8         widthShift;  // loc_5F71C7 imm   = byte_1407A91   log2(texWidth)
    i32        lightStart;  // loc_5F71FC imm   = dword_13FC5A8 = vGrad<<16, the V
                            //   accumulator FRACTIONAL step. Kept only as the
                            //   patcher-state mirror (texraster_recon2_spanpatch);
                            //   the reconstructed loops carry the full step in
                            //   vStep instead — identical addressing.
    i32        vStep;       // FULL 16.16 V step (frac imm dword_13FC5A8 + integer
                            //   part inside dword_13DCE54[0]/dword_13DCE50, the
                            //   `(dVint<<shift)+dUint` / `+width on V-frac-carry`
                            //   combined delta pair). Proven bit-equal.
    // COLOUR-KEY (wave-5 W5-CKEY): the masked span's transparency rule. The
    // original software masked span (FillSpanTexturedMasked @0x5F721A) keys on
    // SOURCE INDEX 0 (`test dl,dl / jz`). For a 24-bit colour-keyed texture the
    // engine's OBSERVABLE behaviour comes from the DDraw path
    // (VIBE_Render_LoadAndStretchTexture @0x5dea50): the transparent-surface
    // descriptor (record +104 bit 2 -> &unk_14080C4) does a DDBLT_KEYSRC blit
    // (&unk_1000000, 0x5df086) keyed on `v111 = pal[0]` — and for a 24-bit
    // source the palette buffer is memset-0 and never rebuilt (the 24-bit arm of
    // VIBE_Bmp_LoadBuffer @0x5f0ce4 takes the direct-RGB passthrough, no
    // VIBE_Quant_BuildPalette), so the key value is (0,0,0) == BLACK. The
    // octree quantizer (VIBE_Quant_TreeCollectPalette @0x603180, DFS child bit
    // 7->0) places the all-zero (black) leaf at a HIGH index, NOT index 0, so
    // "skip index 0" does not coincide with black. The faithful software
    // realisation of the DDraw colour-key is therefore: skip texels whose
    // RESOLVED 16bpp value == colorKey565 (the 565 encoding of black == 0).
    //
    //   useColorKey == false (default): the masked span keeps the EXACT original
    //     `idx != 0` rule — byte-identical for every existing caller/test.
    //   useColorKey == true: skip when palBase[lightRow8 | idx] == colorKey565
    //     (the DDraw-on-black colour-key; see W5-CKEY).
    bool       useColorKey = false; // wave-5: route on resolved colour, not idx
    u16        colorKey565 = 0;     // resolved 565 key (black == 0)

    u32        lightRow8 = 0; // dword_13FC5E0 — the palette LIGHT-ROW selector,
                            //   ((l0+l1+l2)/3) << 8 of the three vertex +66 light
                            //   bytes (computed at 0x5F70BD). Loaded into edx
                            //   before the span loop; the per-pixel `mov dl,
                            //   texel` only replaces the low byte, so the colour
                            //   fetch is palBase[lightRow8 | texel].
                            //
                            //   PALETTE / +72 LAYOUT (VERIFIED wave-5):
                            //   palBase = *(tex+72) (dword_1406A78, set by
                            //   BindActive @0x5DB5C1). +72 is a POINTER to the
                            //   shared HiColTab data block built by
                            //   VIBE_HiColTab_FindOrBuild @0x5DA04C (allocated
                            //   0x8200 bytes) / AddEntry @0x5D9DB8 — NOT a private
                            //   per-texture light array. Block layout:
                            //     row L (L=0..62) at u16 offset 256*L (byte 512*L)
                            //       = a 63-row brightness ramp (channel =
                            //         ch/62*L + 0.5, dbl_6295E8=0.5);
                            //     direct 565 entries at byte 32256 (u16 16128).
                            //   So palBase[(avg<<8)|texel] == element 256*avg +
                            //   texel == ramp row `avg`, entry `texel`.
                            //   => lightRow8 = avg<<8 selects RAMP ROW `avg`; the
                            //   VALID lightRow (avg) range is 0..62 (63 rows).
                            //   0 == row 0 (full-shadow/black end of the ramp).
                            //   The +66 light bytes feeding the avg are scaled
                            //   into 0..62 by the lighting stage (the table has no
                            //   rows beyond 62; an avg of 254 would over-index).
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
// rs.spanLen texels: dst[i] = palBase[lightRow8 | texel] (the edx register
// held dword_13FC5E0 = lightRow8 and `mov dl, texel` replaced only its low
// byte). Parameterised by `p` (see SpanTexParams).
void FillSpanTextured(RasterState& rs, u16* dst, i32 u, i32 v,
                      const SpanTexParams& p);

// gilde.exe 0x5F721A — VIBE_Raster_FillSpanTexturedMasked. As above but skips
// texels whose source index is 0 (`test dl,dl / jz` — colour-key transparency).
void FillSpanTexturedMasked(RasterState& rs, u16* dst, i32 u, i32 v,
                            const SpanTexParams& p);

// ---------------------------------------------------------------------------
// Top-level triangle rasterizers.
// ---------------------------------------------------------------------------

// gilde.exe 0x5F7960 — VIBE_Raster_FillTexturedSpansShaded
//   (__userpurge: a1=rowCount@eax, a2=pitch/width@edx, a3=firstBatch@cl,
//    a4=modeMask@bl, a5=y0 on stack).
// Walks `rowCount` scanlines from row y0. Per row, the span is
// [ceil(xLeft), ceil(xRight)) (dword_13FC588 = right-left). Three modes, gated
// by `modeMask` (the caller-masked a4/a5 byte):
//   bit 1 (a4 & 2): write the shaded BYTE span into the byte map (`fb`, one
//     byte per cell at row*pitch + x): each byte = the integer part of the
//     U/shade accumulator, advanced with the original's ROR'd `adc` chain (the
//     sub-pixel carry lands ONE PIXEL LATE relative to plain 16.16 accumulation
//     — reproduced exactly; see raster.cpp).
//   bits 0/2 (a4 & 5): stamp a tile-TYPE byte into the 24-byte-stride tile
//     array (rs.shadeBase cursor): value 11 when (a4&1)==0, else 0. With
//     rs.blurRadius > 0 each row also stamps a width-clamped halo row
//     ([x-blur, x+len+blur) clamped to [0,pitch)), the FIRST nonempty span of a
//     firstBatch!=0 call stamps a pyramid halo over the `blur` rows above, and
//     after the walk a pyramid halo is stamped below the LAST nonempty span.
// `firstBatch` mirrors a3 (1 for the top sub-triangle, 0 for the bottom).
// Returns the last filled row index, or -1 if no span was emitted (the
// original's return value is dead; v34's >=0/-1 role is preserved).
int FillTexturedSpansShaded(RasterState& rs, Surface* fb, int rowCount, int y0,
                            int firstBatch = 1, u8 modeMask = 2);

// gilde.exe 0x5F7D58 — VIBE_Raster_RasterizeTexturedTriangle
//   (a1=verts@eax, a2=lightBytes@edx, a3=width@ecx, a4=byteMap@ebx,
//    a5=modeMask, a6=tileBase, a7=blurRadius).
// Full pipeline: load the 3 vertices (REVERSED when the literal signed-area
// expression is > 0 — the original's two scan loops), pick the long edge, set
// up both edge interpolators and the horizontal U gradient (dword_13FC590),
// then fill the top and bottom sub-triangles via FillTexturedSpansShaded.
// `fb` is the byte map (a4); `tileBase` the 24-byte-stride tile-type array
// (a6); `blurRadius` = a7; `modeMask` = a5, masked exactly as the original:
// !tileBase -> &2, fb unusable -> &5, 0 -> no-op. The pitch (a3) is
// fb->widthPx, or `gridPitch` when fb is absent (the original's a4==0 case
// keeps a valid a3). The defaults reproduce the prior byte-map-only behaviour.
// Returns nonzero if any span was drawn (the original's return is dead).
int RasterizeTexturedTriangle(Surface* fb, const RasterVertex v[3],
                              u8 modeMask = 2, u8* tileBase = nullptr,
                              i32 blurRadius = 0, i32 gridPitch = 0);

// ---------------------------------------------------------------------------
// Flat-shaded triangle.
//
// gilde.exe 0x603ED4 — VIBE_Shadow_RasterizeTriangle (the shadow-stencil flat
// rasterizer; the ONLY caller of the 0x603DA8/0x603D00 flat-fill leaves) +
//   0x603DA8  VIBE_Raster_FillSpans         (the per-row span fill loop)
//   0x603D00  VIBE_Raster_ComputeEdgeSlope  (the LEFT-edge slope/start setup)
//
// WINDING (verified at 0x603F0F, wave-5): the flat path is NOT forward-only. It
// computes the screen-space cross product
//   (x0-x2)*(y0-y1)  >  (x0-x1)*(y0-y2)
// and when the triangle is back-wound (`>`):
//   * if (poly +38 & 4) == 0  -> CULL the triangle (draw nothing, 0x603FB0),
//   * else                    -> load the vertices REVERSED (v18 = v+2, --v18).
// Front-wound triangles load forward. This is the same +38 bit2 gate as the
// textured leaf 0x5F6C30 (claim 5c) PLUS a back-wound cull when the flag is
// clear. `polyFlags38` carries the poly +38 byte (default 0 -> back-wound
// triangles are culled, matching the binary's default-flag behaviour).
//
// The original's fill VALUE is a fixed shadow-stencil constant
// (dword_13FC5E0 = 1 for <=8bpp, 0xFFFF for 16bpp at 0x6041BA/0x6040BF), not a
// parameter; this reconstruction keeps `color` as the host-supplied fill index
// (the flat leaf itself just memsets dword_13FC5E0 per row) so the generic
// solid-fill callers stay exact. ComputeEdgeSlope (0x603D00) targets the LEFT
// accumulators (xLeft/xLeftStep) with the identical two-path fixed-point divide
// as InterpolateEdgeZ; FillSpans (0x603DA8) is purely the per-row span loop
// (no vertex/winding logic of its own — both confirmed wave-5).
// ---------------------------------------------------------------------------
int RasterizeFlatTriangle(Surface* fb, const RasterVertex v[3], u8 color,
                          u8 polyFlags38 = 0);

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
