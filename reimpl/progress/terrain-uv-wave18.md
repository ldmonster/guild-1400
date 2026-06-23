# Wave-18 — CLOSED: terrain Pass-A UV EMISSION (flt_13FE540) + seam-UV blend

Owner: W18-TERRAINUV. IDA MCP live (gilde.exe, imagebase 0x400000). Closes the
wave-17 named boundary: the seam-UV midpoint-blend was left hooked because "this
repo's terrain pipeline carries only texId in Polygon::uvZ, not the flt_13FE540 UV
subsystem." That whole UV-emission path is now reconstructed 1:1, so the seam-UV
blend (flt_628B48 = 0.5 averaging into the per-tile 24-byte UV scratch) fires.

Owned files only: `src/render/terrain_uvtable.{h,cpp}`,
`src/render/terrain_walk.{h,cpp}`, `src/render/tile_geometry.h` (doc note only),
`tests/unit/render_terrain_uvtable_test.cpp` (NEW),
`tests/unit/render_terrain_walk_test.cpp` (+UV cases), this report.
`play/terrain_render.*` / `city_view3d.*` are the bind sites (NOT edited) — handoff
documented below.

---

## What the decompile showed (the flt_13FE540 UV subsystem)

### Pass-A per-quad UV assignment — VIBE_Floor_RenderTerrain @0x5bf22c

Disasm @0x5c1f78..0x5c203f, decoded 1:1:
```
cellFlag = *v413                                       (var_4, the slope/flag byte)
if (cellFlag & 0x40):
    subTexId = byte_13DCE58[(quadIdx & 0xFF) + base] & 0x3F   (@0x5c1f88)
else subTexId = 0
texRec   = VIBE_TextureCache_GetOrBuildTile(...)       (@0x5c1fc8 -> eax)
uvBase   = subTexId * 0x60                              (imul edx, var_208, 0x60)
poly+0x14 = texRec                                      (always)
if (cellFlag & 0x80):                                   (the visible/slope bit)
    poly+0x3c = texRec                                  (tri1 texid)
    poly+0x10 = &flt_13FE540 + uvBase                   (T0 UV: floats 0..5)
    poly+0x38 = (&flt_13FE540 + uvBase) + 0x18          (T1 UV: floats 6..11)
```
So tri0 takes the FIRST 6 floats of the sub-texture's 24-float record, tri1 the
NEXT 6 (the `+0x18` byte == +6 float advance).

### Seam-UV midpoint blend — @0x5bf22c (four arms; flt_628B48 = 0.5)

Disasm @0x5bfcc8..0x5bfe1b (RIGHT arm; LEFT/UP/DOWN identical):
```
; record N (boundary poly's UV)
qmemcpy(scratchN <- *(boundary+0x10), 24 bytes)        (6-float UV record)
if (boundary->flags38 & 1)   ; TL-BR  (@0x5bfd27)
    scratchN[4] = (scratchN[2] + scratchN[4]) * 0.5
    scratchN[5] = (scratchN[3] + scratchN[5]) * 0.5
else                          ; BL-TR  (@0x5c2348)
    scratchN[0] = (scratchN[0] + scratchN[2]) * 0.5
    scratchN[1] = (scratchN[1] + scratchN[3]) * 0.5
*(boundary+0x10) = scratchN ; re-point boundary poly's UV at scratch
*(boundary+8)    = midpoint  ; far-vertex re-point (the wave-17 geometry)
; record N+1 (appended/copied poly's UV)
qmemcpy(scratchN1 <- *(appended+0x10), 24 bytes)
scratchN1[2] = blendedMidU ; *(eax+8)  -> the appended poly's v1 slot
scratchN1[3] = blendedMidV ; *(eax+0xC)
*(appended+0x10) = scratchN1
*(appended+0x18) = 0x41800000 (16.0f)  ; the wave-17 uvX stamp
counters++ (polyCount/subdivCached/vertCount)
```
The per-tile 24-byte UV scratch is `tile+60` (drawData), consuming TWO 6-float
records per seam poly (edx, then edx+0x18). The blend averages the two seam-edge
endpoint UVs into the midpoint vertex's UV.

### Golden-pinned data (get_bytes)

* `flt_13FE540` (96 bytes) — ALL ZERO in the static image (runtime-filled by
  BuildTerrainUvTable @0x5b94cc; SetMipFilterLevel sets dword_64A038 = mip tile
  size, default 64).
* `byte_13DCE58` (64 bytes) — ALL ZERO (runtime per-cell sub-id table; default 0
  => subTexId == 0 => the single 24-float record).
* `flt_628B48` = `00 00 00 3f` == **0.5** (the seam blend weight). Confirmed.
* `dword_64A038` = 0 static (the runtime mip tile size; 64 default).

---

## What was reconstructed 1:1 (CLOSED)

### `render/terrain_uvtable.{h,cpp}` — the UV-emission primitives

* `TerrainSubTexId(cellFlag, subTexSrc, quadIdx)` — the @0x5c1f78 per-cell sub-id
  select (0x40 gate, `(quadIdx & 0xFF)` mask, `& 0x3F`). Null table -> 0 (shipped).
* `TerrainUvBaseIndex(subTexId)` — `subTexId * 0x60` == 24-float stride.
* `TerrainQuadUvT0/T1` — copy tri0 (floats 0..5) / tri1 (floats 6..11) off the
  table (the poly+0x10 / poly+0x38 image).
