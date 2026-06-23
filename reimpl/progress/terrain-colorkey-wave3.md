# Wave 3 — render fidelity W3-A: terrain heights + "magenta" polygons

Scope: the two fidelity problems in the `guild_run --play` 3D city frame
(`/tmp/guild_session_city3d.png`): (1) flat terrain, (2) large pink/magenta
polygon areas. IDA MCP was OFFLINE this wave — every change below is anchored
to decompile evidence already in-tree plus the real game assets (an explicitly
sanctioned evidence source); everything that would need fresh disassembly is a
named gap.

---

## Problem 2 — the "magenta" polygons: ROOT CAUSE + FIX

### Root cause (pinned, NOT the colour-key hypothesis)

The pink areas are **not** colour-keyed texels drawn opaque. Histogramming the
session frame shows the artifact colour is exactly **RGB(200,24,64)** — which is
the 16bpp word **0xC8C8** read through RGB565. 0xC8 = 200 = the ambient shade
byte (`FinalizeVertexShadeLuma(200,200,200)`, the BuildObjectCache @0x5c8218
seed). Mechanism:

* Polys whose material has **no bound texture** fell back to the 8-BIT shaded
  triangle path — `render::RasterizeTexturedTriangle` @0x5F7D58 over
  `FillTexturedSpansShaded` @0x5F7960, whose span writes ONE SHADE BYTE per
  pixel into an **8bpp** surface (the raster.h banner contract).
* The city framebuffer is **16bpp**; the byte writes land as 0xC8C8 byte pairs
  at half resolution — the pink blobs.
* The original can never reach this state: its SMC span dispatch selects the
  16bpp textured span bodies (0x5F71AD..0x5F744x) for a 16bpp target, and a
  texture record with no texels binds the **1x1 "white" default**
  (`VIBE_Texture_BindActive` @0x5db564 slot==0 / `ResetBinding` @0x5db5f0 —
  see `render/texture_upload.h`), so untextured polys still go through the
  16bpp textured span.
* Colour-key disproof on real data: the rendered frame contains **0**
  magenta-family pixels, and across all 2370 Textures.BIN BMPs **no** texture
  with index-0 texels stores magenta at palette entry 0.

Which materials were falling back (AUGSBURG, 110 rendered members, 951
material slots): 894 bind; **23** resolve to **24-bit BMPs** (no palette
indices — e.g. `Bjoern/hz_Dachleisten_Dunkel_AA.bmp`, ~2000 distinct colours);
**34** have no resolvable BMP at all — vegetation seasonal names
(`vg_nm_BIG_Laub_A_s01` ships only as `…_F/_H/_S/_W` seasonal variants) and a
few genuinely absent files (`dc_dchz_05a_1n_rautenziegel`,
`dc_mrwr_Schiefer_moos_Stdtmr_Ka2`, `sf_putz_1a_1n_kirch_Uni`, …).

### What was fixed (1:1, evidence-anchored)

1. **`src/render/raster.cpp` — `FillTexturedSpansShaded` @0x5F7960 surface
   guard** (reconstruction-only, same class as the existing clip clamps): the
   8bpp shade span refuses to write into a non-8bpp surface. All legitimate
   callers (terrain itest, raster tests, play/terrain_render) use 8bpp
   surfaces — byte-identical for them; the impossible-in-original 16bpp misuse
   becomes a no-op instead of framebuffer corruption.

2. **`src/render/texture.{h,cpp}` — the 1x1 "white" default binding**
   (`WhiteDefaultTexture()` / `WhiteDefaultPalette()`): the engine's
   BindActive @0x5db564 / ResetBinding @0x5db5f0 fallback state exposed as a
   `render::Texture` + 256-entry 0xFFFF LUT, so an untextured poly renders
   through the SAME 16bpp textured leaf.

3. **`src/render/raster_textured.{h,cpp}` — the colour-key (MASKED) span
   variant**: `FillSpanLoopMasked` + `RasterizeTexturedTriangleRgbzMasked` —
   the identical 0x5F6C30 pipeline with the masked span body patched in
   (`PatchSpanConstantsMasked` @0x5f753f → `FillSpanTexturedMasked` @0x5F721A:
   `test dl,dl / jz`, source palette index 0 transparent). The plain entry
   `RasterizeTexturedTriangleRgbz` is behaviour-unchanged (all pre-existing
   goldens pass bit-exact).

