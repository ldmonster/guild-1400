# Wave-19 — Character AI targeting/behaviour + terrain collision (W19-CHARACTER)

**Agent:** W19-CHARACTER · **Date:** 2026-06-15 · **MCP:** live (`gilde.exe`, imagebase 0x400000)

Reconstructs the un-reconstructed live-tree character AI behaviour leaves and the
fixed-point terrain height-scan + mesh-vs-terrain collision resolver, all reachable
from the frame loop `0x4c09a0` (rule 7). New modules:

- `src/sim/character_ai.{h,cpp}` + `tests/unit/character_ai_test.cpp`
- `src/sim/terrain_collision.{h,cpp}` + `tests/unit/terrain_collision_test.cpp`

## Reconstructed (1:1 from the Hex-Rays + disasm)

| addr | name | module | notes |
|------|------|--------|-------|
| 0x4526d8 | VIBE_Character_FindNearestTarget | character_ai | full body — target select + engage delta packet |
| 0x452d38 | VIBE_Character_UpdateGuardBehavior | character_ai | control flow + AI-dispatch tail; roster spawn driven via hook |
| 0x40244c | VIBE_Character_UpdateLowPolyMesh | character_ai | LOD swap (full vs low-poly proxy) by mode + camera distance |
| 0x426488 | VIBE_Character_LoadObjectAnimation | character_ai | .baf attach + .oam fallback, flag bitset |
| 0x401894 | VIBE_Character_SetVisible | character_ai | cull-bit toggle (+140 &/| 0x20) |
| 0x40238c | VIBE_Character_SetAllFreezeState | character_ai | 512-slot walk, sets dword_62D088 = 2-state |
| 0x405504 | VIBE_Character_StandUp | character_ai | cancel sitting action queue |
| 0x426e2c | VIBE_Terrain_ScanLineHeightRange | terrain_collision | triangle height-range raster (fixed-point) |
| 0x427370 | VIBE_Terrain_ScanSegmentHeightRange | terrain_collision | two-triangle quad wrapper |
| 0x427b60 | VIBE_Collision_ResolveMeshAgainstTerrain | terrain_collision | mesh snap onto terrain surface |

## Reused (no ODR fork)

- **0x426da0 VIBE_Terrain_ScanRowHeightRange** — already reconstructed in
  `src/render/terrain_scan.{h,cpp}` (`guild::render::ScanRowHeightRange`). The new
  `ScanLineHeightRange` calls it directly through a `TerrainHeightGrid` adapter, so
  the row-fold primitive has exactly one definition. Test `TerrainScan.RowPrimitiveReused`
  pins this.
- **0x5c6b08 VIBE_Coord_ConvertX** — reused from `src/util/coord.cpp`
  (`guild::util::ConvertX` == `std::trunc`). Every world→grid coordinate goes through
  it (`Trunc(x) = (int)ConvertX(x)`), preserving the original's **truncate-toward-zero**
  (NOT round-to-nearest) semantics. Test `TerrainScan.CoordTruncatesTowardZero` pins it
  (x=3.9 → column 3, never 4).
- The favourability curve + walk-step clamp helpers (`NearestTargetRankWeight`,
  `NearestTargetWalkSteps`) + constants (`kRankDeltaScale`=0.5, `kRankCurveScale`=0.1,
  `kRankCurveBase`=10, `kFavorabilityCeil`=34) already live in `sim/character_path.h`;
  `character_ai` includes and reuses them (the original's `FindNearestTarget` is where
  those `flt_619138..619144` constants are consumed).

## Fixed-point / rounding verification (rule 1)

Disassembled `0x426e2c` to recover the exact register/stack mapping the decompiler
garbled (the 6 `fistp` slots + the 3 vertex-sort swaps):

- The 3 triangle vertices are args **a3, a5, a6** (each `float*` {x@+0, z@+8}).
- Projection: `col = trunc((x - originX[+0x90]) / scaleX[+0xA0])`,
  `row = trunc((z - originZ[+0x98]) / scaleZ[+0xB8])` — `fistp` after `ConvertX`,
  i.e. `(int)trunc(...)`.
- Vertices sorted by **row ascending** (3 swaps at 0x426ed5 / 0x426ee5 / 0x426f0b).
- Edge-walk x positions are also truncated via `ConvertX` before being passed as the
  row-scan column bounds (every `fistp [var_54/58]` site at 0x427327…0x427239).
- World-Y reconstruction: `out = (double)byte * heightScale[+0xC4] + heightBase[+0x94]`.
- Constants pinned via `get_bytes`: `flt_6115B8 = 0x3dcccccd = 0.1` (probe lerp);
  fallback range `±1.0e35`; `flt_6100FC = 1500.0` (LOD swap distance).

`FindNearestTarget`'s affinity read is `movsx ax, byte_1333110[id+768p]` then
`fild word` — a **signed-byte** affinity, modeled as `i8 affinity(p,id)` and used as
`(i16)(i8)`. The combined gate `affinityPlaneHi/2 + rankPlaneHi >= -(rand%32 + 50)`
and the walk-step distance `(int)ConvertX(bestScore)` are reproduced exactly.

