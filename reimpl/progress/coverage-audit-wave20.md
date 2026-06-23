# Wave-20 — Rule-7 Coverage Audit Refresh (post waves 18/19/20)

**Agent:** W20-COVERAGE · **Date:** 2026-06-16 · **MCP:** live (`gilde.exe`, imagebase 0x400000, hexrays ready)

Analysis-only refresh of [`coverage-audit-wave18.md`](coverage-audit-wave18.md). Same
question, same method: *of the functions reachable from the binary's real boot spine,
which still have no reconstructed body in `src/`?* — re-measured after waves 18, 19, 20.

> **Snapshot note:** wave-20 reconstruction files were still landing on disk while this
> audit ran (parallel agents). The headline moved 87.7% → 88.4% → 89.0% across a few
> minutes as `.cpp` bodies appeared. The numbers below are the **final stable snapshot**
> taken after the last file write settled. Re-running `classify.py` will only improve them.

---

## Method (identical to wave-18, re-run)

1. **Roots (boot spine), unchanged:** `0x52895c`, `0x527de0`, `0x527fa4`, `0x528560`,
   `0x4c09a0`.
2. **Reachable set:** BFS over real call edges (`get_first/next_fcref_from` to function
   starts) from the five roots, via IDA `py_eval`. Result: **2152 functions**
   (1,020,658 bytes) — up from wave-18's 2140 (+12 functions newly pulled into the spine
   as IDA's call-edge resolution improved / a few indirect targets resolved).
3. **Reconstruction check:** for each reachable address, an address counts **reconstructed**
   only if it is cited as `0xADDR` inside a `.cpp` body (header-only mentions, enum stubs,
   and virtual-stub hook tables do **not** count). Same conservative classifier as wave-18.
4. **Boundary vs logic** by name + `func_profile`/string-ref verdict (DDraw/D3D/DInput →
   Vulkan/SDL; Miles/MSS32 → SDL audio; Crt/File/Math/Gzip/Inflate/Deflate → host runtime).

---

## Headline coverage number

> **≈89.0% of entry-reachable functions are reconstructed (1916 / 2152).**
> Up from **83.6% (1789/2140)** at wave-18 — a **+5.4 pt** gain.
> Of the **236** remaining with no reconstructed body, only **~62 are genuine game logic**
> (down from 154); the rest are intentional tech boundaries or host CRT/zlib runtime.

| bucket | wave-18 | wave-20 | delta |
|--------|------:|------:|------:|
| reachable from boot spine | 2140 | 2152 | +12 |
| reconstructed (body in src) | 1789 | **1916** | **+127** |
| missing (no body) | 351 | **236** | **−115** |
| — **pure game logic** | **154** | **62** | **−92** |
| — DDraw/D3D/DInput render boundary | ~38 | 24 | (some now cited) |
| — Miles/MSS32 audio boundary | ~39 | 30 | |
| — MSVC-CRT / File / Math / zlib runtime | ~104 | 116 | (more reachable, host-satisfied) |
| — thunks | — | 4 | DirectDraw/exit/close thunks |

**Of the 154 wave-18 pure-logic targets, 125 are now closed; 29 remain.** (The 62-logic
total = 29 surviving wave-18 targets + 33 logic functions newly reachable in the 2152-set
that wave-18's smaller spine didn't enumerate.)

---

## What waves 18/19/20 CLOSED (125 of the 154)

Big clusters delivered, with the modules they landed in:

- **Meister / needs AI brain** — `ai_needs.cpp` (`AiNeeds_BuildScoreTable` 7961 b),
  `ai_meister_calc_{diebe,farming,wache,angriff}.cpp`, `ai_meister_equip.cpp`,
  `ai_meister_passes.cpp`. The single largest wave-18 gap, now reconstructed.
- **Privilege / guild-office action set** — `world/privilege_panels_a.cpp` +
  `world/privilege.cpp`: GenerateHatred, ChangeProfession, ExpelWorker, Blackmail(+Confirm),
  MakePeace, Convert, Interrogation, Medicus, Divorce, Apology, CharmConfirm, Miracle,
  ShowDialog, ShowPrivileges (panel host).
- **Building lifecycle / upgrade** — `sim/building.cpp` (+`building_lifecycle/upgrade/
  create/stock/4/5.cpp`): BuildUpgradeTree, OpenUpgradeWindow(+Tree, +Close),
  BuildGebaeudePath, CheckBuildRequirements, ApplyUpgradeStaff, CheckPlotConnectivity,
  HandleSelectionClick, GetGebaeudeBauplatzPos, SelectRoomToEnter, ReserveBauplatz, Update.
