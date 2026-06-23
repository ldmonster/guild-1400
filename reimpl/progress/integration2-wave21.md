# Wave-21 — INTEGRATION2 (W21-INTEGRATION2): wire remaining documented handoffs

**Agent:** W21-INTEGRATION2 · **Date:** 2026-06-16 · **MCP:** live (`gilde.exe`, imagebase 0x400000)

Rule-13 pass over the wave-20 DEFERRED items + the "reconstructed-but-NOT-wired"
coverage list. Each bind verified against the decompile (MCP) before wiring.
Excluded the sibling-owned files (gamelogic_recon, privilege_panels_b,
recruit_office, wiring.cpp privilege paths).

Owned files edited: `src/render/wire_scene_fx.{cpp,h}`,
`src/world/guildstate_recon.{cpp,h}`, `src/app/engine_init_app_recon.{cpp,h}`,
`tests/unit/wire_scene_fx_test.cpp`, `tests/unit/engine_init_app_recon_test.cpp`.

---

## WIRED (value-verified)

### 1. AI data DFN -> InitGuardState (D1 closed)  ✅
- **gilde.exe:** 0x4520d0 VIBE_GameLogic_InitGuardState **internally calls**
  0x468a40 VIBE_AiMethod_LoadDataFile (decompile-confirmed: the word-table init at
  0x452126.. is gated on `result = VIBE_AiMethod_LoadDataFile(...)`). The shipped
  read path (dword_63C7D8==0 branch) opens `"/gamedata/ai/ai_data.dfn"` `"rb"` via
  VIBE_Vfs_OpenFile @0x450bc8, runs BuildScoreTable, streams 61 x (record schedule)
  records into the catalog (the +80/+48/32 MemMove prev-mirror at 0x468f39), and
  returns 1 on a complete load.
- **Bind:** the reconstruction had split the leaf (`GameLogicInitGuardState(g,
  aiDataFileLoaded)`) from the AI-data load. Re-fused them along the original edge:
  - `world/guildstate_recon.{cpp,h}`: added `GuardStateGlobal()` — the shared
    in-tree mirror of the dword_B56xxx global block the original writes (one
    process-lifetime instance, so the InitGuardState edge has a live target).
  - `app/engine_init_app_recon.{cpp,h}`: new `RealGameLogicInitGuardState(h)` does
    the genuine 0x4520d0 flow: pull the DECOMPRESSED DFN bytes from a new
    `EngineInitHooks::aiDataDfnProvider` (the VFS+gunzip boundary), run
    `sim::AiNeeds_LoadDataFile` (== BuildScoreTable + the 61-record overlay, the
    reconstructed 0x468a40 read path), then `world::GameLogicInitGuardState(
    GuardStateGlobal(), loaded!=0)`. The `gameLogicInitGuardState` hook default is
    bound to this at the top of `VIBE_App_InitEngineAndScriptCommands` when the
    caller left it unset (recording/test hooks still override).
- **Headless faithful:** no provider (default) => empty bytes => load fails =>
  InitGuardState takes the original's failure path (guard table untouched, returns
  0; the cleared +0x04 stamp is still seeded via GameTime_Set(6,0,0)). A real
  backend installs `aiDataDfnProvider` = open `/gamedata/ai/ai_data.dfn` via the
  VFS + `guild::compress::Gunzip`.
- **Remaining gap (documented):** the LIVE spine's InitEngineAndScriptCommands
  (app_init.cpp, 0x528560) is an ABBREVIATED recon that omits the InitGuardState
  step entirely (it ends at scriptRegisterCommands). The detailed 0x528560 recon
  that DOES run InitGuardState lives in engine_init_app_recon.cpp and is exercised
  by its unit test, not `RealSubsystems`. So the edge is now reconstructed-to-
  reconstructed and live wherever that orchestrator runs; bringing the full step
  (+ a VFS provider + a catalog consumer) into app_init.cpp's spine is an
  app-owned change beyond this wiring task. The catalog produced still has no live
  consumer (`character_ai.cpp` does not read an AiNeedsCatalog) — also app/sim-owned.
- **Tests:** engine_init_app_recon (70 -> 83, +13: FullDfnProviderInitsGuardTable,
  NoProviderTakesFailurePath, ShortDfnTakesFailurePath,
  OrchestratorBindsRealDefaultWhenUnset). guildstate_recon (49), ai_needs (1312),
  ai_needs_e2e (1159) — all 0 failures.

### 3. Floor_ReloadTextures -> SetGammaTable edge  ✅
- **gilde.exe:** 0x5b9ef4 VIBE_Render_SetGammaTable. On a gamma change it
  TextureCache_Resets, records the new gamma, then `Floor_ReloadTextures(
  dword_64A028)` for the active floor and `Floor_ReloadTextures(dword_13ECF74[i])`
  for each non-zero world-floor record (decompile-verified). Each argument IS the
  32-bit floor-record pointer.
- **Bind:** `SetGammaTable` (render_recon_objlist.cpp) was already reconstructed
  and calls `ObjListHooks().floorReloadTextures` (an `int(*)(u32 floorHandle)` hook
  that NOTHING installed). The reconstructed body is `ReloadTextures(void* floor,
  const FloorTileAccess&)` in floor_reload.cpp. Added `FloorReloadTexturesAdapter`
  + the bind `ObjListHooks().floorReloadTextures = &FloorReloadTexturesAdapter` to
  `InstallRealSceneFxWiring()` (wire_scene_fx.cpp — already called from the live
  spine, wiring.cpp:737). The adapter restores the original's pointer-as-handle
  call and routes the floor tile fields through FloorTileAccess.
