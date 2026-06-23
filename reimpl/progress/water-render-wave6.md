# Wave 6 — W6-WR: the WATER RENDER ARM + full water-pipeline wiring

Scope: close the wave-5 gap (water is BUILT + ANIMATED but NOT DRAWN). Reconstruct
the water render arm and wire the full pipeline end-to-end: build-on-load →
animate-per-frame → render. IDA MCP was LIVE; the draw function + its call site
were decompiled fresh and reconstructed 1:1.

Owner files (NEW / mine): `src/render/water_render.{h,cpp}`,
`src/play/terrain_render.{h,cpp}` (FloorGround water fields + GroundFrame water
pipeline), `src/play/city_view3d.{h,cpp}` (Options::water + per-frame wiring),
`tests/unit/water_render_test.cpp`, `tests/e2e/water_render_e2e_test.cpp`.

---

## KEY FINDING — where water is drawn

`VIBE_Floor_RenderTerrain @0x5bf22c` does **NOT** contain a per-region water draw.
Its only water touch is the per-frame ANIMATE: at `0x5c2a4f..0x5c2a6f` it loads
`floor[0x19E0]` (mesh array) + `floor[0x1C6D]` (count) + the clock `dword_62EB38`,
calls `VIBE_Floor_AnimateWaterVertices @0x5be428`, and `jmp loc_5C15D3` straight
into the Phase-2 per-tile draw-list append. No waveOut-consuming draw block exists
inside @0x5bf22c.

The water DRAW is `VIBE_Floor_TransformTileGeometry @0x5be668` — called PER TILE
from RenderTerrain's Phase-2 tail at `0x5c17fd` (inside the per-tile append loop).
Despite the generic IDA name it is specifically the water sub-pass (it is the only
reader of the WaterMesh waveOut grid besides the animator). Verified via
`xrefs_to 0x5be668` → single caller `0x5bf22c @0x5c17fd`, and
`xrefs_to AnimateWaterVertices` → single caller `0x5bf22c @0x5c2a6a`.

## The water render arm — VIBE_Floor_TransformTileGeometry @0x5be668 (4 blocks)

`__usercall (floor@eax, tile@edx) -> int`. Gates on tile+94 (LOD nonzero) and
tile+96.

1. **SHADE PREP (0x5be6a5..0x5be73a).** Normalise the floor height axis in view
   space (`dword_13FD520/524/528` == st.axisH) via `VIBE_Math_VectorNormalize
   @0x5cb148`; `waterShade = flt_628B2C - |axisH_norm . sunDir|`
   (flt_628B2C = **0.5**, sunDir = flt_5CA2B0/B4/B8 = **(0,0,1)**, get_bytes-verified).
2. **WATER VERTEX BUILD (the span loop, 0x5be74d..0x5bea49).** Walk the tile's
   20-byte WaterStripSpan list (tile+52). Per span:
   * `region = span.marker` (the FindRegionOffset-stamped id); `mesh = floor[1656]
     + 344*region` (Floor+0x19E0 record).
   * `base = originView + row*axisV + col0*axisU` (row = span+4, col0 = span+8;
     flt_1404250.., flt_13FD500.., flt_13FFD40..) — traced from 0x5be755..0x5be838.
   * Per column c: `wave = mesh.waveOut[(row&3)*4 + (c&3)]` at **float[14]+...
     (byte +0x38)**; the vertex world pos `= axisH_view*waterHeight + wave.xyz +
     base` (0x5be8be..0x5be909 fld/fadd chain). The per-vertex light is the
     terrain type byte's lit/shadow branch (LABEL_42 @0x5becb1 == the SAME
     BuildTileVertex lighting: l = (type&0x7F)*2.0; flt_628B30 = **2.0**).
