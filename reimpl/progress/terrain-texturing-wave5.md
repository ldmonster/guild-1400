# Wave 5 — W5-TX: TEXTURE THE TERRAIN GROUND

Scope: close the wave-4 / W5-TILE handoff — the FloorTextureResolver (per-cell
texture-id -> slot name -> loaded `render::Texture`) was reconstructed but NOTHING
consumed it; the 3D city ground rendered as flat LEVEL-SHADED white tiles. This
wave wires the resolver into CityView3D and makes `GroundFrame` rasterize the
ground TEXTURED, 1:1 with the engine's per-quad texture-fetch + UV assignment.

BEFORE: the ground was a flat white-default level-shade (the 16bpp 1x1 WHITE
binding through the textured leaf — meshlist slot 4 fallback). AFTER: the ground
samples the REAL floor slot BMPs (WIESE/SAND/MOOS/FELS/...) decoded from
Textures.BIN, with the per-vertex tile illumination applied as the span light row.

---

## The engine path reconstructed (gilde.exe, imagebase 0x400000)

### VIBE_Floor_RenderTerrain @0x5bf22c — the per-quad texture fetch + UV assign

Decompiled the per-quad poly-emit region (disasm @0x5c1f00..0x5c2044). For each
visible quad, after building the vertex grid:

```
@0x5c1f6f  cellFlag = *(perCellFlag);  test (cellFlag & 0x40)   (BuildTilePolys +0x80 buffer)
@0x5c1f88  subTexId = byte_13DCE58[(quad & 0xFF) + base] & 0x3F   (else 0)
@0x5c1fc8  texRec   = VIBE_TextureCache_GetOrBuildTile(...)        (eax: the bound record)
@0x5c1fcd  uvBase   = subTexId * 0x60                              (0x60 == 96 == 24 floats)
@0x5c1fdc  poly+0x14 = texRec                                      (the bound texture)
@0x5c1fe5  if (cellFlag & 0x80)  (the +0x80 visible/slope bit)
@0x5c1ff2  poly+0x3c = texRec
@0x5c1ff5  uvPtr = &flt_13FE540[uvBase]
@0x5c2008  poly+0x10 = uvPtr           (tri0 UVs: 6 floats == 3 verts x (u,v))
@0x5c2015/@0x5c201b  poly+0x38 = uvPtr + 0x18   (tri1 UVs: next 6 floats)
```

* **`byte_13DCE58`** (the 256-stride per-cell sub-texture-id table) is **all zero**
  in the shipped static image (`get_bytes` 64 bytes = 0) — it is runtime-filled and
  defaults 0, so **subTexId == 0** and only the single 24-float record at
  `flt_13FE540` is used.
* **`flt_13FE540`** is filled by **VIBE_Render_ComputeFilterWeights @0x5b94cc**
  (driven by `VIBE_Render_SetMipFilterLevel @0x5b9e74`) — a SINGLE 24-float record:
  4 triangles x 3 verts x (u,v) of corner-inset UVs `(e..e+f)`, `e = 1/mipTileSize`.
  Already reconstructed in `render/terrain_uvtable.cpp` (`BuildTerrainUvTable`); the
  GroundFrame builds it at bind into `uvTable_[24]`.
* The two triangles of a quad take UV records **T0 = floats[0..5]** (poly+0x10) and
  **T1 = floats[6..11]** (poly+0x38 == uvPtr+0x18).

### VIBE_Floor_BuildTilePolys @0x5bc45c (decompiled, cross-check)

Confirmed the per-cell +0x80 visibility/slope flag (the split selector the walk
already reconstructs) and the per-tile sun-scale write — it does NOT compute UVs
(those are the @0x5bf22c inline assign above). The lighting ramp (`flt_1405110`,
`flt_5CA2B0`) is the tile-light half (already in tile_lighting/terrain_walk).

### The span light row (verified)

`RasterizeMirrorTriangle @0x5f6c30` (render/raster_textured.cpp) selects the span
palette row `lightRow8 = ((l0+l1+l2)/3) << 8` of the three vertex +66 bytes
(`dword_13FC5E0`), indexing `palBase[(avg<<8)|texel]`. `palBase` is the texture's
`*(tex+72)` HiColTab — **63 light-ramp rows of 256 entries** (render/hicoltab.cpp:
row L entry i = colour * round(L/62)). So the engine DOES apply the tile
illumination to the textured span; W5-TX applies it 1:1 (clamped to the 63 ramp
rows the block provisions).

---

## What now textures the ground (the consume + wire path)

### `src/render/texture_asset.{h,cpp}` — the slot-BMP source

