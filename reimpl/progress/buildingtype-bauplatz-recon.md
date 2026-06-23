# Building / Bauplatz cluster — buildingtype_recon

New files: `src/sim/buildingtype_recon.{h,cpp}`, `tests/unit/buildingtype_recon_test.cpp`.
Caller wave adds: `src/sim/buildingtype_callers.{h,cpp}`,
`tests/unit/buildingtype_callers_test.cpp`; extends `src/sim/trade_sell.{h,cpp}`
(the SellStoragePhase seam + SellResolve node context) and
`src/world/wire_building.cpp` (calls `WireBuildingCallers()`).
Namespace `guild::sim`; reuses existing `sim/types.h` (`ObjectRec`) and
`sim/building_types.h` (`BuildingTypeDef`). CMake globs pick the files up
automatically.

These four functions were on the explicit DEFERRED list in `src/sim/building.cpp`
(lines 241-252) as render/supermap/io/scene-graph coupled. They are now
reconstructed 1:1, with the genuinely-coupled leaves behind inert-default hooks.

## Done (original addr + symbol)

| addr | symbol | what was translated 1:1 |
|---|---|---|
| 0x577628 | VIBE_Bauplatz_GetSize | case-insensitive linear scan over the plot size table (`dword_1234600`/`dword_1234604`, stride 96); `BauplatzSizeRec` layout (name@+0, geometry dwords [16][17][18][20][22]) |
| 0x5774b8 | VIBE_Bauplatz_MapOneToSupermap | 4-corner quad assembly in the original's exact field-pick order (lo→hiX→hiX/hiZ→hiZ at shared Y), WorldToTile per corner, rasterize w/ fill 255 |
| 0x588988 | VIBE_Building_AllocStorageRoom | the security-level clamp for the storage node (kind 42, floor 2) and market-stand storage node (kind 278 / group 6, floor 6): the `lo|hi|!single` gate + `max(field,floor)` clamps + the single-branch asymmetry (storage = single unclamped, market = max(single,6), field29 untouched). Returns the AddObjekt handle / guard-null. |
| 0x504a54 | VIBE_Building_RegisterNames | the candidate-name pick: drop taken names (StrCmp match), drop names ≥0x20, uniform RandomModulo pick over survivors in order, reject-on-store if too long |
| 0x5d3f10 | VIBE_Util_StrCmp | byte-faithful signed strcmp (helper) |
| 0x5cb8f0 | VIBE_Util_StrCmpNoCase | byte-faithful A–Z-fold compare (helper) |

The size-3 thunks `0x589a32..0x589a44` (VIBE_BuildingType_ReturnCodeNN) are NOT
reconstructed: they are `mov al,N; ret` jumptable case targets for
VIBE_BuildingType_MapToProfessionCode, already folded into `building_type.cpp`'s
switch tables (size<12 SKIP rule).

## Hooks (genuine engine leaves — inert defaults, rule 8: named, not faked)
- `IBauplatzMapHooks`: VIBE_Coord_WorldToTile (0x577690, bone-chain transform) +
  VIBE_Map_RasterizeBauplatzEdge (0x5776d8, supermap raster) — rule-3 render leaves.
- `IStorageRoomHooks`: CanAllocate guard (dword_13CE27C/294, dword_6498C0+64<0x2000),
  TypeRecordFor (dword_13CE294 +589*type), VIBE_GameObject_AddObjekt (0x585af4),
  VIBE_GameObject_QueryFind (0x5857fc) — scene-graph leaves.
- `INameRegistry`: IsNameTaken (object name-table scan @dword_13CE298+5) +
  RandomModulo (0x58b89c).
- `SetBauplatzTable`: injection point for the disk-loaded plot table.

The room-slot enumeration in AllocStorageRoom (type record +35 list → per-slot
AddObjekt/recurse) and the market-stand (type 146..151) transport-spawn branch are
pure scene-graph side effects routed through AddObjekt/QueryFind; no rules-core
arithmetic lives there, so nothing is faked.

## Wiring (rule 13)
- Self-wired: the four functions call each other through the new file
  (MapOneToSupermap→GetSize; AllocStorageRoom→clamp helper).
- DONE (this wave): the four real callers are reconstructed 1:1 in
  `src/sim/buildingtype_callers.{h,cpp}` and invoke the recon entry points at
  the binary's exact call sites — see the caller table below.  Live install:
  `WireBuildingCallers()` (called from `world::InstallRealBuildingWiring()`,
  which `app/wiring.cpp` runs) plugs the CreateGebaeude full flow into the
  building_create hook (live callers: command_apply5.cpp opcodes 0x0A/0x4C) and
  the ExSellObjekt storage phases into trade_sell's commit path (live caller:
  command_apply6.cpp opcode 0x11 via VIBE_Command_ExecCommands 0x494088).

