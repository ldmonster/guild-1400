# Wave-16 — DIFF-RENDER (true 1:1 binary diff of the world-entity render cluster)

IDA MCP live (gilde.exe, imagebase 0x400000). Line-for-line diff of every reconstructed
function in the `src/render/` world-entity cluster against the fresh Hex-Rays decompile +
disassembly, with `get_bytes`-verified constants. Every float→int site audited against
`VIBE_Coord_ConvertX @0x5c6b08` (confirmed: `frndint` under RC=11 = truncate-toward-zero;
a bare `fistp` WITHOUT ConvertX = round-to-nearest-even). The two big wave-7/wave-12
deferrals (x87 particle phase chain; terrain Pass-A stitch + slope buffer) are resolved.

Build green (`libguild.a` links; full `cmake --build build` 0 errors). NO git commit.

---

## 1. Sky / SkyColor / Sun / DayCycle / Fog / Falloff
Files: `sky.{h,cpp}`, `skycolor_recon.{h,cpp}`, `sun_state.{h,cpp}`, `daycycle.{h,cpp}`,
`fog.{h,cpp}`, `falloff_lut.{h,cpp}`.

- **FIXED — `BlendAmbientFog` @0x5b8b04** colour-byte cross-fade used the algebraic form
  `a·(1−t)+b·t`; the binary (0x5b8b9e) uses `(B−A)·frac + A` then ConvertX-truncate. These
  are NOT IEEE-equal: 20,971 (A,B,frac) cases truncate to different ints (classic:
  A==B==1, frac=0.05 → binary 1, old form 0 → spurious −1 colour loss). Fixed to the
  binary form + `TruncToward`. Golden `SkyRender.AmbientFogEqualEndpointsNoLoss`.
- **VERIFIED-1:1**: `GetSeasonFromDay @0x58339c` (`(signed char)(day%4)`); `BlendBandLighting
  @0x5b85e4` (scratch `a·(1−t)+b·t`, ambient head `(B−A)·t+A`, luma g·0.59+r·0.30+b·0.11);
  **fog 255.0 clamp CONFIRMED 255.0 not 256.0** (`dbl_628074=dbl_628B34=0x406FE000…=255.0`,
  `flt_628088=0x437F0000=255.0`); `InitFalloffTable @0x5c88f8`
  (`table[i]=1.0−asin(i/1024)·(2/π)`; base is `flt_1405110`, `flt_140510C` never written;
  `AcosGuarded @0x5f0b9c`=asin; `flt_628CB4=1/1024`, `flt_628CB8=2/π`); `BrightnessToBand`
  (band=int%7, blend=frac; `dbl_61DD68=0.01`).
- NOTE (benign): `BuildTimeTable @0x4b2438` uses raw signed `6*season`; source masks `&3` —
  identical for all live days (season 0..3).

## 2. Water
Files: `water_render.{h,cpp}`, `water_vertices.{h,cpp}`, `water_anim.{h,cpp}`,
`floorwater.{h,cpp}`.

- **FIXED — `VIBE_Floor_AnimateWaterVertices @0x5be428`**: the second tex-coord accumulator
  was `texAccumB = Fmod(1.0, texAccumA)` (Hex-Rays arg artifact). disasm 0x5be4d7 shows the
  SAME structure as the first: `texAccumB = (texRateB·dt + texAccumB) mod 1.0` — independent
  accumulators; `texRateB` (float[4], +0x10) was unused. Golden
  `DriverTexAccumIndependentGolden`.
- **VERIFIED-1:1**: the 4-element phase propagation (`phase[k]=fmod(speed[k]·dt+phase[k],2π)`),
  the 16-vec4 wave grid (sin/cos table dbl_628AEC=2.7 … dbl_628B24=4.0, t=2π);
  `PrepareRegions @0x5ba95c` (mesh-init `kMeshInit[9]` byte-exact, 0x158 stride, count at
  Floor+0x1C6D, FillHeightGradient/FloodFillMask/FindRegionOffset);
  `TransformTileGeometry @0x5be668` water arm (region-id span+16, rec+20 wave scale, rec+10&1
  flat-vs-UV, `v85=flt_628B2C(0.5)−|sunDir·(0,0,1)|`); EF_WASS key `((mesh+4−base)>>7)+1`.