- **Groundplan / blueprint window** — `gui/groundplan.cpp`: BuildInfoPanel, LoadBlueprintBmp,
  RenderBlueprint, SetWidgetsVisible, DestroyWidgets, FadeInScene (+CreateWindow in session_init).
- **Object / Character / Anim core** — `sim/character_ai.cpp` (FindNearestTarget,
  UpdateLowPolyMesh, UpdateGuardBehavior, LoadObjectAnimation, SetAllFreezeState, SetVisible,
  StandUp), `render/anim_object.cpp` (CreateObjectAnim, AttachToBone, LoadStreamToStock,
  PruneExpiredAttachments, FreeObjAnimData, FindFreeMeshSlot),
  `sim/object_lifecycle5/7.cpp` (Reparent, ChangeTransparency, TransformPointToParent,
  ToggleSuspend, AssignMeshData, Reinitialize).
- **Script VM front-end** — `script/script_vm.cpp`: EnterFunction, ParseDeclaration,
  ParseSymbolName, DeclareLocal, SkipBraceBlock, DestroyContext, RegisterScriptCommands.
- **Terrain collision** — `sim/terrain_collision.cpp`: ScanLineHeightRange, ScanSegmentHeightRange.
- **Recruit / Office** — `gui/recruit_office.cpp`: Gesetz person-selection window(+OpenIfValid),
  CandidatePick/RecruitmentOffer host shells, CollectNearbyRecruitCandidates,
  ShowCandidateListWithRoles, RenderSessionTimer.
- **StatPanel graph** — `gui/statpanel.cpp`: DrawGraphSeriesA/B/C, DrawGraphLine.
- **Wave-20 leaves** — estate_transfer, vegetation, economy_quality, sky_dome,
  heightmap_create, savestate_decompress (Vfs_WriteBuffered), he_recon/event_recon2
  (He_* messaging), floorgfx_recon (Floor_ReloadTextures, Gfx_CrossFadeStep),
  scene_recon2_orchestrator (Object_UpdateBuildingVisualState), combat (AttachObjectMesh).

---

## STILL MISSING from the wave-18 154 (29) — sorted by size

These survived waves 18–20. **Reconstruct these first** in the next wave.

| addr | bytes | name | note |
|------|------:|------|------|
| 0x41078c | 5919 | VIBE_Entity_InteractionLogic | top of the entity/per-frame core, hdr-only stub |
| 0x418f34 | 2621 | VIBE_EntityChild_Process | child-entity processing |
| 0x40eea0 | 2399 | VIBE_Object_Update | per-frame object update (sibling of Object_Reinit, done) |
| 0x415b78 | 2115 | VIBE_Animation_Apply | per-frame skeletal/anim apply |
| 0x41ceb4 | 1714 | VIBE_DecompressGameState | save-load decompress front (paired w/ Decompressor_Init) |
| 0x55de00 | 1544 | VIBE_Recruit_RunRecruitmentOfferWindow | offer-window modal (shell exists, body not) |
| 0x4ff070 | 1312 | VIBE_Hotkey_OpenAssignWindow | hotkey-assign dialog (hotkey_assign.cpp has helpers, not this) |
| 0x565f9c | 1051 | VIBE_Privilege_PanelEvidenceReview | privilege evidence cluster (panels_b not landed) |
| 0x561bb4 | 1051 | VIBE_Privilege_PanelEnactLaw | |
| 0x565b88 | 1042 | VIBE_Privilege_PanelEvidenceDetails | |
| 0x5667a0 | 1017 | VIBE_Privilege_PanelEvidenceReviewAlt | |
| 0x5628c8 | 980 | VIBE_Privilege_PanelCounterEspionage | |
| 0x561fd0 | 866 | VIBE_Privilege_RemoveFromOffice | |
| 0x4bfc48 | 838 | VIBE_ChatConsole_BuildWindow | chatconsole.cpp exists but cites 0x53629c, not this |
| 0x562cdc | 804 | VIBE_Privilege_PanelSwapSeats | |
| 0x55db0c | 754 | VIBE_Recruit_RunCandidatePickWindow | |
| 0x562334 | 640 | VIBE_Privilege_PanelEmbezzlement | |
| 0x50bf08 | 565 | VIBE_TradePanel_RefreshSellColumns | trade_panel_windows.cpp present, this addr uncited |
| 0x55d990 | 380 | VIBE_Recruit_RunHireConfirmDialog | |
| 0x5651bc | 326 | VIBE_Privilege_PanelMiracle | (a sibling Miracle is done; verify which) |
| 0x4ac0c8 | 231 | VIBE_Cutscene_CheckMaster | |
| 0x4ad508 | 138 | VIBE_DragSlot_BeginDragText | dragselect.cpp present, this addr uncited |
| 0x56c0cc | 123 | VIBE_Config_ApplyCameraAndScrollSettings | config_apply not landed |
| 0x40e50c | 111 | VIBE_Decompressor_Init | pairs with DecompressGameState |
| 0x5d8f1c | 29 | VIBE_ShapeAnim_GetSlot | tiny anim slot accessor |
| 0x5d9104 | 21 | VIBE_Resource_FlushAndFree | tiny |
| 0x5f821c | 8 | VIBE_Util_NullThunk | thunk |
| 0x5f8230 | 5 | VIBE_Util_ExitHandlerThunk | thunk |
| 0x5eb960 | 5 | VIBE_Vfs_CloseHandleThunk | thunk (CloseHandle boundary) |

