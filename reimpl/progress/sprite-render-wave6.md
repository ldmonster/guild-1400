# Wave-6 — World-space sprites / billboards (W6-SPRITE)

Owner: W6-SPRITE. Files: `src/render/sprite_scale.{h,cpp}`,
`src/render/paintbox_shape.{h,cpp}`, `tests/unit/render_billboard_project_test.cpp`.

## Entity

Camera-facing 2D shapes living in the 3D scene (smoke/flame/water/flare/icon
sprites, signs): the **project-world-point → screen + scale-by-depth** stage, plus
the per-quad winding/visibility test. This is the geometry stage that decides
WHERE and at what apparent size (and, when fog is on, at what fade alpha) a world
sprite quad lands; the textured-quad raster leaf (`RasterizeMeshList` →
`RasterizeTexturedTriangleRgbz @0x5F6C30`) then draws it. The 2D-shape blit
siblings (`ShapeShowFromBank*`, the scaled RLE/16bpp blitters) were already
reconstructed in `sprite_scale.cpp` (wave ≤5) and remain.

## Reconstructed this wave

### `gilde.exe 0x5AC970` — VIBE_Particle_UpdateBillboards (FULL, all three arms)

The IDA name "Particle_UpdateBillboards" / "UpdateBillboards" is the world-sprite
projection. The function has three arms gated by two globals, plus an always-run
per-quad visibility pass. Reconstructed 1:1 from the decompile + disassembly
(verified instruction by instruction at 0x5AC970–0x5ACCCB).

Globals → `BillboardParams`:
- `flt_13FCD0C` projScaleX, `flt_13FCAF8` projScaleY (per-frame camera scale)
- `flt_13FCD18` centerX, `flt_13FCD10` centerY (viewport centre)
- `flt_13FC544` fadeMinSq, `flt_13FC5AC` fadeNear, `flt_13FC58C` fadeScale
- `dbl_628074` = 255.0 (alpha cap; bytes `00 00 00 00 00 E0 6F 40`)
- `byte_649D70` enabled, `byte_649DD8` depthFade

`ProjectBillboardVertices(verts, count, params)` — per live vertex (`+0x4C` bit7):
```
invZ    = 1/cz                                      ; +0x1C
screenX = projScaleX*cx*invZ + centerX             ; +0x10
screenY = projScaleY*cy*invZ + centerY             ; +0x14   (the perspective divide
                                                              IS the depth scale)
```
- **enabled && depthFade** (0x5AC9A4): also `colorOut(+0x40)=colorSrc(+0x44)` and
  the depth-fade alpha at `+0x4F`:
  `distSq = cx²+cy²+cz²` (stored flt_13FC548); `alpha = distSq<=fadeMinSq ? 255 :
  255 - min(255,(sqrt(distSq)-fadeNear)*fadeScale)`, truncated toward zero to a byte
  (the `VIBE_Coord_ConvertX @0x5C6B08` x87 RC=truncate `frndint`).
- **enabled && !depthFade** (0x5ACB4F): project + `colorOut`, no alpha.
- **!enabled** (0x5ACC0B): project + `byteOut(+0x42)=byteSrc(+0x46)>>2`, skip dead
  vertices, no fade/alpha.

`BillboardQuadVisibilityPass(quads, count)` — the always-run pass (0x5ACAE0). Per
quad with `+0x24` bit7 set: if bit4 set → `+0x24 |= 0x40`; else cull (clear bit7)
when front-facing per the engine inequality and `+0x26` bit2 clear:
```
(v0.sx-v2.sx)*(v0.sy-v1.sy) > (v0.sx-v1.sx)*(v0.sy-v2.sy)
```

Record layouts (byte-exact): `BillboardVertex` = 0x50 (raw block + accessors,
because `byteSrc`@0x46 aliases the colour word @0x44, `colorOut`@0x40 the copy);
`BillboardQuad` keeps the three vertex pointers + the two flag bytes (pointer width
differs on a 64-bit host, so quad layout is functional, not byte-identical — the
projection touches only those fields).

The effect-tint arm (type byte `a1+0x215 ≥ 5`, reading the node tint floats at
`a1+0x5C/0x60/0x64` and broadcasting a packed BGRA into every vertex `+0x10−16`)
is node-level state setup, NOT part of the per-sprite project/scale math; it is
documented here and left to the node-render owner (it needs the node record, owned
by another agent). Not faked (rule 8).

