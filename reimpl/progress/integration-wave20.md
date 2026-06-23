# Wave-20 — INTEGRATION (W20-INTEGRATION): wire the wave-18/19 handoffs

**Agent:** W20-INTEGRATION · **Date:** 2026-06-16 · **MCP:** live (`gilde.exe`, imagebase 0x400000)

Rule-13 pass: the wave-18/19 agents reconstructed many functions but left them
exposed-but-not-connected because the real CALLER is a bind-site file they didn't
own. This wave WIRES the documented handoffs into their real callers / hook seams so
the reconstructed logic is in the live call tree (or, where the entry subtree is not
yet live, connects the reconstructed pieces along the real call edge and documents
the remaining gap). Each bind was verified against the decompile (MCP) before wiring.

Owned bind-site files edited: `src/sim/person_create.cpp`, `src/ai/aiplayer.{h,cpp}`,
`src/app/wiring.{h,cpp}`, `src/play/gamelogic_recon.{h,cpp}`,
`src/world/wire_building.cpp`, `tests/unit/ai_meister_test.cpp`.

---

## WIRED (value-verified)

### 1. Name tables — `person_create.cpp` `firstName` hook → `sim::NameAt`  ✅
- **gilde.exe:** 0x58da70 CreateAndSpawn; name stores at 0x58ee82 (dynasty),
  0x58e36c (male/female), 0x58f0f9 (female). All use `VIBE_Util_StrNCopyPad(dst,
  dword_8C400C/8C4320/8C4508[idx], 15)`.
- **Bind:** the default `PersonCreateHooks::firstName` (was `nullptr`) now calls a
  reconstructed `DefaultFirstName` → `StrNCopyPad15(dst, NameAt(gender, index))`.
  gender 0/1/2 already match `NameKind` male/female/dynasty. NameTables not loaded
  ⇒ `NameAt` returns "" ⇒ 15 zero bytes written = identical to the original reading
  a null table slot AND to the freshly-memset record region (headless greens kept).
- **1:1 CORRECTION (decompile-verified):** the dynasty name store dst was `rec+0x20`
  in the wave-18 reconstruction; the decompile (`v14+32` as `__int16*` = byte +0x40)
  shows it is **rec+0x40**. Fixed. Copy length corrected to 15 (StrNCopyPad writes
  15 bytes, leaves the 16th for the pre-zeroed record), matching 0x5d9360.
- **Tests:** person_create_harden (1546), person_create_w17 (47), family_record
  (236), world_family_harden (49), sim_person_lifecycle (75), newgame_apply (142),
  newgame_apply_e2e (23), newgame_result_itest (23) — all 0 failures.

### 2. Script commands — `RegisterScriptCommands` in the live engine-init  ✅
- **gilde.exe:** 0x528560 InitEngineAndScriptCommands registers the per-character
  .esc command table via 0x43dfb0 VIBE_Character_RegisterScriptCommands (the
  EngineInitHooks.characterRegisterScriptCommands seam).
- **Bind:** `RealSubsystems::scriptRegisterCommands()` (wiring.cpp — the live
  "register script command tables" step the spine calls) now calls
  `guild::script::RegisterScriptCommands()` (forward-declared to avoid pulling the
  heavy script/script_vm.h into the shared TU). Populates the shared
  `guild::sim::Commands()` table (ImportCommand replaces by name ⇒ idempotent).
  Result captured in `scriptCharCmdResult_` (accessor added).
- **Tests:** engine_init_app_recon (70), script_vm (107), app_wiring2 (32),
  app_session_init (76), app_real_run_e2e, app_real_boot_e2e — all 0 failures.

### 3. Meister AI — `EvaluateMeister` → `DispatchMeisterCalc` (aiplayer.cpp)  ✅
- **gilde.exe:** 0x4533a8 VIBE_Ai_EvaluateMeister. Decompiled: after budget logging
  + currency snapshot + danger-grid stamps it classifies by
  `MapTypeToCategory(typeByte)` + the AiPlayer +0 class byte and dispatches to
  CalcMeisterCraftProduction/Production / CalcMeisterFarming / CalcMeisterWache /
  Diebe / Ambush / Bankmeister / PlanProduction.
