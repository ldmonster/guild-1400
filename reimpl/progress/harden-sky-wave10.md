# Wave-10 hardening — sky / sun / day-cycle / fog / falloff cluster (W10-SKY)

MCP-free memory-safety + edge-case pass over the waves 6-9 reconstructions in:

- `src/render/sky.{h,cpp}` (RenderSky, BlendBandLighting, BlendAmbientFog, LoadSkyBands, ComputeSkyFog)
- `src/render/skycolor_recon.{h,cpp}` (InterpolateBand, StoreBandColors, BandHasLight, CopyGradientEntry, SetBandGradient, BuildFogScratch)
- `src/render/sun_state.{h,cpp}` (sun-direction / lighting-accumulator globals)
- `src/render/daycycle.{h,cpp}` (BuildTimeTable, UpdateBrightness, BrightnessToBand, SunRegimeForBand, ComputeSunState)
- `src/render/fog.{h,cpp}` (SetFogRange, ConfigureFog, ComputeFogFactor, BlendFog*, ApplyAmbientBlend)
- `src/render/falloff_lut.{h,cpp}` (AcosGuarded, InitFalloffTable)

Tests touched (owned): `sky_render_test`, `sun_daycycle_test`, `render_fog_test`,
`render_falloff_lut_test`, `render_fog_blend_itest`. Also re-verified `skycolor_recon_test`
(exercises owned source) under ASAN — left unedited (not in the owned test list).

## Build / sanitizer status

ASAN+UBSAN (`-fsanitize=address,undefined -fno-sanitize-recover=all`):

| target | checks | failures | ASAN/UBSAN |
|---|---|---|---|
| sky_render_test        | 327 | 0 | clean |
| sun_daycycle_test      | 202 | 0 | clean |
| render_fog_test        | 261 | 0 | clean |
| render_falloff_lut_test|  28 | 0 | clean |
| render_fog_blend_itest |  26 | 0 | clean |
| skycolor_recon_test    |  73 | 0 | clean |

Normal `build/` (no sanitizer): all six targets green (same check counts). `guild`
static lib builds clean.

All pre-existing golden values are byte-identical (no golden changed).

## Memory-safety / UB fixes (faithful — in-bounds path unchanged)

Both fixes guard inputs that were **never an in-bounds path in the binary**, so they
cannot change any value the original produced; they only prevent an OOB read/write on
inputs the original would have crashed on (rule: a genuine memory-safety guard is faithful).

1. **`SkyColor_BandHasLight` @0x5b89e8** (`skycolor_recon.cpp`)
   - The original dereferences `*(v3 + 56*band)` with **no null/range check** on
     `obj`, `obj->bands`, or `band`. Added a guard: `if (!obj || !obj->bands ||
     (unsigned)band >= 7u) return 1;` — return is the function's own LABEL_14
     "no light" catch-all (1). The sky has 7 bands (0..6); every live caller passes
     a live object and an in-range band, so the guard only fires on OOB inputs.
   - Pinned by: `sky_render_test` indirectly (band-table bounds) and the existing
     `skycolor_recon_test` BandHasLight suite (unchanged, still passes).

2. **`SkyColor_CopyGradientEntry` @0x5b84c4** (`skycolor_recon.cpp`)
   - `band` indexes the 7-entry `entry[]` and `pending_index` indexes the 6-entry
     `scratch[]`; the original guards only `pending_index >= 0` (no upper bound) and
     does not range-check `band`. Added: out-of-range `band` returns `entry[0]` (no
     valid row to flush into), and the pending flush now also requires
     `pending_index < 6`. On every live path `band` is 0..6 (SetBandGradient's
     `band < 7` gate) and `pending_index` is the ApplyAmbientBlend band a1 in 0..5
     (or -1 disabled), so the guards never alter an in-bounds result.
   - Existing `skycolor_recon_test` CopyGradientEntry tests (pending_index=2, band=4)
     unchanged and passing.

