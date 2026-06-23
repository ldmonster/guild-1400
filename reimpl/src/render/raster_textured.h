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
// FillSpanTextured machine code with the live per-triangle constants
// (captured disasm of 0x5f7500; the masked patcher 0x5f753f writes the same
// six sources into the 0x5F721A body):
//     loc_5F71EF imm  <- unk_1406A8C   texel byte array base (texBase)
//     loc_5F7202 imm  <- dword_1406A78 the bound HiColTab block (palBase)
//     loc_5F71C7 imm  <- byte_1407A91  texture-width log2 shift (widthShift)
//     loc_5F71EA imm  <- dword_13FC594 U FRACTIONAL step  (= uGrad << 16)
//     loc_5F71FC imm  <- dword_13FC5A8 V FRACTIONAL step  (= vGrad << 16)
//     loc_5F71E4 imm  <- dword_1406A88 texel-index wrap mask (texelMask)
// The INTEGER halves of the U/V steps travel in the combined texel-index
// delta pair dword_13DCE54[0] / dword_13DCE50 ((dVint<<shift)+dUint, +width on
// a V-fraction carry), advanced by the span's `adc`/`sbb` chain; and the
// palette LIGHT ROW (dword_13FC5E0 = avg(vertex+66)<<8) rides in edx's upper
// bytes — the per-pixel `mov dl, texel` replaces only the low byte, so the
// colour fetch is palBase[lightRow8 | texel]. Self-modification was a 1990s
// speed hack: hoist the per-triangle constants out of the per-pixel loop into
// instruction immediates. We translate it to BuildSpanTexParams(), which fills
// the SpanTexParams struct (render/raster.h) from a Texture record + the live
// full-16.16 U/V steps + the light row — proven bit-identical addressing (see
// the equivalence note in render/raster.cpp), only the delivery mechanism
// (struct field vs. code immediate) differs.
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
    i32 vf[3];      // per-vertex FOG FACTOR (the vertex+79 FVF specular byte
                    //   from ComputeFogFactor @0x5ac9aa/@0x5beb0b), promoted <<16
                    //   into 16.16 — interpolated as a THIRD span channel (wave-7).

    // -- left-edge accumulators (output of InterpolateEdgeRgbz) ---------------
    i32 xLeft;      // dword_13FC5D8  left edge X, 16.16
    i32 xLeftStep;  // dword_13FC5E8  d(xLeft)/dy, 16.16
    i32 uLeft;      // dword_13FC5F0  left edge U, 16.16
    i32 uLeftStep;  // dword_13FC5CC  d(uLeft)/dy, 16.16
    i32 vLeft;      // dword_13FC5EC  left edge V, 16.16
    i32 vLeftStep;  // dword_13FC5D0  d(vLeft)/dy, 16.16
    i32 fLeft;      // left-edge FOG FACTOR accumulator, 16.16 (wave-7)
    i32 fLeftStep;  // d(fLeft)/dy, 16.16 (wave-7)

    // -- right-edge accumulators (output of InterpolateEdgeZ, X only) ---------
    i32 xRight;     // dword_13FC5BC  right edge X, 16.16
    i32 xRightStep; // dword_13FC5C8  d(xRight)/dy, 16.16

    // -- horizontal gradients across a span (set by the triangle setup) -------
    i32 uGrad;      // dword_13FC5E4  dU/dx, 16.16
    i32 vGrad;      // dword_13FC598  dV/dx, 16.16
    i32 fGrad;      // horizontal dF/dx for the fog-factor channel, 16.16 (wave-7)
    bool fogPerPixel;     // when set, the span carries the interpolated fog
                    //   factor (the engine's per-pixel D3D vertex fog); when clear
                    //   the textured spans run with no fog (or the per-triangle
                    //   SpanFog() constant). Set only when SpanFog().enabled.

    // -- per-span scratch -----------------------------------------------------
    i32 spanLen;    // dword_13FC588  pixel count of the current span

    // -- destination ----------------------------------------------------------
    u16* fbRow0;    // dword_13FC5D4 = dword_7626F0 + 2*y0*pitch  (16bpp cursor)
    i32  fbPitchPx; // dword_7626F8 / 2  (pixels per row; orig stored bytes)

    // -- reconstruction-only horizontal surface clip ---------------------------
    // The Surface clip rect [clipX0, clipX1). The original wrote spans
    // unclipped, trusting the upstream poly clip + the DDraw guard band; the
    // software reconstruction clamps each span so float-rounding overshoot from
    // the plane clip cannot write outside the framebuffer (same defensive clip
    // the shaded path FillTexturedSpansShaded carries, raster.cpp). The
    // zero-init (clipX1 <= clipX0) DISABLES the clamp — existing direct
    // FillSpanLoop callers/tests are unchanged. RasterizeTexturedTriangleRgbz
    // fills these from the destination Surface.
    i32 clipX0;
    i32 clipX1;

    // -- apex (min-Y) search seed ---------------------------------------------
    // gilde.exe 0x5f6c7e seeds the LoadVertices min-Y compare from the runtime
    // viewport constant dword_13FC5C0 (= ConvertX(view arg_14), set once per view
    // in VIBE_Render_SetupViewTransform @0x5af70a — a Y-clip bound).
    // RasterizeTexturedTriangleRgbz* sets this to 0x7FFFFFFF, which is bit-
    // identical for every on-screen triangle (the apex is the global-min vy
    // whenever that min <= the viewport bound). It differs ONLY for a degenerate
    // triangle whose every vy exceeds the bound (original -> apex=-1 -> culls; the
    // recon -> proceeds, then clamps to zero rows). KNOWN DIVERGENCE: faithful
    // reproduction needs dword_13FC5C0 plumbed from the view-transform module
    // (cross-module wiring, not faked here per Rule 8). Zero-init (memset) leaves
    // this 0; only RasterizeTexturedTriangleRgbz* (which calls LoadVertices) reads
    // it, and those set it explicitly before the load.
    i32 minYSeed;
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
// Texture record plus the live U/V steps and the palette light row
// (dword_13FC5E0, default 0 = a plain 256-entry palette), exactly mirroring
// which global went into which patched immediate (see the header banner). The
// reconstructed FillSpanTextured reads these fields where the original read
// its immediates.
// ---------------------------------------------------------------------------
SpanTexParams BuildSpanTexParams(const Texture& tex, const u16* palette,
                                 i32 uStepFrac, i32 vStep, u32 lightRow8 = 0);

