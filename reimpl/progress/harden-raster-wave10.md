# Wave-10 hardening — raster span/triangle/clip cluster (W10-RASTER)

MCP-free memory-safety + edge-case pass over the wave 6-9 software rasterizer
leaves. No new 1:1 reconstruction. All fixes are FAITHFUL: they reproduce the
original x86 two's-complement / wrap semantics with no observable behavior change
(the original did not corrupt memory or invoke UB either). Every existing golden
value is byte-identical; the non-ASAN `build/` stays green.

## Cluster (owned)
- `src/render/raster.{h,cpp}` — shaded-affine span path, flat triangle, blend host
- `src/render/raster_textured.{h,cpp}` — RGBZ affine textured triangle path
- `src/render/raster_blend.{h,cpp}` — blend / OR span variants
- `src/render/meshlist.{h,cpp}` — software draw-list flush + dispatch
- `src/render/clip.{h,cpp}` — Sutherland-Hodgman polygon clipper
- tests: `render_raster_test`, `render_raster_textured_test`, `render_clip_test`,
  `raster_clip_test`, `render_binder_test`, `texraster_recon2_test` (+ the e2e /
  itest siblings).

## Method
Built every owned unit/e2e/itest target under
`-fsanitize=address,undefined -fno-sanitize-recover=all` (build dir `build-asan`),
ran them, fixed each report, pinned with a degenerate/edge test, then verified
both `build-asan/` (sanitized) and `build/` (normal) are green with byte-identical
goldens.

## Bugs found + fixed (all UB; ASAN found no OOB read/write in this cluster)

UBSAN reported, in order:

1. **Signed left-shift of a negative value** in the fixed-point edge-slope and
   sub-pixel setup. The original x86 does `shl reg, 16` (and the Hex-Rays
   `(num << 16)`), which is well-defined for any bit pattern; the C++
   `(i64)num << 16` / `(i32)x << 16` on a negative `num` is UB. Sites fixed by
   moving the `<< 16` into the unsigned domain (identical two's-complement bits,
   identical subsequent 64-bit divide / subtraction):
   - `raster.cpp` `EdgeSlope` (0x603D00 / shared slope), `SubpixelToCeil`,
     `InterpolateEdgeZTex` (0x5F7840), the byte-span `(xL<<16)` (0x5F7960).
   - `raster_textured.cpp` `InterpolateEdgeRgbz` (0x5F6930) X/U/V/F slopes + the
     sub-pixel init, `InterpolateEdgeZ` (0x5F6A8C), `FillSpanLoopWith`'s
     `(xL<<16)` (0x5F6B34), and the per-vertex fog `factor<<16` (now masked to a
     byte, matching the original vertex+79 byte source).