`RenderSky` (`sky.cpp`) was already correctly clip-clamped (null surface/pixels,
negative origin, off-surface clip, width/height 0, odd pixel-stride via `widthPx`).
New degenerate tests confirm it stays in-bounds; no code change needed.

## Edge / degenerate tests added

**sky_render_test** (RenderSky + band math)
- RenderSky: zero-size surface (no byte written), zero-width/non-zero-height,
  1x1 (16 & 32 bpp), odd pixel-stride (stops at width, padding untouched), 24bpp
  fill bounds, off-surface clip (no write), negative clip origin clamped.
- BlendAmbientFog: exhaustive band-index (6..11, 0xFFFFFFFF) & frac (just outside
  [0,1]) early-out never indexes the 6-entry table; boundary frac 0/1 accepted.
- BlendBandLighting: all indices 0..6 stay in-bounds (a==6 wraps to band 0, not 7),
  a==7/1000 reject zero-filled.
- LerpChannelTrunc: factor 0/1 endpoints, signed-16 negative truncate-toward-zero.
- LoadSkyBands: 0 bands, > max bands (clamped to 7), truncated keyframes (< 6),
  overflow keyframes (> 6, clamped) — all bounds-clamped, no OOB.
- BuildFogScratch: 0-band table (in-bounds zero rows), band-6 wrap to band 0
  (low-24-bit colour reassembly).

**sun_daycycle_test**
- ComputeSunState at hour 0 (brightness 0/band 0) and hour 23 (brightness 600/band 6)
  for every season (day 0..3).
- Every season day%4 (day 0..7 wrap) reads an in-range `kDayKeyframeMinutes[4][6]`
  row; band in [0,7), blend in [0,1).
- BuildTimeTable season `& 3` mask keeps the row read in bounds.
- UpdateBrightness hour read as u16: 0x10000 aliases to hour 0.
- BrightnessToBand wraps mod 7 (700 -> band 0; negative brightness stays in (-7,0]).

**render_fog_test**
- near==far and near>far: ConfigureFog gates on `near < far`, so fog disabled and the
  range (slope) is NOT recomputed — no divide-by-zero is taken on the state path.
- SetFogRange directly with near==far -> slope = 255/0 = +inf (documented, not UB/crash).
- ComputeFogFactor at d2 == nearSq (255), just past it (<255), negative d2 (255, no
  sqrt of negative).
- BlendFog565 factor extremes (0/255) and out-of-range (>255 fast path, <0 -> full
  fog) across a matrix of source/fog colours.
- ApplyAmbientBlend band-index (6..11, negative) & frac early-out never indexes the
  6-band table; boundary frac 0/1 accepted.

**render_falloff_lut_test**
- InitFalloffTable writes EXACTLY 1024 floats: a guard slot at index 1024 stays
  untouched; index 0/1/1023 goldens re-verified.
- AcosGuarded at |x|==1 guard boundaries and the largest table argument 1023/1024
  (no NaN).

**render_fog_blend_itest**
- near==far into a 1x1 surface: fog stays disabled, the single pixel is the raw
  texel (no /0 in the blend, no OOB on a 1x1 buffer).

## Out-of-cluster / for-MCP notes

- **`raster_textured.cpp:80` — `InterpolateEdgeZ` left-shift of a (potentially)
  negative `i32`** : `((i64)(rs.vx[b] - rs.vx[a]) << 16)`. Shifting a negative signed
  value left is technically UB. NOT my cluster (raster). With the current owned-test
  data (the per-pixel fog triangle/span tests) the shifted dx is non-negative, so
  UBSAN does NOT trip on a clean rebuild (an earlier trip came from a stale
  pre-existing `build-asan/` object built from older raster source — gone after a full
  rebuild; verified clean across 5 runs + a forced recompile). Flagging for the raster
  cluster owner to harden (cast to `i64` after the subtract is already done; the issue
  is the signedness of the shift operand). No action taken (ownership).

- No BEHAVIORAL ambiguities found in the owned cluster — both guards are pure
  memory-safety with the in-bounds path byte-identical. No 1:1 questions to escalate.
