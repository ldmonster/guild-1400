# Wave-10 hardening — W10-WATER cluster

MCP-free memory-safety + edge-case pass over the wave 5-9 WATER reconstructions.

**Cluster (owned):**
`src/render/water_render.{h,cpp}`, `water_vertices.{h,cpp}`, `water_anim.{h,cpp}`,
`floorwater.{h,cpp}`, `scene_floor.{h,cpp}` and their tests
(`floorwater_regions_test`, `water_render_test`, `water_vertices_test`,
`render_scene_floor_test`, `water_vertices_itest`, the matching e2e).

Built + run under `-fsanitize=address,undefined -fno-sanitize-recover=all`
(`build-asan/`). Normal `build/` kept green too.

---

## Memory-safety / UB bugs FIXED (faithful — in-bounds path byte-identical)

### 1. FloodFillMask — native stack overflow (recursion)  `src/render/floorwater.cpp`
`VIBE_FloorWater_FloodFillMask @0x5ba750` is genuinely recursive (explicit
`+x/-x/+y` descents, iterative `-y` tail). On a large connected water region the
true recursion overflows the call stack.

- **ASAN proof:** `BuildWaterRegions` on a full-grid water mask:
  - N=64/128/256/512 — OK (but N=128 already runs ~1056-deep span runs)
  - **N=1024 — `AddressSanitizer: stack-overflow` at floorwater.cpp:30/35** (the
    recursive descent).
  - The largest shipped grid is N=128, but an N=4096 floor would blow the stack
    outright; the recursion depth is unbounded in cell count.
- **Fix:** converted the recursion to an explicit heap work-stack
  (`std::vector<pair<int,int>>`), preserving the exact neighbour set, bounds
  tests, and `== from` push predicate, plus a `from == to` no-op guard (the
  original never fills `to == from`; the guard prevents an infinite loop) and an
  out-of-range / non-fillable seed guard.
- **Why faithful:** a 4-connected flood fill assigns ONE region id to a whole
  connected component — the result (the region grid) is INDEPENDENT of visit
  order, so the explicit stack is byte-identical in observable output. Verified:
  - `render_terrain_test.RenderTerrainWater.FloodFillMask` still green.
  - `floorwater_regions_test` determinism + region-grid goldens unchanged.
  - N=1024 / N=4096 full-grid now complete with one connected region, no overflow.

### 2. Pass-1 dilation index — signed integer overflow (UB)  `src/render/floorwater.cpp`
The 3x3 wrap-around dilation computes neighbour indices like
`n * (wrap & rowM1) + ...` then reduces with `& wrap` (wrap = N*N-1). The engine
runs this in 32-bit registers (imul/add) and relies on the trailing mask; in C++
the signed `int` multiply overflows.

- **UBSAN proof (surfaced once the stack-overflow was fixed, at N=4096):**
  `floorwater.cpp:188 runtime error: signed integer overflow: 16777215 * 4096
  cannot be represented in type 'int'` (row=0 → `wrap & -1 == wrap`, `n*wrap`
  overflows).
- **Fix:** compute the dilation indices in `u32` (`uwrap`, `un`, `urow`, etc.).
  Unsigned wrap is defined and matches the x86 2's-complement `imul`; the trailing
  `& uwrap` masks the index back into `[0, total)`, so the write is always
  in-bounds.
- **Why faithful:** for all shipped grids (N ≤ 128) the intermediates never
  overflow, so the computed indices — and the entire mask — are identical. The
  change only makes the documented mod-2^32 wrap defined at degenerate large N.
  Verified: all dilation goldens (`SingleCellDilates3x3`, `DilationEdgeWrapNoOOB`,
  determinism) unchanged; N=4096 full-grid now clean under UBSAN.

No other OOB/UB/leak found in the owned source. Audited and confirmed safe:
`FindRegionOffset` (bounded by spanCount + terminator peek), `RenderWaterSurface`
(region/mesh-stride/point-slot/vbuf/pbuf/dl all guarded), `AnimateWaterVertices`
(fixed-size record members), `BuildWaterVertexPos`/`ComputeWaterShade`,
`scene_floor` parsers (length-framed reads, eof-guarded).

---

## Edge / degenerate tests ADDED (drive ASAN through the bounds)