3. **PROJECT (0x5bea4f..0x5bee30).** `ComputeVertexClipFlags @0x5ad614` then the
   per-vertex `1/z` reproject (flt_13FCD0C/D18/AF8/D10; the no-fog branch @0x5bee13,
   the byte_649DD8 fog branch is the city's unused arm).
4. **APPEND (0x5bec1d..0x5bef03).** Per visible front-facing water poly: resolve
   `mesh = floor[1656] + 344*poly.regionId`, read the water texture handle
   `mesh[1]` (mesh+4), append a draw-list entry with sort key `((tex-texBase)>>7)+1`
   (or **0** when the handle is null — the `if(!tex)` branch @0x5bef03 — the white
   default), stamp `dword_649D58` into the texture +84.

Constants recovered (get_bytes): flt_628B2C=0.5, flt_628B30=2.0, flt_628B3C=255.0,
flt_5CA2B0/B4/B8=(0,0,1), dbl_628B34=255.0.

Reconstructed as `render::RenderWaterSurface` (water_render.cpp) over the parsed
`WaterRegions` (spans/polys/waterMeshes) + the live floor heights/types/axes; the
vertex math is the exact decompile; exposed sub-steps `ComputeWaterShade` /
`BuildWaterVertexPos` are golden-tested.

## The WaterMesh layout (RAW vs struct — handoff correction)

`BuildWaterRegions` (wave-5) stores `waterMeshes` as the **RAW 86-float (344-byte)
engine layout** (floorwater.cpp Pass-5a), NOT the named `render::WaterMesh` struct.
`RenderWaterSurface` reads the raw layout directly (waveOut at float[14], member
float[1], texPtr float[0], amp float[5]/[2]). `AnimateWaterVertices @0x5be428`
consumes the named struct, so `GroundFrame::AnimateWater` BRIDGES raw→struct→raw
(copying the advanced waveOut/phase/accum/member/lastTime back). This resolves the
wave-5 handoff's `(WaterMesh*)water_.waterMeshes.data()` cast (the doc-level
simplification was layout-incompatible).

## Pipeline wiring (rule 13)

* **Build on load** — `play::FloorGround` gains `waterHeights` / `waterType` /
  `hasWater`. `BuildFloorGroundFromBlock` fills them: waterType = the min-normalized
  texGrid slot whose typeName UPPER-contains "WASSER" (the loc_5CB930 name match the
  builder takes as a parameter), waterHeights = the block's floor+0x18 array.
  `GroundFrame::BuildWater` runs `render::BuildWaterRegions @0x5ba95c` and sizes the
  water surface buffers. Wired in `CityView3D::doRenderTerrain` (once per floor bind).
* **Animate per frame** — `GroundFrame::AnimateWater` runs `AnimateWaterVertices
  @0x5be428` (the bridge above) when the frame's water-anim gate (a2) is set —
  exactly RenderTerrain's Phase-2 arrangement. CityView3D drives it from a monotonic
  per-frame tick (the dword_62EB38 clock seam, +33/frame; dt>0 gates the wave work).
* **Render** — `GroundFrame::Render` appends the water surface (`RenderWaterSurface`)
  into the SAME ground draw list, AFTER the ground tiles, BEFORE the `RasterizeMesh
  List @0x5AEC88` flush, under the SAME view-space axes / projection scalars / light
  params Phase-0 gathered (`render::TerrainRenderState`) — the engine's per-tile tail
  position. Water flushes through the same textured leaf with the water texture (or
  the white default headless).

## Additive & safe

Gated behind `CityView3D::Options::water` (sibling of `terrain`, **default OFF**).
A scene with no water (`waterMeshCount == 0`) or `water=false` leaves the draw list
and every rasterized pixel **byte-identical** to today — confirmed by the full
suite passing with NO pinned-frame recalibration (no test expectation changed).

## scene_floor handoff

No edit to scene_floor.* (read-only). `SceneFloorBlock` already exposes
`waterFlag` / `waterHeights` / `textureGrid` / `typeNames` / `gridN`; the waterType
derivation (WASSER name match → min-normalized slot) lives in MY
`BuildFloorGroundFromBlock` (terrain_render.cpp).

