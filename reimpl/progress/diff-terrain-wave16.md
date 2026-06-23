# Wave 16 — DIFF-TERRAIN: true 1:1 binary diff of the terrain LOD/UV/lighting path

Owner: DIFF-TERRAIN. IDA MCP LIVE (gilde.exe, imagebase 0x400000). Line-for-line
diff of every owned terrain function vs the FRESH full decompile (the prior waves'
"lost dump" gaps are now resolved). Owned files only:
`src/render/{terrain_render,terrain_walk,terrain_uvtable,tile_geometry,tile_lighting,
tile_visibility}.{h,cpp}` + their tests. `play/terrain_render.*` is a bind site (not
edited); `raster*` untouched.

---

## Per-function verdicts

| addr | function | verdict |
|------|----------|---------|
| 0x5bf22c | VIBE_Floor_RenderTerrain (whole walk) | FIXED (diagonal-split proxy -> real v379 buffer); stitch DEFERRED (recovered+documented) |
| 0x5bc45c | VIBE_Floor_BuildTilePolys (QuadPolyVisible) | FIXED (decision tree diverged) |
| 0x5bc45c | VIBE_Floor_BuildTilePolys (StampSlopeLight) | FIXED (bare-fistp round-nearest, was trunc) |
| 0x5b94cc | VIBE_Render_ComputeFilterWeights (UV-table half) | VERIFIED-1:1 |
| 0x5c47dc | VIBE_Heightmap_BuildLitTileGeometry (all 3 modes) | VERIFIED-1:1 |
| 0x5c4718 | VIBE_Heightmap_ComputeTileIllumination (15-pattern build + lookup) | VERIFIED-1:1 |
| 0x5c4690 | 15-pattern terrain-class table bytes | VERIFIED-1:1 |
| 0x5ba438 | VIBE_Floor_ComputeLodLevel (distance->LOD ladder) | VERIFIED-1:1 |
| 0x5bef08 | VIBE_Floor_UpdateTileVisibility / StitchTileLod / center-radius | VERIFIED-1:1 |
| 0x5c5610 | SelectTileMeshLod (50.0/scaleX, /4, clamp[1,4]) | VERIFIED-1:1 |

---

## RESOLVED GAP 1 — the per-quad diagonal split selector (was a PROXY)

`terrain_walk.cpp` Pass A previously chose the quad diagonal from
`(i8)types[tc & mask]` (a proxy "the original read its own per-tile flag buffer").
The FRESH decompile @0x5bf22c shows the real selector:

```
v375 = lod >> 1;                                   (line 595)
v379 = *(_DWORD *)(v323 + 4*v375 + 36);            (line 719)  <- the slope buffer
v374 = (N*(worldY/lod)/lod + worldX/lod) & Floor+8;(line 718)  masked ONCE (N*N-1)
... per quad row:  v413 = v379 + v374;             (line 732)
    per quad col:  v494 = *v413;  ++v413;          (lines 740/783)
    per quad row:  v374 += N/lod;                  (line 787)
if (v494 >= 0) TL-BR diagonal else BL-TR.          (line 748)
```

`v379` is the **BuildTilePolys @0x5bc45c OUTPUT buffer** for this LOD layer:
in BuildTilePolys `v22 = *(floor + 4*v42 + 36)` (same offset; v42 = layer 0/1/2
built at cell-stride `dword_5B8CEC = {1,2,4}`). Each byte holds the 0x80
slope/visibility bit (BuildTilePolys' QuadPolyVisible result, whose SIGN drives the
split) and the 0x40 sub-texture marker (the byte_13DCE58 gate). It is the SAME
buffer the texture sub-id sampling reads (`v194 = *v413 & 0x40`). FIX: the walk now
reads `floor->mipTexSrc[lod>>1]` (already modelled at +36) with the exact raw
v374/v413 advance; the `types[]` proxy is removed. mipTexSrc[] header doc corrected
(it is the slope/flag buffer, not a raw texture source).

Bind-site (play/terrain_render.cpp:583/585) sets `types` and `mipTexSrc[]` to the
SAME `texGrid` buffer, so the GroundFrame output is byte-identical to before — the
fix is observable only when the two buffers differ (the real engine state).