4. **`src/play/universe_render.cpp` — wiring (rule 13)**: the textured
   SpanDispatch slot now (a) routes a texture whose record carries the
   colour-key trigger (`flags & 8` — the UploadToSurface @0x5db234
   `v9 = (rec->flags & 8) ? 0 : noTransparency` arm) through the MASKED
   triangle, and (b) replaces the 8bpp shaded fallback with the white-default
   binding through the same 16bpp textured leaf. Decoded records default to
   `flags == 0`, so (a) is behaviour-neutral until the flag source is
   recovered (named gap below).

5. **`src/render/meshlist.cpp` — surface-format routing in the default
   slot-3/4 dispatch (`RasterTri`)**: an 8bpp target keeps the shaded path
   @0x5F7D58 byte-identically; a 16bpp target routes through the white
   default + `RasterizeTexturedTriangleRgbz` (the BindActive @0x5db564
   slot==0 state), **LEVEL-shaded**: the engine keys every software draw by
   the poly light LEVEL — `level = max(v0..v2 byte+66)`, the `768*level`
   draw-list key of `ProjectVerticesToScreen` @0x5c5120 (docs/developer/24) —
   and the HiColTab light ramps are LINEAR black→colour
   (`HiColTabAddEntry` @0x5d9db8: `channel/62*step`), so the white default at
   level L presents as the linear gray `PackColor(L,L,L)`. A relight (the
   +66 rebuild) therefore changes untextured polys exactly as the level-keyed
   shade would — this keeps the legacy person/world renders visible AND
   light-sensitive (day/night ambient changes the frame). The CONTENTS of the
   engine's per-level shade palette are a named gap; the linear-gray
   presentation is the ramp-formula-backed host seed.

6. **`src/play/object_mesh_render.cpp` `SpanFillTexturedSample`** — its
   untextured fallback now calls the slot-4 default
   (`render::SpanFillTexturedOpaque`) instead of the 8bpp shaded triangle
   directly, picking up the same format routing. (Without this,
   `session_persons_render_e2e` / `world_render_e2e` regressed to zero person
   pixels under the raster.cpp guard — both verified green again.)

### Results (pinned by `tests/e2e/render_fidelity_w3a_e2e_test.cpp`)

* City overview frame (320x240): **pinkC8C8 3740 → 0** pixels;
  magenta-family 0 before and after. Raster/textured counts unchanged
  (13852 / 9742).
* The session frame (`playable_flow_e2e` TEST E re-run): the pink blobs are
  gone (histogram carries no (200,24,64) at all).
* Masked span over a real city texture (`_DYNAMIC/Partikel/Holzspaene.bmp`,
  sentinel-magenta palette 0): plain draws 2066 idx-0 pixels, masked draws
  **0**, and the two frames differ in EXACTLY those 2066 pixels.

### Named gaps (rule 8) — colour-key / binding

* **Which textures the engine keys**: the texture-record `flags & 8` source
  (the record-create path) and the boot value of `dword_64A1FC`
  ("no-colour-key default") need disassembly. The masked machinery + the
  per-record trigger are wired; the trigger never fires for decoded BMPs yet.
* **24-bit BMP materials** (23 polys in AUGSBURG): the original's HW path
  uploads them via `LoadAndStretchTexture` @0x5dea50 (boundary hook); the SW
  palettize (`LoadSoftPalettize` @0x5da34c, HiColTab "colours actually used")
  cannot represent ~2000 distinct colours in 256 entries. Whether/how the
  original SW path handled 24bpp needs 0x5da34c/0x5f0ce4 decompile. Until
  then these materials render as the white default.
* **Seasonal vegetation textures** (`vg_nm_*` → `…_F/_H/_S/_W`): the texture
  SET selection (`VIBE_Object_SelectTextureSet` @0x5b3f54, the foliage
  "pfl_"/"vg_"/"!vg_" paths in `sim/object_lifecycle2.h`) is a hook
  everywhere — unreconstructed. White default until recovered.
* Genuinely missing BMPs (`dc_dchz_05a_1n_rautenziegel` etc.): absent from
  Textures.BIN — the original also fails the load; white default IS the
  engine state for them.

### HANDOFF → city_view3d owner (file not editable this wave)

`src/play/city_view3d.cpp` `CV3D_SpanTextured` still carries the wave-2 body.
With the raster.cpp guard its 8bpp fallback now draws NOTHING (pink already
gone), leaving sky holes where the engine would draw the white default. To
match `universe_render.cpp`'s wired behaviour, mirror the same two changes:

```cpp
// in CV3D_SpanTextured, textured branch:
const bool keyed = (bt->tex->flags & 8) != 0;            // 0x5db234 v9 arm
int drew = keyed ? render::RasterizeTexturedTriangleRgbzMasked(fb, rv, *bt->tex, bt->palette)
                 : render::RasterizeTexturedTriangleRgbz(fb, rv, *bt->tex, bt->palette);
// fallback branch: REPLACE the RasterizeTexturedTriangle(fb, rrv) call with
// the white default through the same 16bpp leaf:
const render::Texture& white = render::WhiteDefaultTexture();
render::RgbzVertex rv[3] = { {v->screenX, v->screenY, v->u, v->v}, ... };
render::RasterizeTexturedTriangleRgbz(fb, rv, white, render::WhiteDefaultPalette());
```
(Same applies to `src/play/object_mesh_render.cpp` `MakeTexturedDispatch`'s
fallback — also another agent's file.)

---

## Problem 1 — terrain heights: the REAL source found + parsed

### Finding (corrects the wave-2 deferral)

The wave-2 note deferred the city heightmap as "the BuildTerrainMesh height
fill from the boden node mesh". The heights are **not derived from any mesh**
— they are **stored in the scene stream's FLOOR block**:

* `Scene_LoadFromStream` @0x5e7e38 reads, after the recursive object list, a
  **floor flag byte**; nonzero → the floor-region block follows via
  `VIBE_WorldIo_LoadFloorRegions` @0x5e78a8 (gate + position were already
  reconstructed in `render/scene_load.h`; the loader itself was a named hook).
* The floor block begins with a named elevation grid:
  `cstr "<scene>_height"`, `u32 sizeX`, `u32 sizeY`, `u32 third`,
  `u8[sizeX*sizeY]` row-major elevation bytes. Verified over **all 10**
  shipped `Staedte/*.ed3` (tag 0x3A6C00BB): the reconstructed 0x5e67c8 object
  walk lands on the floor flag **to the byte** in every one; grids are 128²
  (cities) / 64² (tutorial/master), 8 of 10 carry real varied terrain
  (0..255). The EMBEDDED `.cty` scene stream (the blob `LoadWorldEx` captures
  at the PostLoadInitScene @0x5a7ef8 position) carries the identical record —
  AUGSBURG's embedded grid is **byte-identical** to the shipped scene's.
* These bytes are exactly the `Heightmap::heights` grid (heightmap.h,
  @0x5c63a0/0x5c65d4); `VIBE_Heightmap_BuildTerrainMesh` @0x5c5610 derives
  the world<->tile scales over them from the scene world bounds
  (`DeriveGridScaleXZ` — already reconstructed 1:1).

### What was built

**`src/render/scene_floor.{h,cpp}`** (new):

* `ParseSceneFloorHeights(ed3, size)` — replays the reconstructed header +
  object-tree grammar (0x5e7e38 / 0x5e67c8 — the same byte walk
  `scene_view.cpp`'s `ParseSceneObjects` performs, duplicated as a pure SKIP
  so the render layer has no play-layer dependency) to the floor flag, then
  reads the heights record. Rejects malformed grids (non-square,
  non-power-of-two, truncated).
* `BuildCityHeightmapFromFloor(f, lo, hi, hm, storage)` — assembles the city
  `render::Heightmap` exactly as @0x5c5610: `DeriveGridScaleXZ` (flt_628BA4 =
  -1.75) + `originY = minY + 1.0`, `scaleY = (maxY-minY) * (1/252.85)`
  (flt_628BA8 per heightmap.h; exact dword to re-confirm when MCP returns).

Verified live (e2e): 49 grid samples through the 1:1
`WorldToTileWithHeight` @0x5c6644 span world-Y 45.7..753.4 over AUGSBURG
(bounds -134..743) — the ground genuinely varies by ~700 world units;
`AverageAreaHeight` @0x427468 (the camera anchor's ground query) returns
202.5 at the city centre, inside the sampled band.

### Provenance / named boundary (rule 8)

* The heights-record layout is recovered from the SHIPPED BYTES (sanctioned
  evidence), cross-validated 10/10 scenes + the embedded stream; the `third`
  header dword (== sizeX everywhere) is of UNVERIFIED meaning.
* The REST of the floor block is NOT parsed (deferred until 0x5e78a8 can be
  decompiled): a sizeX×4·sizeX grid (all zero in shipped cities), a second
  sizeX² byte grid, water records (`"Wasser_Teich_Fluss_blau"` + ~50-byte
  float body), the `"<scene>_texture"` per-cell texture-id grid, and the
  trailing terrain-type name table (`SAND`, `EINFACHER_WEG`, `MOOS`, `FELS`,
  `WASSER`, `ACKER`, `PRUNK_WEG` + floats). The `_texture` grid is the
  natural next pickup for the terrain-render walk's `Floor::textures`.
* `BuildTerrainMesh` @0x5c5610's remaining body (the tile-record/draw-list
  build over this grid) stays the existing named gap.

### HANDOFF → sdl_session owner (file not editable this wave)

`RunSdlSession` builds the camera terrain bind with ZERO heights
(`src/play/sdl_session.cpp`, the "City terrain bind" block). Replace the
zero-fill with the real grid — the scene blob is already in scope:

```cpp
// replaces: terrainHeights.assign(64*64, 0); ... scaleY = 0
render::SceneFloorHeights fl = render::ParseSceneFloorHeights(sceneBlob);
if (fl.ok) {
    render::BuildCityHeightmapFromFloor(fl, lo, hi, terrainHm, terrainHeights);
} else {            // stadt_*.ed3 fallback path: parse the loaded scene bytes
    ...existing zero-fill...
}
cam.BindTerrain(&terrainHm);
```
`SessionCamera::BindTerrain` / `Camera_AnchorToTerrain` @0x4b2900 /
`ClampToTerrainHeight` @0x4b2a0c then follow the REAL ground (their sampling
leaves are the ones the e2e exercises). For `LoadCity()` (scenes.BIN path),
parse the stadt_*.ed3 bytes instead of the embedded blob — CityView3D would
need to expose them or the session can re-open the member.

---

## Files

* `src/render/raster.cpp` — 8bpp shaded-span surface-format guard.
* `src/render/raster_textured.{h,cpp}` — masked span loop + masked Rgbz
  triangle entry (plain entries unchanged).
* `src/render/texture.{h,cpp}` — `WhiteDefaultTexture` / `WhiteDefaultPalette`.
* `src/render/meshlist.cpp` — slot-3/4 default dispatch surface-format routing.
* `src/play/universe_render.cpp` — keyed/masked routing + white-default
  fallback wiring.
* `src/play/object_mesh_render.cpp` — untextured fallback routed through the
  slot-4 default (one-call change; keeps the legacy person render visible).
* `src/render/scene_floor.{h,cpp}` — NEW: floor-block heights parse + city
  heightmap build.
* `tests/integration/world_render_itest.cpp` /
  `tests/integration/object_mesh_render_itest.cpp` — the fixed-pixel oracles
  re-calibrated to the TRUE white-default footprints ((10,25) → (48,50) and
  (24,18) → (48,36)); the old coordinates were calibrated against the
  byte-pair artifact output.
* `src/play/full_session.{h,cpp}` — additive `frameBeforeHash/frameAfterHash`
  (FNV over the device backbuffer) in the session trace;
  `tests/integration/full_session_itest.cpp` asserts frame CONTENT change
  (the nonClear COUNT is blind to a same-coverage layout change under the
  uniform level-shaded fill).
* `tests/unit/render_raster_test.cpp` — +1 test (guard).
* `tests/unit/render_raster_textured_test.cpp` — +3 tests (masked span golden,
  masked triangle == golden minus idx-0, white default).
* `tests/unit/render_scene_floor_test.cpp` — NEW: 4 tests / 30 checks
  (synthetic stream golden, flag-0, malformed/truncated, heightmap scales +
  TileToWorld/WorldToTileWithHeight round trip).
* `tests/e2e/render_fidelity_w3a_e2e_test.cpp` — NEW guarded suite: pink-pixel
  pin (3740 → 0), texture survey, masked-span-on-real-texture pin
  (2066 → 0), terrain floor-block parse over all 10 scenes + embedded .cty +
  live heightmap sampling. 4176 checks green with assets.

## Test results

* `render_raster_test` 601 checks, `render_raster_textured_test` 552,
  `render_scene_floor_test` 30, `raster_clip_test` 9,
  `texraster_recon2_test` 889 — all green.
* `render_fidelity_w3a_e2e_test` 4176 checks green (real assets).
* `city_view3d_test`/`city_view3d_e2e_test`, `universe_render_e2e_test`,
  `playable_flow_e2e_test` (incl. the integrated 3D session) — green,
  unchanged counts. `atmos_lighting_itest` (37), `full_session_itest` (27),
  `world_render_itest` (18), `object_mesh_render_itest` (23),
  `session_persons_render_e2e_test` (67), `world_render_e2e_test` (12) —
  green after the level-shaded routing + witness updates.
* Full ctest suite: **1415/1415 pass** (portable Debug + GUILD_GAME_DIR).

Visual artifacts: `/tmp/guild_w3a_city.ppm` (320x240 city overview, zero pink)
and the regenerated `/tmp/guild_session_city3d.ppm` (session frame, pink gone).