## Caller wave (src/sim/buildingtype_callers.{h,cpp})

| addr | symbol | reconstruction | recon entry invoked |
|---|---|---|---|
| 0x504ce0 | VIBE_Scene_SyncMeisterBuildings | `Scene_SyncMeisterBuildings()`: anchor-global zero, SwitchActiveSlot, flag-gated RecalcAllProduction + 4-slot ComputeSlotStats do/while, the two g_persons scans (kind 12 anchors w/ OVERWRITE semantics + early stop at v5==3; kind 11 shop list at slots [1..]), flag-gated RegisterNames, SyncMasterShopObjects tail | Building_RegisterNames @0x504da1 |
| 0x504a54 | VIBE_Building_RegisterNames | full flow: pass-1 default-template stamp (kind!=28), pass-2 default re-stamp + 12-candidate build + pick + store | Building_PickName core; taken-scan = `Building_IsNameTaken169` (the shared 0x504b65/0x5871b4 169-stride scan over the REAL g_objects); RandomModulo -> util::RandomModulo w/ the (u8) count cast |
| 0x586fb8 | VIBE_Building_CreateGebaeude | full flow: 13CE294 guard, FindFreeSlot scan, the complete fixed-field stamp (all 20 stores recovered from disasm incl. +43/+93/+97/+48 = ecx(0), grey-fill +153 |=1, +165=-1, +101..148 zero), primary/fallback name, candidate de-dup + RandNext%count (REAL Building_PickUniqueName), room-slot loop, storage-node word (+41), REAL Building_ApplyTypeDefaults, LABEL_71 AddObjekt(437) stamp, InitWorkerCapacities/MapKindToCategory/Building4_EnsureDefaultObjects tail | Building_AllocStorageRoom @0x58733d (hi-bit room slots of object-type 2/6) |
| 0x496b90 | VIBE_Command_ExSellObjekt (storage phases) | `Sell_DepleteSourceStockNode` (0x496ec2..: src node +14 decrement, depleted-node removal — type 2/6 -> RemoveStorageRoom else RemoveByProt, reject paths) + `Sell_EnsureDestStorageNode` (0x496f4b..: dest node ensure — type 2/6 -> AllocStorageRoom w/ reject-on-missing-building/alloc-fail, else AddObjekt + v57 count-bump + the kind-23/37 x 42/278 x owner-6/7 "+28=4" stamp; existing node += qty; LABEL_58 dword_631290 latch).  Installed into trade_sell's commit path via `SellStoragePhase1to1`; the deterministic gates stay trade_sell's `TradeSellObjektResolve` | Building_AllocStorageRoom @0x4974af; Building4_RemoveStorageRoom (0x588ce4, REAL) @0x49739b |
| 0x577464 | VIBE_Bauplatz_MapAllToSupermap | `Bauplatz_MapAllToSupermap(mapCtx)`: 256-pointer frame zero (the grey-0 fill thunk), REAL Building_FilterBlockedBauplatze (0x50c8ec, building5.cpp), per-surviving-plot MapOne with the NODE pointer as the plot name (scene nodes begin with their name); exact eax-residue return semantics | Bauplatz_MapOneToSupermap @0x5774b1 |

Module-state mirrors owned here: word_63C740 (SetSceneSyncFlagsWord — NOTE:
building6.cpp mirrors bit 7 separately as g_freeEntryEverywhere, unification
pending), dword_6498E8/dword_6498EC[0..768] (meister anchors + shop list),
byte_6498D7/byte_6498D8 (CreateGebaeude dead-branch latch, translated verbatim).

### Parents of the callers (rule-13 status)
- 0x586fb8: LIVE — command_apply5.cpp `Building_CreateGebaeude` call sites
  (VIBE_Command_HandleSpawnObject 0x496520, VIBE_Command_ExCreateGebaeude
  0x49c19c) run the full flow once WireBuildingCallers() installs the backend.
  PENDING: VIBE_Object_ParseAndAttachAvatar 0x4fffd4 — object_lifecycle8's
  `ObjLife8Hooks.buildingCreateGebaeude` field is set wholesale by its host;
  bind it to `Building_CreateGebaeudeFlow(kind, 0xFFFF)` there.
- 0x496b90: LIVE — command_apply6.cpp ExSellObjekt (opcode 0x11, dispatched by
  VIBE_Command_ExecCommands 0x494088 / command_receive.cpp) commits through
  TradeSellObjektResolve, which now runs the installed storage phases.
