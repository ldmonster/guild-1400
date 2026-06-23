# Building lifecycle / upgrade cluster — wave-19 (W19-BUILDING)

**Date:** 2026-06-15 · **MCP:** live (`gilde.exe`, imagebase 0x400000)

Reconstructs the build/upgrade lifecycle + bauplatz geometry cluster from
coverage-audit-wave18's "Building lifecycle / upgrade" group. New code lives in
the EXISTING `src/sim/building_lifecycle.{h,cpp}` (namespace `guild::sim`) — the
file already held `RemoveAndCleanup`/`FindNearestSameType`; the wave-19 functions
were APPENDED (no ODR clash, no symbol redefinition). Tests:
`tests/unit/building_lifecycle_test.cpp` (33 tests, 118 checks, all green).

## Reusing the existing tree (no ODR)
- `util::ConvertX` (0x5c6b08, truncate-toward-zero) — REUSED for every float->int.
- `util::VectorWithinTolerance` (0x5caa4c) — REUSED for the bauplatz adjacency test.
- `BuildingTypeDef` / `BuildingRec` (`sim/building_types.h`), `ObjectRec`/`Person`
  (`sim/types.h`) — REUSED.
- `buildingtype_recon.{h,cpp}` / `buildingtype_callers.{h,cpp}` (the Bauplatz
  size/supermap + CreateGebaeude flow) — left untouched; this wave covers the
  ORTHOGONAL upgrade/requirement/selection logic, not the create/storage flow.

## Reconstructed 1:1 (deterministic game logic)

| addr | symbol | what was translated 1:1 |
|---|---|---|
| 0x5878b0 | VIBE_Building_MapTypeToCategory | the full switch on the type-record KIND byte (1,3,6,15->3 / 2->6 / 4,5,9->8 / 7->4 / 8,14,18,20-22->1 / 11-13->2 / 19->7 / 23-26->5 / else 0). `Building_MapTypeToCategory(kindByte)`. |
| 0x589850 | VIBE_Building_ClassifyTypeFlag | the nested comparison ladder verbatim (returns 2 for codes 0,7..39,46..51,>=58; 0 elsewhere). `Building_ClassifyTypeFlag(code)`. |
| 0x58a4c8 | VIBE_BuildingType_GroupFromCode | the 6-wide banded lookup with all JUMPOUT epilogues resolved via disasm (1-6->11, 7-12->9, 13-18->7, 19-24->6, 25-30->8, 31-33->13, 34-39->5, 40-45->12, 46-51->1, 52-63->ClassifyTypeFlag, 64-69->3, 70-75->4, else 0). |
| 0x58a25c | VIBE_BuildingType_MapActionToCategory | (helper, `static MapActionToCategory`) the action-kind switch; resolved epilogues; called for its SIDE EFFECT in CheckBuildRequirements (matching call order). |
| 0x587bfc | VIBE_Building_CheckBuildRequirements | the requirement state machine: v5=MapTypeToCategory(a1) (outer switch), record=589*a1+13CE294, objCat=GetCategoryForObject; the FreeBuild (dword_63C7B8) cheat short-circuit (target kind +2 == 6/7 -> 1); the per-category 0/1/2 logic over record.roomList[+547..+552] (the 6-wide scan) + the CollectByType sibling fallback; case-3 state-byte(+358) machine; case-6 level(+13) threshold keyed on the ORIGINAL code byte (ch=a1, verified by disasm); case-2->0, default->1. |
| 0x5942b0 | VIBE_Building_CloseUpgradeWindow | Form_Destroy(dword_13CE288): handle slot model (`SetUpgradeWindowHandle`/`UpgradeWindowHandle`/`Building_CloseUpgradeWindow`). |
| 0x40e2b4 | VIBE_Building_Update | per-frame State_Update -> Animation_Basic ordering (state result feeds the animation step), leaves via IBuildingFrameHooks. |
| 0x50f7c0 | VIBE_Building_OpenUpgradeWindow (charge math) | `Building_ComputeUpgradeChargeAmount(market,level)`: factor = level*0.25 + 0.5 (dbl_621620=0.25 @0x621620, dbl_621628=0.5 @0x621628, get_bytes-verified), base = trunc(market) [ConvertX], amount = trunc(base*factor) [ConvertX]. BOTH ConvertX sites verified TRUNCATE (frndint w/ RC=11). |
| 0x59361c | VIBE_Building_BuildUpgradeTree (node classifier) | `Building_ClassifyUpgradeNode`: the per-node display state v40 in {0,1,2,3} (owned->1, owned+active-handler->3, prereq-count satisfied->2, buildable+active-handler->3, locked->0), for both the office/handler-type branch (type record +0==6) and the normal-tech branch. |
| 0x50cb4c | VIBE_Building_CheckPlotConnectivity (geometry) | `Building_PlotHasAdjacentOwned`: the VectorWithinTolerance(100.0) adjacency probe over owned-building anchors (the "free" flag is the negation: free == NO adjacent owned). |
| 0x50ed14 | VIBE_Building_HandleSelectionClick (decision) | `Building_DecideSelectionClick`: the nested map-mode/pick/suppress/flag631748/production-71/flag631744 gate -> {none, open-dialog, gated-open, family-open, camera-zoom}. |

