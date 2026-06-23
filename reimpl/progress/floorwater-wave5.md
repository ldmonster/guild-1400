# Wave 5 — W5-WATER: floor water vertex animation (VIBE_Floor_AnimateWaterVertices)

Scope: reconstruct/verify the per-frame water-surface vertex animator
`VIBE_Floor_AnimateWaterVertices @0x5be428` against the LIVE IDA decompile
(MCP back online), fix the one 1:1 discrepancy found, and confirm the per-frame
wiring. The full driver + its two halves were already in-tree from a prior wave;
this wave is the ground-truth verification pass that the prior wave (working from
a lost /tmp dump) could not do.

---

## Address + call-tree (verified live)

* `VIBE_Floor_AnimateWaterVertices @0x5be428` — `int *__usercall(result@eax =
  mesh array base, count@edx, firstMesh@ebx = float*)`, size 0x23e, 175 insns.
* SOLE caller `VIBE_Floor_RenderTerrain @0x5bf22c` at call site **0x5c2a6a**:
  ```
  5c2a4f  mov ebx, [floor+0x19E0]    ; a3 = water-mesh array
  5c2a5f  mov dl,  [floor+0x1C6D]    ; count byte
  5c2a65  mov eax, dword_62EB38      ; time = global tick clock
  5c2a6a  call VIBE_Floor_AnimateWaterVertices
  ```
  So the per-frame call is `Animate(time=dword_62EB38, count=*(u8*)(floor+0x1C6D),
  meshes=*(float**)(floor+0x19E0))`.
