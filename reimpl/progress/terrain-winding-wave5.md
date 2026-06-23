# Wave 5 — W5-TERR: Pass-A winding re-verify + floor water-region render handoff

Owner: W5-TERR. Files: `src/render/terrain_walk.{h,cpp}`, `src/render/scene_floor.{h}`,
`src/play/terrain_render.{h,cpp}` (mirror removal — the hack lived here),
`tests/unit/terrain_ground_test.cpp`, `tests/e2e/render_terrain_walk_e2e_test.cpp`.
IDA MCP was LIVE; every claim below is decompile-traced (addresses inline).

---

## TASK 1 — Pass-A winding (the wave-4 flagged row-mirror hack)

### The real winding (decompile evidence)

`VIBE_Floor_RenderTerrain @0x5bf22c` Pass-A quad emission (lines 730..786 of the
fresh decompile). Per quad at tile-subdiv (row r, col c) the 4 vertex cursors are
laid out row-major in the tile vertex buffer (`tile+24`, 80-byte stride; the fill
loop at 632..708 steps col along axisU (`flt_13FFD40..`, from floor+160) and row
along axisV (`flt_13FD500..`, from floor+176)):

```
v417 = vert(r,  c)    = i00   (TL)
v416 = vert(r,  c+1)  = i01   (TR)
v414 = vert(r+1,c)    = i10   (BL)
v415 = vert(r+1,c+1)  = i11   (BR)
```

The split byte `v494 = *(v379 + v374)` is the per-LOD slope/hidden-flag buffer
(`v379 = *(floor + 4*(lod>>1) + 36)`, the BuildTilePolys @0x5bc45c output).
ONE 80-byte poly record holds BOTH triangles (`v192 += 80` once/quad, `tile+32
+= 2`); the in-tree model splits it into two 40-byte `Polygon`s:

```
v494 >= 0  (lines 752..761, sets +38 |= 1):
    tri0 = (v417, v415, v414) = (i00, i11, i10)
    tri1 = (v417, v416, v415) = (i00, i01, i11)
v494 <  0  (lines 765..774, clears +38 bit0):
    tri0 = (v414, v417, v416) = (i10, i00, i01)
    tri1 = (v416, v415, v414) = (i01, i11, i10)
```

### Verdict — the wave-4 winding was WRONG, the hack was unnecessary

