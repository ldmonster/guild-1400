# Wave 4 — T-1: the 3D city draws its REAL ground

Scope: reconstruct what the captured IDA evidence supports of the terrain
tile-walk/draw path, and wire the GROUND PASS into the 3D city frame at the
verified BeginUniverseFrame position, driven by the REAL parsed floor of the
loaded city. IDA MCP was OFFLINE this wave — every claim below traces to the
verification wave's saved decompiles (`/tmp/guild_va_evidence/va3_terrain.md`,
`/tmp/guild_va_evidence/va_parent.md`) or to pre-existing in-tree provenance.

---

## VERIFICATION (what the verification wave established — evidence on file)

From `/tmp/guild_va_evidence/va_parent.md` (17 IDA results + parent agent notes):

* **Floor-block grammar reconciled** — `VIBE_WorldIo_LoadFloorRegions @0x5e78a8`
  full decompile: `Bio_ReadArrayQuick @0x5dcca0` = `u32 elemSize + u32 count +
  payload` (resolves the old "three size dwords" misread); named arrays:
  heights `"d3_fl:Height"` → floor+16, waterHeights `"d3_fl:Water(height)"` →
  floor+24, textureGrid `"d3_fl:Texture"` → floor+20 (min-normalized indices
  into the 8 texture slots), lightOffsets `4*N` `"d3_fl:LightOffset"` →
  floor+32, 344-byte water region records → floor+6624. The in-tree
  `render/scene_floor.{h,cpp}` already carries this (verified, not redone).
* **Exact scaleY bits** — `VIBE_Heightmap_BuildTerrainMesh @0x5c5610` full
  decompile + get_int: `flt_628BA8 @0x628BA8 = 0x3B81848E` (0.0039525721, the
  exact dword in `render/heightmap.h kTerrainScaleYNorm`), `flt_628BA4 =
  0xBFE00000 (-1.75)`, `originY = minY + 1.0` (`@0x5c5b93..0x5c5ba3`),
  `scaleY = (maxY-minY) * flt_628BA8` (`@0x5c5bd0..0x5c5bec`). Verified
  in-tree (scene_floor / heightmap), not redone.
* **The frame hook position (KEY)** — `VIBE_Render_BeginUniverseFrame @0x5b3900`
  full decompile: terrain draws via `VIBE_Floor_RenderTerrain(dword_64A028, a2)`
  **@0x5b3a2f**, AFTER the clear and BEFORE the object scene walk;
  `VIBE_Render_DrawUniverseAndStats @0x5b3bbc` (full decompile) contains NO
  terrain (UV-scroll/pose/stats only). xrefs_to confirms 0x5b3a2f is the ONLY
  caller of 0x5bf22c. In the reconstruction this is `src/render/frame.cpp:76`
  (`fs.hasTerrain && hooks.renderTerrain`) — the hook point pre-existed and is
  what this wave wires.

From `/tmp/guild_va_evidence/va3_terrain.md` (16 IDA results, terrain agent —
interrupted before writing code; its captures are the source of record here):

* `VIBE_Floor_LoadFromHeightmap @0x5bd44c` — FULL decompile (the floor build).
* `VIBE_Heightmap_BuildLitTileGeometry @0x5c47dc` — FULL decompile.
* `VIBE_Heightmap_ComputeTileIllumination @0x5c4718` — FULL disasm + the
  15-pattern table bytes at 0x5c4690 + the loc_5CB930 (strstr) disasm.
* `VIBE_Render_ComputeFilterWeights @0x5b94cc` — FULL decompile (the
  flt_13FE540 UV-table writer; callers `TextureCache_Setup @0x5ba37c` /
  `SetMipFilterLevel @0x5b9e74` also captured; xrefs show 0x5bf22c reads
  flt_13FE540 at 0x5c1ff5/0x5c221c).
