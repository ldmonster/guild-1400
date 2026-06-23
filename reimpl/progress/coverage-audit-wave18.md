# Wave-18 — Rule-7 Coverage Audit (entry-point reachability vs reconstruction)

**Agent:** W18-COVERAGE · **Date:** 2026-06-15 · **MCP:** live (`gilde.exe`, imagebase 0x400000, hexrays ready)

This is an **analysis-only** report (no source edits). It answers one question:
*of the functions reachable from the binary's real boot spine, which are NOT yet
reconstructed in `src/`?* — so the next wave knows exactly what remains.

---

## Method

1. **Roots (the boot spine).** Entry is `VIBE_GameLogic_MainEntryAndShutdown @0x534bbc`
   (WinMain). Its body is mostly the single-instance mutex + locale/cmdline glue; the
   real spine is driven through these five functions (confirmed via `app/wiring.cpp` +
   `app/gamelogic.h` + `lookup_funcs`):

   | root addr | name | size |
   |-----------|------|------|
   | 0x52895c | VIBE_Window_CreateMainWindow | 0x273 |
   | 0x527de0 | VIBE_App_InitSubsystemsAndMovieDll | 0x176 |
   | 0x527fa4 | VIBE_Render_InitDisplayAndPaths | 0x5ba |
   | 0x528560 | VIBE_App_InitEngineAndScriptCommands | 0x3fc |
   | 0x4c09a0 | VIBE_GameLogic_RunFrameLoop | 0xe64 |

2. **Reachable set.** BFS over real call edges (`fl_CN`/`fl_CF` xrefs to function
   starts) from the five roots, via IDA `py_eval`. Result: **2140 functions**
   (≈1,019,810 bytes of code) reachable from the boot spine. (WinMain's own tiny
   subtree adds nothing new.)

3. **Reconstruction check.** For each reachable address, searched all of `src/**` for
   a reconstructed *body* — not just a mention. Provenance in the tree appears in three
   formats (`gilde.exe 0xADDR`, `@0xADDR`, `0xADDR Name`), so a plain provenance-regex
   undercounts; and many addresses are *mentioned* in comments/enum tables/virtual-stub
   hooks without being implemented. Final classifier: an address counts as
   **reconstructed** only if it appears in a `.cpp` within ~12 lines of a function
   definition; otherwise it is **missing** (header-only reference, deferred note, or
   nowhere).

4. **Boundary vs logic verdict** via `func_profile`/`decompile` of the largest hits
   (string refs `d3d_Device->lpVtbl`, `dd_vesa.c`, `d3_engine.c` = D3D/DDraw boundary;
   Miles/MSS32 = audio boundary; `Crt_*`/`File_*`/`Mbcs_*` = MSVC runtime; `Gzip_*`/
   `Inflate_*`/`Deflate_*` = zlib port).

---

## Headline coverage number

> **≈83.6% of entry-reachable functions are reconstructed** (1789 / 2140).
> Of the **351** with no reconstructed body, only **~154 are genuine game logic** still
> to do — the remaining ~197 are intentional tech boundaries (DDraw/D3D/DInput → Vulkan/
> SDL, Miles → SDL audio) or MSVC-CRT / zlib runtime (much of which is already ported
> behaviorally under zlib/libc names without per-address provenance, so the true logic
> debt is even smaller).

| bucket | count | note |
|--------|------:|------|
| reachable from boot spine | 2140 | BFS over real call edges |
| reconstructed (body in src) | 1789 | 83.6% |
| missing (no body) | 351 | 16.4% |
| — of which **pure game logic** | **154** | the real reconstruction targets |
| — DDraw/D3D/DInput render boundary | ~38 | rule 3/4 — replaced by Vulkan/SDL, **not** to reconstruct |
| — Miles/MSS32 audio boundary | ~39 | rule 5 — replaced by SDL audio, **not** to reconstruct |
| — MSVC-CRT / File / Mbcs / Math runtime | ~80 | host CRT; many already behaviorally ported |
| — zlib (Gzip/Inflate/Deflate stream layer) | ~24 | **mostly already reconstructed** in `src/compress/` under zlib names; addresses just not all cited |

**Coverage caveats (why the real number is even higher than 83.6%):**
- The zlib stream wrapper (`gzopen`/`gzread`/`inflateInit2`/`deflateReset` …) lives in
  `src/compress/gzip.cpp|inflate.cpp|deflate.cpp` as a faithful zlib port but does not
  cite every `0xADDR`, so 24 Gzip/Inflate/Deflate entries are flagged missing yet are
  effectively done. Verify, don't blindly reconstruct.
