# Wave-7 — GROUND-shadow quad (terrain-conforming soft shadow)

Agent **W7-SHADOWGND**. Closes the rule-8 named gap deferred by wave-6
(`progress/shadow-render-wave6.md`, "render_leaves7 agent"): the GROUND shadow —
the soft shadow that conforms to terrain height under a caster, distinct from the
mesh-silhouette splat already done in wave 6. Reconstructed 1:1 from the Hex-Rays
reference (IDA MCP live).

## Addresses reconstructed

| addr      | function                          | what                                            |
|-----------|-----------------------------------|-------------------------------------------------|
| 0x5f3048  | VIBE_Shadow_BuildGroundShadow     | clip-rect + caps + dispatch + flat-quad fallback |
| 0x5f216c  | VIBE_Shadow_ProjectGroundQuad     | projected-terrain-tile path (see "deferred")    |
| 0x5f2a58  | VIBE_Shadow_RasterizeHeightField  | rasterize the shadow over the terrain heightfield |

Caller (NOT edited): `VIBE_Shadow_CastFromLight` @0x5f3f98 (xref @0x5f428d), itself
the fxrecon `CastFromLightHook` seam from wave 6.

Callee leaf used (NOT edited): `VIBE_Coord_ConvertX` @0x5c6b08 (x87 `frndint` round)
— the caster colour-key packing; in the portable path the colour key is carried
resolved in `GroundShadowCaster.colorKey`, so the rounding only affected the in-
binary byte packing of the key (documented, no behavioural effect on emission).

## Why a NEW module (`src/render/shadow_ground.{h,cpp}`)

`render_leaves7.{h,cpp}` (wave-7 sibling) reconstructed ONLY the leaf arithmetic
fragments of these three functions and explicitly DEFERRED the pool-emitting bodies:
- `ComputeShadowClipRect` (0x5f3104..e2 block)  — REUSED here (extern via header)
- `ProjectGroundVertex`, `RasterizeHeightVertexY`, `HeightFieldBias`,
  `HeightEdgeDiscontinuous` — REUSED here.

This module supplies the FULL bodies (the per-cell vertex emission into the global
vertex/draw pools, the quad stitch, the edge-discontinuity flag, the dispatch, and
the flat-quad fallback) plus the clean caster-shadow entry. No symbol was
re-derived; `grep` confirmed no other reconstruction of 0x5f3048/216c/2a58 bodies
exists (only the leaf fragments + the dispatch stubs in render_leaves7).

## Pools (gilde.exe runtime globals → caller-owned, rule 3/4)

The original streams two interleaved vertex pools and two draw-command pools and
bumps four counters; modelled byte-exact as `GroundShadowPools`:

| field        | global         | stride   | meaning                          |
|--------------|----------------|----------|----------------------------------|
| vertexA      | dword_1408A50  | 24 (6 f) | per-cell ground vertices (pos+payload) |
| vertexB      | dword_1408A58  | 24 (6 f) | quad-corner UV payload           |
| draw0        | a7[0]          | 80 bytes | phase-1 draw cmds (per cell)     |
| draw1        | a7[1]          | 40 bytes | phase-2 two-tri quad cmds        |
| vertexCountA | dword_1408A64  |          | global vertex-A count            |
| vertexCountB | dword_1408A68  |          | global vertex-B count            |
| drawCount0   | a7[2]          |          | running draw0 count / vtx stamp  |
| drawCount1   | a7[3]          |          | running draw1 count              |
| capacity     | dword_64A7EC   |          | cap gate (0 = unbounded, tests)  |
| drawBaseCount| v5[3]          |          | a7-record running base           |

