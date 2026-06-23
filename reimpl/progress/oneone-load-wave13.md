# Wave-13 1:1 Fidelity Audit — segment: world/save LOAD

MCP DOWN, so this is the MCP-free part of the 1:1 comparison: cross-check the
reconstruction against in-tree evidence (the decompile-of-record provenance
comments + progress docs) and PIN the recovered 1:1 values with golden tests.

Segment files audited:
- `src/io/save_world_load.{h,cpp}` — LoadWorld/LoadWorldEx + the four .cty table loaders
- `src/io/gamestate.{h,cpp}` — top-level save/load orchestration (header+scalar+relink)
- `src/play/building_scene_attach.{h,cpp}` — city objects → 3D scene presence
- `src/render/scene_floor.{h,cpp}` — the floor-block grammar

Per the OWNERSHIP rule, the only source touched is one TEST addition
(`tests/unit/io_save_world_test.cpp`, +1 test). No bind-site / other-segment
files edited. No git commit.

---

## INVENTORY (function → gilde.exe address → confidence)

### `src/io/save_world_load.{h,cpp}`

| addr | symbol | reconstruction | confidence |
|---|---|---|---|
| 0x5a7604 | VIBE_Save_LoadGameFile | `io::LoadWorld` / `io::LoadWorldEx` — full recovered table order (header→scalar→PersonIndex→PersonTable→GlobalCounters→CityRecords→BuildingSlotTables→[Amt]→PostLoadInitScene→partial relink). Partial (header.flag&2) path for the shipped .cty seeds. | GOLDEN-PINNED (real AUGSBURG e2e + synthetic round-trips) |
| 0x5a7ffc | VIBE_Save_LoadPersonIndexTable | `io::LoadPersonIndexTable` — count + 8 fields/slot, 67-stride, dword_6498C0 | GOLDEN-PINNED |
| 0x5a86d0 | VIBE_Save_LoadGlobalCounters | `io::LoadGlobalCounters` — 16×164, version-gated field run | GOLDEN-PINNED |
| 0x5a8d3c | VIBE_Save_LoadCityRecords | `io::LoadCityRecords` — preamble + count×536, biases +1342/+1468, link-slot zeroing | GOLDEN-PINNED |
| 0x5aa058 | VIBE_Save_LoadBuildingSlotTables | `io::LoadBuildingSlotTables` — 5×7952 slot tables + 4×756 city-info | GOLDEN-PINNED (consume-exact + truncation) |
| 0x5a96c0 | VIBE_Save_LoadCharacterSlot | `io::LoadCharacterSlot` — 516-byte live actor, hidden-flag, +300 person id, &=0xDFFB | GOLDEN-PINNED |
| 0x5abb84 | VIBE_Save_RelinkLoadedPointers (person-record column slice 0x5abbe2..0x5abc4f) | `io::RelinkPersonRecordColumns` — +364/+368 resolve-or-0, +380 He clear, +388 zero; gated on player resolve | UNDER-VERIFIED (driven by LoadWorld; no isolated golden of the four-column transform — see gaps) |

Provenance: every function carries a `// gilde.exe 0xXXXX —` header. No RED-FLAG
(address-less) function in this file.

### `src/io/gamestate.{h,cpp}`

| addr | symbol | reconstruction | confidence |
|---|---|---|---|
| 0x5a348c | VIBE_Save_WriteGameFile (header+scalar+relink phase) | `io::WriteGameState` | GOLDEN-PINNED (`io_save_e2e_test`: write→load round-trip incl. relink-table) |
| 0x5a7604 | VIBE_Save_LoadGameFile (header+scalar+relink phase) | `io::LoadGameState` — version-gate 0x10026..0x10045 | GOLDEN-PINNED (`io_save_e2e_test`, and the .cty city loads in many e2e) |

Note: the gamestate slice is the header+scalar+relink ORCHESTRATION only; the
per-table phase is `save_world_load`'s. Both addresses are present and correct.

### `src/play/building_scene_attach.{h,cpp}`

| addr | symbol | reconstruction | confidence |
|---|---|---|---|
| 0x50d01c | Building_LoadAndAlignGebaeudeModel | `BuildingLoadAndAlignGebaeudeModel` | GOLDEN-PINNED (MainFn_* flow tests) |
| 0x50cec0 | Building_ComputePlacementHeight | `BuildingComputePlacementHeight` | GOLDEN-PINNED (ComputePlacementHeight_PlotYaw) |
| 0x50cfd0 | Building_AlignMeshToTerrain | `BuildingAlignMeshToTerrain` | GOLDEN-PINNED (AlignMeshToTerrain_RestoresWhenBelow) |
| 0x5e67c8 | WorldIo_ReadObject | `ReadCityObjectRecord` — +76 pos / +132 euler / +512 owner / +535 class / +533 type, child/sibling recursion, '!'-strip, 10-anchor block | GOLDEN-PINNED (ReadRecord_* + real AUGSBURG e2e) |
| 0x5e7e38 | Scene_LoadFromStream (object portion) | `ParseCityWorld` | GOLDEN-PINNED (ParseCityWorld_HeaderPlusObjects: tag==0x3A6C00BB) |
| 0x4ffe0c | Object_BuildModelName | `ObjectBuildModelName` — gb_ name, +97/+512 links | GOLDEN-PINNED (BuildModelName_Gb) |
| 0x5a8140 | Object_RebuildModelByOwner | `ObjectRebuildModelByOwner` — 169-stride×256 scan, per-iter +512 re-read | GOLDEN-PINNED (RebuildModelByOwner_BindsMatchingRecord) |
| 0x50d01c/0x49c19c | .ogr variant probe | `ResolveGebaeudeOgrVariants` | GOLDEN-PINNED (VariantProbe_*) |