- **Bind:** new `guild::ai::EvaluateMeister(category, aiClass, meisterRec,
  attackBudget)` performs exactly that real call edge: `ClassifyMeisterRoutine` then
  `guild::sim::DispatchMeisterCalc` (the wave-19 Calc bodies for Farming/Wache/Diebe/
  Ambush; Production/Bank/PlanProduction stay with their own planner module —
  DispatchMeisterCalc no-ops them, matching the wave-19 split). Connects the two
  reconstructed pieces along the original's edge; null leaves take the original's
  null path. (Live entry `MeisterAi_ProcessPlayerTurn @0x5321ec` not yet in the
  spine — see DEFERRED.)
- **Tests:** ai_meister (70, +7 new EvaluateMeister golden), ai_meister_wache (46),
  diebe (37), farming (39), subplanners (57), passes (54), ai_recon_brain (72),
  person_reconcile (63) — all 0 failures.

### 4. Character visibility/standup — gamelogic_recon defaults → sim::SetVisible/StandUp  ✅
- **gilde.exe:** 0x401894 SetVisible, 0x405504 StandUp. Live call sites in the
  reconstructed CleanupTurnHandlers (0x53037f visibility pass; 0x530813 stand-up).
- **Bind:** `IGameLogicHooks::characterSetVisible(int rec,int vis)` default (was a
  no-op) now delegates to `guild::sim::SetVisible(rec,vis)` (out-of-line in
  gamelogic_recon.cpp to keep the header free of the character_ai dependency). With
  the inert query path's rec=0 SetVisible takes the original's "invalid character"
  reportError branch (no-op under inert hooks). `characterStandUp` changed from `()`
  to `(int actor)` and delegates to `guild::sim::StandUp(actor)`; the original's
  actor is `dword_12CEA94[218*i]` (host-owned action-handle table) so the inert
  retarget loop passes 0 ⇒ StandUp returns early (null actor). RecordingGameLogicHooks
  overrides both (trace unaffected).
- **Tests:** gamelogic_recon (57) — 0 failures.

### 5. Estate transfer — command_apply2 opcode 0x0D → world::PersonTransferEstateOwnership  ✅
- **gilde.exe:** 0x496888 ExBindObjectProto calls
  `VIBE_Person_TransferEstateOwnership(*(a1+16)=fromId, *(int**)(a1+20)=TO id,
  a2=ctx)`. command_apply2's ExBindObjectProto passes `spec = pkt.bytes+0x14`, so
  `spec[0]` IS the TO id; the original's a3 ctx is the apply-jump-table dispatch
  context (0). NB: the binary's signature has **no `mode` parameter** — the wave-19
  doc's "spec[1]=mode" is corrected here (the recon's 3rd arg is the a3 ctx passed
  to buildingSetObjectParent, default 0; verified against 0x58c4a8 decompile).
- **Bind:** `wire_building.cpp` installs `SetEstateTransferHook(&RealEstateTransfer)`
  where `RealEstateTransfer(fromId, spec) = PersonTransferEstateOwnership(fromId,
  spec[0], 0)`. The hook was inert (returned 0); now LIVE. `RegisterApplyHandlers2`
  + the apply jump table are live (newgame_apply.cpp / commandQueueInitAndSync), so
  opcode 0x0D now mutates the real person/building ownership tables.
- **Tests:** wire_building (10), recruit_office_estate (60), sim_command_apply_e2e
  (9), app_full_wired_playthrough_e2e, playable_flow_e2e — all 0 failures.

### 6. Building lifecycle / per-frame bridges — wire_building.cpp  ✅
- **gilde.exe:** 0x40e2b4 Building_Update (frame-loop object sweep), 0x587bfc
  CheckBuildRequirements. Installing the inert defaults (`Set*Hooks(nullptr)`) makes
  the reconstructed control flow LIVE over inert sub-leaves (the established
  headless-faithful pattern).
