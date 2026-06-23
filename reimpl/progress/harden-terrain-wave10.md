# Wave-10 hardening — TERRAIN cluster (W10-TERRAIN)

MCP-free memory-safety + edge-case pass over the wave 4-9 terrain reconstructions.
Method: build every owned unit test under ASAN+UBSAN
(`-fsanitize=address,undefined`), add degenerate/edge tests that drive the REAL
entries, and fix every OOB/UB found with FAITHFUL guards (the guard keeps the
original's in-bounds path byte-identical). Behavioral ambiguities are flagged for
MCP, not changed. All existing golden values kept byte-identical.

## Owned cluster

Source: `src/render/terrain_render.{h,cpp}`, `terrain_walk.{h,cpp}`,
`terrain_uvtable.{h,cpp}`, `terrain_tile_query.{h,cpp}`, `tile_geometry.{h,cpp}`,
`tile_lighting.{h,cpp}`, `tile_visibility.{h,cpp}`, `terrain_scan.{h,cpp}`,
`floorgfx_recon.{h,cpp}`, `heightmap.{h,cpp}`.
(`play/terrain_render.*` is a bind site — NOT edited.)

Tests: `render_terrain_walk_test`, `render_tile_lighting_test`,
`render_tile_textures_test`, `terrain_ground_test`, `render_terrain_render_test`,
`render_terrain_scan_test`, `terrain_mesh_test`, `floorgfx_recon_test`.

## Bugs FIXED (genuine memory-safety, faithful)

### 1. `RenderTerrain` Pass-B stitch — heap-buffer-overflow (poly-buffer over-read)
`src/render/terrain_walk.cpp` (~line 258-283; the OOB faulted at line 335 / the
tail).  `gilde.exe 0x5bf22c VIBE_Floor_RenderTerrain`.

The Pass-B 2:1 LOD-seam stitch INCREMENTED `tile->polyCount` (`tile->polyCount +=
strip`) for stitch polygons it never WROTE into `polyBuf`. Pass C
(`pb[i]` for `i < tile->polyCount`) and the Phase-2 tail then iterated PAST the
polys Pass A actually wrote → ASAN `heap-buffer-overflow` reading
uninitialised / out-of-buffer `Polygon` records. Reproduced with a poly buffer
sized to exactly the Pass-A count and neighbour edge LODs set so the stitch gate
fires (`/tmp/probe_stitch.cpp`).

FIX: keep the engine's CONTROL FLOW (the per-edge gate is still evaluated) but do
NOT inflate the buffer-iterated count for stitch polys this reconstruction does
not emit. The in-bounds Pass-A path is byte-identical; no golden uses edge LODs so
no golden changes. Pinned by `TerrainWalkHardening.StitchPolyCountStaysInBuffer`
(exact-size poly buffer → ASAN would trip on any re-inflation).

NOTE / BEHAVIORAL (needs MCP): the original @0x5bf22c lines 848..1691 WRITES
`tileSpan/lod` stitch `Polygon` records (half-step `v387 = lod>>1` vertex offsets)
into `polyBuf` and bumps `*(tile+32)` per record. Those exact stitch-poly vertex
layouts were never recovered from the captured decompile, so the stitch is
reproduced GATE-ONLY. To finish it faithfully, MCP must recover the per-edge
stitch poly count + vertex indexing so the records can be written (not merely
counted). Marked inline in the source.

### 2. `StampSlopeLight` falloff-LUT — heap-buffer-overflow (LUT over-read)
`src/render/tile_lighting.cpp` line ~290.  `gilde.exe 0x5bc45c
VIBE_Floor_BuildTilePolys` slope stamp.

`idx = (int)(d * -1023.0)` indexes the 1024-entry `falloffLut`. The engine's `d`
is the dot of a NORMALISED surface normal with a UNIT rotated light direction, so
`d ∈ [-1,0) → idx ∈ [0,1023]` — always in range. A non-unit `lightDir` (or float
rounding making `d` slightly < -1) drove `idx` to 1024+ → ASAN
`heap-buffer-overflow` (`/tmp/probe4.cpp`, `|L|=5` → idx ~5115).

FIX: clamp `idx` to `[0,1023]` (the documented LUT size). FAITHFUL: for every
valid engine input `idx` is already in range, so the in-range path is
byte-identical; only the would-be-OOB index is pinned. Regression:
`TileLightingHardening.SlopeLightLutIndexClamp` (incl. the exact `d == -1` →
`idx == 1023` boundary).

## Findings DOCUMENTED, NOT changed (BEHAVIORAL — need MCP)

### A. `LookupTileAttribute` div-by-zero on `bucketSize == 0`
`src/render/terrain_tile_query.cpp` line 16 — `200*(y/bs) + 69 + 25*(x/bs)`.
With `bucketSize == 0` this is a division by zero → SIGFPE (UBSAN: "division by
zero"; ASAN: FPE).  The original `gilde.exe 0x5c3418` performs the same `idiv`,
which on x86 traps identically — i.e. the original would ALSO crash on a
zero-bucketSize grid.  Adding a guard would CHANGE observable behavior
(return -1 vs. trap), so this is a 1:1 question, not a memory-safety bug the
reconstruction introduced.  BEHAVIORAL — needs MCP to confirm whether @0x5c3418
guards `a1[1] == 0` before the divide.  `bucketSize == 0` is a degenerate/invalid
grid the heightmap setup never produces; the tests do NOT pass it (suite stays
green).  The valid-bucketSize bounds (x/y strictly inside the grid, x>=size →
-1, chained entries) are covered by `render_terrain_scan_test`.

## Functions PROVEN safe under degenerate inputs (no fix needed)

* `MipDownsample(src, n, dst)` — for any `n` (odd or even), `m = n>>1` and the
  max index `(2m-1)*n + (2m-1) < n*n`, always in-bounds (odd n drops the last
  row/col). `n == 0` → no-op.  Test: `TileLightingHardening.MipDownsampleMinSize`.
* `BuildLitTileGeometry` — all three branches (r==1 equal-size, ratio>1
  downsample, r>1 mip pyramid) with flat/degenerate heights + the null/zero
  guards. Test: `TileLightingHardening.BuildLitTileGeometryDegenerate`.
* `ComputeTileIllumination` — `value[t & 7]` is masked; `size == 1` (mask 0)
  folds every (x,y) to cell 0; hole gate (high bit) returns 0.
* `BuildTileIlluminationTable` — the stored pattern index is always in `[0,14]`
  (the empty-string pattern 14 is the always-match tail).
* `BuildTerrainUvTable` — `size == 0` guarded to 1; all 24 floats written even at
  size 1/2.  Test: `TerrainGroundHardening.UvTableTinyMips`.
* `FloodFillTileType` — only interior cells `[1,n-1)` written; n==1/n==2 → empty
  loop; full/empty/checkerboard grids all stay in `n*n*24`.  Test:
  `TerrainGroundHardening.FloodFillPatterns`.
* `AverageAreaHeight` — the 8x8 box at a grid corner falls back to the bilinear
  sample for off-grid cells (no OOB).  Test:
  `TerrainGroundHardening.AverageAreaHeightCorners`.
* `FindNearestWalkableTile` — off-grid centre (negative / over-size), full grid,
  size-1 grid: every (row,col) read is clamped to `[0,size)`.  Tests:
  `RenderTileQueryHardening.Walkable*`.
* `ScanRowHeightRange` — span normalised + clamped to `[0,width-1]`; fully-clipped
  / out-of-range rows leave the seeded accumulators untouched.  Test:
  `RenderTileQueryHardening.ScanRowBorders`.
* `RenderTerrain` whole-walk over the SMALLEST (size 8, span 1) and LARGEST
  (size 128, span 16) grids, the index-7 edge clamp (base − 4/lod underflow → 0
  iterations), and the grid-border (7,7) wrap-mask tile, with per-tile buffers
  sized EXACTLY to the LOD-1 subdivision.  Tests: `TerrainWalkHardening.*`.
* `TileSubdivCount` / `SelectTileMeshLod` at min/max LOD + negative/extreme
  `scaleX` (no UB).  Tests: `TerrainRenderLod.*`.

## Edge tests added (per owned file)

* `render_terrain_walk_test.cpp` (+4): `SmallestGrid8`, `LargestGrid128`,
  `BorderTileWrapMask`, `StitchPolyCountStaysInBuffer` (OOB regression).
* `render_tile_lighting_test.cpp` (+5): `SlopeLightLutIndexClamp` (OOB
  regression), `MipDownsampleMinSize`, `IlluminationTable15PatternBounds`,
  `IlluminationSizeOneAndHoleGate`, `BuildLitTileGeometryDegenerate`,
  `StampLightCircleBorder`.
* `terrain_ground_test.cpp` (+3): `UvTableTinyMips`, `FloodFillPatterns`,
  `AverageAreaHeightCorners`.
* `render_terrain_scan_test.cpp` (+4): `WalkableOffGridCenter`,
  `WalkableFullGridCorners`, `WalkableSizeOne`, `ScanRowBorders`.
* `render_terrain_render_test.cpp` (+2): `TileSubdivCountMinMaxAndUnderflow`,
  `SelectMeshLodExtremes`.

## Build/test status

ASAN+UBSAN (`-fno-sanitize-recover=all`) — owned targets, all CLEAN:
* render_terrain_walk_test 1629, render_tile_lighting_test 126,
  render_tile_textures_test 79, render_terrain_test 135,
  render_terrain_render_test 50, render_terrain_scan_test 88,
  terrain_mesh_test 316, floorgfx_recon_test 54, terrain_texturing_test 32,
  terrain_render3_test 114 — 0 failures, 0 sanitizer reports.

Normal `build/` (non-ASAN): full build green; full `ctest` = 1464/1464 passed.

## Out-of-cluster sanitizer findings (NOT this agent's files — left for the owner)

* `src/render/raster_textured.cpp:52` and `:80` — UBSAN "left shift of negative
  value -937" (surfaces via `terrain_ground_test` → GroundFrameRender). The
  rasterizer is the water/raster agent's cluster, not terrain.
* `tests/unit/terrain_render2_test.cpp:78` — UBSAN "store to misaligned address
  … requires 8 byte alignment" in the test's `PickFloor::setptr` harness
  (stores a `const void*` into an unaligned offset of a `raw[256]` buffer).
  `terrain_render2` tests `render/terrain_render2.h`, NOT this cluster's
  `render/terrain_render`. Pre-existing; left untouched.
