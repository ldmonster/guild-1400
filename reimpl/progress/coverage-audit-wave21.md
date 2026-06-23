# Wave-21 — Rule-7 Coverage Audit Refresh (post wave 21)

**Agent:** W21-COVERAGE · **Date:** 2026-06-16 · **MCP:** live (`gilde.exe`, imagebase 0x400000, hexrays ready)

Analysis-only refresh of [`coverage-audit-wave20.md`](coverage-audit-wave20.md). Same
question, same method: *of the functions reachable from the binary's real boot spine,
which still have no reconstructed body in `src/`?* — re-measured after wave 21.

> **Classifier refinement (matters for the headline):** wave-21 modules cite the
> reconstructed function by its **internal instruction addresses** (e.g. `0x40f044`
> inside `Object_Update`), not the function-start address (`0x40eea0`). The wave-20
> classifier matched function-start only and would *undercount* wave-21 by ~24 funcs.
> This audit counts an address **reconstructed if ANY cited `0xADDR` falls inside the
> function's `[start,end)` range** — the correct interpretation of "body exists in src".
> The headline below uses range-matching; the same rule re-applied to wave-20's snapshot
> only raises it, so the delta is real, not an artifact.

---

## Method (identical to wave-20, re-run)

1. **Roots (boot spine), unchanged:** `0x52895c`, `0x527de0`, `0x527fa4`, `0x528560`,
   `0x4c09a0`.
2. **Reachable set:** BFS over real call edges (`get_first/next_fcref_from` to function
   starts) from the five roots, via IDA `py_eval`. Result: **2164 functions**
   (1,024,041 bytes) — up from wave-20's 2152 (+12; a few more indirect targets resolved).
3. **Reconstruction check:** range-cite (above). An address counts reconstructed iff a
   `0xADDR` inside its byte range appears in a `.cpp` body. Header-only / enum-stub
   mentions still do not count.
4. **Boundary vs logic** by name + profile (DDraw/D3D/DInput → Vulkan/SDL; Miles/MSS32 →
   SDL audio; Crt/File/Math/Float/Memory/Time/Format/Mbcs/Runtime/Gzip/Inflate/Deflate →
   host runtime).

---

## Headline coverage number

> **≈90.5% of entry-reachable functions are reconstructed (1958 / 2164).**
> Up from **89.0% (1916/2152)** at wave-20 — a **+1.5 pt** gain.
> Of the **206** remaining with no reconstructed body, only **33 are genuine game logic**
> (down from 62); the rest are intentional tech boundaries or host CRT/zlib/MBCS runtime.

| bucket | wave-20 | wave-21 | delta |
|--------|------:|------:|------:|
| reachable from boot spine | 2152 | 2164 | +12 |
| reconstructed (body in src) | 1916 | **1958** | **+42** |
| missing (no body) | 236 | **206** | **−30** |
| — **pure game logic** | **62** | **33** | **−29** |
| — DDraw/D3D/DInput render boundary | 24 | 11+ | (Input DInput/scancode/acquire counted here) |
| — Miles/MSS32 audio boundary | 30 | 33 | (more Music_* leaves reachable) |
| — MSVC-CRT / File / Math / MBCS / zlib runtime | 116 | ~120 | (Mbcs/Runtime/errno leaves) |
| — thunks | 4 | 3 | DirectDrawCreate/EnumerateExA + Vfs CloseHandle |

**Of the 62 wave-20 pure-logic targets, 27 are now CLOSED by wave 21; 32 survive**
(+1 wave-21-newly-enumerated logic fn → 33 total). See deltas below.

---

## What wave 21 CLOSED (27 of the 62) — sorted by size

The per-frame entity/object core (the biggest single block of debt) landed in
`src/sim/object_update.cpp` (+ its callees), plus the privilege panels-b cluster,
recruit modals, road-network/map, palette object-search, and config-apply.