// ---------------------------------------------------------------------------
// gilde.exe 0x5F6B34 — VIBE_Raster_FillSpanLoop. Walks `rowCount` scanlines,
// each emitting one textured span [ceil(xLeft), ceil(xRight)) via
// FillSpanTextured, with the per-pixel U/V starts interpolated from the left
// edge accumulators + the horizontal gradients. Advances every edge accumulator
// one scanline. `p` carries the patched span constants (BuildSpanTexParams).
// Returns the (unused) final fb-step value, like the original's `result`.
// ---------------------------------------------------------------------------
int FillSpanLoop(RgbzRasterState& rs, int rowCount, const SpanTexParams& p);

// The COLOUR-KEY sibling: the identical scanline walk with the masked span body
// patched in — the engine state after VIBE_Raster_PatchSpanConstantsMasked
// @0x5f753f targeted the loc_5F72xx span (VIBE_Raster_FillSpanTexturedMasked
// @0x5F721A: `test dl,dl / jz` skips texels whose source palette index is 0).
// Identical edge/accumulator arithmetic; only the inner span body differs —
// exactly the difference between the two patched code paths in the binary.
int FillSpanLoopMasked(RgbzRasterState& rs, int rowCount, const SpanTexParams& p);

// ---------------------------------------------------------------------------
// A textured-triangle vertex: screen position (pixels, float), texture coords
// (TEXELS, float) and the +66 light byte. The original read screen x/y from the
// projected Vertex record (+16/+20), the UVs from the poly's float array as
// (u_norm + scrollU + polyOffsetU) * (mipWidth * 65536.0f), and the light from
// the vertex +66 byte. The caller performs the normalised->texel conversion
// (u_norm * width) and any scroll/poly offset; this layer applies the * 65536
// (one float-multiply association differs from the original's single
// `(u+off) * (width*65536)` — recorded in progress/raster-verify-wave4.md).
// ---------------------------------------------------------------------------
struct RgbzVertex {
    float x;  // screen X (pixels)   -> *65536 into vx[]
    float y;  // screen Y (pixels)   -> *65536 into vy[]
    float u;  // texture U (texels)  -> *65536 into vu[]
    float v;  // texture V (texels)  -> *65536 into vv[]
    u8 light = 0; // vertex +66 light byte: the span palette row is
                  // ((l0+l1+l2)/3) << 8 (dword_13FC5E0). 0 == row 0, which
                  // keeps a plain 256-entry palette valid.
    int fogFactor = 255; // the per-vertex D3D fog factor (vertex+79 FVF specular
                  // byte from ComputeFogFactor; 255 == no fog, 0 == full fog).
                  // Interpolated linearly across the triangle and applied per
                  // pixel when SpanFog().enabled (wave-7 W7-FOGPIX). The default
                  // 255 keeps fog-off triangles byte-identical.
};

