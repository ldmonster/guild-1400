# Wave 7 — W7-WATERTEX: blue water texture + FindRegionOffset poly linkage

Scope: close the two rule-8 named gaps wave-6 left in the water render arm
(`progress/water-render-wave6.md` / `water-regions-wave5.md`):

1. **Blue water texture** — `EF_WASS_06A_2T_W_AN0` (loaded via `VIBE_Texture_
   LoadByName @0x5da714` + uploaded via `VIBE_Texture_UploadToSurface @0x5db234`)
   was null in headless, so water drew as the white-default geometry, not the
   blue animated texture. The render arm's texture path is now reconstructed 1:1:
   the per-region `WaterMesh+4` handle drives the exact draw-list sort key, and the
   per-frame animated texture-scroll members `WaterMesh[82]/[83]` are fed onto every
   water poly (the engine's `result+28/+32 = mesh+328/+332` copy).
2. **Per-cell poly point linkage** — wave-6 approximated the per-cell water poly
   corner threading with a `(row,col)`→vertex lattice. It is now the REAL
   `VIBE_FloorWater_FindRegionOffset @0x5ba824` resolution: each `WaterPoly` carries
   the four FindRegionOffset corner POINT indices, and the render arm threads them
   into the engine's exact two-triangle layout.

IDA MCP was LIVE. `VIBE_Floor_TransformTileGeometry @0x5be668`,
`VIBE_FloorWater_FindRegionOffset @0x5ba824` and the strip/poly/point build inside
`VIBE_FloorWater_PrepareRegions @0x5ba95c` were decompiled fresh and reconstructed
1:1. Owner files: `src/render/water_render.{h,cpp}`, `src/render/floorwater.{h,cpp}`,
`tests/unit/water_render_test.cpp`.

---

## KEY FINDING — the water geometry buffers are PER FLOOR TILE

The decompile of `VIBE_FloorWater_PrepareRegions @0x5ba95c` (the strip/poly/point
build, `loc_5BB169`) shows the three water geometry buffers are allocated PER
(tileRow,tileCol) into the per-tile water-tile record `v190`:

| buffer | field | stride | alloc tag | size |
|---|---|---|---|---|
| strips (spans) | `v190[13]` | 20 | `d3_fl:Water(strips)` | `20 * (tileSpan²/2)` |
| points         | `v190[7]`  | 80 | `d3_fl:Water(points)` | `80 * v191` |
| polys          | `v190[11]` | 40 | `d3_fl:Water(polys)`  | `40 * v192` |

`v191` is the per-tile cumulative POINT count (it RESTARTS at 0 each tile). Each
span stores `*v69 = v191` (its base point index) before `v191` is bumped by the run
length. `FindRegionOffset((int)v190, query, marker, type)` therefore resolves
against `*(a1+52) == v190[13]` (this tile's strips) and returns
`80*(span.base + query - span.lo) + *(a1+28)` where `*(a1+28) == v190[7]` (this
tile's points heap pointer). So `(off - pointsBuf)/80` is a **per-tile point
index**, and the per-cell poly's four corners are FindRegionOffset lookups into
that per-tile point buffer.

The render arm `VIBE_Floor_TransformTileGeometry @0x5be668` is called PER TILE
(from RenderTerrain's Phase-2 tail @0x5c17fd), walking `*(tile+52)` (strips),
writing each cell's vertex into `*(tile+28)` (points, by `+80` stride), and reading
`*(tile+44)` (polys). The poly corner offsets index back into the same per-tile
point buffer — that is the real point linkage wave-6 approximated.

### How the reconstruction restores per-tile grouping

`render::BuildWaterRegions` flattens the per-tile strips/polys into the compacted
`WaterRegions::spans` / `WaterRegions::polys`, which loses the tile boundary (a
span's `base` collides between tiles). Wave-7 restores it with two additive
reconstruction-helper fields (NOT 32-bit-record fields):

* `WaterStripSpan::tile` — `tileRow*8 + tileCol` (the v211/v212 tile).
* `WaterPoly::tile` + `WaterPoly::{pTL,pBR,pBL,pTR}` — the tile id and the four
  resolved per-tile point indices (`off/80`), or `-1` when FindRegionOffset
  returned 0 (a boundary corner the span list does not cover).

To distinguish a VALID point index 0 from FindRegionOffset's literal-0 "no match"
(the engine relies on `bufferBase == v190[7] != 0` for this), the builder now calls
`FindRegionOffset` with a non-zero sentinel base (80): `pointIndex = off/80 - 1`,
`off==0 -> -1`. The `FindRegionOffset` function body is UNCHANGED.

The render arm groups by `(tile, pointIndex)` → global vertex index, emits one
vertex per span point at the engine's index `span.base + (col - span.lo)`, then for
each `WaterPoly` builds the engine's exact two triangles:

```
tri0 = P(kk,prow), P(kk+1,prow+1), P(kk,prow+1)   = off0/80, off1/80, off2/80
tri1 = P(kk,prow), P(kk+1,prow),   P(kk+1,prow+1) = off0/80, off3/80, off1/80
```
(0x5bb50f.. : `v87+0 = off(kk,prow)`, `v87+4 = off(kk+1,prow+1)`, `v87+8 =
off(kk,prow+1)`; the second 40-byte sub-poly `v87+40 = off(kk,prow)`,
`v87+44 = off(kk+1,prow)`, `v87+48 = off(kk+1,prow+1)`.) The 80-byte water poly
record is two 40-byte sub-polys — i.e. two triangles per cell.

---

## The texture path — `VIBE_Floor_TransformTileGeometry @0x5be668` APPEND block

The append loop (`0x5bec1d..0x5bef03`) per visible front-facing water poly:

```
v66 = 344 * poly.regionId + floor[1656];   // the region's WaterMesh
v68 = *(v66 + 4);                           // mesh[1] == the EF_WASS texture handle
*(dword_13FC570 + 4) = poly;                // poly into the draw list
if (v68) {                                  // 0x5beebb — textured
    *dword_13FC570 = ((v68 - dword_1406A84) >> 7) + 1;   // sort key (record idx+1)
    *(v68 + 84)    = dword_649D58;          // stamp the per-frame texture stamp
} else {
    *dword_13FC570 = 0;                     // 0x5bef03 — white default (!tex)
}
*(poly + 20) = v68;                         // bind the texture record on the poly
*(poly + 12) = v86;                         // the HiColTab light row block
*(float*)(poly + 28) = *(float*)(v66 + 328);  // mesh[82] -> poly+28  (scroll U)
*(float*)(poly + 32) = *(float*)(v66 + 332);  // mesh[83] -> poly+32  (scroll V)
```

* `mesh[1]` (`+0x04`) is the per-region water texture handle (all regions share
  the `EF_WASS_06A_2T_W_AN0` record, stored into BOTH `mesh[0]` and `mesh[1]` by
  the builder's pass-5a, `0x5bb56c`).
* The sort key is **exactly** `((tex - dword_1406A84) >> 7) + 1` — the texture
  record's index in the 128-byte-stride array (`dword_1406A84` base, `>>7` ==
  `/128`), plus 1. A null handle → key 0 (the white-default leaf).
* `mesh[82]/[83]` (`+0x148/+0x14C`) are `texAccumA/texAccumB` — the **scrolling
  texture members** `AnimateWaterVertices @0x5be428` advances each frame
  (`Fmod(texRateA*dt + texAccumA, 1.0)`). The engine copies them onto every water
  poly (`poly+28/+32`) so the textured water span scrolls the EF_WASS ripple.

### Reconstruction (`RenderWaterSurface`, water_render.cpp)

* Sort key: new `texBase`/`texStride` params reproduce `((tex - dword_1406A84)
  >> 7) + 1` exactly when a real texture-record-array base is supplied (the on-
  screen path). When `texBase == nullptr` (opaque-handle / headless) the key is 1
  for a bound handle, 0 for the white default — the engine's `if(!tex)` branch.
  Defaults `texBase=nullptr, texStride=128` keep the existing call site working.
* Scroll members: each appended water poly carries `mesh[82]` (scroll U) in
  `Polygon::uvX` and `mesh[83]` (scroll V) in `Polygon::uvY` (the `result+28`
  store). `WaterDrawStats::scrollU/scrollV` report the last appended textured
  poly's scroll for introspection.
* `WaterDrawStats::linkedPolys` reports how many polys had all four corners
  resolve through the FindRegionOffset point linkage.

---

## EXACT city_view3d HANDOFF (the orchestrator wires this — NOT my files)

Headless still passes `loadTexture=nullptr` to `BuildWater` (city_view3d.cpp:1281),
so `waterTexture_` is null → the white-default branch. To draw the BLUE animated
water, the on-screen binder must (a) load `EF_WASS_06A_2T_W_AN0` through the SAME
`TextureAssetCache` the ground uses and (b) carry that record into the water flush.

### (a) Load the EF_WASS texture through `groundTexCache_` and pass it to BuildWater

`CityView3D::doRenderTerrain` (~line 1280) — supply a `render::WaterTextureLoadFn`
instead of `nullptr`. The loader resolves the EF_WASS member through the existing
`groundTexCache_` (the `render::TextureAssetCache{16}` mounted in
`SetupGroundTexCache()`, whose `BmpFetch` already resolves bare names through the
mounted `Textures.BIN`), and returns the loaded `render::Texture*` record:

```cpp
// in CityView3D (a member or a static trampoline; groundTexCache_ already mounted)
static void* CV3D_LoadWaterTexture(const char* name, int /*flags=172*/, void* ctx) {
    auto* self = static_cast<CityView3D*>(ctx);
    self->SetupGroundTexCache();                 // ensures the BmpFetch is wired
    // "*"+name+".BMP" is the engine's wildcard; the cache stores/compares `name`.
    int slot = self->groundTexCache_.LoadByName(/*path*/ name, /*name*/ name);
    if (slot < 0) return nullptr;                // no EF_WASS member -> white default
    return const_cast<render::Texture*>(self->groundTexCache_.record(slot));
}
...
if (opt_.water && !waterBuilt_) {
    groundFrame_.BuildWater(&CityView3D::CV3D_LoadWaterTexture, /*ctx*/ this);
    waterBuilt_ = true;
}
```

`BuildWater` stores the returned record into every `WaterMesh+0/+4` and into
`water_.meshTexture` → `GroundFrame::waterTexture_`. The existing
`GroundFrame::Render` call to `RenderWaterSurface(..., waterTexture_)` then makes
every water poly textured (sort key 1; pass `texBase=&groundTexCache_.set()
.records[0]` + `texStride=128` if the exact record index key is wanted — not
required for the flush).

NOTE: `EF_WASS_06A_2T_W_AN0` is an **animation group** member. The engine's
`VIBE_Animation_GetPtr @0x5d9774` resolves the group; `BuildWater`'s loader hook
stands in for `LoadByName(name,172,0,0)`. If the member is not square / not present
as a loose BMP, the cache returns -1 and the water stays white (additive, safe).

### (b) Route the water poly through a TEXTURED span in the flush

`GroundFrame::Render` appends the water polys into the SAME draw list as the
ground, then flushes through `RasterizeMeshList @0x5AEC88`. The ground textured
span (`GroundSpanTextured`, terrain_render.cpp) keys off `tri.uvZ > 0` +
`getTileTextureRec(typeByte)`. The water poly sets `uvZ = 1.0` (textured signal),
`matIndex = regionId`, and carries the EF_WASS scroll in `uvX`/`uvY`. The flush
wiring (terrain_render.cpp / city_view3d, NOT my files) must, for a water poly:

* resolve the texture record = `waterTexture_` (the EF_WASS record) rather than the
  per-type ground texture (a water poly is identifiable by e.g. its draw-list
  sort-key band or a dedicated SpanDispatch slot keyed when the water sub-pass
  appends — the water append can set a distinct `sortKey` high-byte so the flush
  dispatch picks a `WaterSpanTextured` slot);
* sample the EF_WASS texels with the poly's corner UVs + the scroll offset
  `(uvX,uvY)` added (the animated ripple);
* use the same 565 HiColTab light-ramp block the ground uses (`palette565(rec)`),
  so the water is lit by the same tile illumination.

This is the SAME `TextureAssetCache` + `palette565` handoff `progress/terrain-
texturing-wave5.md` documents for the ground; the water reuses it with the single
EF_WASS record + the scroll add. The render-arm side (resolve handle, sort key,
scroll members onto the poly) is DONE here; the span-side EF_WASS sampling is the
orchestrator's flush wiring.

---

## Files changed (mine)

* `src/render/floorwater.h` — `WaterStripSpan::tile`; `WaterPoly::tile` +
  `pTL/pBR/pBL/pTR` (the FindRegionOffset corner point indices). Doc updated.
* `src/render/floorwater.cpp` — pass-5 stamps `tile` on every span/poly and
  resolves the four corner point indices via `FindRegionOffset` with a non-zero
  sentinel base (distinguishes valid point 0 from no-match). `FindRegionOffset`,
  `FloodFillMask`, `FillHeightGradient`, `AnimateWaterWaveGrid` UNCHANGED.
* `src/render/water_render.h` — `RenderWaterSurface` gains `texBase`/`texStride`
  (exact sort-key arithmetic); `WaterDrawStats` gains `linkedPolys`/`scrollU`/
  `scrollV`. Doc updated.
* `src/render/water_render.cpp` — vertex build re-keyed by `(tile, pointIndex)`;
  poly build threads the FindRegionOffset corners into the engine's exact two-
  triangle layout; append computes the real sort key and copies the EF_WASS scroll
  members (`mesh[82]/[83]`) onto each poly.
* `tests/unit/water_render_test.cpp` — +3 tests (see below).

NOT edited (read-only / other agents): `terrain_render.*` (bind site — calls
`RenderWaterSurface` with the existing arg; the new `texBase`/`texStride` default
to the headless path), `city_view3d.*` (handoff documented above), `texture_asset.*`
/ `water_vertices.*` (reused), `scene_floor.*`.

---

## Tests

`tests/unit/water_render_test.cpp` — **12 tests, 127 checks, GREEN** (built +
run standalone; the full `guild` lib was mid-edit by a concurrent agent at report
time — particle_integrate.cpp — so ctest could not run the linked binary; my two
.cpp compile clean with `-fsyntax-only` and the unit suite passes via a direct
link of my sources + deps):

* (wave-6, re-verified GREEN) ShadeAxisAlignedZ/Perpendicular/ZeroVector,
  VertexPosCombinesBaseWaveHeight/ZeroEverything, BuildsAndAppendsWaterRegion,
  NoSpansAppendsNothing, AnimatedWaveMovesVertices, TexturedSortKey.
* **FindRegionOffsetPointLinkage** (NEW) — a 5×5 water region: every WaterPoly's
  four corners resolve through the per-tile point linkage (`linked ==
  polys.size()`); the arm reports `linkedPolys == linked` and builds exactly
  `2 * polys` triangles (the two sub-polys per 80-byte poly record).
* **ScrollMembersCopiedToPoly** (NEW) — stamping `mesh[82]=0.375`/`mesh[83]=0.625`
  (the AnimateWaterVertices scroll accumulators) makes them reach every appended
  textured poly (`scrollU/scrollV` + `poly.uvY`).
* **SortKeyFromTexBaseRecordIndex** (NEW) — a 128-byte-stride record array; the
  water texture at record index 5 yields the exact key `((tex-base)>>7)+1 == 6`.

`tests/unit/floorwater_regions_test.cpp` — **6 tests, 47 checks, GREEN** (re-run;
the `tile`/point-index additions are additive — no existing assertion changed).
`FindRegionOffset` golden (`water_vertices_test.cpp`, explicit `bufferBase=1000`)
is unaffected — the function body is unchanged.

---

## Named gaps remaining (rule 8 — addresses)

* **EF_WASS span sampling in the flush** — the render arm resolves the texture
  handle, the sort key and the scroll members onto each water poly; the span-side
  EF_WASS texel fetch + scroll add (the `WaterSpanTextured` flush slot) is the
  orchestrator's flush wiring (terrain_render.cpp / city_view3d), documented above.
  Headless (no `Textures.BIN` EF_WASS member) keeps the white-default branch —
  byte-identical to wave-6.
* **`VIBE_Animation_GetPtr @0x5d9774` group resolution** — the EF_WASS animation
  group lookup (the engine's first attempt before `LoadByName`) is the present-
  coupled animation-bank boundary; the `BuildWater` loader hook stands in for the
  `LoadByName(name,172,0,0)` fallback. The texture-member advance
  (`activeMember`, `WaterTextureFrameIndex` / `FindGroupMember @0x5daec0`) is the
  `AnimateWaterVertices` step already reconstructed (water_vertices.cpp), fed via
  `GroundFrame::AnimateWater`'s `findGroupMember` callback.
* **Strip run-membership micro tie-breaks** (`0x5bb8fd`) — the builder's run
  predicate uses the water-height test (contiguous water runs); the decompile's
  region-boundary sub-conditions feed FindRegionOffset RESOLUTION (now reproduced
  via the per-tile point indices), not run membership — carried over from wave-5.