* **`TextureAssetCache::SetBmpFetch(BmpFetch)`** (ADDITIVE) — a pluggable
  bare-name -> BMP-bytes hook. The engine's `VIBE_Vfs_ResolveAndBuildPath @0x4500a0`
  resolves the `"*"+name+".BMP"` wildcard over EVERY mounted container (the floor
  slot BMPs are Textures.BIN members, e.g. `_DYNAMIC/Boden/Wiese.bmp`); a loose-file
  `VfsSlurp("*WIESE.BMP")` can't see an archive member. When the hook is set,
  `LoadByName` uses it; unset -> byte-identical to before.
* **`DecodeBmpIntoTexture` 24-bit arm** (faithful @0x5da34c): the floor slot BMPs
  are **24-bit**. An 8-bit `BmpLoadBuffer(8)` of a 24-bit source yields nothing, so
  when it does, the decode now runs the engine's software palettizer
  (`VIBE_Texture_LoadSoftPalettize @0x5da34c -> VIBE_Quant_BuildPalette @0x6029f0`,
  `render/texture_palettize.cpp PalettizeDecodedBmp` over `DecodeBmpBuffer`),
  producing the same 8-bit indices + 256-colour palette an 8-bit source carries —
  exactly what `VIBE_Texture_LoadByName`'s software branch does. The 8-bit path is
  unchanged.

### `src/play/city_view3d.{h,cpp}` — mount the cache + bind/install the resolver

* **`groundTexCache_`** (`render::TextureAssetCache{16}`, exposed via
  `textureCache()`) mounted in `SetupGroundTexCache()` (called from `Init`): its
  `BmpFetch` resolves a bare slot name through the already-mounted `RealTextureSource`
  TextureBin (`ResolveName` -> member, `mount()->OpenMember` -> raw BMP bytes).
* **`BindGroundTextures()`** (called from `doRenderTerrain`, idempotent): copies the
  floor's 8 slot names (Floor+0x1A64, `FloorGround::typeNames[i].name`) into the
  member `groundSlotNames_` (the resolver keeps them BY POINTER — must outlive it),
  `groundTexResolver_.Bind(names, &groundTexCache_)`,
  `SetActiveFloorTextureResolver(&groundTexResolver_)`, and installs
  `getTileTexture = &FloorTextureResolver::GetTileTextureHook` via
  `SetTerrainRenderHooks` — exactly the W5-TILE handoff snippet. Then hands
  `GroundFrame` a `GroundTexBinder` (the per-type record + the 565 HiColTab block).
* **`groundPalette565For(rec)`** builds (once, cached by record ptr) the
  **63-row x 256-entry HiColTab light-ramp block** from the record's source palette
  (row L entry i = palette[i] * round(L/62), packed 565) — the `*(tex+72)` palBase
  the textured span samples; laid out BY SOURCE INDEX so the raw texel index
  addresses it directly (the engine compacts texels onto HiColTab indices in
  LoadSoftPalettize; the resolver keeps source indices, so the block is
  source-index-major — identical ramp values, no texel remap).

### `src/play/terrain_render.{h,cpp}` — GroundFrame consumes the texture

* **`GroundFrame::SetTexBinder(GroundTexBinder*)`** — the per-type texture source.
* **`GroundGetOrBuildTile`** (the `getOrBuildTile` walk hook, @0x5ba1e8 stand-in):
  samples the cell type byte from `texSrc[(v*width+u) & (width*width-1)]`, calls the
  active `getTileTexture(typeByte)`, returns `typeByte + 1` when a texture binds
  (stamped into each quad poly's `uvZ` by the walk = the engine's poly+0x14 texId),
  else 0 (untextured). Installed only when a binder is bound.
* **Post-walk UV-selector pass**: tags each per-tile poly's `uvX` by its poly-pair
  parity (even -> T0 sel 0, odd -> T1 sel 6) — the @0x5bf22c poly+0x10 / poly+0x38
  record selection (the phantom stitch indices carry flags36 < 0x80 and are never
  appended/drawn).
* **`GroundSpanTextured`** (the custom SpanDispatch slot 3/4): for a poly with
  `uvZ > 0`, resolves the texture (`getTileTextureRec(uvZ-1)`) + the 565 HiColTab
  block (`palette565`), reads the 3 corner UVs from `uvTable_ + sel` (* mipWidth),
  sets `RgbzVertex.light = clamp(vertex +66, 0, 62)` (the tile illumination), and
  rasterizes via the 1:1 affine leaf `RasterizeTexturedTriangleRgbz @0x5F6C30`.
  Untextured / unbound -> `render::SpanFillTexturedOpaque` (the established
  white-default level-shaded fallback, byte-identical).