**Cluster verdict:** the remaining real debt is concentrated in (a) the **Privilege panels-b
/ evidence** sub-cluster (8 fns, ~7.4 kB — a `privilege_panels_b.cpp` that did not fully
land), (b) the **Recruit modal windows** (3 fns), and (c) the **entity/object/anim per-frame
core** (Entity_InteractionLogic, EntityChild_Process, Object_Update, Animation_Apply,
~13 kB — the largest single block left). DecompressGameState+Decompressor_Init gate save-load.

---

## NEW logic targets (reachable now, were outside the wave-18 154-set)

The 2152-spine pulled in 33 more logic functions wave-18's enumeration missed. **Also
reconstruct these** (already de-duplicated against what waves 18–20 closed):

| addr | bytes | name |
|------|------:|------|
| 0x5cf150 | 3026 | VIBE_Anim_CreateMorphAnim |
| 0x592d98 | 2180 | VIBE_Map_ComputeRoadNetworkLayout |
| 0x4294d4 | 1891 | VIBE_Rain_UpdateDrop (particle physics — logic, not GPU) |
| 0x42a644 | 1329 | VIBE_Snow_UpdateFlake (particle physics — logic) |
| 0x459264 | 1150 | VIBE_Ai_CalcBankmeister (Meister-AI sibling, missed in w18) |
| 0x56589c | 747 | VIBE_Privilege_BuildEvidenceEntry |
| 0x413220 | 719 | VIBE_Widget_LayoutBounds |
| 0x47abfc | 585 | VIBE_ObjectSearch_FindByPaletteRange |
| 0x57b480 | 570 | VIBE_Amt_ComputeOfficeWages |
| 0x47b008 | 454 | VIBE_ObjectSearch_FindPeopleByPalette |
| 0x47ae48 | 447 | VIBE_ObjectSearch_FindOneByPaletteRange |
| 0x50b1c4 | 394 | VIBE_TradePanel_RefreshItemColumns |
| 0x4159dc | 386 | VIBE_Property_Set |
| 0x592c7c | 283 | VIBE_Map_ComputeBuildingChainDepth |
| 0x40eaf0 | 277 | VIBE_Gui_ResolveObjectState |
| 0x40e9e8 | 262 | VIBE_State_Update |
| 0x40e728 | 237 | VIBE_State_GetCurrent |
| 0x494910 | 178 | VIBE_Command_QueueRequestBuffer28 |
| 0x4152cc | 159 | VIBE_Property_Get |
| 0x4c2ba0 | 158 | VIBE_Gesetz_ComputeMaxWantedLevel |
| 0x5437d8 | 150 | VIBE_MapView_AddCornerObjects |
| 0x412ea4 | 113 | VIBE_Gui_MarkObjectUsed |
| 0x5929f0 | 105 | VIBE_Person_QueryByGoodType |
| 0x494d68 | 40 | VIBE_Command_QueueRequestQuad46 |
| 0x495124 | 40 | VIBE_Command_QueueRequestQuad60 |
| 0x495070 | 39 | VIBE_Command_QueueRequestQuad54 |
| 0x494b74 | 35 | VIBE_Command_QueueRequestPair35 |
| 0x494ca4 | 35 | VIBE_Command_QueueRequestPair42 |
| 0x4ac9a0 | 31 | VIBE_Cutscene_SetRandSeed |
| 0x5d8b00 | 16 | VIBE_Coord_Transform |

(Tiny CRT-ish ones — `Format_VsnprintfCore` 0x6073e0, `Format_Vsprintf` 0x60740c — are host
printf and need only provenance, not reconstruction.)

---

## TOP-15 still-missing LOGIC (next-wave priority, all buckets combined, by size)