- **Bind:** `wire_building.cpp` adds `SetBuildLifecycleHooks(nullptr)` +
  `SetBuildingFrameHooks(nullptr)`. (The OpenUpgradeWindow charge math
  `Building_ComputeUpgradeChargeAmount @0x50f7c0` + node classifier take their hooks
  as direct args and have no live modal-window caller yet — see DEFERRED.)
- **Tests:** wire_building (10), building_lifecycle (118) — 0 failures.

### 7. Privilege panels SET A — InstallPrivilegePanelsA via SetPrivilegeLeafHook  ✅
- **gilde.exe:** the 11 SET-A VIBE_Privilege_Panel* leaves (GenerateHatred /
  ChangeProfession / ExpelWorker / Blackmail / MakePeace / Convert / Interrogation /
  Medicus / Divorce / Apology / CharmConfirm). `InvokePrivilegeLeaf` is a LIVE caller
  (sim/contextaction2.cpp), but ran the inert g_privilegeHook (returned 0).
- **Bind:** `wiring.cpp InstallAllRealGameplayHooks()` now calls
  `world::InstallPrivilegePanelsA()` — installs a SetPrivilegeLeafHook adapter that
  dispatches the 11 SET-A leaf ids to the reconstructed panel bodies (cost math, RNG
  draw order, relation deltas, return codes — verbatim). Non-SET-A leaf ids return 0
  (identical to the prior inert default). No provider yet ⇒ panels compute verdicts
  against the inert PrivilegePanelHooks (emit nothing live) — the documented wave-19
  deferral posture; a live provider binds the command queue later.
- **Tests:** privilege_panels_a (117), privilege_panels_b (127),
  contextaction2_boundary (27), sim_interaction_handlers (119),
  office_recon_privilege (79), app_real_run_e2e — all 0 failures.

---

## DEFERRED (reason + address — left unwired, NOT faked)

### D1. AI data file — AiNeeds_LoadDataFile → GameLogicInitGuardState
- **Addresses:** 0x4520d0 GameLogicInitGuardState, 0x468a40 LoadDataFile, 0x4764e8
  BuildScoreTable; data `Resources/gamedata/ai/AI_DATA.DFN`.
- **Reason:** `GameLogicInitGuardState` (world/guildstate_recon.cpp) has **no live
  caller** — the only call is `h.gameLogicInitGuardState()` inside the
  `VIBE_App_InitEngineAndScriptCommands` reconstruction (engine_init_app_recon.cpp),
  which is itself exercised only by its unit test, NOT the live spine (the spine uses
  `RealSubsystems` via `sub_`). There is also **no live consumer** of the produced
  AiNeeds catalog (no shared `g_aiNeedsCatalog` global; the reconstruction writes a
  caller-provided `AiNeedsCatalog out`). Wiring the gunzip+load+InitGuardState into
  the parameterless `gameLogicInitGuardState` default (no VFS/GuardState in scope)
  would either be a no-op or a fabricated path. Making it genuinely live requires
  bringing `VIBE_App_InitEngineAndScriptCommands` into the spine AND a live catalog
  consumer — beyond a few-line bind. The read path itself is fully reconstructed and
  ready (ai_needs_e2e over the real DFN passes); only the live entry is missing.

### D2. Object/anim per-frame — objectUpdate/entityChildProcess + 0x43/0x44 branches
- **Addresses:** 0x4139a8 Interactions; 0x40eea0 Object_Update, 0x418f34
  EntityChild_Process, 0x415b78 Animation_Apply, 0x41078c Entity_InteractionLogic.
- **Reason:** the `Interactions()` per-record switch BODY is not reconstructed (the
  interaction list `dword_62D26C[]` is engine-owned and empty under inert wiring, so
  the loop body runs zero times — documented as structure-only in gamelogic_recon.cpp
  165–201). The object_update cores (FormatAmount/WrapText/ComputeBarFraction/…) are
  render-state-coupled (the 740-byte scene-node array, clip-rect globals) and the
  object-anim doc itself states "the render-leaf emission is the renderer's job."
  Binding the `objectUpdate()/entityChildProcess()/animationApply()` hook defaults to
  the pure cores is not meaningful without a record + render context. The pure cores
  are reachable for the renderer when the interaction-list walk + render context land.