- A handful of uppercase-hex provenance (e.g. `0x49818C ExComputeObjectCoords`) were
  normalized to lowercase and confirmed present — so the audit is slightly conservative
  (under-reports a few as missing).

**Sanity spot-check (claimed-reconstructed, confirmed real bodies, 16/16):**
`0x4c09a0`(frameloop), `0x528560/0x527fa4/0x52895c/0x527de0`(app spine, `app/app_init.cpp`),
`0x5b5f48`(universe cams), `0x5b7134`(picksel), `0x6080e8`(huft_build), `0x529d08`(main menu),
`0x49818c`(opcode-0x1B apply, `command_apply6.cpp`), `0x4b2c34`(edge scroll),
`0x56d48c`(thumbnail), `0x446b2c`(sample bank), `0x52f154`(sfx preload), `0x534bbc`(cmdline).

---

## Top-20 MISSING game-logic functions (the next-wave priority list)

Sorted by size. All reached via the frame loop (`0x4c09a0`) or engine init (`0x528560`)
unless noted. **Verdict = real game logic** for every row here.

| addr | bytes | name | reaching root |
|------|------:|------|---------------|
| 0x4764e8 | 7961 | VIBE_AiNeeds_BuildScoreTable | 0x528560 |
| 0x41078c | 5919 | VIBE_Entity_InteractionLogic | 0x4c09a0 |
| 0x457440 | 5262 | VIBE_Ai_CalcMeisterDiebe | 0x4c09a0 |
| 0x4af4a8 | 4783 | VIBE_Groundplan_BuildInfoPanel | 0x4c09a0 |
| 0x454f50 | 3461 | VIBE_Ai_CalcMeisterFarming | 0x4c09a0 |
| 0x455cd8 | 3279 | VIBE_Ai_CalcMeisterWache | 0x4c09a0 |
| 0x59361c | 2775 | VIBE_Building_BuildUpgradeTree | 0x4c09a0 |
| 0x4569a8 | 2709 | VIBE_Ai_CalcAngriff | 0x4c09a0 |
| 0x418f34 | 2621 | VIBE_EntityChild_Process | 0x528560 |
| 0x4588d0 | 2452 | VIBE_Ai_CalcMeisterAmbush | 0x4c09a0 |
| 0x40eea0 | 2399 | VIBE_Object_Update | 0x4c09a0 |
| 0x4c9dec | 2154 | VIBE_NpcAction_NotifyJoinLeaveGroup | 0x4c09a0 |
| 0x576ac4 | 2137 | VIBE_Building_BuildGebaeudePath | 0x4c09a0 |
| 0x427b60 | 2124 | VIBE_Collision_ResolveMeshAgainstTerrain | 0x4c09a0 |
| 0x415b78 | 2115 | VIBE_Animation_Apply | 0x4c09a0 |
| 0x55a224 | 1942 | VIBE_Gesetz_RunPersonSelectionWindow | 0x4c09a0 |
| 0x50f7c0 | 1788 | VIBE_Building_OpenUpgradeWindow | 0x4c09a0 |
| 0x41ceb4 | 1714 | VIBE_DecompressGameState | 0x528560 |
| 0x55fe60 | 1694 | VIBE_Panel_ShowPrivileges | 0x4c09a0 |
| 0x41536c | 1646 | VIBE_Window_LayoutScrollContent | 0x528560 |

### Biggest un-reconstructed logic clusters

- **Meister/needs AI scoring** (≈25k bytes): `AiNeeds_BuildScoreTable` (7961, INI-driven
  needs scorer w/ `Bildung`/`DummyUnversehrtheit` weight strings) + `Ai_CalcMeister*`
  (Diebe/Farming/Wache/Ambush) + `Ai_CalcAngriff` + `MeisterAi_EquipStaffWeapon`. This
  is the master/NPC decision brain — the single largest logic gap.
- **Privilege / office-power UI + effects** (22 functions, ≈21k bytes): `Privilege_Panel*`
  (GenerateHatred, ChangeProfession, ExpelWorker, Blackmail, MakePeace, EnactLaw,
  Interrogation, Convert, Divorce, Medicus, Evidence*, Embezzlement, Miracle …) +
  `RemoveFromOffice`/`ShowDialog`. The whole guild-office privilege action set.
- **Building lifecycle / upgrade** (14 functions): `Building_BuildUpgradeTree`,
  `OpenUpgradeWindow`, `BuildGebaeudePath`, `CheckBuildRequirements`, `ApplyUpgradeStaff`,
  `CheckPlotConnectivity`, `Update`, `HandleSelectionClick`, `GetGebaeudeBauplatzPos` …