- **Headless faithful:** no live floor records / tile surfaces => an inert
  FloorTileAccess: the 8 slots x 3 mips iterate but every tile-surface lookup is
  null so no decode is attempted (returns 0 attempted) — the faithful behaviour for
  a SetGammaTable with no live floor. A null handle takes ReloadTextures' early-out.
- **Note:** SetGammaTable itself has no live caller yet (callers are
  Render_ApplyGfxSettings @0x56be58 / Render_ApplyFogAndLightFlags @0x5b04a8, not in
  the spine), so the install changes no boot/run golden — it connects the
  reconstructed pieces along the real edge for when SetGammaTable runs.
- **Tests:** wire_scene_fx (15 -> 22, +7: BindsFloorReloadTexturesEdge,
  SetGammaTableReachesFloorReloadEdge). floorgfx_recon (54), render_scene_floor
  (138), render_leaves3 (119), render_recon2 (72) — all 0 failures.

---

## DEFERRED (reason + address — left unwired, NOT faked)

### D-A. NpcAction_NotifyJoinLeaveGroup @0x4c9dec — NOT RECONSTRUCTED
- **Coverage-report claim was wrong:** the wave-20 "reconstructed-but-NOT-wired"
  row listed 0x4c9dec as having a body in src/sim/npcaction.cpp. It does NOT — the
  only src reference is the **DEFERRED list** in npcaction.cpp (line 319). There is
  no reconstructed body anywhere in src/ (verified by grep over the whole tree).
- **Callers (decompile-verified):** 0x4ca658 VIBE_NpcAction_TavernSocializeState
  (calls it twice: tag 0x6E657720 "new " and 0x65786563 "exec", with the resolved
  occupant entity v13 + seat entity v6) and 0x49cac4 VIBE_Command_ExUpdateBuildingLinks
  (3 sites). Both callers ARE reconstructed (npcaction10.cpp TavernSocializeState;
  command_apply5.cpp ExUpdateBuildingLinks).
- **The TavernSocializeState recon currently STUBS the two call sites** (npcaction10.cpp
  489/498) as `H->sendMessage(tavern_objId, 0)` — which does NOT reproduce the
  original (the original passes a FourCC + occupant entity + seat entity to a 2154-
  byte state machine, not a 2-arg message). Wiring requires the leaf to be
  reconstructed first (npcaction reconstruction work, not a wiring task). Per rule
  8, NOT faked. Hand-off to the npcaction reconstruction wave: reconstruct 0x4c9dec,
  then replace the two npcaction10.cpp stubs + the 3 command_apply5.cpp sites.

### D-B. Gfx_CrossFadeStep @0x41e814 — caller owned by sibling
- **Caller (decompile-verified):** the ONLY caller is 0x4139a8
  VIBE_GameLogic_Interactions (gamelogic_recon.cpp, the per-frame entity switch,
  line 270 `h.gfxCrossFadeStep(...)`). gamelogic_recon.cpp is owned by a sibling
  wave-21 agent (explicitly excluded). The leaf is reconstructed
  (command_leaves.cpp GfxCrossFadeStep + gamelogic_recon.cpp GfxCrossFadeStep);
  the bind belongs to the gamelogic_recon owner. Left to that agent.

### D-C. Building_OpenUpgradeTreeWindow @0x594100 — caller owned by sibling (sim/building)
- Reconstructed in sim/building.cpp; the upgrade-tree UI open click lives in the
  building/options modal subtree (sim-owned + GUI-coupled). Not in my owned files.

### D-D. String_MbsCompareN @0x609e30, String_WideCharToBytes @0x609510 — host CRT
- **Callers (decompile-verified):** purely CRT internals — VIBE_Crt_FindEnvVar,
  VIBE_Crt_WideStrByteLen, VIBE_Crt_FormatConversion, VIBE_String_WideToBytesBuffer.
  These are the MSVC-CRT runtime bucket (host C++ runtime satisfies them, per the
  coverage report's own boundary note). Not logic to wire. Left as host CRT.

### D2–D6 (from integration-wave20.md) — unchanged
- D2 object/anim per-frame, D3 Gesetz person-selection, D4 recruit candidate
  source, D5 building OpenUpgradeWindow charge, D6 privilege panels SET B — all
  owned by sibling waves / blocked on the GUI frame-loop modal boundary. No change.

---

## Cross-wave build note (not my files)

The full `guild` library was transiently RED mid-wave from a concurrent sibling
edit to `src/play/gamelogic_recon.cpp` (the InteractionRecord struct +
IGameLogicHooks were being extended by the gamelogic wave-21 agent; members
`flag76Nonzero/flag88Nonzero/.../stateFinalize/life/setLife` were referenced
before the struct landed). Verified independent of my changes: stashing my edits
and rebuilding shows the lib GREEN; the breakage appeared/cleared as that agent
wrote the file. My owned changes add no ODR clashes (grep-checked: GuardStateGlobal,
RealGameLogicInitGuardState, FloorReloadTexturesAdapter, aiDataDfnProvider are all
new unique symbols) and the `guild` library builds clean with them.

## Verification

`guild` builds clean. Suites run (0 failures): wire_scene_fx (22),
engine_init_app_recon (83), guildstate_recon (49), ai_needs (1312), ai_needs_e2e
(1159), floorgfx_recon (54), render_scene_floor (138), render_leaves3 (119),
render_recon2 (72), app_wiring2 (32), app_session_init (76), ai_recon_brain (72),
character_ai (27). e2e goldens (no shift): app_real_run_e2e (0), app_real_boot_e2e
(0), playable_flow_e2e (14), app_full_wired_playthrough_e2e (1). Headless behaviour
is preserved by every bind — the wired logic diverges only with real data/records
installed, exactly as the original.