2. **Signed integer overflow in the per-pixel span accumulator advance.** The
   inner span loops step the 16.16 U/V/fog accumulators with `u += step`
   (`i32 += i32`); the original `add edx, step` wraps mod 2^32, but the C++ `+=`
   is signed-overflow UB once the accumulator nears INT_MAX (steep gradient near
   the fixed-point range edge — hit by the new near-overflow edge test). Added
   `WrapAddI32(a,b) = (i32)((u32)a+(u32)b)` to `raster.h` and routed every
   accumulator advance through it:
   - `raster.cpp` FillSpanTextured (3 loops), FillSpanTexturedMasked (2 loops),
     FillTexturedSpansShaded per-scanline edge advances, RasterizeFlatTriangle's
     `fillRange` edge advances.
   - `raster_blend.cpp` all four blend/OR span loops.
   - `raster_textured.cpp` FillSpanLoopWith per-scanline edge advances, and the
     `clampRows` skip = step*skip done in the unsigned domain (the multiply
     equals `skip` row-by-row adds mod 2^32, the original's row-at-a-time walk).

No memory-corruption (OOB / use-after-free / leak) was found in this cluster:
the span loops already no-op on `spanLen <= 0`, the byte-span and flat-fill paths
already clip x to `[0,fbW)` and gate row to `[0,fbH)`, the RGBZ path carries the
recon-only surface clamp (`raster_clip_test`), and the clipper's ping-pong lists
/ new-vertex pool stay within their 128 / 256 capacities for the original's
≤127-vertex polys. The wave-2 "590-byte overrun" and the wave-8 "OOB into an
adjacent global" the brief cites were already fixed upstream; this pass confirms
they stay fixed under ASAN.

## Edge / degenerate tests added (all pass under ASAN+UBSAN and normal)

`render_raster_test.cpp` (shaded-affine, flat, blend spans):
- FillSpanZeroAndNegativeLenNoWrite — spanLen 0 / negative + NULL tex/pal: no write.
- FillSpanOnePixelMaskEdge — 1-px span, texelMask 0, huge U/V start (wrap to 0).
- FillSpanAccumulatorWrapNearOverflow — U near INT_MAX, big step → mod-2^32 wrap.
- EdgeInterpNegativeDxNoUb — right-to-left edge (negative dx/dlight) slope.
- OffScreenTriangleByteSpanClips — top-left/bottom overhang, negative ceil() edge.
- TileStampOneCellGrid — 24-byte single-cell tile array, gridPitch 1.
- FlatDegenerateCollinearNoWrite — collinear flat triangle draws nothing.
- FlatOverhangClampsToTinySurface — big flat tri on a 3x3 surface clamps.
- BlendOrSpansZeroLenNoWrite — all four blend/OR spans no-op at spanLen ≤ 0.
- BlendSpanOnePixelMaskEdge — blend span 1-px at mask edge (no inter-channel carry).

`render_raster_textured_test.cpp` (RGBZ path):
- FillSpanLoopZeroRowsAndEmptySpan — rowCount 0 / xLeft≥xRight: no write.
- InterpolateEdgeRgbzNegativeDeltasNoUb — negative dx/du/dv slopes.
- PartiallyClippedLeftEdge — left-overhang, negative-ceil left edge, clamp.
- FogFactorClampBoundary — per-pixel fog factor swept below 0 and past 255.
- MaskedSpanAllIndexZeroSkipsEverything — all-index-0 texture leaves dst intact.
- DegenerateZeroAreaReturnsEarly — v44==0 (collinear) early-out.

`render_clip_test.cpp` (clip + flush):
- ZeroPlaneContextNoPass / EmptyEverythingReturnsNull — empty contexts.
- NearCapacityPolygonNoOverflow — 120-vertex poly close-copy stays in the 128 list.
- FlushEmptyListDrawsNothing / FlushNullPolyEntrySkipped — empty / null entries.
- FlushDirectTranslucentRoutesSlot3 — non-clip dispatch via slot index 3.

`raster_clip_test.cpp` (RGBZ surface clamp):
- OverhangRightBottomClamps / FullyBelowSurfacePaintsNothing / OnePixelSurface.

## BEHAVIORAL — needs MCP to confirm against the decompile

1. **FillTexturedSpansShaded tile-type CORE span has no upper-X clamp**
   (`raster.cpp` ~L396: `cell = tileRow + 24*xL; for k in [0,spanLen) *cell=stamp`).
   The HALO paths clamp width to `pitchPx - xL`, but the core span trusts the
   caller's geometry (matching the documented "writes unclipped" note and the
   live caller BuildTerrainMesh @0x5c5610, whose terrain geometry is grid-bounded).
   Adding an upper clamp here would change observable writes for out-of-grid input
   and may diverge from 0x5F7960 — left UNCHANGED. All new tile-stamp tests use
   in-bounds geometry. Needs the 0x5F7960 decompile to confirm whether the
   original clamps the core span's high end (it does not appear to).

2. **Clip ping-pong list capacity is exactly 128 (close-copy writes `in[inCount]`).**
   For `inCount == 128` the close-copy `in[128]` would be OOB; the original arrays
   are also 128 entries, so this is the engine's own envelope (it never feeds a
   >127-vertex polygon). The capacity test uses 120 to stay inside the original's
   safe range. Whether the original guards a 128-vertex input needs the 0x5AD7D8
   decompile — left UNCHANGED (no guard added, to avoid a behavioral divergence).

## Verification
- `build-asan/` (ASAN+UBSAN): all 9 owned targets pass, 0 sanitizer reports.
- `build/` (normal): render_raster_test 1822, render_raster_textured_test 1350,
  render_clip_test 48, raster_clip_test 13, render_binder_test 24,
  render_picture_test 103 (raster_blend coverage) — all 0 failures, goldens
  byte-identical.