Test: `render_terrain_walk_test.TerrainWalkPassA.QuadWindingGolden` Branch B now
drives the SLOPE buffer (texSrc/mipTexSrc) negative, not the type grid. Both
diagonals (winding + flag38 bit0) verified 1:1.

## RESOLVED GAP 2 — the LOD-boundary stitch (Pass B) vertex layout RECOVERED

The four stitch arms (@0x5bf22c lines 848..1691) are fully decompiled and the
vertex/poly layout is recovered (documented in full inside terrain_walk.cpp). Per
edge with a strictly-finer neighbour (`*(neighbour+318) < lod`) the engine, for
each existing boundary poly, (1) emits ONE midpoint vertex at
`v465 = vertBuf + 80*subdivCached` placed at the HALF-STEP offset `v387 = lod>>1`
along the seam (right arm world = origin + axisU*(tileSpan+colW) + axisV*(v387+rowW),
advancing axisV*lod/cell; height cell `(tileSpan+colW + N*(v387+rowW)) & mask`;
Pass-A RGB light), recorded into the clip scratch `v483[0..2] = {vert,seamU,seamV}`;
(2) midpoint-blends the shared-edge UV with `flt_628B48 = 0.5`, diagonal chosen by
`*(poly+38)&1`; (3) RE-POINTS the existing poly's far-vertex pointer to v465 AND
appends ONE 40-byte poly at `polyBuf + 40*polyCount` whose +24 dword is stamped
`1098907648 = 16.0f`, sharing v465; (4) bumps polyCount/subdivCached/vertCount +1
and the UV cursor +2. Arms: RIGHT(col<7) LEFT(col>0) UP(row>0) DOWN(row<7).

**DEFERRED (rule 8, addr 0x5bf22c lines 848..1691):** the emission patches the
engine's 80-byte 2-triangle poly record in place via raw +N dword pointer surgery
(`v470+40/+48/+56`, `v468[2]/[4]/[14]`, `v482+1/+4/+6`); this repo SPLITS that record
into two 40-byte `Polygon`s with `Vertex*` members, so every offset must be
re-derived against a different struct — and there is NO golden: the stitch fires only
for adjacent tiles of UNEQUAL LOD, a state none of the captured AUGSBURG scenes or
any vector exercises, and the original cannot be run to produce reference output.
~900 lines of unverifiable pointer-patching in the binary's most complex function
would risk silent divergence (worse rule-1 violation than a documented gate-only
deferral). The GATE (control flow) is reproduced exactly; polyCount is NOT inflated
(the wave-10 OOB fix). Also corrected an arm-gate bug carried from wave-4/10: the old
code gated DOWN/UP both on `row>0` with the labels swapped — now RIGHT/LEFT/UP/DOWN
match v487<7 / v487>0 / v486>0 / v486<7 (1:1).

## FIXED — QuadPolyVisible decision tree (0x5bc45c quad-flag cascade)

The prior reconstruction enumerated 9 patterns with an Unchanged default. The real
cascade @0x5bc984..0x5bcb30 is a nested guard with a HIDDEN (set 0x80) fall-through:
```
if ((h0||!h1||!h2||!h3) && (!h0||h1||h2||h3)) {        // G1
  if ((h0&&!h1&&h2&&h3)||(!h0&&h1&&!h2&&!h3)) Visible;  // C1
  if ((h0&&h1&&!h2&&h3)||(!h0&&!h1&&h2&&!h3)) Visible;  // C2
  if ((!h0||!h1||!h2||h3) && (h0||h1||h2||!h3)) {       // G2
    if (h0&&!h1&&!h2&&h3) Visible;                      // C3
    if (h0||!h1||!h2||h3) Unchanged;                    // C4
  }
}
Hidden;                                                  // fall-through (or 0x80)
```
Corners: h0=cell, h1=col+step, h2=row+step, h3=both. Truth table recomputed:
`[0,2,1,0,1,0,2,2,2,1,0,1,0,1,2,0]` (0=Unchanged,1=Visible,2=Hidden); the prior
golden `[0,2,2,1,0,0,1,1,1,0,0,1,2,2,0,0]` was wrong. Golden updated in-test
(binary beats conflicting golden, rule 1).

## FIXED — StampSlopeLight lit-value rounding (ConvertX vs bare fistp)