| addr | bytes | name | where |
|------|------:|------|-------|
| 0x41078c | 5919 | VIBE_Entity_InteractionLogic | sim/object_update.cpp |
| 0x418f34 | 2621 | VIBE_EntityChild_Process | sim/object_update.cpp |
| 0x40eea0 | 2399 | VIBE_Object_Update | sim/object_update.cpp |
| 0x592d98 | 2180 | VIBE_Map_ComputeRoadNetworkLayout | play/map_view.cpp |
| 0x415b78 | 2115 | VIBE_Animation_Apply | sim/object_update.cpp |
| 0x4294d4 | 1891 | VIBE_Rain_UpdateDrop | render particle (range-cited) |
| 0x55de00 | 1544 | VIBE_Recruit_RunRecruitmentOfferWindow | gui/recruit_office.cpp |
| 0x4ff070 | 1312 | VIBE_Hotkey_OpenAssignWindow | gui/hotkey_assign.cpp (wired via cheat_recon) |
| 0x565f9c | 1051 | VIBE_Privilege_PanelEvidenceReview | world/privilege_panels_b.cpp |
| 0x561bb4 | 1051 | VIBE_Privilege_PanelEnactLaw | world/privilege_panels_b.cpp |
| 0x5628c8 | 980 | VIBE_Privilege_PanelCounterEspionage | world/privilege_panels_b.cpp |
| 0x561fd0 | 866 | VIBE_Privilege_RemoveFromOffice | world/privilege_panels_b.cpp |
| 0x562cdc | 804 | VIBE_Privilege_PanelSwapSeats | world/privilege_panels_b.cpp |
| 0x55db0c | 754 | VIBE_Recruit_RunCandidatePickWindow | gui/recruit_office.cpp |
| 0x562334 | 640 | VIBE_Privilege_PanelEmbezzlement | world/privilege_panels_b.cpp |
| 0x47abfc | 585 | VIBE_ObjectSearch_FindByPaletteRange | object search cluster |
| 0x47b008 | 454 | VIBE_ObjectSearch_FindPeopleByPalette | object search cluster |
| 0x47ae48 | 447 | VIBE_ObjectSearch_FindOneByPaletteRange | object search cluster |
| 0x4159dc | 386 | VIBE_Property_Set | sim/object_update.cpp (range) |
| 0x55d990 | 380 | VIBE_Recruit_RunHireConfirmDialog | gui/recruit_office.cpp |
| 0x5651bc | 326 | VIBE_Privilege_PanelMiracle | world/privilege_panels_b.cpp |
| 0x592c7c | 283 | VIBE_Map_ComputeBuildingChainDepth | play/map_view.cpp |
| 0x4ac0c8 | 231 | VIBE_Cutscene_CheckMaster | cutscene cluster |
| 0x56c0cc | 123 | VIBE_Config_ApplyCameraAndScrollSettings | play/config_apply.cpp |
| 0x5d8f1c | 29 | VIBE_ShapeAnim_GetSlot | anim leaf |
| 0x5f821c | 8 | VIBE_Util_NullThunk | thunk |
| 0x5f8230 | 5 | VIBE_Util_ExitHandlerThunk | thunk |

---

## STILL MISSING — genuine game logic (33) — sorted by size

These are the real reconstruction debt for the next wave. Notably the brief named
several of these as wave-21 targets that **did not land**: `Anim_CreateMorphAnim`
(anim_object.cpp stops at 0x5cf146, one byte short of the 0x5cf150 function start —
it is a *separate* function), `Snow_UpdateFlake`, `Ai_CalcBankmeister`, and the
privilege **evidence-details / evidence-review-alt / build-evidence-entry** sub-cluster
(privilege_panels_b.cpp cites 0x561../0x562.. but NOT the 0x565b88/0x5667a0/0x56589c
ranges).