## Real AUGSBURG proof (water_render_e2e)

`LoadCity("AUGSBURG")`: floor `hasWater=1`, `waterType=5` (the WASSER slot),
`waterHeights=16384` (128²). Water ON builds **1 region** (the WASSER-typed cells;
wave-5's 25-region figure used `DominantNonzeroType`, not the WASSER match), **234
water vertices**, **400 water polys**, and the shared flush rasterizes **1303 tris
vs 903** ground-only — i.e. exactly the **400** water polys reached the rasterizer
(`terrainRasterTris - base == waterPolys`, pinned). Determinism: two fresh views
running an identical 3-frame animate sequence reach byte-identical final frames.
Artifact: `/tmp/guild_w6_water.ppm` (320×240).

## Named gaps (rule 8 — addresses)

* **Blue water texture** — `EF_WASS_06A_2T_W_AN0` via `VIBE_Texture_LoadByName
  @0x5da714` + `UploadToSurface @0x5db234` (DDraw, rule-3 Vulkan-routed) is the
  present-coupled `WaterTextureLoadFn`; HEADLESS passes null → the engine's
  `if(!tex)` white-default branch (@0x5bef03) draws the water co-located with and
  shaded like the ground. So headless water is real geometry but not blue/animated-
  pixel-distinguishable; the wave-grid animation IS applied (golden-tested at the
  vertex level in water_render_test.AnimatedWaveMovesVertices) and the draw is
  proven by the +400 rasterized triangles. Wiring the real loader (the Textures.BIN
  EF_WASS member) is the on-screen consumer's job (the same path the ground W5-TX
  binder uses).
* **Per-cell poly point linkage** — the engine resolves each cell poly's corner
  point indices through `FindRegionOffset @0x5ba824` into the span point list. The
  arm reproduces the vertex math + region binding and assembles the surface as the
  per-cell quad lattice (the pass-4 region-seed predicate: cell + right/down/diag
  all water), addressing cells by a (row,col)→vertexIndex map. The exact span-point
  index threading is the FindRegionOffset RESOLUTION (already reconstructed in
  floorwater.cpp); the produced surface covers the same cells.
* **waterHeight buffer indexing** — the engine indexes a1[6] at `v91 + (c&mask)`
  with `v91 = (mask & span.lo)*N` (0x5be762); the arm samples the live floor
  water-height grid at `[row*N + (c&mask)]` (the faithful per-cell water height).
* **lastTime float round-trip** — the raw record stores lastTime as a dword (the
  engine writes the tick bits); the host bridge round-trips it through float for the
  small monotonic clock (exact to ~16M ticks; documented seam).
* **One-sided cull** — the engine backface-culls per signed area; the arm draws the
  water as no-cull (flags38 bit1, the engine's water +38 bit) so the flat sheet is
  visible at any camera azimuth (dropping only degenerate zero-area tris).

## Tests

* `tests/unit/water_render_test.cpp` — 9 tests, **60 checks**, GREEN:
  ShadeAxisAlignedZ/Perpendicular/ZeroVector (ComputeWaterShade goldens),
  VertexPosCombinesBaseWaveHeight/ZeroEverything (BuildWaterVertexPos),
  BuildsAndAppendsWaterRegion (full arm over a 2×2 synthetic region: verts/polys
  built + appended, headless sort key 0), AnimatedWaveMovesVertices (AnimateWater
  Vertices → moved surface vertices), NoSpansAppendsNothing, TexturedSortKey
  (non-null handle → sort key 1).
* `tests/e2e/water_render_e2e_test.cpp` — 1 test, **19 checks**, GREEN (guarded on
  GUILD_GAME_DIR): real AUGSBURG water built/animated/drawn; water-off == ground-
  only (additive); +400 water tris rasterized; deterministic across fresh views;
  PPM artifact.

Full `ctest` (GUILD_GAME_DIR set) = **1445/1445 pass, 0 failures**. No pinned
ground/terrain/city/playable frame expectation changed (additive default-off).