- **Groundplan / blueprint window** (7): `BuildInfoPanel`, `LoadBlueprintBmp`,
  `RenderBlueprint`, `CreateWindow`, `FadeInScene`, `SetWidgetsVisible`, `DestroyWidgets`.
- **Object/Character/Anim per-frame core**: `Object_Update`, `Entity_InteractionLogic`,
  `EntityChild_Process`, `Animation_Apply`, `Character_FindNearestTarget`,
  `Character_UpdateLowPolyMesh`, `Anim_CreateMorphAnim`/`CreateObjectAnim`/`AttachToBone`.
- **Script VM front-end** (6): `Script_EnterFunction`, `ParseDeclaration`, `ParseSymbolName`,
  `DeclareLocal`, `SkipBraceBlock`, `DestroyContext` — the .esc parser/interpreter.
- **Recruit / Office / StatPanel / DragSelect / Hud** dialog + interaction leaves.

The full **154-row** pure-logic missing list and the **boundary** lists follow.

---

## Full MISSING — pure game logic (154) — RECONSTRUCT THESE

| addr | bytes | name | root |
|------|------:|------|------|
| 0x4764e8 | 7961 | VIBE_AiNeeds_BuildScoreTable | 0x528560 |
| 0x41078c | 5919 | VIBE_Entity_InteractionLogic | 0x4c09a0 |
| 0x457440 | 5262 | VIBE_Ai_CalcMeisterDiebe | 0x4c09a0 |
| 0x4af4a8 | 4783 | VIBE_Groundplan_BuildInfoPanel | 0x4c09a0 |
| 0x454f50 | 3461 | VIBE_Ai_CalcMeisterFarming | 0x4c09a0 |
| 0x455cd8 | 3279 | VIBE_Ai_CalcMeisterWache | 0x4c09a0 |
| 0x59361c | 2775 | VIBE_Building_BuildUpgradeTree | 0x4c09a0 |
| 0x4569a8 | 2709 | VIBE_Ai_CalcAngriff | 0x4c09a0 |
| 0x418f34 | 2621 | VIBE_EntityChild_Process | 0x528560 |
| 0x4588d0 | 2452 | VIBE_Ai_CalcMeisterAmbush | 0x4c09a0 |
| 0x40eea0 | 2399 | VIBE_Object_Update | 0x4c09a0 |
| 0x4c9dec | 2154 | VIBE_NpcAction_NotifyJoinLeaveGroup | 0x4c09a0 |
| 0x576ac4 | 2137 | VIBE_Building_BuildGebaeudePath | 0x4c09a0 |
| 0x427b60 | 2124 | VIBE_Collision_ResolveMeshAgainstTerrain | 0x4c09a0 |
| 0x415b78 | 2115 | VIBE_Animation_Apply | 0x4c09a0 |
| 0x55a224 | 1942 | VIBE_Gesetz_RunPersonSelectionWindow | 0x4c09a0 |
| 0x50f7c0 | 1788 | VIBE_Building_OpenUpgradeWindow | 0x4c09a0 |
| 0x41ceb4 | 1714 | VIBE_DecompressGameState | 0x528560 |
| 0x55fe60 | 1694 | VIBE_Panel_ShowPrivileges | 0x4c09a0 |
| 0x41536c | 1646 | VIBE_Window_LayoutScrollContent | 0x528560 |
| 0x4526d8 | 1631 | VIBE_Character_FindNearestTarget | 0x4c09a0 |
| 0x563000 | 1553 | VIBE_Privilege_PanelGenerateHatred | 0x4c09a0 |
| 0x55de00 | 1544 | VIBE_Recruit_RunRecruitmentOfferWindow | 0x4c09a0 |
| 0x4aea5c | 1498 | VIBE_Groundplan_LoadBlueprintBmp | 0x4c09a0 |
| 0x44b2e0 | 1469 | VIBE_Text_ParseLabelDefinition | 0x528560 |
| 0x565304 | 1430 | VIBE_Privilege_PanelChangeProfession | 0x4c09a0 |
| 0x40244c | 1395 | VIBE_Character_UpdateLowPolyMesh | 0x4c09a0 |
| 0x426e2c | 1346 | VIBE_Terrain_ScanLineHeightRange | 0x4c09a0 |
| 0x4ff070 | 1312 | VIBE_Hotkey_OpenAssignWindow | 0x4c09a0 |
| 0x5639f4 | 1311 | VIBE_Privilege_PanelExpelWorker | 0x4c09a0 |
| 0x560f14 | 1258 | VIBE_Privilege_PanelBlackmail | 0x4c09a0 |
| 0x563f14 | 1235 | VIBE_Privilege_PanelMakePeace | 0x4c09a0 |
| 0x58c4a8 | 1153 | VIBE_Person_TransferEstateOwnership | 0x4c09a0 |
| 0x4af038 | 1066 | VIBE_Groundplan_RenderBlueprint | 0x4c09a0 |
| 0x43dfb0 | 1061 | VIBE_Character_RegisterScriptCommands | 0x528560 |
| 0x565f9c | 1051 | VIBE_Privilege_PanelEvidenceReview | 0x4c09a0 |
| 0x561bb4 | 1051 | VIBE_Privilege_PanelEnactLaw | 0x4c09a0 |
| 0x565b88 | 1042 | VIBE_Privilege_PanelEvidenceDetails | 0x4c09a0 |
| 0x5667a0 | 1017 | VIBE_Privilege_PanelEvidenceReviewAlt | 0x4c09a0 |
| 0x4431dc | 1010 | VIBE_Script_EnterFunction | 0x4c09a0 |
| 0x5643e8 | 992 | VIBE_Privilege_PanelConvert | 0x4c09a0 |
| 0x563614 | 989 | VIBE_Privilege_PanelInterrogation | 0x4c09a0 |
| 0x5628c8 | 980 | VIBE_Privilege_PanelCounterEspionage | 0x4c09a0 |
| 0x45e350 | 970 | VIBE_MeisterAi_EquipStaffWeapon | 0x4c09a0 |
| 0x560500 | 944 | VIBE_Privilege_PanelMedicus | 0x4c09a0 |
| 0x5608b0 | 875 | VIBE_Privilege_PanelDivorce | 0x4c09a0 |
| 0x561fd0 | 866 | VIBE_Privilege_RemoveFromOffice | 0x4c09a0 |
| 0x4ba2bc | 856 | VIBE_DragSelect_UpdateUnitList | 0x4c09a0 |
| 0x587bfc | 851 | VIBE_Building_CheckBuildRequirements | 0x4c09a0 |
| 0x4bfc48 | 838 | VIBE_ChatConsole_BuildWindow | 0x4c09a0 |
| 0x5671f4 | 811 | VIBE_Item_UseObjectAction | 0x4c09a0 |
| 0x562cdc | 804 | VIBE_Privilege_PanelSwapSeats | 0x4c09a0 |
| 0x56ec14 | 789 | VIBE_Plant_LoadVegetationModel | 0x4c09a0 |
| 0x5066b8 | 776 | VIBE_Scene_EnterBuildingInterior | 0x4c09a0 |
| 0x442d88 | 762 | VIBE_Script_ParseDeclaration_42d88 | 0x4c09a0 |
| 0x560c1c | 760 | VIBE_Privilege_BlackmailConfirm | 0x4c09a0 |
| 0x55db0c | 754 | VIBE_Recruit_RunCandidatePickWindow | 0x4c09a0 |
| 0x4c6964 | 748 | VIBE_He_AssignIconForHandler | 0x4c09a0 |
| 0x5843c8 | 693 | VIBE_Building_ApplyUpgradeStaff | 0x4c09a0 |
| 0x4bdc3c | 654 | VIBE_DragSelect_ApplyToUnits | 0x4c09a0 |
| 0x4bdecc | 645 | VIBE_DragSelect_ApplyToSelection | 0x4c09a0 |
| 0x562334 | 640 | VIBE_Privilege_PanelEmbezzlement | 0x4c09a0 |
| 0x55ebf0 | 638 | VIBE_StatPanel_DrawGraphSeriesC | 0x4c09a0 |
| 0x44b8f4 | 615 | VIBE_Text_LoadDefinitionFile | 0x528560 |
| 0x5b7e54 | 598 | VIBE_Object_ReparentWithTransform | 0x4c09a0 |
| 0x5b2710 | 596 | VIBE_Object_ChangeTransparency | 0x4c09a0 |
| 0x4adef4 | 585 | VIBE_Dialog_OpenBuildingForActiveChar | 0x4c09a0 |
| 0x543994 | 572 | VIBE_MapView_UpdateScrollState | 0x4c09a0 |
| 0x41fa1c | 572 | VIBE_DragCursor_Render | 0x4c09a0 |
| 0x55e9b4 | 570 | VIBE_StatPanel_DrawGraphSeriesB | 0x4c09a0 |
| 0x55e778 | 570 | VIBE_StatPanel_DrawGraphSeriesA | 0x4c09a0 |
| 0x50bf08 | 565 | VIBE_TradePanel_RefreshSellColumns | 0x4c09a0 |
| 0x5dc0d0 | 525 | VIBE_Vfs_WriteBuffered | 0x528560 |
| 0x4bc07c | 515 | VIBE_Hud_UpdateEdgeScroll | 0x4c09a0 |
| 0x452d38 | 512 | VIBE_Character_UpdateGuardBehavior | 0x4c09a0 |
| 0x5cef14 | 509 | VIBE_Anim_CreateObjectAnim | 0x4c09a0 |
| 0x4ae3b8 | 483 | VIBE_Groundplan_CreateWindow | 0x4c09a0 |
| 0x5647c8 | 468 | VIBE_Privilege_PanelApology | 0x4c09a0 |
| 0x5d0b64 | 465 | VIBE_Anim_AttachToBone | 0x4c09a0 |
| 0x594df0 | 465 | VIBE_Object_ComputeMarketValue | 0x4c09a0 |
| 0x506b68 | 457 | VIBE_Object_UpdateBuildingVisualState | 0x527fa4 |
| 0x426488 | 449 | VIBE_Character_LoadObjectAnimation | 0x4c09a0 |
| 0x594100 | 430 | VIBE_Building_OpenUpgradeTreeWindow | 0x4c09a0 |
| 0x4c5d98 | 422 | VIBE_He_SendQuickjumpMessage | 0x4c09a0 |
| 0x5ef980 | 401 | VIBE_Sky_BuildDomeMesh | 0x527fa4 |
| 0x55d990 | 380 | VIBE_Recruit_RunHireConfirmDialog | 0x4c09a0 |
| 0x555f8c | 379 | VIBE_Office_ShowCandidateListWithRoles | 0x4c09a0 |
| 0x55e600 | 373 | VIBE_StatPanel_DrawGraphLine | 0x4c09a0 |
| 0x5bd2d8 | 371 | VIBE_Floor_ReloadTextures | 0x527fa4 |
| 0x50cb4c | 365 | VIBE_Building_CheckPlotConnectivity | 0x527fa4 |
| 0x4c64bc | 365 | VIBE_He_ArrangeIconsInCircle | 0x4c09a0 |
| 0x5792e0 | 357 | VIBE_City_ApplyStatsFromAck | 0x4c09a0 |
| 0x442b34 | 346 | VIBE_Script_ParseSymbolName | 0x4c09a0 |
| 0x4ae678 | 345 | VIBE_Groundplan_SetWidgetsVisible | 0x4c09a0 |
| 0x441280 | 334 | VIBE_Script_DeclareLocal | 0x4c09a0 |
| 0x50ed14 | 330 | VIBE_Building_HandleSelectionClick | 0x4c09a0 |
| 0x5651bc | 326 | VIBE_Privilege_PanelMiracle | 0x4c09a0 |
| 0x4ae828 | 326 | VIBE_Groundplan_DestroyWidgets | 0x4c09a0 |
| 0x445a28 | 325 | VIBE_Script_DestroyContext | 0x4c09a0 |
| 0x4c5c54 | 323 | VIBE_He_SendEntityMessage | 0x4c09a0 |
| 0x40e818 | 306 | VIBE_Object_Reinitialize | 0x528560 |
| 0x4c5b40 | 274 | VIBE_EventPanel_HandleSlotClick | 0x4c09a0 |
| 0x5b7d14 | 272 | VIBE_Object_TransformPointToParent | 0x4c09a0 |
| 0x41e814 | 266 | VIBE_Gfx_CrossFadeStep | 0x4c09a0 |
| 0x56a700 | 259 | VIBE_Menu_RunSaveNameInput | 0x4c09a0 |
| 0x5627cc | 249 | VIBE_Privilege_CharmConfirm | 0x4c09a0 |
| 0x427370 | 248 | VIBE_Terrain_ScanSegmentHeightRange | 0x4c09a0 |
| 0x485e88 | 241 | VIBE_Combat_AttachObjectMesh | 0x4c09a0 |
| 0x4b0758 | 235 | VIBE_Groundplan_FadeInScene | 0x4c09a0 |
| 0x4ac0c8 | 231 | VIBE_Cutscene_CheckMaster | 0x4c09a0 |
| 0x49514c | 228 | VIBE_Command_QueueRequestGuardTarget61 | 0x4c09a0 |
| 0x50cd94 | 227 | VIBE_Building_GetGebaeudeBauplatzPos | 0x4c09a0 |
| 0x40c2f0 | 213 | VIBE_CharAction_InsertActionArgs | 0x4c09a0 |
| 0x49d910 | 208 | VIBE_Office_RenderSessionTimer | 0x4c09a0 |
| 0x5d3858 | 201 | VIBE_Anim_LoadStreamToStock | 0x4c09a0 |
| 0x5b4274 | 199 | VIBE_Object_ToggleSuspendStateNamed | 0x4c09a0 |
| 0x4416c4 | 194 | VIBE_Script_SkipBraceBlock | 0x4c09a0 |
| 0x4c0864 | 192 | VIBE_Camera_ComputeWorldTarget | 0x4c09a0 |
| 0x40238c | 189 | VIBE_Character_SetAllFreezeState | 0x527fa4 |
| 0x401894 | 184 | VIBE_Character_SetVisible | 0x4c09a0 |
| 0x51db4c | 181 | VIBE_Building_SelectRoomToEnter | 0x4c09a0 |
| 0x579a38 | 152 | VIBE_Economy_ComputeAverageQuality | 0x4c09a0 |
| 0x5c63a0 | 151 | VIBE_Heightmap_Create | 0x527fa4 |
| 0x5d0d38 | 148 | VIBE_Anim_PruneExpiredAttachments | 0x4c09a0 |
| 0x55d530 | 143 | VIBE_Recruit_CollectNearbyRecruitCandidates | 0x4c09a0 |
| 0x41fcbc | 139 | VIBE_DragCursor_SetSprite | 0x4c09a0 |
| 0x4ad508 | 138 | VIBE_DragSlot_BeginDragText | 0x4c09a0 |
| 0x571218 | 135 | VIBE_Privilege_ShowDialog | 0x4c09a0 |
| 0x485f7c | 125 | VIBE_Command_FindOrAllocSlot | 0x4c09a0 |
| 0x5799a8 | 124 | VIBE_GameTick_HandleTurnControlCommand | 0x4c09a0 |
| 0x56c0cc | 123 | VIBE_Config_ApplyCameraAndScrollSettings | 0x527fa4 |
| 0x4be154 | 122 | VIBE_DragSelect_DrawBox | 0x4c09a0 |
| 0x609e30 | 113 | VIBE_String_MbsCompareN | 0x527de0 |
| 0x40e50c | 111 | VIBE_Decompressor_Init | 0x528560 |
| 0x5b2c70 | 104 | VIBE_Object_AssignMeshData | 0x4c09a0 |
| 0x4bea64 | 97 | VIBE_Window_PumpMessages | 0x527fa4 |
| 0x405504 | 83 | VIBE_Character_StandUp | 0x4c09a0 |
| 0x40e2b4 | 82 | VIBE_Building_Update | 0x4c09a0 |
| 0x50ccbc | 64 | VIBE_Building_ReserveBauplatzForActiveChar | 0x527fa4 |
| 0x5cec00 | 62 | VIBE_Anim_FreeObjAnimData | 0x4c09a0 |
| 0x609510 | 58 | VIBE_String_WideCharToBytes | 0x52895c |
| 0x5cf114 | 58 | VIBE_Anim_FindFreeMeshSlot | 0x4c09a0 |
| 0x55a9bc | 58 | VIBE_Gesetz_OpenPersonSelectionIfValid | 0x4c09a0 |
| 0x5f0b18 | 50 | VIBE_SceneGraph_RemoveMeshFromTree | 0x4c09a0 |
| 0x5d8f1c | 29 | VIBE_ShapeAnim_GetSlot | 0x4c09a0 |
| 0x5d8ae8 | 24 | VIBE_Coord_Push | 0x527fa4 |
| 0x5d9104 | 21 | VIBE_Resource_FlushAndFree | 0x527de0 |
| 0x5cb8e0 | 16 | VIBE_Util_RandSeed | 0x527de0 |
| 0x5942b0 | 11 | VIBE_Building_CloseUpgradeWindow | 0x4c09a0 |
| 0x5cb8b0 | 10 | VIBE_Util_GetRandStatePtr | 0x527de0 |
| 0x5f821c | 8 | VIBE_Util_NullThunk | 0x52895c |
| 0x5f8230 | 5 | VIBE_Util_ExitHandlerThunk | 0x527de0 |
| 0x5eb960 | 5 | VIBE_Vfs_CloseHandleThunk | 0x527de0 |
| 0x4f73ec | 1 | VIBE_Building_EmptyCallbackStub | 0x528560 |