ADDITIVE & SAFE: when no binder is bound (the only state before this wave), the
`getOrBuildTile` hook is not installed, `uvZ` stays 0, the default SpanDispatch
runs, and the ground is the established white-default level-shade — byte-identical.
Verified by `terrain_texturing_test.TexturedVsShadedSelection` (a fresh unbound
GroundFrame's render == the pre-binder render) and by all pre-existing pinned-frame
suites staying green.

---

## Before / after (real AUGSBURG, proof)

* `terrain_texturing_e2e_test` over the REAL AUGSBURG floor:
  - 8 slots resolve: slot[0]="WIESE", slot[1]="SAND", slot[2]="EINFACHER_WEG", ...
  - WIESE decodes to a 64x64 texture (24-bit -> palettized), 251 distinct colours.
  - Textured frame (320x240): **69436 COLOURED (non-grey) pixels**, **distinct
    colours = 2149**, **37827 pixels match the resolved slot textures' exact texel
    colours**, **21918 pixels match the WIESE grass texture**.
  - Determinism: two textured renders are byte-identical.
  - Artifact: `/tmp/guild_tx_ground.ppm` (the textured city ground — grass, stone
    paths, earth, with the buildings on top).
* The white-default path produces ONLY grey (r==g==b) ground; the textured path
  produces thousands of coloured texel pixels — the unit test pins
  `colourShaded == 0` (unbound) vs `colour > 50` + red/blue texel hits (bound).

---

## Files

* `src/render/texture_asset.{h,cpp}` — `SetBmpFetch` hook + the 24-bit palettize arm.
* `src/play/city_view3d.{h,cpp}` — `groundTexCache_` / `textureCache()`,
  `SetupGroundTexCache`, `BindGroundTextures`, `GroundTileTextureRec` /
  `GroundTilePalette565` trampolines + `groundPalette565For` (the 63-row HiColTab
  block), wired into `Init` + `doRenderTerrain`.
* `src/play/terrain_render.{h,cpp}` — `GroundTexBinder`, `GroundFrame::SetTexBinder`,
  `GroundGetOrBuildTile`, the post-walk UV-selector pass, `GroundSpanTextured`.
* `tests/unit/terrain_texturing_test.cpp` (NEW) — UV-record corner insets golden +
  the textured-vs-shaded selection + additive-safety + determinism.
* `tests/e2e/terrain_texturing_e2e_test.cpp` (NEW, guarded) — the real AUGSBURG
  textured ground (slot resolve, texel-colour match, WIESE pixels, determinism, PPM).

NOT edited (other agents / read-only): `render/floorgfx_recon.*` (W5-TILE's
resolver — used, not changed), `render/terrain_walk.*`/`terrain_render.h`/
`tile_*`, `render/raster*`/`texture.*`/`meshlist.*`, `floorwater.*`.

---

## Tests

* **`terrain_texturing_test`** (NEW) — 3 cases, all green: `UvRecordCornerInsets`
  (the flt_13FE540 T0/T1 corner-inset golden), `TexturedVsShadedSelection` (white
  default = all grey / 0 colour; bound mock texture -> coloured red/blue texels
  appear, frames differ, unbound fresh render byte-identical), `TexturedDeterminism`.
* **`terrain_texturing_e2e_test`** (NEW, guarded) — 20 checks, 0 failures over real
  AUGSBURG (see Before/after above).
* Neighbour suites re-run green: `terrain_ground_test`, `terrain_ground_e2e_test`,
  `city_view3d_e2e_test`, `real_city_render*`, `playable_flow_e2e_test`,
  `render_terrain*`, `floorgfx_recon_test`, `render_tile_textures_test`,
  `tile_textures_e2e_test`.
* **FULL ctest: 1432/1432 passed** (GUILD_GAME_DIR set).

## Recalibrated pins (+justification)

* **`real_texture_driver_e2e_test`**: `texturesDecoded` 1949 -> **2370** (and
  `squareTextures == texturesDecoded` still holds). The pin counted 8-bit square
  BMPs only; the 24-bit palettize arm added to `DecodeBmpIntoTexture` (required to
  load the 24-bit floor slot BMPs) is the engine's own software path, so ALL 2370
  square BMPs now decode (8-bit + the 421 palettized 24-bit). The old pin
  under-counted by skipping the 24-bit textures the engine loads. Written
  justification in-test.

## Named gaps (rule 8)

* **byte_13DCE58 sub-texture id** (the 256-stride per-cell sub-pattern, @0x5c1f88)
  is all-zero in the shipped static image (runtime-filled, default 0), so subTexId
  is 0 and the single flt_13FE540 UV record is used — the recovered + golden path.
  The runtime writer of byte_13DCE58 is the named boundary (no static evidence).
* **The `*(tex+72)` HiColTab compaction** (LoadSoftPalettize rewrites texels onto
  HiColTab indices @0x5da34c): the resolver keeps the raw source indices, so the
  W5-TX palBase block is built source-index-major (identical ramp VALUES — row L
  entry i = colour*round(L/62) — just not compacted). The ramp math is the
  reconstructed `render/hicoltab.cpp` semantics; the dedup/compaction is the
  texture-cache boundary and is not needed for a correct textured span.
* **VIBE_TextureCache_GetOrBuildTile LRU @0x5ba1e8** stays superseded by the
  resolver (one record per slot, reused by name) — the W5-TILE deferral, unchanged.