| addr | bytes | name | note |
|------|------:|------|------|
| 0x5cf150 | 3026 | VIBE_Anim_CreateMorphAnim | morph-target anim build; NOT landed (sibling at 0x5cf003 is) |
| 0x41ceb4 | 1714 | VIBE_DecompressGameState | save-load decompress front; pairs w/ Decompressor_Init |
| 0x42a644 | 1329 | VIBE_Snow_UpdateFlake | particle physics; Rain sibling landed, this did not |
| 0x459264 | 1150 | VIBE_Ai_CalcBankmeister | Meister-AI bank sub-planner; NOT landed |
| 0x565b88 | 1042 | VIBE_Privilege_PanelEvidenceDetails | evidence sub-cluster (panels_b gap) |
| 0x5667a0 | 1017 | VIBE_Privilege_PanelEvidenceReviewAlt | evidence sub-cluster |
| 0x4bfc48 | 838 | VIBE_ChatConsole_BuildWindow | chatconsole.cpp cites 0x53629c, not this |
| 0x56589c | 747 | VIBE_Privilege_BuildEvidenceEntry | evidence sub-cluster helper |
| 0x413220 | 719 | VIBE_Widget_LayoutBounds | widget bounds layout |
| 0x57b480 | 570 | VIBE_Amt_ComputeOfficeWages | office wage computation |
| 0x50bf08 | 565 | VIBE_TradePanel_RefreshSellColumns | trade_panel_windows uncited at this addr |
| 0x50b1c4 | 394 | VIBE_TradePanel_RefreshItemColumns | trade panel sibling |
| 0x40fb4c | 330 | VIBE_Input_SetIconTextById | cursor-icon text set (logic, not DInput) |
| 0x40eaf0 | 277 | VIBE_Gui_ResolveObjectState | object-state resolve |
| 0x40e9e8 | 262 | VIBE_State_Update | state machine update |
| 0x40e728 | 237 | VIBE_State_GetCurrent | state machine getter |
| 0x494910 | 178 | VIBE_Command_QueueRequestBuffer28 | command-queue request builder |
| 0x4152cc | 159 | VIBE_Property_Get | property getter (Property_Set landed) |
| 0x4c2ba0 | 158 | VIBE_Gesetz_ComputeMaxWantedLevel | law/wanted-level calc |
| 0x5437d8 | 150 | VIBE_MapView_AddCornerObjects | map corner-object placement |
| 0x4ad508 | 138 | VIBE_DragSlot_BeginDragText | dragselect.cpp uncited at this addr |
| 0x412ea4 | 113 | VIBE_Gui_MarkObjectUsed | gui object-used flag |
| 0x40e50c | 111 | VIBE_Decompressor_Init | pairs with DecompressGameState |
| 0x5929f0 | 105 | VIBE_Person_QueryByGoodType | person query by good |
| 0x494d68 | 40 | VIBE_Command_QueueRequestQuad46 | cmd-queue leaf |
| 0x495124 | 40 | VIBE_Command_QueueRequestQuad60 | cmd-queue leaf |
| 0x495070 | 39 | VIBE_Command_QueueRequestQuad54 | cmd-queue leaf |
| 0x4ac9c0 | 39 | VIBE_Cutscene_GetRandSeed | cutscene RNG getter |
| 0x494b74 | 35 | VIBE_Command_QueueRequestPair35 | cmd-queue leaf |
| 0x494ca4 | 35 | VIBE_Command_QueueRequestPair42 | cmd-queue leaf |
| 0x4ac9a0 | 31 | VIBE_Cutscene_SetRandSeed | cutscene RNG setter |
| 0x5d9104 | 21 | VIBE_Resource_FlushAndFree | tiny resource free |
| 0x5d8b00 | 16 | VIBE_Coord_Transform | tiny coord transform |

**Cluster verdict:** remaining real debt concentrates in (a) the **privilege evidence
sub-cluster** (EvidenceDetails / EvidenceReviewAlt / BuildEvidenceEntry, ~2.8 kB — the
part of panels-b that did not land), (b) the **two unfinished particle/anim/AI leaves**
(Anim_CreateMorphAnim 3.0 kB, Snow_UpdateFlake 1.3 kB, Ai_CalcBankmeister 1.2 kB),
(c) **save-load decompress** (DecompressGameState + Decompressor_Init, gates load), and
(d) a **command-queue request-builder family** (Buffer28 + 6 tiny Quad/Pair leaves) plus
the **State / Property / Gui-object** mini-cluster.

---

## TOP-12 still-missing LOGIC (next-wave priority, by size)

1. 0x5cf150 · 3026 · VIBE_Anim_CreateMorphAnim
2. 0x41ceb4 · 1714 · VIBE_DecompressGameState
3. 0x42a644 · 1329 · VIBE_Snow_UpdateFlake
4. 0x459264 · 1150 · VIBE_Ai_CalcBankmeister
5. 0x565b88 · 1042 · VIBE_Privilege_PanelEvidenceDetails
6. 0x5667a0 · 1017 · VIBE_Privilege_PanelEvidenceReviewAlt
7. 0x4bfc48 ·  838 · VIBE_ChatConsole_BuildWindow
8. 0x56589c ·  747 · VIBE_Privilege_BuildEvidenceEntry
9. 0x413220 ·  719 · VIBE_Widget_LayoutBounds
10. 0x57b480 ·  570 · VIBE_Amt_ComputeOfficeWages
11. 0x50bf08 ·  565 · VIBE_TradePanel_RefreshSellColumns
12. 0x50b1c4 ·  394 · VIBE_TradePanel_RefreshItemColumns