## 3. Terrain (LOD / UV / lighting / stitch) — BIG DEFERRAL RESOLVED
Files: `terrain_render.{h,cpp}`, `terrain_walk.{h,cpp}`, `terrain_uvtable.{h,cpp}`,
`tile_geometry.{h,cpp}`, `tile_lighting.{h,cpp}`, `tile_visibility.{h,cpp}`. (full detail in
`diff-terrain-wave16.md`)

- **RESOLVED GAP 1 — diagonal split was a PROXY.** The selector `v494=*v413` reads the per-LOD
  SLOPE buffer `v379 = *(Floor + 4*(lod>>1) + 36)` = the **BuildTilePolys @0x5bc45c** output
  (cell-stride `dword_5B8CEC={1,2,4}`; SIGN drives the diagonal, 0x40 bit the sub-texture id).
  Now read via `mipTexSrc[lod>>1]` with the exact `v374/v413` advance; proxy removed.
- **RESOLVED GAP 2 — the four LOD-boundary STITCH arms** (lines 848..1691) fully decompiled +
  vertex layout recovered: half-step `v387=lod>>1` midpoint vertex, 0.5 edge-UV blend
  (`flt_628B48`), in-place far-vertex re-point + 1 appended poly stamped 16.0f, gates
  RIGHT(col<7)/LEFT(col>0)/UP(row>0)/DOWN(row<7) (a carried wave-4/10 DOWN/UP gate swap fixed).
  EMISSION deferred (rule 8): it patches the engine's 80-byte 2-triangle record by raw pointer
  surgery vs this repo's split 40-byte Polygons, with no golden (fires only on unequal-LOD
  adjacency, in no captured scene) — gate-only kept, polyCount not inflated (wave-10 OOB safe).
- **FIXED**: `QuadPolyVisible` decision tree (real nested cascade, Hidden fall-through; truth
  table `[0,2,1,0,1,0,2,2,2,1,0,1,0,1,2,0]` — old golden was wrong); `StampSlopeLight` lit
  value uses bare `fistp @0x5bc7b3` = round-nearest (`std::lrint`), was truncate.
- **VERIFIED-1:1**: `ComputeFilterWeights @0x5b94cc` UV-table half; `BuildLitTileGeometry
  @0x5c47dc` (3 modes); `ComputeTileIllumination @0x5c4718` + 15-pattern table @0x5c4690;
  `ComputeLodLevel @0x5ba438` distance ladder; `SelectTileMeshLod @0x5c5610`.

## 4. Shadow
Files: `shadow_render.{h,cpp}`, `shadow_project.{h,cpp}`, `shadow_ground.{h,cpp}`,
`shadow_light_list.{h,cpp}`.

- **FIXED — `Shadow_RasterizeTriangle @0x603ed4`** (3 winding/apex bugs, register trace
  0x60400b..0x6041eb): flat-top right edge must be `(next[a4], next[next[a4]])`; first span
  count = `ceil16(min(py[L],py[R])) − topRow`; second-span count sign swapped for the `!v36`
  branch (was producing negative count → lower sub-triangle never filled). Goldens
  `FlatTopTriangleWinding`, `SecondSpanNotV36Branch`.
- **FIXED — `ProjectVertexPoint @0x5f3ce3`**: `t=(L.y−groundY)/−d.y` off **light** position,
  `v'=L+t·d` (was vertex-based; same point, different exact float ops).
- **FIXED — `RasterizeHeightField @0x5f2a58`**: phase-2 vertex indices relative to phase-entry
  base; vertexB six flat floats (`uv[1]` not `uv[3]`).
- **FIXED — `BuildGroundShadow @0x5f3048` flat-quad**: tri cmds into drawPool1 by
  drawBaseCount; `drawCount0 += 4`, `drawBaseCount += 2`.
- **VERIFIED-1:1**: `ComputeEdgeSlope @0x603d00`, `InterpolateEdgeZ @0x5f6a8c`, `FillSpans
  @0x603da8`, `ProjectVertexDirectional @0x5f3721`, `ComputeCasterHeight @0x5f34c0`,
  `ShadowResetLightList @0x5f4428` + `PushToDrawList @0x5f43f4` (4-light, 1↔0-based remap).