**floorwater_regions_test.cpp** (47 → 372 checks):
- `DegenerateSizesNoUB` — n ≤ 0, null grid, n ∈ (0,4] (interior scan never runs).
- `SingleCellRegionNoOOB` — a 1-cell water region on the smallest grid that dilates.
- `FullGridLargeRegion` — N=128 every-cell-water (pins fix #1; one region, no seeds).
- `CheckerboardManyRegions` — max-region flood-fill stress (mesh-array sizing).
- `FloodFillEdgeCases` — 1x1, out-of-range seed, `from==to` no-op, non-fillable
  seed, full corner fill.
- `FindRegionOffsetOutOfRange` — below/gap/above all spans, spanCount short of the
  terminator (in-bounds reject).
- `DilationEdgeWrapNoOOB` — water at the last interior cell + a (0,0) corner so the
  `& wrap` neighbour path runs (pins fix #2; every mask byte is 0x00/0xFF).

**water_render_test.cpp** (127 → 140 checks):
- `NoWaterMeshCountZero` — a scene with NO water (meshCount 0) → no-op no-append.
- `OutOfRangeRegionSkipped` — span marker ≥ meshCount skipped (no OOB mesh read).
- `CapacityBoundariesClamp` — undersized vbuf(5)/pbuf(3)/dl(2) + zero-capacity:
  emit/append clamp to capacity (ASAN red-zone check on the 344-byte stride walk).

**water_vertices_test.cpp** (52 → 401 checks):
- `DriverZeroCount` — count==0 touches nothing.
- `DriverDtExactlyZero` — dt==0 gate boundary (wave half skipped).
- `DriverHugeDt` — dt ~2.1e9 → phases/accums/grid finite and reduced.
- `PropagatePhasesEdge` — dt 0 and 1e9.
- `MeshArrayRecordBounds` — 3-record array, mixed dt, full grid write (record stride).

**render_scene_floor_test.cpp** (127 → 138 checks):
- `EmptyAndTinyStreams` — null/empty/sub-header/truncated-at-objcount.
- `DegenerateGridDimensions` — N=0 (not pow2), N=1 (accepted), N=8192 (>4096 cap,
  count-mismatch reject path). (Existing tests already cover truncated payload,
  count!=N, elemSize!=N, non-pow2 N=6.)

---

## Cross-cluster ASAN findings (NOT owned — flagged for the relevant wave owners)

These pre-exist my changes and live in files I must not edit:

1. **`src/render/raster_textured.cpp:53` — UBSAN left-shift of a negative value**
   (`left shift of negative value -4063232`). Surfaced by `water_render_e2e_test`
   driving the textured rasterizer. → RASTER cluster owner.

2. **Memory leak in `compress::Inflater::NewBlocks()` (`src/compress/inflate.cpp:
   1094/1095`)** — the inflate huffman/window blocks `new`'d per `Inflater` are
   never freed. Reached on every zip extract via
   `InflateRaw → ZipArchive::ExtractCurrentFile (zip_archive.cpp:437) →
   ArchiveMount::OpenMember`. Reported by BOTH guarded e2e tests when real assets
   are present (GUILD_GAME_DIR):
   - `floorwater_regions_e2e_test`: ~12.9 KB / 3 allocs (via `CityView3D::LoadCity`
     → `OpenMember`).
   - `water_render_e2e_test`: ~20.5 MB / 4797 allocs (via `RealTextureSource::
     DecodeAndPalettize → TextureBin::Decode (texture_bin.cpp:141) → OpenMember`,
     called once per bound texture during the scene walk).
   This is a single root cause in the COMPRESS cluster (`Inflater` missing
   cleanup / missing destructor delete). NOT a water-cluster bug — the water
   builder/animator/render arm allocate only `std::vector` (no raw `new`), and the
   `BuildWaterRegions` frame that appears in one leak group is merely an unrelated
   allocation higher in the same call chain. → COMPRESS owner (+ verify
   `Inflater`'s destructor frees `NewBlocks()`).

---

## Behavioral ambiguities — NONE this wave
Both fixes are pure memory-safety with byte-identical observable output on the
shipped grid sizes; no `1:1` behavioral question requires MCP confirmation. No
golden values were changed.

## Verification summary
- `build-asan/`: floorwater_regions_test 372/0, water_render_test 140/0,
  water_vertices_test 401/0, render_scene_floor_test 138/0, water_vertices_itest
  94/0, water_vertices_e2e_test 368/0 — all clean under ASAN+UBSAN (no
  leak/OOB/UB).
- `build/` (normal): same suites green; render_terrain_test 135/0 (cross-checks the
  refactored FloodFillMask).