> Note: a few rows here are render-adjacent leaves that are still *engine logic*
> (Sky_BuildDomeMesh, Heightmap_Create, Floor_ReloadTextures, Object_UpdateBuildingVisualState,
> Coord_Push) — reconstruct as math/scene logic, not as GPU-API boundaries.

---

## MISSING — DDraw / Direct3D / DirectInput boundary (rules 3-4 — DO NOT reconstruct as logic)

These are the original DirectDraw/Direct3D fixed-function + DirectInput devices, already
replaced by Vulkan (`shim::IGraphicsDevice`) and SDL (`shim::IPlatform`). Their string
refs prove the boundary: `dd_DD->lpVtbl->SetDisplayMode/EnumDisplayModes` (`dd_vesa.c`),
`d3d_Device->lpVtbl->...` (`d3_engine.c`). Verify the swap covers the observable behavior;
no 1:1 reconstruction expected.

| addr | bytes | name |
|------|------:|------|
| 0x4337d4 | 3086 | VIBE_Render_InitDisplayMode (DDraw) |
| 0x42d220 | 2591 | VIBE_Render_BuildSnowTexture |
| 0x5dda1c | 2501 | VIBE_Render_ApplyRenderStates (D3D) |
| 0x5ae434 | 2129 | VIBE_Render_DrawTexturedTriangles (D3D) |
| 0x5b5474 | 1218 | VIBE_Render_DrawTexturedQuad |
| 0x5dd61c | 937 | VIBE_Render_CreateDeviceAndViewport |
| 0x40ca38 | 774 | VIBE_Input_DirectInputInit (DInput) |
| 0x5dfbd8 | 758 | VIBE_Render_CreateSurfacePalette |
| 0x5dd1c4 | 670 | VIBE_Render_EnumDevices |
| 0x5af5f8 | 668 | VIBE_Render_SetupViewTransform |
| 0x56be58 | 626 | VIBE_Render_ApplyGfxSettings |
| 0x43343c | 600 | VIBE_Render_SelectDDrawDevice |
| 0x5dfed0 | 572 | VIBE_Render_CreateDynamicTexture |
| 0x5eef90 | 523 | VIBE_Render_DrawSkyFlareSprite |
| 0x431f44 | 484 | VIBE_Render_ReleaseSurfaces |
| 0x40fb4c | 330 | VIBE_Input_SetIconTextById |
| 0x5dd0a0 | 291 | VIBE_Render_EnumTextureFormats |
| 0x5daec0 | 182 | VIBE_Texture_FindGroupMember |
| 0x43371c | 163 | VIBE_Render_EnumDisplayModes |
| 0x5dbbb4 | 129 | VIBE_Texture_DetachClone |
| 0x40c710 | 126 | VIBE_Input_BuildScancodeTable |
| 0x5de6e4 | 123 | VIBE_Render_SetZEnable |
| 0x435cb4 | 122 | VIBE_Render_SetSurfacePrivateData |
| 0x5dd5a4 | 118 | VIBE_Render_PrecacheTexture |
| 0x40c950 | 103 | VIBE_Input_FormatErrorMessage |
| 0x5b5410 | 98 | VIBE_Render_FillBackBuffer |
| 0x5d8fb0 | 97 | VIBE_Render_WithSurfaceContext |
| 0x5b9ef4 | 94 | VIBE_Render_SetGammaTable |
| 0x4346c8 | 73 | VIBE_Render_LockSurfaceRegion |
| 0x5dd9dc | 61 | VIBE_Render_DrawTriangleList (D3D) |
| 0x40c9f8 | 61 | VIBE_Input_AcquireKeyboardDevice (DInput) |
| 0x40c9b8 | 61 | VIBE_Input_AcquireMouseDevice (DInput) |
| 0x5e0324 | 50 | VIBE_Render_EndScene |
| 0x4350ac | 21 | VIBE_Render_UnlockSurface |
| 0x435094 | 21 | VIBE_Render_ReleaseSurface |
| 0x60ddd2 | 6 | DirectInputCreateA (import thunk) |
| 0x60ddcc | 6 | DirectDrawCreate (import thunk) |
| 0x60ddc6 | 6 | DirectDrawEnumerateExA (import thunk) |