- Deferred (rule 8, unchanged): `ProjectGroundQuad @0x5f216c` tile body (engine-private tile
  cache; null-tile → heightfield path).

## 5. Particles — BIG DEFERRAL RESOLVED (the x87 phase chain)
Files: `particle_integrate.{h,cpp}`, `particle_render.{h,cpp}`, `particle_spawn.{h,cpp}`,
`particle_emitter_create.{h,cpp}`.

- **RESOLVED — the x87 phase-wrap chain.** `VIBE_Math_Fmod @0x5d3fb2` = `fprem` (st0 mod st1)
  then `fstp st(1)`. Hex-Rays lists args `(a1@st1, a2@st0)`, so its `Fmod(pi, x)` means
  **`x mod pi`**; the bogus `Fmod(pi,pi)`/`Fmod(pi,dt)` came from Hex-Rays mistracking which
  deep FPU slot held the live `pi` copy (`fld st(2)`/`fxch` across the 5 calls). **No transient
  divide-by-zero exists.** The five accumulators are **independent** (do NOT seed off each
  other): `ang_i = fmod(dt·rate_i + ang_i, π)` (i=0..3) and `phase = fmod(dt·phaseRate + phase,
  π)`. `dbl_62BAA4/AC4/AE4 = π`.
- **FIXED along the way** (3 real divergences vs the wave-7 reconstruction):
  - `UpdatePoints @0x5e1e0c`: velocity advanced by `dt·a1[+0x90/94/98]` (asm 0x5e246f), not
    `phase·…`.
  - `UpdatePolys @0x5e2814`: angle rates are `a1[+0x50/54/58/5C]` and phase rate `a1[+0xB4]`
    (the Points fields), NOT `a1[+0x80..]/+0xB0`.
  - `UpdateLens @0x5e32c0`: same — angles `+0x50..`, phase `+0xB4`, velocity `+0x90..`.
  Goldens `PointsWrapChainAndDtVelocity`, `PolysAndLensWrapChainSameRates` (0x80.. fields
  seeded with garbage to prove they are unused).
- **VERIFIED-1:1**: `SpawnSystemByType @0x5e3ae0`, `AllocSystem @0x5e1000`, `CreateEmitter
  @0x43fd24`. ConvertX-truncate confirmed at every float→int site (alpha out, cap, birthTick).

## 6. Mirror / Sprite / Node-LOD / Env-map / Reflective-nodes
Files: `mirror_project.{h,cpp}`, `mirror_silhouette.{h,cpp}`, `sprite_scale.{h,cpp}`,
`node_lod.{h,cpp}`, `reflective_nodes.{h,cpp}`, `env_map_walk.{h,cpp}`.

- **FIXED — `PrepareReflectionNode @0x5F676C` mirror-plane normal** (was a cheap analogue,
  rule-8 violation): the binder fabricated the normal via `TriangleNormal(origin,v0,v1)`. The
  engine (disasm 0x5f688a..) rotates the bound poly's STORED normal (`*(boundPoly+16)+44`) by
  the camera frame via `RotateVectorWithFrame @0x5c8ab4` into `ctx+24..32`, then
  `*(ctx+36)=n·v0`. Added `MirrorPoly::normal[3]` + a NAMED rotation hook (default identity =
  the engine `dword_13FCD1C==0` standalone path). Golden
  `MirrorPrepareNode.PlaneFromStoredNormalAndFirstVertex`.
- **VERIFIED-1:1**: `CreateClippingPlanes @0x5F5D08` (alloc sizes, back-face collect, dedup
  0.001, per-edge normal + `d=−(n·a)`, frustum append); `BuildSilhouettePoints @0x5F5740`
  (conn stamp, −0.01 tol); `CreateOutline @0x5F58FC`; `SelectLodFrame @0x5ADB6C` (forced
  `((u8)(4·flags)>>6)−1` underflow-clamp; distance `sqrt·lodCount·fovScale`; far bias
  `flt_62807C=−1.0`; cull bit `+528|=0x40`); `UpdateBillboards @0x5AC970` (3 project arms;
  depth-fade clamp 255.0 = `0x406FE000`; quad winding cull; nodeType≥5 tint arm,
  nodeType==8 → `0x1F1FFF`); `ComputeVertexLighting @0x5C9054` env-map (gate `texRec+104&1`,
  reflect `dot·(−2.0)`, `uv=0.5·R+0.5`; `flt_628CBC=−2.0`, `flt_628CC0=0.5`,
  `flt_628CC4=2/255`, `flt_628CC8=−128.0`); reflective marker `LoadByName @0x5DA714`
  (`v85=((flag2>>6)&1)&&((u8)flag0<0xFF)`, bit5 write); `BuildMirroredGeometry @0x5F637C`
  append (in `mirror.cpp`, shared — verified, left untouched per ownership).