### D3. Gesetz person-selection — GesetzUiHooks.runPersonSelection (office_law3)
- **Addresses:** 0x55a9bc OpenPersonSelectionIfValid, 0x55a224 harvest core.
- **Reason:** **no live caller** of `GesetzOpenPersonSelectionIfValid` (nor any other
  office_law3 function) in `src/` — the whole Gesetz-selection subtree is not in the
  live tree. The selection WINDOW (radio-group widget pump) is the GUI boundary
  (wave-19 DEFERRED). Installing `runPersonSelection` → `GesetzHarvestSelectionCandidates`
  would fill an output buffer with no live consumer; returning a fabricated selection
  without the GUI would be a cheap analogue (rule 8). Left unwired.

### D4. Recruit candidate source — RunCandidatePickWindow ← RecruitCollectNearbyCandidates
- **Addresses:** 0x55db0c RunCandidatePickWindow, 0x55d530 CollectNearbyRecruitCandidates.
- **Reason:** `RunCandidatePickWindow` (personnel_gui.cpp) is a cutscene/widget-coupled
  dialog SHELL — the recruitment/offer/pick/confirm windows are explicitly the SDL/
  Vulkan GUI boundary (wave-19 recruit doc + sim/recruit.h list them as DEFERRED).
  Its caller is internal and the shell is not reached from the live spine. Binding the
  candidate source requires the GUI frame-loop modal, which is unreconstructed.

### D5. Building OpenUpgradeWindow charge — Building_ComputeUpgradeChargeAmount (0x50f7c0)
- **Reason:** the deterministic charge math + node classifier are reconstructed and
  take their hooks as direct args, but the FULL 0x50f7c0 is a modal frame-loop
  (RunFrameLoop + Dialog_ShowMessageBox + command enqueue) — unreconstructed GUI.
  building_options_menu.cpp only maps a click to the `kOpenUpgradeWindow` action
  enum; it does not execute the window. No live caller of the charge core exists
  until the modal window is reconstructed; the core is reachable for that handler.

### D6. Privilege panels SET B + Gesetz UI providers
- **Reason:** W19-PRIV-B created NO wire installer (its panels supply decision logic
  a host's privilege hook calls; the SET-A adapter returns 0 for SET-B ids =
  unchanged inert behaviour). A live provider binding the command queue / live person
  arrays / Form loop to both sets is the documented next step. Left unwired (no
  fabricated provider).

---

## Cross-wave build note (not my files)

The full `guild` library was RED at wave start from two **untracked, in-flight
sibling-wave files** unrelated to this task:
- `src/sim/command_leaves.{cpp,h}` — an orphan (no live caller) cluster that failed
  to compile: missing `#include "sim/actionqueue.h"` for `RunActionOrFree` (0x406a00)
  + a `<functional>` include-ordering symptom. I added the single missing include
  (a minimal build-unblock matching the owning wave's evident intent — their newer
  in-tree revision already had this include) so `libguild.a` links for verification.
  The file is otherwise untouched and remains untracked.
- `tests/unit/skycam_wave20_test.cpp` — uses `#include "framework/test.h"` (wrong
  path; every other test uses `"tests/framework/test.h"`). Untracked, not mine, not
  built by my verification. Left as-is (the owning wave's bug).

Both predate and are independent of this wave's changes (verified by stashing my
edits — the breakage persists). My owned changes add no ODR clashes and the `guild`
library builds clean.

## Verification

`guild` builds clean. 28+ test suites across every touched area pass with 0 failures,
including the brief-flagged e2e goldens (no shift): playable_flow_e2e (154),
real_scene_driver_e2e (0), newgame_apply (142) / _e2e (23), newgame_result_itest
(23), app_full_wired_playthrough_e2e (1), app_real_run_e2e (0), app_real_boot_e2e
(0) / _edge (52). Headless behaviour is preserved by every bind — the wired logic
diverges only with real data/records installed, exactly as the original.
