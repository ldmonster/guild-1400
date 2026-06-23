# Wave-12 hardening — render leaves / recon / misc cluster

Agent: **W12-LEAVES** (MCP down → hardening only; no new reconstruction).

Scope (owned): `src/render/` leaf/recon/misc modules NOT already hardened by
waves 10/11 (raster/texture/shadow/particle/water/terrain/sky/light/mesh/anim/
camera/sprite/node_lod/sun/daycycle/falloff/emitter/scene were skipped):

```
render_leaves.{h,cpp}  render_leaves2..9.{h,cpp}
render_recon2.{h,cpp}  render_recon4_d3dcfg.{h,cpp}  render_recon4_ddraw.{h,cpp}
render_recon_objlist.{h,cpp}
coord_view.{h,cpp}  d3_projection.{h,cpp}  cull.{h,cpp}  fade.{h,cpp}
paintbox.{h,cpp}  paintbox_shape.{h,cpp}  thumbnail_capture.{h,cpp}
menu_widgets.{h,cpp}  perf_overlay.{h,cpp}
```

## Method
ASAN+UBSAN build in an isolated dir (`build-asan-leaves`,
`-fsanitize=address,undefined -fno-sanitize-recover=all`), all 18 cluster test
targets built + run, then added boundary/malformed tests and fixed every
sanitizer finding. Normal `build/` re-verified green for the cluster targets.

```
cmake -S . -B build-asan-leaves -DCMAKE_BUILD_TYPE=Debug -DGUILD_BACKEND=OFF \
  -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-sanitize-recover=all -g"
```

## Sanitizer findings + fixes

### 1. LeakSanitizer — render_leaves5_test (32 KB+ leaked, 32 allocations) — FIXED
Every spawn test (`SpawnBloodEffect`, `SpawnBlood`, `SpawnDebris`,
`SpawnExplosion`, `SpawnSmoke`, `InitColors`, `UpdateRainStep`) calls
`AllocSystem` (particle cluster, wave-10) which allocates four blocks per system
via the default heap backend, but the tests only called `ResetSpawnStats()` and
never released the systems. The particle owner exposes
`render::FreeAllSpawnAllocations()` exactly for this (its own
`particle_spawn_test.cpp` calls it after each spawning case).
**Fix (test-only):** added `render::FreeAllSpawnAllocations();` at the end of each
spawning test in `tests/unit/render_leaves5_test.cpp`. Production code untouched;
the allocator/free live in the non-owned particle cluster (not edited). Re-run:
clean, 0 leaked.

### 2. stack-buffer-overflow — render_recon4_ddraw_test (EnumZBufferFormatsCallback) — FIXED
The test passed `u8 acc[32]`, but `EnumZBufferFormatsCallback` (0x5dd534) reads
`acc` dword[5] (offset 20) and does `memcpy(acc+8, fmt, 32)` (offsets 8..40), so
the accumulator must be ≥40 bytes — the original indexed a full accumulator
struct, not a 32-byte buffer (the sibling `EnumTextureFormatsCallback` test
correctly uses `acc[80]`). **Fix (test-only):** sized the buffer to `u8 acc[40]`
with a comment documenting the real contract. The reconstructed function matches
the binary's pointer arithmetic; no source change.

## Boundary / malformed tests added (all green under ASAN+UBSAN)

- **d3_projection_test** (+3): degenerate `w==0` (z==-near) returns before the
  divide — no NaN/Inf, sx/sy/ndcZ left at 0; zero-width viewport
  (`aspect=(w!=0)?:0` guard); `near==far` (`q=(denom!=0)?:1` guard).
- **render_cull_test** (+3): zero vertex AND polygon counts (no base-pointer
  deref); ±FLT_MAX coordinates classify deterministically; a zero-extent
  (degenerate) triangle is kept with `flags36 == 0x80`.
- **render_paintbox_test** (NEW file, paintbox + paintbox_shape): brush-2 diamond
  at the bottom-right corner — satellites x±2/y±2 fall off-surface and are
  dropped by the per-pixel `SurfaceSetPixelRgb` clip (no OOB); odd-width surface
  line draw; null surface + far-negative/huge origins; `Surface_CopyRegionRgb` /
  `BlitPaletteToPixels` exact-buffer round-trips; `BlitRgbToPixels` zero-height /
  null-surface early-outs.