// ---------------------------------------------------------------------------
// gilde.exe 0x5F6C30 — VIBE_Raster_RasterizeMirrorTriangle.
// The full affine textured-triangle pipeline: convert the 3 float vertices to
// 16.16 — REVERSED (slot i <- vertex 2-i) when `polyFlags38` has bit 2 set
// (the +38 "double-sided" byte of the 40-byte poly record) AND the literal
// screen-space cross test (x0-x2)*(y0-y1) > (x0-x1)*(y0-y2) flags a back-wound
// triangle — find the top (min-Y) vertex, compute the horizontal dU/dx & dV/dx
// gradients from the UV/position cross products, pick the long edge, set up
// the edge interpolators, build the span constants (BuildSpanTexParams, light
// row = avg of the three vertex light bytes << 8) and fill the top then bottom
// sub-triangles via FillSpanLoop into a 16-bit surface. Returns nonzero if any
// span was drawn. (The original bound the active texture via BindActive
// @0x5db564 / PatchSpanConstantsTextured @0x5f7500 and reset it after
// (ResetBinding @0x5db5f0); we take the Texture + palette explicitly.)
// ---------------------------------------------------------------------------
int RasterizeTexturedTriangleRgbz(Surface* fb, const RgbzVertex v[3],
                                  const Texture& tex, const u16* palette,
                                  u8 polyFlags38 = 0);

// ---------------------------------------------------------------------------
// COLOUR-KEY variant: the same triangle pipeline with the MASKED span body
// (FillSpanLoopMasked / VIBE_Raster_FillSpanTexturedMasked @0x5F721A) — texels
// whose source palette index is 0 are skipped (`test dl,dl / jz`, left
// untouched in the framebuffer).
//
// WAVE-4 EVIDENCE STATUS (captured IDA results, va1_raster.md): BOTH
// 0x5F721A (the masked span) and its patcher 0x5f753f have NO code xrefs and
// NO data references in the static image (xrefs_to + find_bytes of their
// little-endian addresses all came back empty) — the masked body is patched
// 1:1 from the binary but its RUNTIME SELECTOR is unrecovered. The previously
// claimed trigger "texture flags & 8" is NOT supported by the captured
// evidence: in VIBE_Texture_UploadToSurface @0x5db234, flags&8 only zeroes the
// 5th argument (v9, otherwise dword_64A1FC) of VIBE_Render_LoadAndStretchTexture
// @0x5dea50, whose semantics are UNVERIFIED (decompile not captured). Bind
// sites must default to the PLAIN variant until the selector is recovered.
// See progress/raster-verify-wave4.md (UNVERIFIED list).
// ---------------------------------------------------------------------------
// `colorKey565` (wave-5 W5-CKEY) selects the masked span's transparency rule:
//   < 0  (default): the EXACT original `source index == 0` rule (byte-identical
//                   to the binary's software masked span; existing callers).
//   >= 0          : the faithful DDraw colour-key — skip texels whose RESOLVED
//                   16bpp value == (u16)colorKey565. For a 24-bit colour-keyed
//                   texture the engine keys on pal[0] == black (== 565 0x0000);
//                   see the FillSpanTexturedMasked banner in raster.cpp and
//                   progress/colourkey-integration-wave5.md.
int RasterizeTexturedTriangleRgbzMasked(Surface* fb, const RgbzVertex v[3],
                                        const Texture& tex, const u16* palette,
                                        u8 polyFlags38 = 0,
                                        i32 colorKey565 = -1);

} // namespace guild::render
