#pragma once
#include "guild/common/types.h"
#include "render/raster.h"
#include "render/surface.h"
#include "render/texture.h"

// =============================================================================
// guild::render — the AFFINE textured-triangle span path (the U/V-walking
// rasterizer the engine uses for textured / mirrored / reflected polygons).
//
// Faithful 1:1 reconstruction of the gilde.exe "RGBZ" rasterizer cluster — the
// sibling of the shaded-affine path already in render/raster.{h,cpp}. Where that
// path interpolates ONE channel (an 8-bit shade byte) and writes it straight to
// an 8-bit surface, THIS path interpolates TWO channels (U and V texture coords)
// down the triangle edges, walks them across each span, and fetches a real texel
// (texBase[(V<<widthShift)+U & mask] -> palBase[idx]) into a 16-bit surface.
//
//   0x5F6930  VIBE_Raster_InterpolateEdgeRgbz   (edge X + U + V interpolator)
//   0x5F6B34  VIBE_Raster_FillSpanLoop          (scanline driver -> FillSpanTextured)
//   0x5F6C30  VIBE_Raster_RasterizeMirrorTriangle (full textured-triangle pipeline)
//   0x5F7500  VIBE_Raster_PatchSpanConstantsTextured  (modelled by BuildSpanTexParams)
//   0x5F76CD  VIBE_Raster_PatchSpanTextureBase        (modelled by BuildSpanTexParams)
//
// ALL interpolation is 16.16 FIXED-POINT (x/u/v). The original kept the edge and
// gradient accumulators in the same 0x13FC5xx scratch block as the shaded path;
// here they live in RgbzRasterState (each field carries its original global).
//
// ---------------------------------------------------------------------------
// SELF-MODIFYING SPAN CODE (documented; reconstructed as parameters)
// ---------------------------------------------------------------------------
// Before each triangle the original called VIBE_Raster_PatchSpanConstantsTextured
// (0x5F7500) which OVERWRITES six immediate operands embedded in the
// FillSpanTextured machine code with the live per-triangle constants:
//     loc_5F71EF imm  <- unk_1406A8C   texel byte array base (texBase)
//     loc_5F7202 imm  <- dword_1406A78 palette/index->16bpp LUT (palBase)
//     loc_5F71C7 imm  <- byte_1407A91  texture-width log2 shift (widthShift)
//     loc_5F71EA imm  <- dword_13FC594 U fractional step (uStepFrac)
//     loc_5F71FC imm  <- dword_13FC5A8 U accumulator start  (lightStart)
//     loc_5F71E4 imm  <- dword_1406A88 texel-index wrap mask (texelMask)
// (0x5F76CD PatchSpanTextureBase patches just the texBase immediate into the
// four blend/OR variants.) Self-modification was a 1990s speed hack: hoist the
// per-triangle constants out of the per-pixel loop into instruction immediates.
// We translate it to BuildSpanTexParams(), which fills the SpanTexParams struct
// (render/raster.h) from a Texture record + the live U/V step — behaviour is
// bit-identical, only the delivery mechanism (struct field vs. code immediate)
// differs. The reconstructed FillSpanTextured (render/raster.cpp) reads those
// fields exactly where the original read its patched immediates.
// =============================================================================
namespace guild::render {

// ---------------------------------------------------------------------------
// RgbzRasterState — the 16.16 edge-walk / gradient accumulator block for the
// textured path. Mirrors the gilde.exe 0x13FC5xx scratch globals (the same area
// RasterState mirrors; the textured path reuses the X/edge slots and adds U/V).
// ---------------------------------------------------------------------------
struct RgbzRasterState {
    // -- per-vertex inputs (filled by RasterizeTexturedTriangleRgbz) ----------
    i32 vx[3];      // dword_13FC5B0[]  screen X, 16.16
    i32 vy[3];      // dword_13FC59C[]  screen Y, 16.16
    i32 vu[3];      // dword_13FC55C[]  texture U, 16.16
    i32 vv[3];      // dword_13FC550[]  texture V, 16.16

    // -- left-edge accumulators (output of InterpolateEdgeRgbz) ---------------
    i32 xLeft;      // dword_13FC5D8  left edge X, 16.16
    i32 xLeftStep;  // dword_13FC5E8  d(xLeft)/dy, 16.16
    i32 uLeft;      // dword_13FC5F0  left edge U, 16.16
    i32 uLeftStep;  // dword_13FC5CC  d(uLeft)/dy, 16.16
    i32 vLeft;      // dword_13FC5EC  left edge V, 16.16
    i32 vLeftStep;  // dword_13FC5D0  d(vLeft)/dy, 16.16