### `src/render/scene_floor.{h,cpp}`

| addr | symbol | reconstruction | confidence |
|---|---|---|---|
| 0x5e78a8 | WorldIo_LoadFloorRegions | `ParseFloorRegions` / `ParseSceneFloorBlock` — full block grammar, version-gated | GOLDEN-PINNED (FullBlockGoldenParse + FloorVersionGates) |
| 0x5dcca0 | Bio_ReadArrayQuick | `ReadArrayQuick` — [elemSize][count][payload], count==expected accept gate | GOLDEN-PINNED (ParsesHeightsRecordBehindObjectTree, accepted flag) |
| 0x5bd44c | Floor_LoadFromHeightmap (pure-math mirrors) | `DeriveFloorPlacement` / `NormalizeFloorTextureGrid` | GOLDEN-PINNED (FloorPlacementDerivation, TextureGridMinNormalization) |
| 0x5c5610 | Heightmap_BuildTerrainMesh (scale derivation) | `BuildCityHeightmapFromFloor` | GOLDEN-PINNED (BuildsHeightmapWithEngineScales) |

No address-less function in any of the four files.

---

## 1:1 VALUE PINNING — what the brief asked, where it's pinned

| recovered value | pinned by |
|---|---|
| .cty scene-stream tag **0x3A6C00BB** | `building_scene_attach_test` ParseCityWorld_HeaderPlusObjects (`header.tag==kVerBB`, portable) + E2E_Augsburg (`tag==0x3A6C00BBu` over the real embedded blob); `render_scene_floor_test` header builder. |
| node layout offsets **+76 pos / +132 euler / +512 ownerId** (+535 class, +533 type) | `building_scene_attach_test` ReadRecord_MeshBody_PosEulerOwner (`n.pos`, `n.euler`, `n.ownerId==4711`, `n.classOvr==3`, `n.spawnType==4`). |
| floor-block grammar (ArrayQuick `[elemSize][count]` framing, `<scene>_height`/texture/water records, type-name table, origin vec3) | `render_scene_floor_test` FullBlockGoldenParse + FloorVersionGates + ParsesHeightsRecordBehindObjectTree. |
| RebuildModelByOwner **@0x5a8140** id matching (`*(node+512)` vs record id +1) | `building_scene_attach_test` RebuildModelByOwner_BindsMatchingRecord (`node.d(512)==9001` matches rec id, then overwritten with slot link). |
| table loaders' record **strides** (67 / 164 / 536 / 7952 / 756 / 516 / 169) + capacities + counter biases (+1342/+1468) + version range (0x10026..0x10045) | **NEW** `io_save_world_test` recovered_strides_and_constants_pinned (explicit CHECK_EQ on every constant). Previously used only implicitly. |

### Test addition (the one source edit this wave)
`tests/unit/io_save_world_test.cpp` — `+TEST recovered_strides_and_constants_pinned`
(20 explicit CHECK_EQ assertions). Pins the strides/capacities/biases/version
range that the round-trip tests only exercised implicitly. Values sourced from
the provenance comments in `save_world_load.h` / `save_person.h` / `save.h` —
nothing invented. Suite now 88 checks (was 68), 0 failures.

---

## INTERNAL CONSISTENCY CHECK — drift scan

- **save_world_load.h/.cpp**: constants in the header MATCH their use in the .cpp
  (67/164/536/7952/756/169) and the round-trip tests. The version gates
  (0x1002C/0x10014/0x10015/0x10018/0x1003B/0x1003E/0x10031/0x10024/0x1002A/
  0x10021/0x10036) in LoadCityRecords/LoadGlobalCounters match their comments.
  **No drift.**
- **gamestate**: load range comment (0x10026..0x10045) matches
  `kSaveVersionLoadMin/Max` in save.h and the runtime check in gamestate.cpp.
  **No drift.**
- **building_scene_attach**: the .cpp's anon-namespace offset constants
  (kOffPos=76, kOffEuler=132, kOffOwner=512, kOffType533=533, kOffClass535=535,
  kOffMatrix=396) match the provenance doc and are behaviorally pinned. Version
  constants kVerOwnerId=0x3A6C00B2 / kVerClassByte=0x3A6C00AB in the header match
  the doc. **No drift.**