- **render_leaves7_test** (+1): `DirectionalFalloffScale` at `NdotL == -1` gives
  the largest valid index `(int)(-1 * -1023) == 1023` — exactly the last entry of
  the documented 1024-entry falloff LUT (in-bounds); plus the idx-0 and NdotL==0
  early-branch cases.
- **render_recon_objlist_test** (+2): empty list (`head()==sentinel` on the first
  probe) drains with zero frees/hook calls; a single owned node that is both head
  and tail (self-ring) exercises the `FreeObjectNode` prev/next splice on the
  smallest possible list with no OOB.
- **render_thumbnail_capture_test** (+2): 1×1 source surface — every 320×240
  nearest-neighbour tap truncates to src index 0 (in-bounds resample);
  screenshot of a 0-size target short-circuits at the `w<=0||h<=0` guard
  (ok==false, empty buffers, serial still post-increments).
- **menu_widgets_test** (+4): out-of-range `gfxRecord` (negative and beyond the
  archive count) → inert fallback, no record-table OOB; degenerate sizes
  (`widthPx/w/h <= 0`) early-return with the framebuffer untouched; too-narrow
  button forces the cap-overlap clamp path (centre==0); fully off-buffer
  placement clipped per-pixel (nothing written).

## Behavioral / engine-envelope items (documented, NOT changed — need MCP)

- **DirectionalFalloffScale** (`render_leaves7.cpp`, 0x5c73e4): for an
  *un-normalised* `NdotL < -1`, `idx = (int)(NdotL * -1023)` exceeds 1023 and
  would read past a 1024-entry LUT. In our API the LUT is caller-supplied
  (`const float* lut`), so this is the caller's contract; in the original it
  indexed the fixed global `flt_1405110` with no bound — i.e. the engine's own
  faulting envelope on degenerate (non-unit) input. The valid domain is the
  normalised `[-1,0)`, pinned by the new max-index test. No clamp added (would
  change observable behavior on the engine's valid path). 1:1 question if a clamp
  is ever desired — needs MCP to confirm the original table size/guard.
- **paintbox_shape** raw-stride blits (`Surface_BlitPaletteToPixels`,
  `Surface_BlitRgbToPixels`, `Surface_CopyRegionRgb`, 0x422EE4/0x422F80/0x423050)
  have no internal clip — they faithfully reproduce the engine's pointer
  arithmetic (`dst[y*fbWidth + x] = ...`). The caller sizes the destination to
  the blit; tests pin exact-buffer behavior. Not a recon bug (matches the binary).
- **render_recon4_ddraw** caps/enum callbacks index DDraw records by fixed byte
  offsets (e.g. `mode+820`, `dev+250`, `acc+44`); they trust the caller to pass
  correctly-sized records, exactly as the original did. Tests supply adequately
  sized buffers.
- **render_recon4_ddraw.cpp `ConfigureSurfaceCaps`**: `v52` is documented (in the
  source comment) as always 0 because the original reads `*(mode+781)` *after* a
  `memset(mode+780,0,44)` — a faithful quirk of the decompile, preserved 1:1.

## Status
- ASAN+UBSAN: all 18 cluster test targets pass, 0 sanitizer errors, 0 leaks.
- Normal `build/`: cluster targets re-verified green.
- Files edited (test-only + this doc): `tests/unit/render_leaves5_test.cpp`,
  `tests/unit/render_recon4_ddraw_test.cpp`, `tests/unit/d3_projection_test.cpp`,
  `tests/unit/render_cull_test.cpp`, `tests/unit/render_leaves7_test.cpp`,
  `tests/unit/render_recon_objlist_test.cpp`,
  `tests/unit/render_thumbnail_capture_test.cpp`,
  `tests/unit/menu_widgets_test.cpp`, NEW `tests/unit/render_paintbox_test.cpp`.
- No `src/` production code changed: every finding was a test-side leak / undersized
  test buffer; all production functions in the cluster faithfully match the binary
  and are bounded either internally (per-pixel clip, count-guarded loops,
  `?:`-guarded divisors) or by the documented caller contract.
- `build-asan-leaves/` removed at end.