1. 0x41078c · 5919 · VIBE_Entity_InteractionLogic
2. 0x5cf150 · 3026 · VIBE_Anim_CreateMorphAnim
3. 0x418f34 · 2621 · VIBE_EntityChild_Process
4. 0x40eea0 · 2399 · VIBE_Object_Update
5. 0x592d98 · 2180 · VIBE_Map_ComputeRoadNetworkLayout
6. 0x415b78 · 2115 · VIBE_Animation_Apply
7. 0x4294d4 · 1891 · VIBE_Rain_UpdateDrop
8. 0x41ceb4 · 1714 · VIBE_DecompressGameState
9. 0x55de00 · 1544 · VIBE_Recruit_RunRecruitmentOfferWindow
10. 0x42a644 · 1329 · VIBE_Snow_UpdateFlake
11. 0x4ff070 · 1312 · VIBE_Hotkey_OpenAssignWindow
12. 0x459264 · 1150 · VIBE_Ai_CalcBankmeister
13. 0x561bb4 · 1051 · VIBE_Privilege_PanelEnactLaw
14. 0x565f9c · 1051 · VIBE_Privilege_PanelEvidenceReview
15. 0x565b88 · 1042 · VIBE_Privilege_PanelEvidenceDetails

---

## Reconstructed-but-NOT-WIRED (flag for integration, rule 13)

These reconstructed functions (their `VIBE_*` public symbol) have **no external caller**
in any other `src/` file — body exists, but nothing in the live tree reaches it yet.
Each needs a one-line bind at its real call site.

| addr | bytes | name | file | handoff |
|------|------:|------|------|---------|
| 0x4c9dec | 2154 | VIBE_NpcAction_NotifyJoinLeaveGroup | src/sim/npcaction.cpp | wire into the NPC group join/leave path (caller is an action-dispatch site) |
| 0x594100 | 430 | VIBE_Building_OpenUpgradeTreeWindow | src/sim/building.cpp | wire into the building upgrade-tree UI open click |
| 0x5bd2d8 | 371 | VIBE_Floor_ReloadTextures | src/render/floorgfx_recon.cpp | wire into the floor/texture-reload (settings-change) path |
| 0x41e814 | 266 | VIBE_Gfx_CrossFadeStep | src/render/floorgfx_recon.cpp | wire into the per-frame cross-fade tick |
| 0x609e30 | 113 | VIBE_String_MbsCompareN | src/crt/string.cpp | CRT helper — likely referenced by name elsewhere; verify or leave (host CRT) |
| 0x609510 | 58 | VIBE_String_WideCharToBytes | src/crt/string.cpp | CRT helper — same caveat |

> Note: dozens of file-local `static`/`Def*`/helper functions in the wave-19/20 modules
> show "no external caller" by design (they are internal to their own `.cpp`) and are NOT
> listed here — only top-level reconstructed entry points that should be reachable are flagged.

---

## Remaining boundary / runtime buckets (DO NOT reconstruct as logic)

- **Render boundary (24):** DDraw/D3D fixed-function + DInput devices (Render_Init*,
  Draw*Triangles/Quad, EnumDevices, Create{Device,Surface,DynamicTexture}, SetGamma,
  Input_Acquire{Mouse,Keyboard}Device, Input_BuildScancodeTable, …) — covered by
  `shim::IGraphicsDevice`/`IPlatform`. Verify the swap covers observable behavior.
- **Audio boundary (30):** Miles/MSS32 Sound/Audio/Voice/Sound3d + `Music_*` track
  playback — covered by `shim::IAudioDevice` (SDL). The Sound3d listener-math leaves are
  partly logic and could be reconstructed atop the SDL mixer if positional audio is wanted.
- **CRT / zlib runtime (116):** `Crt_*`/`File_*`/`Math_*`/`Float_*`/`Memory_*`/`Time_*`/
  `Format_*` + `Gzip_*`/`Inflate_*`/`Deflate_*`. Host C++ runtime + the zlib port in
  `src/compress/`. **Action: add `@0xADDR` provenance to existing zlib/CRT bodies** rather
  than re-reconstructing (avoid ODR). This bucket grew vs wave-18 only because more CRT
  leaves are now reachable, not because of regressions.
- **Thunks (4):** DirectDraw{Create,EnumerateExA} import thunks + Util null/exit thunks.

---

## Reproduce

- `/tmp/guild_w20/reach_rows.txt` — full 2152-row reachable set (addr·size·name) from the
  IDA BFS (`py_eval`, edges via `get_first/next_fcref_from`).
- `/tmp/guild_w20/classify.py` — headline classifier (cpp-cite = reconstructed).
- `/tmp/guild_w20/delta.py` — wave-18-154 closed/still-missing delta.
- `/tmp/guild_w20/bucket.py` — logic vs boundary bucketing.

*All addresses are gilde.exe (imagebase 0x400000). Snapshot taken 2026-06-16 after the
last wave-20 file write settled; headline = 89.0% (1916/2152).*