- 0x504ce0: PENDING parents — VIBE_GameLogic_InitOrLoadSession 0x533a54 is
  in-tree (app/session_init.cpp) but emits the call only as the
  `SetupStep::NewGameSyncScene` step through the per-ctx `SessionInitCtx::step`
  sink (no global consumer exists yet; the host sink must call
  `Scene_SyncMeisterBuildings()`).  VIBE_Net_StartNetworkGame 0x503f78 /
  VIBE_Net_LoadSavedNetworkGame 0x50442c (src/net lobby spines) have no
  scene-sync hook surface yet.
- 0x577464: PENDING parent — VIBE_Building_BuildGebaeudePath 0x576ac4 is still
  unreconstructed (building.cpp deferral list, pathfinding cluster).

### Named gaps (rule 8 — hooks with inert defaults, never faked)
- VIBE_Building_ComputeSlotStats 0x583c74 (per-city stats columns; QueryFind).
- VIBE_GameObject_AddObjekt 0x585af4 (scene-graph creation; both flows hook it).
- VIBE_GameObject_RemoveByProt 0x5859b4 (routed via Building4Hooks).
- VIBE_Building_FindStorableObject 0x5877ac / VIBE_Building_InitWorkerCapacities
  0x586ed8 — building4 owns the decision/clamp cores, but the found node's type
  word / the two resolved worker records need live scene memory.
- Name tables byte_620EFC / unk_62661C / dword_8C4788 / dword_8C4790 and the
  city table byte_13CD6A0 + clock qword_13CE852 are runtime/disk-loaded —
  exposed as hook providers (all-zero in the cold image).
- The ExSellObjekt raw-material branch (*(a1+35): AddObjektToParent /
  RemoveObjektAmount / the v58 counters @0x496ee2/0x496f84) and the 42/278 HUD
  banner + voice tail (0x496fe2..) remain trade_sell deferred leaves.
- ~~command_apply6.cpp still overwrites g_lastTradeId with its modeled `moved`
  AFTER the resolve~~ — **CLOSED in fixups wave 2** (progress/fixups-wave2.md):
  the unfaithful second store is removed; the only dword_631290 write is the
  faithful LABEL_58 latch (*(node+2)) in Sell_EnsureDestStorageNode here.
- ~~cutscene_misc5.cpp's `SceneClassifyMeisterRecords` latches the FIRST
  kind-12 match per anchor~~ — **CLOSED in fixups wave 2**: the kernel now
  implements the binary's overwrite-until-both-found semantics (tests added);
  the caller flow here keeps its verbatim record-pointer translation.
- The 0x533a54 NewGameSyncScene parent sink is **WIRED** (fixups wave 2):
  app::InitOrLoadSession's NewSingle branch dispatches the REAL
  Scene_SyncMeisterBuildings() at the NewGameSyncScene step
  (src/app/session_init.cpp; pinned in app_session_init_test).

## Tests (rule 11)
`tests/unit/buildingtype_recon_test.cpp` — 19 tests, 87 checks, all passing.
Golden vectors for: StrCmp/StrCmpNoCase semantics; GetSize hit/miss/empty/case;
MapOneToSupermap corner order + quad packing + unknown-plot path; the four clamp
branches; full AllocStorageRoom flow (guard, both nodes, single-branch field29
preservation); PickName taken/empty/length/modulo/no-survivor paths.

`tests/unit/buildingtype_callers_test.cpp` — 32 tests, 141 checks, all passing.
Golden vectors for: the 169-stride name-clash scan (dead-slot / case-sensitive);
RegisterNames default stamp + kind-28 skip + taken/too-long filters + the
RandomModulo replay vector; SyncMeisterBuildings anchor OVERWRITE + early-stop +
shop list + flag gating (0x40/4) + the city-slot do/while; CreateGebaeude
table-guard, the full fixed-field stamp golden, name fallback/primary/RandNext
pick, the room-slot loop (AllocStorageRoom protos + v52 AddObjekt suppression),
storage-node word, type defaults (71/32/38/production-5000), LABEL_71 437-node
stamp; both sell phases (all reject paths, count mutations, the +28=4 stamp,
the dword_631290 latch) + the phases running inside TradeSellObjektResolve;
MapAllToSupermap empty/per-plot fill-255/unknown-plot; the two wiring installs.
Regression-run green: buildingtype_recon_test (87), sim_inventory_test (66),
sim_inventory_e2e_test (24), wire_building_test (10), sim_command_apply5_test
(92) + e2e (132), sim_command_apply6_test (110) + e2e (115).