- **scene_floor**: the wave-5 re-decompile in `scene_floor.h` SUPERSEDES the
  wave-3 doc (`terrain-colorkey-wave3.md`) on two constants, and the SOURCE is
  the correct one — this is documented supersession, not unflagged drift:
  - placement `flt_628AE0 @0x628AE0 = 0xC2800000 = **-64.0**` (wave-3 doc said
    -50.0 / 0xC2480000 — a misread of the neighbouring dword; scene_floor.h/.cpp
    + the FloorPlacementDerivation test all use -64.0).
  - `flt_628BA8 @0x628BA8 = 0x3B81848E ≈ **1/253.0**` (wave-3 doc said 1/252.85;
    `heightmap.h kTerrainScaleYNorm` + scene_floor.cpp use the exact dword
    0x3B81848E, reciprocal 252.99977). No code change needed — the code already
    carries the corrected values, cross-checked against `heightmap.h`.
  The wave-3 doc itself flags that scene_floor.h supersedes it (its header notes
  the wave-5 re-decompile), so the docs are internally consistent.

No code drift was found that warranted a source fix.

---

## CONFIDENCE MAP

GOLDEN-PINNED (constants + control flow tested → high 1:1 confidence):
- All four table loaders (PersonIndex/GlobalCounters/CityRecords/BuildingSlot),
  LoadCharacterSlot, LoadWorld/LoadWorldEx, the gamestate orchestration.
- All building_scene_attach functions (incl. the embedded-scene tag, node
  offsets, RebuildModelByOwner id-match) — portable goldens + real-AUGSBURG e2e.
- All scene_floor grammar + placement-math functions.

UNDER-VERIFIED (reached on the flow; isolated 1:1 transform not independently
golden-pinned):
- `RelinkPersonRecordColumns @0x5abb84` (the four-column +364/+368/+380/+388
  transform): exercised end-to-end inside `LoadWorld` over real AUGSBURG, but no
  unit test isolates the column transform with constructed link ids (-1 / hit /
  miss). The repo models building IDS (pointer-as-id) where the original resolves
  to pointers; the gates are observably identical, but a focused golden would
  raise confidence. (Not added this wave to stay strictly within a low-risk test
  footprint; flagged here for pickup.)

NEEDS-LIVE-MCP (1:1 fidelity cannot be confirmed from in-tree evidence; exact
binary-diff targets for when MCP returns):
- The full-save (.SAV, non-partial) TAIL of `VIBE_Save_LoadGameFile @0x5a7604`
  (Gesetz/MapTiles/GameGlobals/Avatar/Object/Amt/History/ActionQueues/Hotkey +
  Characters + Mission) is out of this slice; the shipped cities are partial, so
  it is never on the city flow. Decompile @0x5a7604 tail when verifying .SAV.
- `RelinkPersonRecordColumns` full He-relink (`He_FindFirstHandlerByFilter`
  @0x5abe4f) — the partial path always misses; the full-save behavior needs the
  live He registry. Decompile @0x5abe4f / @0x5abb84.
- The floor block's UNPARSED remainder beyond the heights/texture/water grids the
  scene_floor module reads (the consumer side `Floor_LoadFromHeightmap @0x5bd44c`
  texture/water side effects) — owned by the floorwater (W5-WATER) segment, not
  this one; listed for completeness.
- `Scene_LoadObjectGroup @0x5e84f4` live VFS open and the lc7 hooks
  (ParseNameAndBind @0x4ffb40) — the framing is reused but the live wildcard
  resolve is a hook with a real Groups.BIN default; the bind itself is e2e-proven
  but the un-decompiled hook bodies are named gaps (building-scene-attach.md).

---

## TEST RESULTS (this wave)

Portable (no assets):
- `io_save_world_test` — **88 checks, 0 failures** (was 68; +1 test, +20 checks).
- `render_scene_floor_test` — 138 checks, 0 failures (unchanged).
- `building_scene_attach_test` — 99 checks, 0 failures (skip path, unchanged).
- `io_save_e2e_test` (gamestate coverage) — 19 checks, 0 failures.

With real assets (`GUILD_GAME_DIR` / repo `europe_guild_1400_original`):
- `io_save_world_real_e2e_test` — 16 checks, 0 failures. Real AUGSBURG.cty:
  sceneTileCount=551, objectCount=55, cityRecCount=1, version 0x1003B, partial.
- `building_scene_attach_test` E2E_Augsburg — 170 checks, 0 failures:
  parsed=1375, meshNodes=796, attached=53, resolvedModels=49, distinctPos=53,
  visible=372; embedded scene tag 0x3A6C00BB confirmed over the real .cty blob.

Build kept green; no other-segment / bind-site files touched.