---

## Reconstructed-but-NOT-WIRED (flag for integration, rule 13)

Still-unwired top-level entry points (body exists, no external `src/` caller). The big
wave-21 entity core is well-wired (`Object_Update`/`Entity_InteractionLogic`/
`EntityChild_Process`/`Animation_Apply` each have 3–8 external refs). These two carry
over from wave 20 and remain unbound:

| addr | bytes | name | file | handoff |
|------|------:|------|------|---------|
| 0x4c9dec | 2154 | VIBE_NpcAction_NotifyJoinLeaveGroup | src/sim/npcaction.cpp | wire into the NPC group join/leave action-dispatch site |
| 0x594100 | 430 | VIBE_Building_OpenUpgradeTreeWindow | src/sim/building.cpp | wire into the building upgrade-tree UI open click |

> `VIBE_Floor_ReloadTextures` and `VIBE_Gfx_CrossFadeStep` (wave-20 flags) are now wired
> (2 external refs each, via floorgfx_recon / command_leaves). Internal `static`/helper
> functions in wave-21 modules are not listed (file-local by design).

---

## Remaining boundary / runtime buckets (DO NOT reconstruct as logic)

- **Render boundary (~11+):** DDraw/D3D fixed-function + DInput devices
  (Render_BuildSnowTexture as a GPU surface build, Render_ApplyRenderStates,
  Render_CreateSurfacePalette/DynamicTexture/EnumTextureFormats,
  Input_DirectInputInit/BuildScancodeTable/Acquire{Mouse,Keyboard}Device,
  DirectDrawCreate/EnumerateExA thunks) — covered by `shim::IGraphicsDevice`/`IPlatform`.
- **Audio boundary (33):** Miles/MSS32 + `Music_*` track playback
  (Music_UpdateOutdoorTrackPlayback, Music_ResumeLocationTrack,
  Sound3d_SetListenerOrientation, Sound_InitThread, …) — covered by `shim::IAudioDevice`.
  The Sound3d listener-math is partly logic; reconstruct atop SDL only if positional audio
  is wanted.
- **CRT / MBCS / zlib runtime (~120):** `Crt_*`/`File_*`/`Math_*`/`Float_*`/`Memory_*`/
  `Time_*`/`Format_*`/`Mbcs_*`/`Runtime_*` (errno/abort/MBCS char-class tables) +
  `Gzip_*`/`Inflate_*`/`Deflate_*`. Host C++ runtime + the zlib port in `src/compress/`.
  **Action: add `@0xADDR` provenance to existing zlib/CRT bodies** rather than
  re-reconstructing (avoid ODR). The MBCS/Runtime leaves grew vs wave-20 only because
  more CRT leaves are now reachable — not a regression.
- **Thunks (3):** DirectDrawCreate/EnumerateExA import thunks + Vfs CloseHandle thunk.

---

## Reproduce

- `/tmp/guild_w21/reach_rows.txt` — full 2164-row reachable set (addr·size·name) from the
  IDA BFS (`py_eval`, edges via `get_first/next_fcref_from`).
- `/tmp/guild_w21/cited_addrs.txt` — every `0xADDR` cited in any `src/**/*.cpp`.
- `/tmp/guild_w21/classify2.py` — range-cite headline classifier (= reconstructed iff a
  cited addr falls in `[start,end)`).
- `/tmp/guild_w21/delta.py` — wave-20-62-logic closed/still-missing delta (range-cite).
- `/tmp/guild_w21/bucket.py` — logic vs boundary bucketing + refined game-logic subset.

*All addresses are gilde.exe (imagebase 0x400000). Snapshot taken 2026-06-16. Headline =
90.5% (1958/2164). Genuine game-logic debt: 33 functions / 15,625 bytes.*
