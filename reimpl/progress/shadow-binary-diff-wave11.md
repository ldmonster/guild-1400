# Wave-11 — SHADOW cluster TRUE 1:1 binary diff (line-for-line vs decompile)

Agent **W11-SHADOWDIFF**. A line-for-line binary diff of the shadow projection /
rasterize cluster against the live Hex-Rays decompile (IDA MCP), focused on
control flow, constants, rounding (ConvertX truncate vs fistp round), winding,
and pool counters. Every divergence found was fixed to match the binary EXACTLY
and pinned with a golden test. No bind-site edits.

Owned files: `src/render/shadow_render.{h,cpp}`, `shadow_project.{h,cpp}`,
`shadow_ground.{h,cpp}`, `shadow_light_list.{h,cpp}` + their tests.

## VIBE_Coord_ConvertX @0x5c6b08 — confirmed TRUNCATE-toward-zero

Disasm: `fstcw` save CW; set CW high byte to `0x1F` (RC=11 = round-toward-zero,
PC=11); `fldcw`; `frndint` (rounds st0 to integer using truncate mode, in place);
`fldcw` restore. The subsequent `fistp` then stores an already-integer-valued
float, so the net is **truncate toward zero** — i.e. C++ `(int)x`. Every shadow
float→int conversion that routes through ConvertX uses `(int)` and is correct.
(The bounds-rect ConvertX sites in RenderMeshShadow/BuildGroundShadow likewise
truncate; the reconstruction routes the bounds-rect through ComputeShadowClipRect
and carries the colour-key resolved — documented, no behavioural change.)

## Shadow_RasterizeTriangle @0x603ed4 — FIXED (winding / apex resolution)

Verified the cross-product winding test, the back-flag gate (`+38 & 4`, only the
reverse branch returns when clear), the reverse vertex-load order (verts 2,1,0 →
px/py[0,1,2]), the truncating ConvertX conversion, and the px/py validation loop —
all 1:1. Three divergences in the apex/edge resolution were FOUND and FIXED
(register trace at 0x60400b..0x6041eb, A=edi B=esi C=ebp D=ecx):

1. **Equal/flat-top branch (0x604025)** was wrong. For `v8 == py[next[a4]]` the
   binary sets the RIGHT edge to `InterpolateEdgeZ(next[a4], next[next[a4]])`, i.e.
   `rGuard = kEdgeNext[a4]`, `rightV = kEdgeNext[kEdgeNext[a4]]`. The
   reconstruction had `rGuard=a4`, `rightV=kEdgeNext[a4]` (a4→next), the wrong
   edge. (Note next∘next == prev for a 3-cycle, so the flat-top right edge runs
   next[a4]→prev[a4].) The prev-equal (0x604181) and else (0x604197) cases were
   already correct.

2. **First span block (0x6040C4..0x60411a)** count was wrong. The binary fills
   `ceil16(MIN(py[leftV],py[rightV])) - topRow` (decompile `v30 = min(py[v26],
   py[v28])`). The reconstruction used `ceil16(py[leftV])` only — wrong whenever
   `py[rightV] < py[leftV]`.

3. **Second span block (0x60412b..0x6041eb)** count sign was wrong for `!v36`.
   For `v36` (py[leftV]<py[rightV]) → ComputeEdgeSlope(leftV,rightV), count =
   `ceil16(py[rightV]) - ceil16(py[leftV])`. For `!v36` → InterpolateEdgeZ(rightV,
   leftV), count = `ceil16(py[leftV]) - ceil16(py[rightV])` (the disasm subtracts
   edx-eax with operands swapped by branch). The reconstruction used the v36 form
   for both → a NEGATIVE count in the !v36 case → FillSpans did nothing → the
   lower sub-triangle was never filled. Now branch-correct.

Pinned by NEW goldens in `shadow_object_render_test.cpp`:
`FlatTopTriangleWinding` (flat-top → equal branch; exact filled=207, monotone
non-increasing row widths) and `SecondSpanNotV36Branch` (left-bottom lower than
right-bottom → !v36; exact filled=191, lower band fills — was 0 pre-fix).

## ComputeEdgeSlope @0x603d00 / InterpolateEdgeZ @0x5f6a8c / FillSpans @0x603da8 — VERIFIED 1:1

- EdgeSetup: `dy>=0x10000 → (i64)dx<<16 / dy` (signed); else `(0x40000000/dy) *
  (i64)dx) >>14` (shrd); start = `px0 + ((i64)slope * frac) >>16` with
  `frac = ((py0+0xFFFF) & ~0xFFFF) - py0`. Disasm confirms `(x sar16) shl16` ==
  `& ~0xFFFF` (clears low 16 bits) and the `imul`/`shrd` give the exact decompile
  value. The wave-10 UB rewrites (`*65536`, `& ~0xFFFF`, widened products) are
  bit-identical. InterpolateEdgeZ is identical but writes the right-edge globals.
- FillSpans: 8bpp memset / 16bpp word loop, `ceil16(leftX)`/`ceil16(rightX)` span,
  edge accumulators advance, dstRow += pitch (8bpp) / 2*width (16bpp). 1:1; the
  wave-10 row/span window guards are no-ops on the square in-bounds path.

## RenderMeshShadow projection (extracted into shadow_project) — FIXED point branch