* Constant sweeps: `flt_628ADC = 0.5`, **`flt_628AE0 = -64.0 (0xC2800000 =
  u32le 3263168512; captured twice: get_int + the float sweep)**,
  `flt_64A074/78/7C = 200.0` (sun ambient), `flt_64A084/88/8C = 0.0`
  (sun bias), floor+7268 = 0x41A00000 (20.0), floor+7272 = 0x42200000 (40.0).
* `VIBE_Floor_RenderTerrain @0x5bf22c` decompile was TRUNCATED in the capture
  ("[87917 chars total]" — locals + refs only); the py_eval that dumped the
  full bodies of 0x5bf22c / 0x5be668 / **0x5bc45c (BuildTilePolys)** /
  0x5ba1e8 / 0x5be428 / 0x5bef08 / 0x5bd010 / 0x5bced8 / 0x5ba508 wrote to a
  /tmp file that NO LONGER EXISTS. Those bodies are therefore NOT in evidence.

---

## New reconstructions (this wave, all evidence-traced)

* **`src/render/scene_floor.{h,cpp}` — flt_628AE0 corrected to -64.0**
  (`DeriveFloorPlacement` originY = heightScale * -64.0). The earlier -50.0
  (0xC2480000) documentation was a misread; the captured dword is 0xC2800000
  (= 3263168512), confirmed twice. Unit expectation recalibrated
  (`tests/unit/render_scene_floor_test.cpp`: -100 → -128, justification
  comment in the test).
* **`src/render/tile_lighting.{h,cpp}`**
  - `kTerrainTypePatterns[15][9]` — the exact 0x5c4690 pattern bytes
    (`'_ill*','_unk*','SAND','ERDE','WIESE','MOOR','PFLASTER','KIESEL','FELS',
    'EIS','WASSER','WEG','WEG','_ill*',''`).
  - `BuildTileIlluminationTable` — the previously-deferred name→terrain-class
    build half of `@0x5c4718` (slot default 1; first matching pattern index;
    pattern 14 = "" always matches, so non-empty unmatched names land on 14).
    `VIBE_Util_StrToUpper @0x5e9f50`'s body was not captured — uppercase-the-
    name semantics inferred from name + call shape (inert for the shipped
    uppercase slot names; documented).
  - CORRECTED interpretation of Floor+0x1A64: the 8 64-byte slots are the
    floor TEXTURE-SLOT names the `@0x5bd44c` loader copies there (`v144 =
    floor+6756` loop @0x5bd810) — not dynamic light sources as previously
    documented.
  - `BuildLitTileGeometry` — the COMPLETE `@0x5c47dc` driver (was deferred):
    equal/upsample branch (direct fill at stride r, with the heights store
    index reconstructed from the entries-cursor symmetry where the decompile
    lost a register to the ConvertX clobber), the in-place midpoint pyramid
    (`avg4 = sum>>2`, top/left edge midpoints `(avg4 + 2*edgePair)/5u`,
    centre `avg4`, entries midpoints copy entries[A]), and the downsample
    branch (ratio² box-average of ConvertX-truncated elevations + 15-bin
    first-max majority vote of illumination classes).
* **`src/render/terrain_uvtable.{h,cpp}`** — `BuildTerrainUvTable`, the
  deterministic 24-float half of `VIBE_Render_ComputeFilterWeights @0x5b94cc`
  (e = 1/mipTileSize, f = 1-2e; 12 (u,v) pairs = the 4 tile-quad triangles of
  the two diagonal-split modes). The random-jitter tail (the `v27 != 6144`
  loop) is a NAMED GAP: its four output cursors (v33..v36) are unresolved in
  the captured decompile.
* **`src/play/terrain_render.{h,cpp}` — the 3D GROUND PASS**
  - `FloorGround` / `BuildFloorGroundFromBlock`: the parsed floor block →
    engine Floor fields via the byte-exact `@0x5bd44c` math
    (`DeriveFloorPlacement`, `NormalizeFloorTextureGrid`, the 0x5bd707
    heights+textureGrid gate, the `@0x5e7d28` stream-origin override, the 8
    typeName slots).
  - `FloorGroundWorldY`: floor-lattice ground sample (placement inverse).
  - `GroundFrame`: owns the walk's Floor/tile records + buffers; drives the
    COMPLETE in-tree `render::RenderTerrain` (`@0x5bf22c`, terrain_walk) with
    per-tile LODs through the REAL leaves (`ComputeTileVertices @0x5bdec4` →
    `ComputeTileCenterRadius @0x5bef08` → `ComputeLodLevel @0x5ba438` with the
    captured thresholds 20.0/40.0 → `StitchTileLod @0x5bef08`), the real
    `ComputeVertexClipFlags @0x5ad614` as the clip hook, then flushes the
    appended draw list through the REAL `RasterizeMeshList @0x5AEC88` under
    the same six-plane clip set + SetupViewTransform scalars the city object
    flush uses. The flt_13FE540 UV-table image is built at bind.
  - **Floor+0x08 mask fix (evidence-backed)**: `@0x5bd54e..0x5bd55e` writes
    `(N-1) | (N*N-1) = N*N-1` to floor+8 (the LINEAR cell mask) and `N-1` to
    floor+12. The ground pass feeds `mask = N*N-1`; `render/terrain_walk.h`'s
    doc comment claims "size-1" (which collapses the walk's linear cell
    arithmetic onto the first grid row) — **handoff to the terrain_walk
    owner** (file not mine this wave).

## Wiring (rule 13)

* **`src/play/city_view3d.{h,cpp}`** — `Options::terrain` (DEFAULT OFF) +
  `Result::terrainDrawn/terrainTiles/terrainPolys/terrainRasterTris`.
  `LoadCity`/`LoadCityFromWorld` parse the floor block of the SAME stream
  (`ParseSceneFloorBlock`) and build the `FloorGround`. `RenderFrame` installs
  `fs.hasTerrain + hooks.renderTerrain = CV3D_RenderTerrain` so the ground
  draws inside `render::BeginUniverseFrame` at the verified 0x5b3a2f position
  (frame.cpp:76), under the SAME camera/frustum/projection scalars as the
  city objects (`buildViewParams`, a pure factor of the doSceneWalk prologue).
  `cityHeightmap()` wires the new `@0x5c47dc` fill live: the city Heightmap
  (scales per `@0x5c5610`) with REAL remapped heights + per-cell terrain
  classes (feeds `TileToWorld`/`WorldToTileWithHeight`/`AverageAreaHeight`).
* **`src/play/universe_render.{h,cpp}`** — same hook wiring behind
  `Options::ground` (+ groundEye/groundRot/near/far); default unbound, frames
  byte-identical.
* `BuildTileVertex`, `AppendTilePolysToDrawList`-class leaves, TileSubdivCount
  etc. were already live through `render::RenderTerrain`; the ground pass is
  their first REAL-data 3D consumer.

## Option default + recalibrations

* `CityView3D::Options::terrain = false` (additive): every pinned frame
  (city_view3d, universe, playable_flow, full_session, fidelity e2es) stays
  byte-identical — verified by the full suite (1424/1424).
* ONE pinned expectation recalibrated: `render_scene_floor_test`
  FloorPlacementDerivation originY -100 → -128, justified by the captured
  flt_628AE0 dword (see above).

## Named gaps (rule 8)

* **Tile textures**: `VIBE_Floor_LoadTexture @0x5bd010` (slot name → texture)
  and `VIBE_TextureCache_GetOrBuildTile @0x5ba1e8` bodies are not in evidence
  (lost dump) — `getOrBuildTile` stays inert; ground polys render through the
  engine's untextured 16bpp LEVEL-shaded white-default leaf (the established
  in-tree binding behaviour). The texture grid + the 8 slot names + the
  illumination classes ARE parsed/live, ready for the binding when recovered.
* **Floor+0x1C per-cell type/light fill** (`AllocLightBuffers @0x5bced8` body
  lost): host seed = the normalized texture grid byte (distinct level shade
  per terrain class through `BuildTileVertex`'s `(type&0x7F)*2` term).
* **Floor+208/212/216 light scale writer**: not captured; host seed 1.0. Sun
  ambient/bias ARE the captured values (200/200/200, 0/0/0).
* **Row winding (re-verify @0x5bf22c when MCP returns)**: the in-tree walk's
  Pass-A quad order (translated from a decompile no longer in evidence) winds
  CLOCKWISE for a floor seen from ABOVE under the engine projection — every
  poly backface-culled (verified over real AUGSBURG data from both azimuths).
  The original observably draws the ground from above, so the ground pass
  feeds the ROW-MIRRORED lattice (rows reversed; origin' = origin +
  (N-1)*axisV, axisV' = -axisV): the IDENTICAL world surface, opposite
  traversal winding. The walk itself is untouched; flagged for re-decompile.
* **ComputeFilterWeights jitter tail** (cursors unresolved — above).
* `UpdateTileVisibility @0x5bef08` whole-walk change-detect: forced "rebuild"
  per frame (the LOD leaves are real; the per-frame buffers are rebuilt).
* `Polygon::flags38 bit0` collision: the walk's split selector vs this repo's
  translucency proxy — bit cleared before the flush (split already baked into
  the vertex pointers).

## Tests

* `tests/unit/terrain_ground_test.cpp` (NEW, 98 checks): UV-table goldens
  (mip 64 + 4), illumination-table build goldens (all 10 terrain classes +
  case-fold + empty/unmatched + the 0x80-grid-byte lookup), BuildLitTile
  Geometry goldens for ALL THREE fill modes (hand-computed from the captured
  decompile, incl. the (avg4+2*edge)/5 pyramid), FloorGround placement/origin-
  override/normalization/gate, FloorGroundWorldY, and a headless deterministic
  GroundFrame render (walk + flush, 16bpp).
* `tests/e2e/terrain_ground_e2e_test.cpp` (NEW, guarded, 34 checks): real
  AUGSBURG — floor block parses (128 grid, accepted heights+textureGrid),
  terrain=false is bit-inert, terrain=true draws (nonClear 23910 → 59939;
  below-horizon band 7201 → 19172 of 19200; 64 tiles, 888 polys), ground
  meets the building bases (926 placed instances sampled, **909 within 10% of
  the terrain Y span; mean |dy| = 19.0 world units over a 1275-unit span**),
  the @0x5c47dc-filled city heightmap is live (varied heights + classes,
  TileToWorld in band), deterministic re-render, artifact dumped.
* Recalibrated: `render_scene_floor_test` (-64 originY; see above).
* Suite results: full **ctest 1424/1424 pass** (with GUILD_GAME_DIR; includes
  the 2 new tests). Neighbour suites individually verified: render_scene_floor
  127, render_tile_lighting 97, render_terrain_walk 1598 (+e2e 4721),
  play_terrain_render 32 (+e2e 12), city_view3d 52 (+e2e 52),
  wire_terrain_bridge 36 (+e2e 15), universe_render_e2e 22,
  render_fidelity_w3a_e2e 3976 — all green.
* Artifact: `/tmp/guild_t1_ground.ppm` (320x240, the AUGSBURG overview with
  the real ground: relief + horizon under the textured buildings).

## HANDOFF → sdl_session owner (file not editable this wave)

The session opt-in is ONE line in `RunSdlSession`'s `if (use3d)` block
(src/play/sdl_session.cpp, ~line 523):

```cpp
        opt3.textured = cfg.textured && view.texturesMounted();
        // GROUND PASS (terrain-ground wave 4): the real parsed city floor
        // through the BeginUniverseFrame @0x5B3900 0x5b3a2f arm. No-op when
        // the loaded scene carried no floor block.
        opt3.terrain = view.hasGround();
```

NOTE: flipping this changes the session's 3D frames — `playable_flow_e2e`'s
session-frame pins (frame hashes / nonClear counts) will need recalibration by
their owner in the same change. Optionally the camera terrain bind can upgrade
from the raw-copy heightmap to the REAL `@0x5c47dc` fill:

```cpp
        // replaces the BuildCityHeightmapFromFloor raw-copy bind:
        if (const render::Heightmap* hm = view.cityHeightmap())
            cam.BindTerrain(hm);            // owned by `view`; REAL lit fill
```
(also recalibrates camera ground heights — same owner caveat).

## HANDOFF → terrain_walk owner

`render/terrain_walk.h` `TerrainFloor::mask` doc says "+0x08 ... == size-1";
the captured `@0x5bd44c` evidence (`@0x5bd54e..0x5bd55e`) shows floor+8 =
`(N-1)|(N*N-1)` = **N*N-1** (linear cell mask; N-1 lives at +12). The ground
pass feeds N*N-1. Suggest correcting the doc + the walk-harness tests, and
re-checking Pass A's quad vertex order (the row-winding gap above) when the
MCP returns.