---

## MISSING — Miles / MSS32 audio boundary (rule 5 — replaced by SDL audio)

The original sound was Miles Sound System; replaced by `shim::IAudioDevice` (SDL). These
Audio_*/Sound_*/Sound3d_*/Voice_* leaves are the Miles voice/sample/stream API surface +
the 3D listener math. The 3D-listener math leaves (`Sound3d_UpdateListener`,
`SetListenerOrientation`) are partly logic and could be reconstructed atop the SDL mixer
if positional audio is wanted; the rest are Miles API shims.

| addr | bytes | name |
|------|------:|------|
| 0x425208 | 2984 | VIBE_Sound3d_UpdateListener (3D math — partly logic) |
| 0x445f00 | 718 | VIBE_Sound_UpdateVoices |
| 0x426014 | 652 | VIBE_Sound3d_SetListenerOrientation (3D math) |
| 0x446e4c | 522 | VIBE_Audio_UnloadSampleBank |
| 0x44737c | 396 | VIBE_Audio_PlayVoiceSample |
| 0x439bf8 | 210 | VIBE_Sound_InitThread |
| 0x57ef28 | 190 | VIBE_VoiceQueue_Enqueue |
| 0x446340 | 152 | VIBE_Sound_StartVariation |
| 0x424538 | 140 | VIBE_Sound3d_InitPool |
| 0x4463d8 | 116 | VIBE_Sound_SetBankPath |
| 0x447214 | 101 | VIBE_Audio_StartVoiceSample |
| 0x581fc0 | 99 | VIBE_Voice_PlayPositionalSample |
| 0x43aa64 | 82 | VIBE_Audio_StopAmbientTrack |
| 0x582024 | 80 | VIBE_Voice_PlayQueuedSample |
| 0x43a8c4 | 76 | VIBE_Audio_FindFreeVoiceSlot |
| 0x449f4c | 68 | VIBE_Audio_SetNamedSampleFile |
| 0x4b5d5c | 58 | VIBE_Sound3d_UpdateListener_b5d5c |
| 0x44a9f8 | 53 | VIBE_Audio_GetStreamMsLength |
| 0x44a9c0 | 53 | VIBE_Audio_GetStreamMsPosition |
| 0x44a884..0x44a184 | 51-52 ea | Audio_Set/Get/Start/End/Pause/Resume Sample+Stream (Miles API, ~16 leaves) |
| 0x446480 | 45 | VIBE_Sound_FindBankBySample |
| 0x43a898 | 41 | VIBE_Audio_FindFreeAmbientSlot |
| 0x424c88 | 37 | VIBE_Sound3d_SetLooping |
| 0x4262a0 | 32 | VIBE_Sound3d_SetListenerFromVectors |
| 0x44759c | 28 | VIBE_Audio_SetMasterVolume |
| 0x449570 | 16 | VIBE_Audio_IsInitialized |
| 0x446330 | 14 | VIBE_Sound_SetLoopFlag |
| 0x4248c8 | 11 | VIBE_Sound3d_DetachIfValid |