`a7` is the per-object shadow-batch record at obj+492+244 (the original's v5/a3/a7).

## Math reconstructed (verbatim, with addresses)

- **RasterizeHeightField phase 1** (0x5f2b67..0x5f2ca9): for each cell of the
  clamped clip rect emit one ground vertex:
  `X = x0*xStep+xBase` advancing by `xStep`; `Z = y0*zStep+zBase` advancing by
  `zStep`; `Y = ((double)h + bias)*yScale + (baseY + bias)` (0x5f2bfb, == the
  reused `RasterizeHeightVertexY`). `bias = byte_64A351 ? 1.5 : 1.0` (0x5f2b02).
  Stamps colour key into draw0+68, vertex slot into draw0+72; bumps A64 + a7[2].
- **RasterizeHeightField phase 2** (0x5f2cce..0x5f2fe3): stitch the (w-1)x(h-1)
  cells into two triangles each; each tri → a 40-byte draw1 record (3 vtx slots,
  material id at +20, the `-1.0f` (0xBF800000 == -1082130432) homogeneous-w
  sentinel at +24) + a UV payload vertex into vertexB; bumps A68 + a7[3]. The
  **edge-discontinuity bit** (+38 bit1, value 2) is set when neighbouring terrain
  heights differ by > 25.0 (dbl_62C270) — `HeightEdgeDiscontinuous` (0x5f2dd5 /
  0x5f2ea8).
- **BuildGroundShadow** (0x5f3048): caster colour-key derivation (0x5f306f..da via
  ConvertX); the `byte_1408A6D`+dim+`!directional` gates (0x5f30de..fe); clip-rect
  via the reused leaf (0x5f3104..e2); the two capacity gates vs `dword_64A7EC`
  (0x5f31fa: `2*cap <= 2*(h-1)*(w-1)+base`; 0x5f323f: `25*(cap>>4) <= count+area`);
  dispatch (0x5f3286): tile → ProjectGroundQuad, else RasterizeHeightField; and the
  **flat-quad fallback** (0x5f337d..0x5f34b0) emitting one two-triangle quad from
  the +72/+76/+80/+84/+88 corner floats (vertex triple order (Xext, mid, Yext)),
  bumping drawBaseCount by 2 (`v5[3] += 2`).

## Constants verified by get_bytes (bit-exact)

| symbol      | addr     | bytes (LE)              | value | use                         |
|-------------|----------|-------------------------|-------|-----------------------------|
| flt_62C278  | 0x62C278 | 00 00 00 3F             | 0.5   | caster-height scale         |
| flt_62C26C  | 0x62C26C | 00 00 00 3F             | 0.5   | ground-Y bias (ProjectGroundQuad) |
| dbl_62C270  | 0x62C270 | 00 00 00 00 00 00 39 40 | 25.0  | edge-discontinuity tolerance |
| byte_64A351 | 0x64A351 | 00                      | 0     | high-detail bias (1.5 vs 1.0) |
| byte_1408A6D| 0x1408A6D| 00                      | 0     | heightfield-enable master flag |
| dword_64A7EC| 0x64A7EC | 00 00 00 00             | 0     | vertex/draw capacity cap    |
| -1.0f bits  | —        | 00 00 80 BF             | -1.0  | 0xBF800000 == -1082130432 (+24/+64 sentinel) |

All match the values already baked into render_leaves7.

## Clean entry exposed

```cpp
char BuildGroundShadow(const GroundShadowCaster& caster, bool hfEnabled,
                       const GroundShadowTile* tile, GroundShadowPools& pools);
```
Build + project + rasterize a caster's ground shadow over a heightfield in one call.
Sub-entries `RasterizeHeightField` and `ProjectGroundQuad` are also exposed for the
dispatch and for golden testing.

## Tests — `tests/unit/shadow_ground_test.cpp` (7 tests, 40 checks)

Clean under `-fsanitize=address` (`checks=40 failures=0`).
1. `HeightVertexYGolden` — `(h+bias)*yScale+(baseY+bias)` golden values.
2. `RasterizePhase1Grid` — 4x3 cell grid → 12 vertices; X/Z advance, colour key &
   +72 vertex-slot stamp.
3. `RasterizePhase2EdgeFlag` — 3x2 cells → 12 two-tri draw1 records; -1.0f sentinel;
   tri vertex indices; no false edge flags on a flat field.
4. `EdgeDiscontinuityFires` — `HeightEdgeDiscontinuous` at the 25.0 boundary + an
   end-to-end cliff lighting the +38 bit1 flag.
5. `BuildDispatchHeightfield` — hfEnabled + null tile → heightfield path emits grid.
6. `BuildGatesReject` — directional / hfEnabled-off → flat-quad path (4 verts, 2
   draws, drawBaseCount=2); empty rect (maxX<=minX) → reject (returns 0, no emit).
7. `ProjectGroundQuadNullTile` — null tile → 0 (the 0x5f3286 guard), no emission.

## Completeness / deferred (rule 8)

- **Reconstructed 1:1, tested:** BuildGroundShadow (clip+caps+dispatch+flat-quad),
  RasterizeHeightField (both phases, edge flag, all pool bumps), the capacity gates,
  the caster colour-key derivation path.
- **ProjectGroundQuad (0x5f216c) tile body — INERT NAMED HOOK:** the projected-
  terrain-tile re-use path (0x5f22a6..0x5f29c6) walks an engine-private cached-tile
  record whose binary layout (`a1` here: stride `a1[1]`, the `BYTE2(a1[...])` cell
  flags, the `a1[v34+79+v47]` per-cell stamps) is NOT recoverable into a portable
  struct without the tile-cache allocator that produces it. Per rule 8 it is left as
  a NAMED inert hook returning 0 for a null tile — which is exactly the original's
  `if (v48)` guard at 0x5f3286: when no tile is present the engine takes the
  heightfield path, so the live frame's terrain-conforming shadow is FULLY
  reconstructed via RasterizeHeightField. The tile path is a perf cache, not a
  distinct visual; reconstructing its body needs the tile-cache module (ask user /
  future wave).

## EXACT CityView3D handoff

No bind-site edit was made (ownership). The ground shadow attaches at the SAME seam
wave-6 documented, one step later in `CastFromLight`:

```
ProcessSceneNode (0x5add1c)  — per scene object, AFTER terrain draw (0x5b3a2f),
  └ UpdateNodeShadows (0x5f4494)        — per object, ≤4 lights  [fxrecon]
      └ CastFromLight (0x5f3f98)        — alloc/reuse caster slot, decide redraw
          ├ (1) RenderMeshShadow (0x5f363c)  — splat the mesh SILHOUETTE into the
          │      caster's shadow surface           [wave-6 RenderObjectShadow]
          └ (2) BuildGroundShadow (0x5f3048) — EMIT THE GROUND-SHADOW QUAD  [THIS WAVE]
                if (BuildGroundShadow(...)) *(dword_649D68+528) |= 4;   // 0x5f428d/9b
```

**WHERE the ground-shadow quad draws relative to the mesh shadow + objects:**
- It is emitted AFTER the mesh silhouette is rasterized into the caster's shadow
  surface (step 1 above) and uses that surface as its texture (the colour key /
  payload threaded through `colorKey`).
- The emitted verts/tris go into the shared ground vertex/draw pools
  (dword_1408A50/58 + a7[0]/[1]), which the frame flushes as part of the terrain
  pass — so the ground shadow draws **on the terrain, between the terrain and the
  objects** (objects draw on top). The `*(+528) |= 4` redraw bit BuildGroundShadow
  triggers tells the frame the shadow pool changed and must be re-flushed.
- Gate the orchestrator wires (unchanged from wave 6): `Options::shadows`
  (→ dword_1408A60), object `+529 & 0x4`, non-empty `CollectedShadowLights()`, and
  the heightfield master flag `byte_1408A6D` (→ `hfEnabled`). With `hfEnabled` and
  no projected-terrain tile, BuildGroundShadow takes RasterizeHeightField (the
  terrain-conforming path); without it, the flat-quad fallback.

The orchestrator must pass the per-frame pools (the engine globals) and the per-
object shadow-batch record (obj+492+244) as `GroundShadowPools`, and the caster's
shadow-map record fields as `GroundShadowCaster`. No new GPU/Vulkan/SDL code; pure
software emission into the existing pools, presented via the existing path.