* `TerrainSeamBlendUv(rec, diagTLBR)` — the flt_628B48 = 0.5 midpoint average, both
  diagonal cases decoded exactly (TL-BR -> verts 1,2 into vert-2 slot; BL-TR ->
  verts 0,1 into vert-0 slot).

### `render/terrain_walk.{h,cpp}` — wired into the live walk

* `TerrainRenderState` gains `uvTable` (flt_13FE540) + `subTexSrc` (byte_13DCE58).
* `TerrainTile` gains `polyUv` (6-float-per-poly record array, the poly+16/+56
  image) + `uvScratch` (the per-tile 24-byte UV scratch == drawData/tile+60) +
  `uvScratchCount` (the per-tile scratch cursor, reset per tile in Pass B).
* **Pass A** stamps each quad poly's tri0/tri1 UV record (TerrainQuadUvT0/T1 at
  `subTexId*0x60`) into `tile->polyUv` — the exact poly+0x10/+0x38 assignment.
* **Pass B** (the `emitSeamPoly` lambda) now performs the two-qmemcpy + 0.5 blend:
  copies the boundary poly's UV record into uvScratch[N], blends, re-points the
  boundary poly's UV at it; copies the appended poly's record into uvScratch[N+1],
  stamps the blended midpoint into the appended midpoint vertex slot, re-points it.
  All counters/geometry (midpoint vertex, far-vertex re-point, append, 16.0f stamp)
  are the unchanged wave-17 1:1 geometry.

ADDITIVE & SAFE: when `st.uvTable == nullptr` (or `tile->polyUv == nullptr`) the
walk emits geometry ONLY (UV arrays untouched) — byte-identical to before. The
existing StitchHarness tests (no UV binding) stay green unchanged.

---

## Handoff to the bind sites (NOT owned — do not edit here)

`play/terrain_render.cpp` / `city_view3d.cpp` are the GroundFrame raster bind
sites. To consume the new TRUE per-poly UVs they would:
1. Allocate `polyUv` (`6 * polyCap` floats) + `uvScratch` (`6 * scratchCap`) per
   tile, alongside the existing vertexBuf/polyBuf, and set `tile->polyUv` /
   `tile->uvScratch` at Bind.
2. Set `st.uvTable = uvTable_` (the GroundFrame already builds the 24-float image)
   and `st.subTexSrc` (null in the shipped image).
3. In `GroundSpanTextured`, read the 3 corner UVs from `tile->polyUv[6*polyIdx +
   2*k]` instead of the current parity-selector `uvTable_[sel + ...]` heuristic.
   The seam (stitch) polys then carry their blended midpoint UVs directly.

The current bind site's `uvX = (i&1)` parity selector + `uvTable_[sel]` read
remains a faithful PROXY for subTexId==0 ground (it picks T0/T1 within the single
record, which is exactly what the per-poly records hold for non-seam quads); the
new path additionally makes the SEAM polys carry the 0.5-blended midpoint UVs (the
quads the proxy never textured because stitch polys aren't drawn). This is a
strict superset — wiring it is the bind-site owner's follow-up, documented here.

---

## Tests / build

* `render_terrain_uvtable_test` (NEW) — 50 checks, 0 failures: the 24-float
  corner-inset golden (mip 64), the T0/T1 record select + subTexId*0x60 stride,
  the byte_13DCE58 sub-id selector (0x40 gate / 0x3F mask / 0xFF index mask), both
  seam-blend diagonals, the 0.5 weight pin.
* `render_terrain_walk_test` — 2467 checks, 0 failures (was 1670; +797 new UV +
  re-run geometry): `TerrainWalkUv.PassAQuadUvRecords` (per-quad T0/T1 records),
  `SeamBlendIntoScratch` (the boundary record copied + 0.5-blended into uvScratch,
  re-pointed, appended midpoint stamped), `InertWithoutTable` (additive-safety:
  no uvTable -> UV arrays stay zero, geometry still built). All wave-17 stitch
  geometry goldens unchanged.
* Memory-safety: the walk + uvtable cluster built under
  `-fsanitize=address,undefined` and ran clean (2467 + 50 checks, 0 failures) —
  no OOB reads/writes in the new UV indexing, no UB.

Consumers re-run green (0 failures): `render_terrain_render_test` 50,
`render_tile_textures_test` 79, `terrain_texturing_test` 32,
`play_terrain_render_test` 32, `render_terrain_walk_e2e_test` 4721,
`render_tile_lighting_test` 126, `city_view3d_test` 52, `wire_terrain_bridge_test`
36, `terrain_ground_test` 281, `terrain_mesh_test` 316, `render_scene_floor_test`
138. `guild` core library builds clean.

## What closed / residual

* CLOSED: the wave-17 "Seam-UV midpoint blend" named boundary — the flt_13FE540
  per-quad UV assignment + the per-tile 24-byte UV scratch + the flt_628B48 = 0.5
  midpoint blend are all reconstructed 1:1 and wired into the live walk.
* RESIDUAL (not a boundary, a bind-site follow-up): the GroundFrame raster
  (`terrain_render.cpp`, NOT owned) still consumes UVs via the wave-5 parity-
  selector proxy; consuming the new per-poly `polyUv` records (so seam polys
  texture with their blended UVs) is the documented handoff above. The reconstruct-
  ed subsystem is complete and exercised; only the raster READ-side wiring (owned
  by city_view3d/terrain_render) remains, by ownership rule.
