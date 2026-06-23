# Fixups — wave 2, agent W2-C (fidelity divergences + one rule-13 wiring gap)

Date: 2026-06-11. Four wave-1-diagnosed divergences closed against the binary
plus one pending-parent wiring. IDA MCP was offline for this wave; every fix
below was diagnosed against the decompile by a wave-1 agent and is restated
from that diagnosis (no fresh decompilation was needed — none of the fixes
turned out ambiguous).

## 1. `SceneClassifyMeisterRecords` — first-match latch vs. overwrite (0x504ce0)

* **File:** `src/sim/cutscene_misc5.cpp` (+ doc in `cutscene_misc5.h`).
* **Diverged:** the kernel latched the FIRST kind-12 match per anchor
  (`if ((found & bit) == 0)` guards) and never updated it again.
* **Binary:** `VIBE_Scene_SyncMeisterBuildings` @0x504ce0 stores the candidate
  UNCONDITIONALLY on every kind-12 match (`v4 = rec; v5 |= 1` @0x504d45/47 for
  sub-state 0, `v3 = rec; v5 |= 2` @0x504e09/0b for sub-state 1) and the scan
  loop only stops once BOTH anchors were seen (`while (.. && v5 != 3)`). So the
  LAST qualifying match before the both-found point wins; records after that
  point never update an anchor, and if the second kind never appears the scan
  runs the full array and the overall last match wins.
* **Changed:** removed the `(found & bit)` guards; the stop condition
  (`i < count && found != 3`, checked at loop top) is unchanged. Comments in
  .cpp/.h restated. The verbatim caller-flow scan in
  `sim/buildingtype_callers.cpp` (which already had the correct semantics) is
  untouched; its header NOTE about the kernel deviation is updated.
* **Tests:** `tests/unit/cutscene_misc5_test.cpp` — existing
  `ClassifyMeisterRecords` vector kept (its expected anchors are identical
  under both semantics; comments corrected) + 3 new tests:
  `ClassifyMeisterOverwritesUntilBothFound` (last sub-0 before both-found wins,
  post-stop record ignored), `ClassifyMeisterOverwritesAnchorB` (symmetric),
  `ClassifyMeisterLastMatchWinsWhenNeverBothFound` (full-scan overwrite).
  Suite: 82 checks, 0 failures.

## 2. Relation grid A — 192-byte stride / unsynchronized second store (0x5942fc / 0x49818C)

* **Files:** `src/world/relation.{h,cpp}`, `src/sim/command_apply6.{h,cpp}`.
* **Diverged:** `world/relation.h` modeled grid A with a 192-BYTE row stride
  and a separate 9 KB backing store (value byte at `192*a + b + 3`) — a
  misread of the decompile's `dword_123D6CD[192*a]`: that is a DWORD index, so
  a row is 4*192 == **768 bytes**, and the `>> 24` (arithmetic) extracts the
  signed byte at `0x123D6CD + 768*a + b + 3` == `0x123D6D0 + 768*a + b`. The
  binary has ONE 768x768 signed-byte grid A @0x123D6D0 shared by the reader
  (`VIBE_Relation_LookupMatrixEntry` @0x5942fc) and the opcode-0x1B mutation
  handler (`VIBE_Command_ExComputeObjectCoords` @0x49818C); the reimpl had two
  unsynchronized models with two different layouts.
* **Changed:**
  - `world/relation.{h,cpp}`: `kRelationRowStride = 768`, `kRelationDim = 768`,
    `kRelationBytes = 768*768`; `RelationGet(a,b)` returns the signed byte at
    `768*a + b` (provably bit-identical to the dword`>>24` read — derivation in
    the header), `RelationSet` writes that byte. Self (a==b) stays the
    hard-coded 127, never stored.
  - `sim::RelationState::matrixA` (command_apply6.h) is now an `i8*` ALIAS of
    `world::g_relationMatrix` (bound in the ctor; `static_assert`s pin
    `kRelCells == world::kRelationBytes` and
    `kRelPersons == world::kRelationRowStride`), so the 0x1B handler, the
    0x5942fc reader and every play-slice consumer address the SAME memory with
    the SAME `768*i + j` arithmetic — exactly one global, like the binary.
    Grid B (`byte_1333110`) stays the single `matrixB` vector (only the 0x1B
    handler touches it).
* **Callers checked (grep, all coherent):** the play slices
  (`slice_*/playthrough/full_session/multi_city/save_roundtrip/playable_slice/
  sdl_session/world_digest`) memset/fold `g_relationMatrix` via `sizeof` —
  size-agnostic; `world_law(_e2e)_test`, `city_satisfaction_grid_itest`,
  `world_location_e2e_test`, `slice_combat*` use `RelationGet/Set` with person
  indices < 768; `newgame_apply(_e2e)_test` reads `rel.A/rel.matrixA`. All
  pass unchanged (the e2e `201 % kRelationDim` vector now maps to row 201
  instead of 9 — self-consistent within the test).
* **Tests:** covered by the existing golden suites over both APIs, which now
  exercise the unified store: `world_law_test` 117, `world_law_e2e_test` 22,
  `sim_command_apply6_test` 113, `newgame_apply_test` 118 (its
  `RelationPacketsApplyThroughBatch6` now proves the 0x1B mutation is visible
  through the SAME memory the slices fold), `city_satisfaction_grid_itest`
  105 — all 0 failures.

