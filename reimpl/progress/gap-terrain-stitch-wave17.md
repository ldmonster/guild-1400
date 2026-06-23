# Wave-17 — CLOSED: terrain LOD-boundary STITCH EMISSION (@0x5bf22c lines 848..1691)

Owner: W17-STITCH. IDA MCP live (gilde.exe, imagebase 0x400000). Closes the
wave-16 deferral "terrain LOD-boundary stitch EMISSION" — the four stitch arms of
`VIBE_Floor_RenderTerrain @0x5bf22c` were fully decompiled and documented in wave 16
but the actual emission was deferred (rule 8) because it patches the engine's 80-byte
2-triangle poly record by raw pointer surgery vs this repo's split 40-byte `Polygon`s,
with no captured unequal-LOD scene to golden. CLOSED NOW with a 1:1 geometric
reconstruction + a synthetic golden.

Owned files only: `src/render/terrain_walk.{h,cpp}`,
`tests/unit/render_terrain_walk_test.cpp`, this report.
`play/terrain_render.*` is the bind site (read, not edited); other render modules
untouched.

---

## What the decompile showed (the 80-byte 2-tri record)

Pass A's poly record is **80 bytes = TWO 40-byte triangles** (`v192 += 80`, `polyCount
+= 2` per quad, @0x5bf22c lines 711..795). Per quad the engine writes two triangles
(`*v192`/`+40` vertex-ptr triples) PLUS, crucially, **UV-table pointers at +16 and +56**
(`*(v192+16) = &flt_13FE540[24*subTexId]+12`, `*(v192+56) = +18`, lines 750/757/768/771)
and the bound texture id at +20/+60. `flt_13FE540` is a 24-float-stride per-sub-texture
UV table.

The four stitch arms (RIGHT col<7 / LEFT col>0 / UP row>0 / DOWN row<7) each walk the
boundary edge and, per non-null boundary poly (`*ptr != 0`), do FIVE things:

1. **Midpoint vertex** at `v465 = vertexBuf + 80*subdivCached`, placed at the half-step
   `v387 = lod>>1` along the seam (RIGHT/LEFT step down axisV by `lod`, cell `+= N*lod`;
   UP/DOWN step across axisU by `lod`, cell `+= lod`). World = `origin + axisU*u +
   axisV*v` and the per-channel RGB light is the EXACT Pass-A `BuildTileVertex` clamp.
2. **Clip scratch** `v483[3i + {0,1,2}] = {vert, seamU, seamV}` (stride 3).
3. **Seam-UV midpoint blend** with `flt_628B48 = 0.5` into a per-tile 24-byte UV scratch
   (`tile+60 + 24*counter`) backed by `flt_13FE540`, diagonal chosen by `*(poly+38)&1`.
4. **Far-vertex re-point**: the boundary poly's far seam-vertex pointer := the midpoint.
5. **Append ONE poly** at `polyBuf + 40*polyCount` — a qmemcpy of the boundary triangle
   with `v1 := midpoint` and `+24 (uvX)` stamped `1098907648 == 0x41800000 == 16.0f`.
   Counters bumped: `polyCount(+32)+1`, `subdivCached(+16)+1`, `vertCount(+56)+1`.

Per-arm boundary-poly walk (v455 = cols = v180, v456 = rows = v179; quad pairs are
`PBuf[2*(qr*colsM1+qc)]`/`+1` from Pass A):

| arm | guard | iters | boundary poly | re-point slot | append slot | seam |
|-----|-------|-------|---------------|---------------|-------------|------|
| RIGHT (col<7) | rows≠1 | rows−1 | `PBuf[2·cols−4 +1]` (last-col 2nd-tri), +2·(cols−1)/row | TL-BR→v2, BL-TR→v0 | v1 | down axisV |
| LEFT (col>0)  | rows≠1 | rows−1 | `PBuf[0]` (first-col 1st-tri), +2·(cols−1)/row | always v0 | TL-BR→v2, BL-TR→v1 | down axisV |
| UP (row>0)    | cols≠1 | cols−1 | `PBuf[0]` (first-row), +2/col | TL-BR→2nd-tri.v0, BL-TR→1st-tri.v2 | v1 | across axisU |
| DOWN (row<7)  | cols≠1 | cols−1 | `PBuf[(cols−1)·(2·rows−4)]` (last-row), +2/col | v123.v2 (v123 = 1st-tri if TL-BR else 2nd-tri) | v1 | across axisU |

Verified each arm's exact re-point/copy/append against the disassembly-level dword
offsets: RIGHT `*(v470+48)/*v386`, LEFT `*v469`+`v481+1/+2`, UP `*v488`/`v468[2]` +
`v480+1`, DOWN `*(v123+2)` + `v479+1`; the 16.0f stamp at `+6 dword` everywhere. The
copy is taken BEFORE the re-point (so the appended poly keeps the original far corner),
exactly as the engine ordered the qmemcpy ahead of the pointer write.

## What was reconstructed 1:1 (CLOSED)

`terrain_walk.cpp` Pass B now emits, for every finer-neighbour edge:
- the midpoint Vertex (world xyz + RGB light via the shared `EmitVertex`/`BuildTileVertex`
  — byte-identical light to Pass A),
- the clip-scratch `{vert, seamU, seamV}` triple,
- the boundary far-vertex re-point (the exact per-arm/per-diagonal slot),
- the appended poly (copy of the boundary `Polygon` with `v1 := midpoint`, `uvX = 16.0f`),
- the `polyCount` / `subdivCached` / `vertCount` bumps,
all with the exact per-arm world/cell advance, loop counts, guards, and null-skip.

The equal-LOD path is untouched (the stitch only runs when `edge*Lod != 0 && < lod`),
so every pre-existing golden is unchanged (e2e 4721 / unit 1631 all still pass).

## Genuine remaining boundary (rule 8, addr + reason)

**Seam-UV midpoint blend** (@0x5bf22c lines 953..1043 / 1167..1265 / 1382..1473 /
1601..1666): the `flt_628B48 = 0.5` averaging of the shared-edge UV pair into a per-tile
24-byte UV scratch (`tile+60`) backed by the global `flt_13FE540` UV table. This repo's
terrain pipeline does **not** model that UV subsystem at all — Pass A here carries only
the bound texture id in `Polygon::uvZ` (no `flt_13FE540` table, no per-tile UV scratch,
no poly+16/+56 UV pointers), and `flt_13FE540` is uninitialised (all-zero) in the IDB.
Reproducing the blend faithfully would require first reconstructing the whole Pass-A UV
emission path (in `tile_geometry` / the texture cache — NOT owned here) and it has no
golden (the table is zero in the static image and no unequal-LOD scene was captured).
Documented inline in `terrain_walk.cpp` with the address. Every GEOMETRIC effect of the
stitch (vertex, re-point, append, 16.0f stamp, counters) IS reconstructed 1:1.

## Tests / build

`render_terrain_walk_test`: 1670 checks, 0 failures (was 1629; +41 from new stitch
goldens, +reworked `StitchPolyCountStaysInBuffer`):
- `TerrainWalkStitch.RightArmMidpointGolden` — pins midpoint world xyz, light byte,
  clip scratch `{vert,seamU,seamV}`, far-vertex re-point (TL-BR→v2), appended poly
  (v1=midpoint, uvX=16.0f, shared near corner), counts.
- `TerrainWalkStitch.RightArmBlTrDiagonal` — slope buffer high-bit set → BL-TR split →
  boundary re-points v0; appended v1=midpoint, uvX=16.0f.
- `TerrainWalkStitch.FourArmsCountAndStamp` — all 4 arms on an interior LOD-2 tile →
  +8 verts/+8 polys; every stitch poly stamped 16.0f.
- `TerrainWalkStitch.GateNoFireWhenNotFiner` — equal/coarser/absent neighbour → no
  stitch (Pass-A counts intact).
- `StitchPolyCountStaysInBuffer` — reworked: was a gate-only "stays 8" regression; now
  asserts the real growth (8 Pass-A + 8 stitch = 16; subdiv 9+8; vertCount 8).

Consumers green: `render_terrain_walk_e2e_test` 4721, `render_tile_lighting_test` 126,
`render_terrain_render_test` 50, `render_terrain_test` 135, `render_terrain_scan_test`
88, `terrain_mesh_test` 316, `play_terrain_render_test` 32, `terrain_render3_test` 114,
`city_view3d_test` 52, `wire_terrain_bridge_test` 36, `render_scene_floor_test` 138,
`universe_render_e2e_test` 1 — all 0 failures. `guild` core library builds clean.

Memory-safety: the stitch was also built under `-fsanitize=address,undefined` (terrain
cluster only) and ran clean (1670 checks, 0 failures) — no OOB writes/reads, no UB.