---

## MISSING — MSVC CRT / file / zlib runtime (host runtime — mostly already ported)

~80 `VIBE_Crt_*` / `VIBE_File_*` / `VIBE_Mbcs_*` / `VIBE_Math_*` / `VIBE_Float_*` /
`VIBE_Runtime_*` / `VIBE_Memory_*` / `VIBE_Time_*` are the statically-linked Microsoft
C runtime (printf/fopen/localtime/mbcs/fdiv-helper/exception). These are *not* game logic
and are satisfied by the host C++ standard library; do not hand-reconstruct. The ~24
`VIBE_Gzip_*` / `VIBE_Inflate_*` / `VIBE_Deflate_*` stream-wrapper entries (gzopen/gzread/
gzseek/inflateInit2/inflateEnd/deflateReset/deflateEnd …) are **already reconstructed**
behaviorally as a zlib port in `src/compress/gzip.cpp|inflate.cpp|deflate.cpp` — they only
lack per-address provenance comments. **Action: add the missing `@0xADDR` provenance to the
existing zlib bodies rather than re-reconstructing.** Notable still-real-but-tiny:
`0x60d300 VIBE_Math_FmodCore` (518 b), `0x603b3c VIBE_Memory_ResizeBlockInPlace` (652 b),
`0x44e420 VIBE_MemPool_StartupStack` (289 b) — verify these against existing `src/mem/`.

---

## Notes for the next wave

- **Priority 1 = the AI brain** (`AiNeeds_BuildScoreTable` + `Ai_CalcMeister*`/`CalcAngriff`):
  largest single logic cluster, INI-driven, fans out from `AiMethod_LoadDataFile @0x468a40`
  (engine-init) and the frame loop. Reconstruct `AiMethod_RegisterFromIni @0x468f6c` with it.
- **Priority 2 = the Privilege/office action set** (22 fns): one tight module
  (`office_law*`/`contextaction2` already have the enum stubs at these addrs), high value
  for "playable guild politics".
- **Priority 3 = Building lifecycle + Groundplan window** (the build/upgrade UX).
- The Script VM front-end (`Script_EnterFunction`/parser leaves) gates `.esc` execution.
- Before reconstructing any Gzip/Inflate/Deflate/CRT/Math row, **grep `src/compress/`,
  `src/io/`, `src/mem/`, `src/util/` first** — most are present under
  zlib/libc names and only need provenance, not a rewrite (avoid ODR clashes).

*All addresses are gilde.exe (imagebase 0x400000). Verdicts cross-checked with
func_profile/decompile on the largest hits.*