## 3. `newgame_apply.h` stale "named gap" comment

* **File:** `src/play/newgame_apply.h` (comment-only).
* **Stale:** the NAMED GAPS list still claimed the opcode-27 (relationship)
  apply handler was "not yet reconstructed in any command_apply batch / six
  packets dispatch to the no-op slot".
* **Reality** (per `progress/newgame-apply.md`, closure 2026-06-11): the
  handler IS `sim::ExComputeObjectCoords` (gilde.exe 0x49818C, jump-table slot
  27 == opcode 0x1B, batch 6), resolving persons in the live `sim::g_persons`;
  with `RegisterApplyHandlers6` on the queue the six mode-0/delta-127 packets
  genuinely saturate the relation grid.
* **Changed:** the bullet was moved out of NAMED GAPS into a "CLOSED FORMER
  GAP" paragraph pointing at the handler + both progress notes.

## 4. `ExSellObjekt` — unfaithful `g_lastTradeId = moved` overwrite (0x496b90)

* **File:** `src/sim/command_apply6.cpp` (+ doc in `command_apply6.h`).
* **Diverged:** after the faithful LABEL_58 latch
  (`dword_631290 = *(v55+2)` — the dest stock NODE id, reconstructed 1:1 in
  `buildingtype_callers.cpp::Sell_EnsureDestStorageNode`, which
  `TradeSellObjektResolve`'s commit path runs), the handler OVERWROTE
  `g_lastTradeId` with the modeled `moved` quantity — a store the binary does
  not make.
* **Changed:** the second store is removed (`command_apply6.cpp`, the
  `ExSellObjekt` commit tail); the handler now never writes `g_lastTradeId` —
  the only writer on this path is the storage-phase latch. Banner + header
  comments updated to name the real latch site.
* **Tests:** `tests/unit/sim_command_apply6_test.cpp` —
  `SellObjektTransfersAndAcks` now pre-seeds `g_lastTradeId` with a sentinel
  and pins that the handler leaves it untouched under the inert storage phase;
  new `SellObjektKeepsStoragePhaseTradeIdLatch` installs a latching
  `SellStoragePhase` and pins that the phase's node-id latch (the LABEL_58
  value) survives the rest of the handler. The faithful real-phase latch
  itself stays pinned by `buildingtype_callers_test` (616/99/31/7777/4242
  vectors). Suites: 113 + 141 checks, 0 failures.

## 5. Rule-13 wiring: 0x533a54 NewGameSyncScene → `sim::Scene_SyncMeisterBuildings`

* **Files:** `src/app/session_init.cpp` (the sink), comment in
  `src/sim/buildingtype_callers.h`.
* **Gap:** `Scene_SyncMeisterBuildings` (reconstructed @0x504ce0, installed
  via `WireBuildingCallers()` / `InstallRealBuildingWiring()`) had no live
  parent at the InitOrLoadSession new-game sync point — the
  `SetupStep::NewGameSyncScene` emit (the 0x533fXX `Scene_SyncWorldOnEnter /
  SyncMeisterBuildings / SyncObjectHeights` block) was a pure recorded hook.
* **Changed:** `InitOrLoadSession`'s `SessionMode::NewSingle` branch now
  dispatches the REAL `sim::Scene_SyncMeisterBuildings()` immediately before
  emitting `NewGameSyncScene` (the same internal-dispatch pattern the module
  already uses for `GameInitWorldAndSounds` / the RNG seed). Its
  unreconstructed leaves stay routed through `BuildingCallerHooks` (inert
  defaults safe headless); `Scene_SyncWorldOnEnter` (0x50456c) and
  `Scene_SyncObjectHeights` (0x504e14) remain DEFERRED hook leaves of the
  session bootstrap. The other live parent integration (`src/play/
  sdl_session.cpp`) is owned by a concurrent agent and was NOT touched; that
  session drives the SAME `app::InitOrLoadSession` orchestration, so it
  inherits this wire with no handoff needed beyond this note.
* **Tests:** `tests/unit/app_session_init_test.cpp` — new
  `NewGameSyncSceneRunsRealMeisterSync`: seeds the live `g_persons` table from
  the `NewGameLoadCty` hook (the point where the .cty load would populate it)
  and pins that the bootstrap published the kind-12 sub-0/sub-1 anchors
  (`MeisterAnchorA/B` == dword_6498E8 / dword_6498EC[0]) and the kind-11 shop
  list. Suite: 67 checks, 0 failures (e2e `app_session_init_e2e_test` 19,
  `app_wiring_e2e_test` 13, `app_spine_e2e_test` 25 — all green).

## Full results

`cmake --build build` clean. Full tree run after all five fixes:
**679 unit test binaries + 730 integration/e2e binaries — 0 failures**
(guarded suites ran with `GUILD_GAME_DIR` pointing at the original game
assets). Stale cross-references updated in
`progress/command-apply-relation.md` and
`progress/buildingtype-bauplatz-recon.md` (this file is the closure record;
`progress/INDEX.md` deliberately untouched per the wave brief).