## Relationship to `object_project.{h,cpp}` (another agent, SAME address)

`src/render/object_project.cpp` already reconstructs `0x5AC970` as
`ProjectObjectVertices` — but ONLY the mesh / no-fade arm + the winding cull, and it
EXPLICITLY names the depth-fade alpha branch (`+0x79`, the `byte_649DD8` sub-branch
over `flt_13FC544/5AC/58C + dbl_628074`) as a deferred rule-8 boundary ("not
reconstructed here"). That deferred boundary is exactly this wave's deliverable.

- `object_project::ProjectObjectVertices` = the WIRED mesh-vertex arm (no fade).
- `sprite_scale::ProjectBillboardVertices` = the COMPLETE function incl. the
  depth-fade alpha arm + the disabled (`byteOut`) arm + the colorOut copy.
- `sprite_scale::BillboardQuadVisibilityPass` = the same winding cull, standalone.

No symbol clash (distinct names, distinct structs). No bind-site files edited.

## CityView3D / universe frame handoff (EXACT)

Frame spine: `render::BeginUniverseFrame @0x5B3900` → object pass. In the object
pass, per scene node (`VIBE_Render_ProcessSceneNode @0x5ADD1C`):
1. view transform (`InterpolateMorphVertices @0x5C953C`),
2. clip classify (`ComputeVertexClipFlags @0x5AD614`, sets the `+0x4C/+76` bit7 gate),
3. **`0x5AC970` projection** — currently `ProjectObjectVertices` (mesh arm),
4. draw-list append → radix sort → `RasterizeMeshList @0x5AEC88`.

Live bind site today: `src/play/universe_render.cpp` ~L342 and
`src/play/city_view3d.cpp` ~L1375 call `render::ProjectObjectVertices(...,
projectAll=false)` right after `ComputeVertexClipFlags`.

To enable world-sprite **depth fade** (the `byte_649DD8` path, e.g. fog/atmosphere
on), the bind site (owned by the universe/city-view agent — NOT edited here) should,
at step 3 for billboard/sprite nodes, call:
```cpp
render::BillboardParams bp;          // fill from the per-frame camera + fog globals
bp.projScaleX = projScalars.xScale;  bp.projScaleY = projScalars.yScale;
bp.centerX    = projScalars.xOffset; bp.centerY    = projScalars.yOffset;
bp.fadeMinSq  = fogMinSq;            // flt_13FC544
bp.fadeNear   = fogNear;            bp.fadeScale = fogScale; // flt_13FC5AC / 58C
bp.enabled    = billboardsEnabled;   bp.depthFade = fogEnabled; // byte_649D70/649DD8
render::ProjectBillboardVertices(verts, count, bp);
render::BillboardQuadVisibilityPass(quads, quadCount);
```
Gate: `BillboardParams.depthFade` (= `byte_649DD8`). When false the result is
identical to the already-wired `ProjectObjectVertices` (modulo the colorOut copy,
which the affine raster ignores) — so the no-fade scene is unchanged.
Inputs: camera-space vertex records (`+0/+4/+8`), the per-frame projection scalars,
the fog scalars. Order: AFTER the clip classify, BEFORE the draw-list append, in
the object pass (same slot as today's `ProjectObjectVertices`).

## Tests

`tests/unit/render_billboard_project_test.cpp` — 9 tests / 26 checks, all pass:
- CenteredVertex, PerspectiveScale (closer z → larger screen offset),
  SkipsDeadVertex, DepthFade (255 / 215 / 0 golden), DisabledPath (byteOut, no
  alpha), AlphaTruncatesTowardZero (frndint truncate-toward-zero), and the quad
  pass: ForceKeepSetsBit6, NotVisibleIgnored, BackFaceWindingCull (keep / cull /
  never-cull). Golden values hand-derived from the formulas, not the source.

Build note: the full `libguild.a` link currently fails only on another agent's
in-progress `src/render/water_render.cpp` (`DrawList` undeclared). The owned files
compile clean; the test was built/run by linking `sprite_scale.o + shape_blit.o +
colorformat.o + test_main.o` directly. The pre-existing `render_sprite_scale_test`
(299 checks) still passes against the modified TU.

## Status: COMPLETE

`0x5AC970` fully reconstructed (all three project arms + fade + quad visibility),
exposed via a clean re-entrant entry, golden-tested, handoff documented. The only
documented non-deliverable is the node-level effect-tint arm (needs the node record
owned elsewhere); flagged, not faked.