## Hook boundaries (rule 8 — real boundaries only)

Both modules use an installable hook struct for the cross-module leaves that are NOT
part of this slice (genuinely owned elsewhere), with inert defaults so the headless
build needs nothing:

- **character_ai**: object/anim leaves (`VIBE_Object_ToggleSuspendStateNamed`,
  `VIBE_Anim_AttachToBone`, `VIBE_Mesh_LoadObjectAnimation`, `VIBE_Light_BuildObjectCache`,
  …) — **owned by W19-OBJANIM**, referenced by extern-style hook, never redefined; the
  command-delta codec (`VIBE_Command_*`), the He/AI evaluators
  (`VIBE_AiNeeds_EvaluateActions`, `VIBE_AiMethod_SelectBestForPerson/ExecuteSelected`),
  `VIBE_Person_ComputeOfficeRank`, `VIBE_Ai_ComputePersonFavorability`,
  `VIBE_Math_RandomModulo`; the 768-entry person table (word_12CE910 stride 536, the
  byte_1333110 / unk_133310D affinity/rank planes, dword_123D6CD) via a `PersonTable`
  view.
- **terrain_collision**: scene-graph/object-transform leaves
  (`VIBE_Object_SetPosition/SetWorldTranslation/ReparentWithTransform`,
  `VIBE_Mesh_ComputeHeightRange/DrawBoundingBox/TestAabbOverlapRecursive`) + the active
  scratch node `dword_13FCD1C` and terrain root `dword_64A028`.

The deterministic geometry/decision cores run with the inert defaults and are golden-
pinned directly (no hook needed for the math).

## Deferred / partial within the slice (documented, not faked)

- `UpdateLowPolyMesh`'s walk-anim **sub-mesh attach** branch (0x4025fd..0x40292e:
  `lowpolycharacter/%s/%s_%s_LOW.baf` build + `Anim_FindFreeMeshSlot` +
  `Anim_LoadStreamToStock` + `Anim_AttachToBone`) is skipped under inert wiring — it
  depends entirely on the W19-OBJANIM anim leaves and the `loc_5CB930` predicate.
  Control flow up to that branch is faithful; with real anim hooks the host drives it.
- `UpdateGuardBehavior`'s **building-roster spawn loop** (0x452d5f..0x452e75, 256×169-byte
  records keyed off dword_13CE298/294) is gated through the `queueRequestGuardTarget61`
  hook; with an empty roster (inert) it falls through to the AI-dispatch tail, which is
  fully reconstructed. The `byte_B572A1` method-table re-check is a host-table read; the
  common `method-changed` dispatch branch is covered.
- `LoadObjectAnimation` models the name copy as a plain string copy (the original's
  word-wise loop is byte-identical for the attach path) and `StripPathAndExt` is host-
  owned (its output only feeds the unused stock id on this path).

## Wiring (rule 13)

The two small visibility leaves have real bind sites in **`src/play/gamelogic_recon.h`**
(a bind-site file NOT owned by this agent):

- `characterSetVisible(int rec, int vis)` virtual stub @ line 171 → `0x401894` → wire to
  `guild::sim::SetVisible(rec, vis)`.
- `characterStandUp()` virtual stub @ line 183 → `0x405504` → wire to
  `guild::sim::StandUp(actor)`.

**Handoff:** the owner of `src/play/gamelogic_recon.h` should replace those two stub
bodies with calls into `sim/character_ai.h` (install the `CharacterAiHooks` that route
`readNodeI32`/object leaves to the real Object/Universe records first). The frame-loop
caller of `UpdateGuardBehavior` / `UpdateLowPolyMesh` / `ResolveMeshAgainstTerrain`
(`0x4c09a0` subtree, bind-site files universe_render / scene_view — also not owned here)
installs the full hook set the same way. `character_ai.h` / `terrain_collision.h` expose
everything needed; no further code in this module's files is required.

## Tests

- `tests/unit/terrain_collision_test.cpp` — **7 tests, 18 checks, all pass**: flat
  triangle, varying-height range, off-grid clip, ConvertX truncation, quad merge,
  both-clipped quad, row-primitive reuse.
- `tests/unit/character_ai_test.cpp` — **8 tests, 27 checks, all pass**: rank-weight
  curve, walk-step clamp, no-engage-flags, enemy-select+engage-packet, affinity-gate
  reject, favourability-ceiling reject, SetVisible cull-bit toggle + invalid-actor error,
  StandUp null/no-state/clear-queue.

Built standalone with the project include set (`-Isrc -Iinclude`); both `.cpp` compile
clean under `-Wall`.

## Build note

`src/sim/character_ai.cpp` + `src/sim/terrain_collision.cpp` compile cleanly. The full
`guild` library target currently fails on an **untracked, pre-existing** file from
another wave — `src/sim/../script/script_vm.cpp:314` (`error: jump to label 'term'`) —
which is outside this agent's ownership and unrelated to these modules. My files add no
new build errors and no ODR clashes.