## 7. Cloth / Vegetation / Texture-set (.TXS)
Files: `cloth_anim.{h,cpp}`, `vegetation_anim.{h,cpp}`, `texture_set_table.{h,cpp}`.

- **FIXED — `BuildVegetationCache @0x5c8560` quantize rounding**: both arms store via bare
  `fistp` (unlit 0x5c8844; lit 0x5c8764/8775/8786) = round-nearest, source used `(int)`
  truncate. Fixed to `(int)std::lrint(...)`. Goldens corrected to binary truth (200, 100, 42,
  220; lit clamp 128, 64).
- **VERIFIED — NO WIND SWAY (wave-8 finding CONFIRMED)**: `BuildVegetationCache` has zero
  fsin/fcos / time term — it is a per-frame relight only. `TransformPackedVertices @0x5c9d04`
  unpack `((i16)(u8)b−128)·(1/127.5)` VERIFIED-1:1.
- **VERIFIED — NO CLOTH-WAVE (wave-8 finding CONFIRMED)**: `AttachFlag @0x4b5d98` /
  `RefreshFlagAnimation @0x4b5ef8` — flags are skeletal `.baf` (`sp_WIMPEL.baf`) waved by the
  pose driver; no vertex motion math. 180° yaw, heraldry bias −62, gate
  `byte_12CE912[536·h]∈{5,6,7}`.
- **VERIFIED-1:1 — .TXS**: loader @0x5d2240 (magic `0x23F209AE`, u32 setCount, u32
  namesPerSet, set-major NUL-terminated names PACKED on disk via ReadString, 64-byte stride
  only in memory); `SelectTextureSet @0x5b3f54` row math; foliage gate
  @0x506df4→@0x506388 (`pfl_`/`vg_`/`!vg_`, season index passed directly);
  `GetSeasonFromDay @0x58339c` (`day%4`, signed idiv).

---

## ConvertX (truncate) audit — summary
All cluster float→int sites classified. Truncate-via-ConvertX confirmed: sky band/blend
channels, water shade, particle alpha/cap/birthTick, billboard depth-fade, env-map. Bare
`fistp` (round-nearest) sites that had been wrongly truncated and are now FIXED:
`StampSlopeLight` lit value (terrain), `BuildVegetationCache` rgb quantize (both arms).

## Tests
All owned suites green, 0 failures: sky_render 372 (+1 golden), render_fog 265,
render_falloff_lut 32, sun_daycycle 204; water_vertices 405 (+goldens); render_terrain_walk
1629, render_tile_lighting 126, render_terrain_walk_e2e 4721 (+11 terrain suites);
shadow_object_render 37, shadow_project 32, shadow_ground 66, shadow_light_list 22, render_shadow
2196, shadow_node_update 54; particle_integrate 97 (+2 phase goldens), particle_emitter_create
107, particle_render_wave6 56, particle_spawn 90; mirror_scenegraph 113, mirror_render_wave6
107, node_lod 143, env_map_walk 37, reflective_nodes 1074, billboard 71, sprite_scale 305,
fxrecon_mirror_shadow 92; vegetation_anim 42, cloth_anim 84, render_texture_set_table 77.

## Remaining deferrals (rule 8, named — not faked)
- Terrain LOD-boundary stitch EMISSION (@0x5bf22c lines 848..1691) — 80-byte 2-tri record
  pointer-surgery vs this repo's split Polygons; no captured unequal-LOD scene to golden.
- `ProjectGroundQuad @0x5f216c` tile body — engine-private tile cache; null-tile → heightfield.
- Mirror normal-rotation hook, env-map/bone-lighting per-instance object-space normals,
  CreateClippingPlanes portable-view outcode — carried instance-data boundaries (wave-7).