* `dword_62EB38` — the global animation/tick clock (also read by Character_Update,
  Sound3d_UpdateListener, Rain_Create, ResourceEvict… — the engine's ms tick).
* Callees: `VIBE_Texture_FindGroupMember @0x5daec0`, `VIBE_Math_Fmod @0x5d3fb2`
  (= x87 FPREM == C `fmod`).
* Wave constants (get_bytes, all confirmed): dbl_628AF4 = **6.283185…(2π)** (the
  `t` amplitude AND the phase-prop modulus), dbl_628B14 = π, dbl_628AEC = 2.7,
  dbl_628AFC = 2.4, dbl_628B04 = 2.5, dbl_628B0C = 2.6, dbl_628B1C = 2.2,
  dbl_628B24 = 4.0.

## Mesh record (0x158 = 344 bytes = 86 floats, the `a3` walk stride)

| float | byte | field |
|---|---|---|
| 0 | +0x00 | texture record ptr (gates texture anim) |
| 1 | +0x04 | active texture member (FindGroupMember out) |
| 3 | +0x0C | texAnimRate (×dt → accumA) |
| 6..9 | +0x18..+0x24 | wave phase-propagation speeds |
| 10..13 | +0x28..+0x34 | amplitudes amp[0..3] |
| 14..77 | +0x38..+0x134 | 16-vec4 wave output grid (a3[14+4c]) |
| 78..81 | +0x138..+0x144 | phase accumulators (in-place advanced; read by grid) |
| 82 | +0x148 | texAccumA = Fmod(rate·dt + texAccumA, 1.0) |
| 83 | +0x14C | texAccumB = Fmod(1.0, texAccumA) |
| 84 | +0x150 | lastTime latch (int) |

Texture-record byte fields (inside the record a3[0] points at): +0x70 memberCount,
+0x72 low nibble = speed selector; groupId = (rec − dword_1406A84) >> 7. Speed
divisor table dword_5D93C8[1..10] = {15,13,11,9,8,7,5,3,2,1}.

## Function body (verified 1:1)

For each of `count` meshes (stride 86 floats):
1. **Texture advance** (if rec && rec+0x70 && (rec+0x72 & 0xF)):
   `a3[1] = FindGroupMember((rec−texBase)>>7, time/divisor[sel] % memberCount)`.
2. **Time gate**: `dt = time − lastTime; if (dt <= 0) -> next mesh` (texture
   advance still ran; wave half skipped).
3. **Texture accumulators**: `texAccumA = Fmod(rate·(float)dt + texAccumA, 1);
   texAccumB = Fmod(1, texAccumA)`.
4. **Phase propagation** (loc_5BE50B, edx: a3 → a3+0x10, 4 iters):
   `phase[k] = Fmod(speed[k]·dt + phase[k], 2π)` for k=0..3 — **IN PLACE**.
5. **Wave grid** (loc_5BE53C, c=0..15, out = a3[14+4c]):
   * x = amp[0]·sin(cos(c·2.7)·t + c + phase[0])
   * y = amp[1]·cos(sin(π − c·2.4)·t + c + phase[1])
   * z = amp[2]·sin(c − cos(c·2.5)·t + 2.2 + phase[2])
   * w = amp[3]·cos(c − sin(c·2.6 + π)·t + 4.0 + phase[3])
   with t = 2π. (16·(c>>2)+4·(c&3) == 4·c, so a flat 16-vec4 grid.)
6. **Latch** `lastTime = time`.

## 1:1 FIX made this wave (the verification payoff)

The prior reconstruction (built from a lost /tmp dump) modelled step 4 as an
**overlap shift**: it claimed the post-increment store `[edx+0x134]` landed one
float below the `[edx+0x138]` read, giving `phase[0]<-prop[1] … phase[3]
untouched` plus a spurious +0x134 scratch field. The LIVE disasm shows the store
target `(edx after +4) + 0x134 == edx_start + 0x138` — the **same byte it read**,
i.e. a clean in-place update of all four accumulators (no shift, no +0x134
field). Fixed:
* `src/render/water_vertices.cpp` — driver now assigns `m.phase[k]=prop[k]`,
  k=0..3 (was the shift). `phaseOut[]` kept only as a test-observable mirror.
* `src/render/water_vertices.h` — propagation doc corrected; `phaseOut` field
  re-documented (binary has no separate +0x134 scratch).
* Golden tests recalibrated to the in-place semantics (see below).

The wave-grid half (`render/floorwater.cpp AnimateWaterWaveGrid`) and the
texture-schedule half (`render/water_anim.h`) were re-checked against the live
decompile + constants and are **bit-correct as written** — no change needed.

`render/floorwater.cpp` `FloodFillMask @0x5ba750` / `FillHeightGradient @0x5ba898`
/ `FindRegionOffset @0x5ba824` are unrelated and untouched.

## Per-frame API + install handoff

The clean per-frame entry already exists and is the real driver:
```cpp
void render::AnimateWaterVertices(WaterMesh* meshes, u32 count, i32 time,
                                  FindGroupMemberFn findGroupMember, void* ctx);
```
It is wired live in `src/play/wire_atmos_bridge.{h,cpp}` (`AnimateAtmosWater`,
one mesh, `++waterTime` each frame, `NoGroupMember` no-op bank).

**HANDOFF → terrain-walk owner (`src/play/terrain_render.cpp` / the @0x5bf22c
walk — not my file):** to drive the REAL terrain water regions per frame, call
at the @0x5c2a4f arm equivalent inside `GroundFrame::Render` (gated on the
BeginUniverseFrame a2 `frameFlags & 2`, the water-anim gate at 0x5bf2b8):
```cpp
render::AnimateWaterVertices(
    /*meshes*/ (render::WaterMesh*)floor.waterMeshes,   // *(float**)(floor+0x19E0)
    /*count */ floor.waterMeshCount,                    // *(u8*)(floor+0x1C6D)
    /*time  */ globalTick,                              // dword_62EB38
    findGroupMember, ctx);
```
The terrain-render Floor record must EXPOSE `waterMeshes` (floor+0x19E0, the
86-float WaterMesh array) and `waterMeshCount` (floor+0x1C6D). Those source
records (the 344-byte water bodies) are produced by the still-deferred
`VIBE_FloorWater_PrepareRegions @0x5ba95c` parse; until that loader is
reconstructed the terrain walk has no real array to pass and the bridge mesh is
the only live consumer.

## Named gaps (rule 8)

* **Texture-group bank** — `VIBE_Texture_FindGroupMember @0x5daec0` body +
  `dword_1406A84` (texBase) are data-coupled global texture-cache state, injected
  as the `findGroupMember` callback (no-op in the bridge). The SCHEDULE math
  (groupId, divisor table, frame index) IS reconstructed; only the bank walk is
  deferred. (Unchanged from the prior wave.)
* **Water-mesh source array** — `VIBE_FloorWater_PrepareRegions @0x5ba95c` (the
  1248-insn allocation-heavy region/mesh builder that fills floor+0x19E0 /
  floor+0x1C6D from the 344-byte "Wasser_Teich_Fluss_blau" records) stays LISTED
  deferred. The driver + record layout are ready for it.

## Tests

* `tests/unit/water_vertices_test.cpp` — `PropagatePhasesGolden`,
  `WaveGridGolden`, `DriverDtPositive` (recalibrated to in-place phase
  [1.6,3.2,4.8,0.1168147] + waveOut[0] using phase[0]=1.6), `DriverDtZeroSkips
  Wave`, `DriverNoTextureAdvance`, FindRegionOffset golden. **green**.
* `tests/integration/water_vertices_itest.cpp` — driver==sibling-on-propagated-
  phase (recalibrated to in-place, all 4 phases), texture bank, multi-mesh
  independent advance. **green**.
* `tests/e2e/water_vertices_e2e_test.cpp` — 30-frame pond replay (bounded grid,
  all 4 phases in [0,2π) — recalibrated), deterministic replay, guarded real
  assets. **green**.
* Live wiring: `wire_atmos_bridge_test` / `_itest` / `_e2e` — **green** (exercise
  the per-frame `AnimateWaterVertices`).

Suite result (water + atmos): 6/6 ctest pass. Broader render/terrain regression:
4 pre-existing failures (`render_clip_test`, `terrain_ground_test`,
`render_terrain_walk_e2e_test`, `terrain_ground_e2e_test`) are transient WIP in
concurrently-edited raster/meshlist/terrain_walk/terrain_render files (NOT water;
no `water`/`phase`/`floorwater` reference) — out of W5-WATER scope.