ConvertX (0x5c6b08) confirmed via disasm: it loads a control word with the
high byte forced to 0x1F (RC field = 11 = round-toward-zero) before `frndint` —
i.e. TRUNCATE toward zero. Every float->int site in OpenUpgradeWindow /
BuildGebaeudePath goes through it (the brief's "ConvertX truncates" note holds).

## Hooks (genuine engine leaves — inert defaults, rule 8: named, never faked)
- `IBuildLifecycleHooks`: TypeRecord (589*type+13CE294), CategoryForObject
  (0x589d24), PersonStateByte358 / PersonLevelByte13 (+358/+13), FreeBuildEverywhere
  (dword_63C7B8), PersonKindByte2 (+2), CollectByType (0x587b9c).
- `IBuildingFrameHooks`: StateUpdate (0x40e9e8), AnimationBasic (0x5d85b8).
- `IUpgradeTreeHooks`: BuildingOwns/ObjectOwns (GameObject_QueryFind 0x5857fc),
  IsHandlerType (65*type+13CE27C==6), HandlerActiveFor (He_FindFirstHandlerByFilter
  0x4c63f8 chain).
- Upgrade-window handle slot dword_13CE288 (Form_Destroy target).

## Genuine LEAVES — documented, NOT reconstructed (rule 8)
The cluster's remaining functions are dominated by render / scene-graph / window
/ pathfinding leaves that are NOT deterministic rules-core. Their deterministic
cores are reconstructed above; the leaf orchestration is left to its real owners:

- **VIBE_Building_BuildUpgradeTree (0x59361c) full body** — beyond the node
  classifier, it is window construction (Window_AddChildWindow 0x41a598,
  Object_AddToWindow 0x41ae10, Paintbox_DrawLine 0x41ec40, Text_RenderRichString
  0x59d6e8) + a progress bar from GameTime_DiffMinutes (0x5832bc). Render/UI leaves.
- **VIBE_Building_OpenUpgradeTreeWindow (0x594100)** — Form_* window assembly +
  Map_ComputeRoadNetworkLayout (0x592d98) + the inter-node Paintbox_DrawLine
  wiring. Pure GUI; the charge math + node states it drives are reconstructed.
- **VIBE_Building_OpenUpgradeWindow (0x50f7c0) full body** — the modal frame-loop
  (VIBE_GameLogic_RunFrameLoop 0x4c09a0) + Dialog_ShowMessageBox + the command
  enqueue path (EnqueueCmd15 0x494604 etc.). The deterministic money math
  (Building_ComputeUpgradeChargeAmount) is reconstructed; the command enqueue is
  the lockstep command layer (already reconstructed in command_apply*).
- **VIBE_Building_BuildGebaeudePath (0x576ac4)** — Heightmap_Create/Free
  (0x5c63a0/0x5c6438), SceneGraph_WalkAndInvoke (0x5ac738), Coord_WorldToTile
  (0x577690), Map_FindNearestDoorCell (0x577320), Floor_ComputeSlopeFlags
  (0x5bbdb0). It is a supermap PATH RASTERIZER (the door-cell Bresenham loops use
  ConvertX-truncated tile coords) over the heightmap/floor render grid — a
  scene/render subsystem (pathfind_map / heightmap modules own these leaves).
- **VIBE_Building_ApplyUpgradeStaff (0x5843c8)** — a scene-graph REBUILD over
  dword_13C3B50 (1988-stride staff rows) driven entirely by GameObject_QueryFind
  / GameObject_AddObjekt (0x5857fc/0x585af4). Scene-graph mutation, no rules math.
- **VIBE_Building_GetGebaeudeBauplatzPos (0x50cd94)** — Universe_SwitchActiveSlot
  + Transform_PointThroughBoneChain (0x5c8b38) + SceneGraph_WalkAndInvoke; a
  bone-chain world-position query (transform/scene leaf).
- **VIBE_Building_ReserveBauplatzForActiveChar (0x50ccbc)** — the thin loop
  (FilterBlockedBauplatze 0x50c8ec, already in building5.cpp -> per-plot
  CheckPlotConnectivity); the connectivity geometry it drives is reconstructed
  (Building_PlotHasAdjacentOwned). The scene-restore tail (ForEachBauplatzReserve
  0x50cac4 -> Universe_RestoreObjectStates 0x5b43f0) is a scene leaf.

## Wiring (rule 13) — handoff
- **Live entry:** the GUI hub `src/gui/building_options_menu.cpp`
  (`BuildingMenuAction::kOpenUpgradeWindow`, text 5055) dispatches to
  VIBE_Building_OpenUpgradeWindow (0x50f7c0). Its deterministic money core
  (`Building_ComputeUpgradeChargeAmount`), its node-state classifier
  (`Building_ClassifyUpgradeNode`), and its close path (`Building_CloseUpgradeWindow`)
  are this module's reconstructed callees — ready for the menu's upgrade handler
  to call once the window/command leaves are bound.
- **Per-frame entry:** `Building_Update` (0x40e2b4) is reached from the frame loop
  (0x4c09a0 via the object-update sweep); the StateUpdate/AnimationBasic leaves it
  orders are owned by the object/animation modules.
- **HANDOFF (bind-site files I do not own — documented, not edited):**
  - `src/world/wire_building.cpp` (`InstallRealBuildingWiring`) is the live
    install point. To make the upgrade charge/node/close logic LIVE, the building
    options-menu upgrade handler should call this module's entry points and bind
    `IBuildLifecycleHooks`/`IUpgradeTreeHooks` to the real scene-graph queries
    (GameObject_QueryFind 0x5857fc, He_FindFirstHandlerByFilter 0x4c63f8) — same
    pattern as the existing `SetBuilding*Hooks(nullptr)` defaults. The inert
    defaults already keep the deterministic control flow correct under test.

## Tests (rule 11)
`tests/unit/building_lifecycle_test.cpp` — 33 tests, 118 checks, all passing.
Golden vectors for: MapTypeToCategory (all bands + default); ClassifyTypeFlag
(every range boundary 0/1/3/6/7/39/40/42/45/46/51/52/53/57/58); GroupFromCode
(all 12 bands + the two ClassifyTypeFlag tail-call bands + default); the
CheckBuildRequirements state machine (invalid target, FreeBuild cheat 6/7,
cat-1 roomList hit + collect fallback, cat-2 always-0, cat-3 full state machine,
cat-4 scan, cat-6 level); CloseUpgradeWindow handle slot; Building_Update
ordering + state-result feed; the ConvertX-truncation charge math (truncate not
round); ClassifyUpgradeNode (normal owned/buildable/locked + handler-type owned/
active/buildable); PlotHasAdjacentOwned (hit/miss/empty/boundary); and the full
DecideSelectionClick gate tree.

Verified compiling cleanly with the project include set; ran standalone (118
checks, 0 failures). NOTE: the full `build/` target is currently RED on a
PRE-EXISTING, unrelated breakage in another wave's uncommitted file
`src/play/session_npc_daily.cpp` (references `CityView3D::boundPersons()` /
`BoundPerson` that don't yet exist) — confirmed present with this wave's changes
stashed, so it is not introduced here and is outside this wave's ownership.