- **ProjectVertexPoint @0x5f3ce3 — FIXED.** Disasm (ecx=light L, edx=vertex v):
  `t = (L.y - groundY) / -(L.y-v.y)` (numerator uses **L.y**, not v.y) and
  `v' = L + t*(L-v)` (built off the **light position**, not the vertex). The
  reconstruction used `(v.y-groundY)` and added the vertex. Although algebraically
  the same ray/ground intersection (so the result is float-near-identical and the
  tolerance goldens passed both ways), the **exact float operation order differs**;
  fixed to the binary's ops for true bit-1:1. Header math comment corrected.
- **ProjectVertexDirectional @0x5f3721 — VERIFIED 1:1.** `t=(v.y-g)/-dir.y`,
  `v'=v + t*dir` (built off the vertex, with the light DIRECTION). Confirmed by
  disasm (edx=vertex, eax=dir).
- Bounds fold (>= / <= forms), `ShadowBoundsAcceptable` (±16384, all 4 bounds +
  2 spans), `MapShadowVertexToSurface` (surfW/(extent), (p-min)*scale), and
  `ComputeCasterHeight @0x5f34c0` (cached-dir override → onGround → 8-sample min)
  — all VERIFIED 1:1.

## Ground shadow (shadow_ground) — FIXED (3 divergences)

- **RasterizeHeightField @0x5f2a58 phase-2 vertex base — FIXED** (resolves wave-10
  flagged ambiguity #1). Phase 2 references the phase-1 records relative to the
  ENTRY draw0/vertexA base (`v55 = 80*a7[2]_entry + *a7`), NOT absolute 0. Now
  indexes `entryBaseA + row*v33 + col` so reused pools (entry count != 0) match.
- **RasterizeHeightField phase-2 vertexB payload — FIXED.** The vertexB record is
  SIX flat floats (`v11[0..5]`), not pos+pl; the reconstruction wrote only 3, at
  the wrong fields, with `uv[3]` where the binary uses `uv[1]`. Now emits the exact
  six floats per triangle (tri1: u,v,uStep+u,v,u,vStep+v; tri2: uStep+u,v,uStep+u,
  vStep+v,u,vStep+v) from 0x5f2ded..0x5f2f26.
- **BuildGroundShadow @0x5f3048 flat-quad counters — FIXED.** The binary emits the
  two triangle COMMANDS into drawPool1 indexed by `drawBaseCount` (v24 =
  40*v5[3]+a7[1]) and FOUR per-vertex records into drawPool0, bumping
  `drawCount0 += 4` (v5[2], 0x5f34ad), `drawBaseCount += 2` (v5[3]), and
  `vertexCountA += 4`. The reconstruction wrote 2 draw0 records and bumped
  drawCount0 by only 2. Now: tri commands → draw1[drawBaseCount..+1], drawCount0
  += 4. Test `BuildGatesReject` golden updated (drawCount0 4, tri indices in d1).
- VERIFIED 1:1: the hf-enable+data gates, the two capacity gates (2*cap vs
  2*(h-1)*(w-1)+base; 25*(cap>>4) vs count+area — runtime cap is nonzero, the
  `capacity==0` skip is a documented test affordance), the dispatch (`if(tile)`),
  the flat-quad vertex order (Xa/Xb, mid, Ya/Yb), the -1.0f sentinel, the 25.0
  edge-discontinuity test, HeightFieldBias (1.0/1.5).
- ProjectGroundQuad @0x5f216c tile body remains the documented inert NAMED hook
  (rule 8 — engine-private tile-cache layout; null tile → 0 → heightfield path,
  matching the original `if(v48)` guard).

## Light list (shadow_light_list) — VERIFIED 1:1

- `ShadowResetLightList @0x5f4428`: count=0, WalkAndInvoke(root, mask 16,
  PushToDrawList). 1:1.
- `PushToDrawList @0x5f43f4`: `(+529 & 4)==0 → return count<4`; else write at
  count+1 (1-based in the binary; 0-based here — same collected set/count), bump,
  `return count_new < 4` (stops the walk at four). 1:1 — confirmed the boundary
  collects exactly four casters with count reaching 4.

## Tests (all pass; built against the prebuilt libguild.a + recompiled objects)

| target                               | checks |
|--------------------------------------|--------|
| shadow_object_render_test (+2 new)   | 37     |
| shadow_project_test                  | 32     |
| shadow_ground_test (golden updated)  | 66     |
| shadow_light_list_test               | 22     |
| shadow_project_itest                 | 25     |
| shadow_project_e2e_test              | 20     |
| render_shadow_test                   | 2196   |
| shadow_cache_test                    | 63     |
| fxrecon_particle_mirror_shadow_test  | 92     |
| shadow_node_update_test              | 54     |

Build note: the repo-wide `cmake --build` is transiently broken by an UNRELATED
in-flight edit in `src/play/city_view3d.cpp` (missing `BuildTileIlluminationTable`
/ `LitFloorView` / `BuildLitTileGeometry` from another agent — not a shadow file).
All four owned TUs compile cleanly in isolation (`-Isrc -Iinclude -Ishim`), and
every affected test was linked against the prebuilt `build/libguild.a` (with the
recompiled shadow objects taking precedence) and passes. No `src/play` file was
touched.
```