The falloff-LUT INDEX uses `call VIBE_Coord_ConvertX; fistp` (truncate toward zero,
disasm @0x5bc791) — correct as `(int)ConvertX(...)`. But the LIT VALUE
`v42 = (int)v19` uses a BARE `fistp` (NO ConvertX, disasm @0x5bc7b3) = round-to-
nearest-even under the default x87 control word. The source used `(int)lit` (trunc) —
a divergence. FIX: `(int)std::lrint(lit)` (honours FE round-to-nearest-even, the
codebase's established bare-fistp idiom). Inert for the existing golden (the value
clamps to 127), pinned by the in-range comment.

## ConvertX truncation audit (the float->int sites in the owned cluster)

* SelectTileMeshLod (0x5c5610): `(int)v53` after `+0.5` bias = ConvertX truncate. ✓
* BuildTileElevationByte (0x5c47dc): `(int)ConvertX(v28)` truncate. ✓
* BuildLitTileGeometry box-average (0x5c47dc): `(i64)ConvertX(e)` truncate. ✓
* StampSlopeLight LUT index (0x5bc791): ConvertX truncate. ✓
* StampSlopeLight lit value (0x5bc7b3): BARE fistp round-nearest — FIXED above.
* Pass-C projection (0x5bf22c lines 1759..1769): float stores, no int convert.
* Pass-C fog-shade `*(k+79) = (int)v270` (line 1791): preceded by ConvertX. (modelled
  in the walk as the documented no-op store; truncate when enabled.)
* Light-byte clamps (Pass A/B, e.g. `(int)(v412 + flt_13FD4F0)`): bare fistp
  round-nearest, applied via tile_geometry::ClampLightByte (already round-nearest).

## ComputeFilterWeights — UV-table half VERIFIED-1:1

Mapped all 24 a1[] writes from the captured value-flow: e = 1/dword_64A038,
f = 1-2e; the 12 (u,v) pairs match `terrain_uvtable.cpp` exactly. The random-jitter
TAIL (`v27 != 6144` loop, 64x96-float jittered filter samples via RandNext +
MatrixFromEuler) writes a SEPARATE buffer through cursors v33..v36 that are
uninitialised in the Hex-Rays output (the output base register was lost) — it is NOT
the 24-float terrain UV record (flt_13FE540) and is correctly carried as a named gap.

## LOD ladder VERIFIED-1:1 (0x5ba438)

`v7 = tile.radius(+76) / Floor+160`; `bias = (flt_628758=40.0 - count) * flt_62875C=
0.4`; if `(Floor+7272 + tile.+72 + bias) >= v7` then (if `(Floor+7268 + tile.+72 +
bias) >= v7` LOD 1 else LOD 2) else LOD 4; + the 8-frame change debounce (+88/+95/+97)
and the forced-LOD `(flags & 0x1C) -> (8*flags)>>5` override. `tile_visibility.cpp
ComputeLodLevel` matches every branch (thrFar=Floor+7272, thrNear=Floor+7268, the
runtime 20.0/40.0 floor fields). Constants confirmed: flt_628BAC=50.0, flt_628BA4=
-1.75, dbl_628BB4=0.5, flt_628758=40.0, flt_62875C=0.4, dword_5B8CEC={1,2,4}.

## Tests / build

* render_terrain_walk_test 1629, render_tile_lighting_test 126,
  render_tile_textures_test 79, render_terrain_render_test 50, render_terrain_test
  135, render_terrain_scan_test 88, terrain_mesh_test 316, floorgfx_recon_test 54,
  terrain_texturing_test 32, terrain_ground_test 281, play_terrain_render_test 32,
  render_terrain_walk_e2e_test 4721, terrain_render3_test 114 — ALL 0 failures.
* Consumers green: city_view3d_test 52, wire_terrain_bridge_test 36,
  render_scene_floor_test 138, universe_render_e2e_test 1.
* `guild` core library builds clean.

## Pre-existing unrelated breakage (NOT this cluster)

`tests/integration/world_mission_save_itest.cpp:58` references
`MissionRewardInfo.voiceIndex`, which `src/world/mission.h` (concurrent world-cluster
WIP) no longer defines -> that one target fails to BUILD. Outside the terrain cluster;
left for the world/mission owner.