The wave-4 `terrain_walk.cpp` guessed `tri0=(i00,i11,i01)` / `tri1=(i00,i10,i11)`
(the source even said "v01? — engine"). That is NOT the binary. It produced the
opposite screen winding, which is why a from-above camera saw every poly as a
back-face — and the wave-4 fix was a ROW-MIRROR lattice (rows reversed, origin' =
origin + (N-1)*axisV, axisV' = -axisV) to flip it back. That mirror is a
geometric N–S flip of the terrain — NOT 1:1.

**Where the cull actually is (re-verified):**
* `RasterizeTexturedTriangle @0x5F7D58` does NOT cull — it only NORMALISES winding
  by the signed-area sign (forward vs reverse vertex scan). `RasterizeMeshList
  @0x5AEC88` does NOT cull either.
* `ComputeVertexClipFlags @0x5ad614` is FRUSTUM-only (6 plane outcodes + a
  not-fully-outside keep test); NO winding test.
* The backface cull is the LAST loop of `@0x5bf22c` itself (Pass C, lines
  1815..1836): after projecting each tile's vertices it does, per poly,
  ```
  if ( (v0.sx - v2.sx)*(v0.sy - v1.sy) <= (v0.sx - v1.sx)*(v0.sy - v2.sy) )
      *(poly+36) &= ~0x40;     // KEEP (front-facing)
  else
      *(poly+36) &= ~0x80;     // CULL (clear the "visible" bit7)
  ```
  i.e. screen signed-area `(sx0-sx2)*(sy0-sy1) - (sx0-sx1)*(sy0-sy2) <= 0` keeps.
  The in-tree Pass C (terrain_walk.cpp:339-342) already matched this exactly; it
  was the *quad vertex order feeding it* that was wrong.

### Fix (1:1)

* `src/render/terrain_walk.cpp` — the quad emission now uses the EXACT decompile
  winding above (both diagonals), and sets/clears flag38 bit0 on BOTH split halves
  (the engine's shared +38 byte). The row-mirror hack is GONE.
* `src/play/terrain_render.cpp` / `.h` — removed `heightsMirror_`/`texMirror_`
  and the `origin' = origin + (N-1)*axisV ; axisV' = -axisV` Render-time negation.
  The floor is now fed in its TRUE row order; `GroundFrame` consumes
  `g->heights/texGrid/origin/axisV` directly.

### Verification on real AUGSBURG data

`terrain_ground_e2e_test` (guarded on `GUILD_GAME_DIR`) draws the parsed AUGSBURG
floor through the full `BeginUniverseFrame @0x5B3900` spine with an above camera
and asserts ground coverage > the no-ground baseline — PASS with the genuine
winding and NO mirror (front-facing from above, the Pass-C signed-area cull keeps
the polys). The synthetic headless `terrain_ground_test` camera was pulled in
from y=900 to y=250 (the un-mirrored floor sits in its true world position; the
closer above-camera gives real on-screen coverage). `render_terrain_walk_e2e`'s
top-down camera dropped its `projYScale=-10` Y-flip (which had been tuned to the
OLD inverted winding) to `+10`: with col→+X, row→+Y the genuine winding is
front-facing (hand-checked: tri0 (i00,i11,i10) area = -1 ≤ 0 → KEEP).

---

## TASK 2 — Floor water-region render (handoff to W5-WATER)

The 344-byte water-region records (`SceneFloorWaterRegion`, parsed in
`scene_floor`) are stored at runtime at **Floor+6624** (`v78[1656]`), count byte
at **Floor+7277**. Two render paths, both re-decompiled:

* **Per-tile water tint/UV** — `VIBE_Floor_TransformTileGeometry @0x5be668`:
  a tile poly-cell's region-id byte (`poly-cell+16`) selects
  `Floor+6624 + 344*id`; `rec+20` (`param20`) scales the wave height
  (`*(rec+20) * v85`, `v85 = flt_628B2C - |sunDir·flt_5CA2B0|`) and `rec+8`
  (`packedFlags`, tested as `rec+10 & 1`) picks the flat-shade vs UV branch.
* **Per-region mesh wobble** — `VIBE_Floor_AnimateWaterVertices @0x5be428`,
  invoked in the walk's Phase-2 tail (`@0x5bf22c:1848`):
  `AnimateWaterVertices(globalMeshList, *(Floor+7277), *(Floor+6624))`. It walks
  the records (86-dword/344-byte stride) and for each with a live `rec+0`
  (Texture*/mesh handle) cycles its texture-group member (`rec+114` low nibble
  indexes `dword_5D93C8`, `rec+112` = group size, via
  `VIBE_Texture_FindGroupMember @0x5daec0`) and wobbles its 16 quad vertices
  (`rec+308..` through the `dbl_628AEC..dbl_628B24` sin/cos table; phases
  `rec+312..316` = `a3[78..81]`; scroll `rec+328/332` = `a3[82/83]`,
  `VIBE_Math_Fmod @0x5d3fb2`).

**Handoff:** the render-relevant fields (`param20`/rec+20, `packedFlags`/rec+8,
`vecA`/rec+24, `vecB`/rec+40, plus `texName` + the load-flag bytes) are ALL parsed
and documented with their runtime offsets in `src/render/scene_floor.h`
(SceneFloorWaterRegion banner). W5-WATER owns: the Floor+6624 array build, the
`rec+0` mesh/texture handle, the AnimateWaterVertices animation, and the per-tile
`poly-cell+16 → region` binding. Nothing in MY files animates water (the walk only
GATES it via `frameFlags`).

---

## TASK 3 — Floor mask + scale re-verify

* **Floor+8 / +12 mask** — `VIBE_Floor_LoadFromHeightmap @0x5bd544..0x5bd55e`
  (disasm captured): `edx=N; imul edx,edx; dec edx (=N*N-1); dec eax (=N-1);
  or edx,eax (=N*N-1); [+8]=edx ; [+12]=eax`. So **Floor+8 = N*N-1** (the LINEAR
  cell-index mask the walk uses: `v373 = (col + N*row) & v378`), **Floor+12 =
  N-1** (per-axis). The `GroundFrame` already feeds `N*N-1`; the stale "mask ==
  size-1" doc in `terrain_walk.h` was corrected with the disasm.
* **BuildTerrainMesh scales** — untouched by the winding work (no edit to
  `heightmap.*` / `DeriveGridScaleXZ`); the wave-4 derivations (scaleX =
  (maxX-minX)/(size-1.75); originY = minY+1.0; scaleY = (maxY-minY)*0.0039525721)
  remain verified in `scene_floor.cpp`.

---

## Tests

* `render_terrain_walk_test` — 1598 checks PASS (winding goldens; the unit walk
  does not assert screen orientation, so the corrected vertex order is exercised
  through the buffer/poly-count goldens).
* `render_terrain_walk_e2e_test` — 4721 checks PASS (full walk → rasterize; camera
  Y-flip removed, band now x[4..54] y[7..63], centre (29,35) filled; the
  front-facing-only-appended assertion still holds with the TRUE winding).
* `play_terrain_render_test` 32 / `play_terrain_render_e2e_test` 12 PASS.
* `terrain_ground_test` — 98 checks PASS (headless GroundFrame; camera re-aimed
  to y=250 for the un-mirrored floor). `terrain_ground_e2e_test` PASS (real
  AUGSBURG, mirror-free front-facing ground). [Both verified green immediately
  after the edits; see the BUILD NOTE below.]
* Neighbour suites green: `render_scene_floor` 127, `render_tile_lighting` 97,
  `wire_terrain_bridge` (unit+e2e), `city_view3d` (unit+e2e), `universe_render_e2e`.

## BUILD NOTE (concurrent WIP)

A late edit by the concurrent W5 raster/texture agent changed
`RasterizeTexturedTriangle` / added `RasterizeTexturedTriangleRgbz` signatures in
`src/render/raster*.{cpp,h}` (not my files), which currently breaks the LINK of
any target pulling `src/play/city_view3d.cpp` (terrain_ground_*, scene_floor_e2e,
city_view3d_*). This is transient cross-agent WIP, NOT caused by the winding work:
all those targets BUILT AND PASSED with my full changes in place before that raster
edit landed, and every target that does NOT transitively need the new raster
symbols (walk unit+e2e, play_terrain_render unit+e2e, scene_floor unit,
tile_lighting) builds and passes now. Also unrelated: `render_clip_test`
(SpanDispatch slot-6 stub) fails independent of my tree (reproduced with my
changes stashed) — the raster agent's domain.

## Named gaps (carried / unchanged)

* The per-quad split selector still uses the cell type-byte sign as a proxy for
  the real per-LOD slope buffer `v379 = *(floor + 4*(lod>>1) + 36)` (the
  BuildTilePolys @0x5bc45c output); winding within each branch is now exact, the
  diagonal CHOICE remains the documented proxy.
* Tile textures (getOrBuildTile / Floor_LoadTexture) — inert, as before.