    // -- right-edge accumulators (output of InterpolateEdgeZ, X only) ---------
    i32 xRight;     // dword_13FC5BC  right edge X, 16.16
    i32 xRightStep; // dword_13FC5C8  d(xRight)/dy, 16.16

    // -- horizontal gradients across a span (set by the triangle setup) -------
    i32 uGrad;      // dword_13FC5E4  dU/dx, 16.16
    i32 vGrad;      // dword_13FC598  dV/dx, 16.16

    // -- per-span scratch -----------------------------------------------------
    i32 spanLen;    // dword_13FC588  pixel count of the current span

    // -- destination ----------------------------------------------------------
    u16* fbRow0;    // dword_13FC5D4 = dword_7626F0 + 2*y0*pitch  (16bpp cursor)
    i32  fbPitchPx; // dword_7626F8 / 2  (pixels per row; orig stored bytes)
};

// ---------------------------------------------------------------------------
// gilde.exe 0x5F6930 — VIBE_Raster_InterpolateEdgeRgbz (a@eax, b@edx).
// Sets up xLeft/xLeftStep, uLeft/uLeftStep AND vLeft/vLeftStep for the long
// left edge a->b (b below a). Same two-path fixed-point slope as the shaded
// InterpolateEdgeZTex, but carries U and V instead of a single shade channel.
// ---------------------------------------------------------------------------
void InterpolateEdgeRgbz(RgbzRasterState& rs, int a, int b);

// gilde.exe 0x5F6A8C — VIBE_Raster_InterpolateEdgeZ (short right edge, X only).
// Re-declared here over RgbzRasterState (same math as raster.h's; shares the X
// slots). Sets xRight/xRightStep for edge a->b.
void InterpolateEdgeZ(RgbzRasterState& rs, int a, int b);

// ---------------------------------------------------------------------------
// BuildSpanTexParams — the portable reconstruction of the PatchSpanConstants*
// self-modifying-code step (0x5F7500 / 0x5F76CD). Fills a SpanTexParams from a
// Texture record plus the live U/V steps, exactly mirroring which global went
// into which patched immediate (see the header banner). The reconstructed
// FillSpanTextured reads these fields where the original read its immediates.
// ---------------------------------------------------------------------------
SpanTexParams BuildSpanTexParams(const Texture& tex, const u16* palette,
                                 i32 uStepFrac, i32 vStep);

// ---------------------------------------------------------------------------
// gilde.exe 0x5F6B34 — VIBE_Raster_FillSpanLoop. Walks `rowCount` scanlines,
// each emitting one textured span [ceil(xLeft), ceil(xRight)) via
// FillSpanTextured, with the per-pixel U/V starts interpolated from the left
// edge accumulators + the horizontal gradients. Advances every edge accumulator
// one scanline. `p` carries the patched span constants (BuildSpanTexParams).
// Returns the (unused) final fb-step value, like the original's `result`.
// ---------------------------------------------------------------------------
int FillSpanLoop(RgbzRasterState& rs, int rowCount, const SpanTexParams& p);

// ---------------------------------------------------------------------------
// A textured-triangle vertex: screen position (pixels, float) and texture coords
// (texels, float). The original read these from the projected Vertex records +
// the per-poly UV scroll LUTs (flt_1406950/flt_14069D0, both 0 in a static
// frame); we pass them in so the path is re-entrant and testable.
// ---------------------------------------------------------------------------
struct RgbzVertex {
    float x;  // screen X (pixels)   -> *65536 into vx[]
    float y;  // screen Y (pixels)   -> *65536 into vy[]
    float u;  // texture U (texels)  -> *65536 into vu[]
    float v;  // texture V (texels)  -> *65536 into vv[]
};

// ---------------------------------------------------------------------------
// gilde.exe 0x5F6C30 — VIBE_Raster_RasterizeMirrorTriangle.
// The full affine textured-triangle pipeline: convert the 3 float vertices to
// 16.16, find the top (min-Y) vertex, compute the horizontal dU/dx & dV/dx
// gradients from the triangle's UV/position cross products, pick the long edge,
// set up the edge interpolators, build the span constants (BuildSpanTexParams)
// and fill the top then bottom sub-triangles via FillSpanLoop into a 16-bit
// surface. Returns nonzero if any span was drawn. (The original additionally
// applied the per-poly UV scroll offset and bound the active texture via the
// global TextureSet; we take the Texture + palette explicitly.)
// ---------------------------------------------------------------------------
int RasterizeTexturedTriangleRgbz(Surface* fb, const RgbzVertex v[3],
                                  const Texture& tex, const u16* palette);

} // namespace guild::render
