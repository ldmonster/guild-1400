# Wave-19 — Meister/master NPC AI decision calculators (`src/sim/ai_meister`)

**Agent:** W19-AIMEISTER · **Date:** 2026-06-16 · **MCP:** live (`gilde.exe`, imagebase 0x400000)

Reconstructs the per-frame **Guild-Master AI "brain"** branches — the largest single
game-logic gap in the wave-18 coverage audit (the "Meister/needs AI scoring" cluster).
These are dispatched once per AI-controllable Meister per evaluation tick from
`VIBE_Ai_EvaluateMeister` (0x4533a8; dispatch classifier already in
`src/ai/aiplayer.cpp` `ClassifyMeisterRoutine`).

## Reconstructed functions (1:1 from the decompile)

| addr | name | reimpl | file |
|------|------|--------|------|
| 0x457440 | VIBE_Ai_CalcMeisterDiebe | `CalcMeisterDiebe` | ai_meister_calc_diebe.cpp |
| 0x454f50 | VIBE_Ai_CalcMeisterFarming | `CalcMeisterFarming` | ai_meister_calc_farming.cpp |
| 0x455cd8 | VIBE_Ai_CalcMeisterWache | `CalcMeisterWache` | ai_meister_calc_wache.cpp |
| 0x4588d0 | VIBE_Ai_CalcMeisterAmbush | `CalcMeisterAmbush` | ai_meister_calc_wache.cpp |
| 0x4569a8 | VIBE_Ai_CalcAngriff | `CalcAngriff` | ai_meister_calc_angriff.cpp |
| 0x45e350 | VIBE_MeisterAi_EquipStaffWeapon | `EquipStaffWeapon` | ai_meister_equip.cpp |

### Shared MeisterAi sub-planner FULL bodies (loop drivers; callees of the above)

| addr | name | file |
|------|------|------|
| 0x45a62c | MeisterCollectStorageItems | ai_meister_equip.cpp |
| 0x45c10c | MeisterGatherRequiredItems | ai_meister_equip.cpp |
| 0x45bd68 | MeisterReserveWorkstationItems | ai_meister_equip.cpp |
| 0x45ba84 | MeisterCheckWorkstationCapacity | ai_meister_equip.cpp |
| 0x45c670 | MeisterHireStaff | ai_meister_subplanners.cpp |
| 0x45d2ac | MeisterTrainStaff | ai_meister_subplanners.cpp |
| 0x45df7c | MeisterFlagIdleStaff | ai_meister_subplanners.cpp |
| 0x45e71c | MeisterCollectTransporters | ai_meister_subplanners.cpp |
| 0x45f1e4 | MeisterTradeManageStorage | ai_meister_subplanners.cpp (outer scan; inner restock DEFERRED, see below) |
| 0x45cfac | MeisterFillAiSlots (full body; core = `AiSlotDeficit`) | ai_meister_subplanners.cpp |
| 0x45c9ac | MeisterRenovateBuilding (full body; core = `RenovateRoomBudget`) | ai_meister_subplanners.cpp |
| 0x45e12c | MeisterFindFreeStaffSlot (full body; core = `FreeStaffSlotCount`) | ai_meister_subplanners.cpp |
| 0x4599f0 | MeisterAssignWorkstations | ai_meister_calc_farming.cpp |
| 0x45d4c4 | MeisterCancelMatchingTasks (cores = `CancelTaskFlagMask`/`TaskMatchesOrder`) | ai_meister_passes.cpp |
| 0x45d618 | MeisterDispatchOrders (core = `DispatchOrderCount`) | ai_meister_passes.cpp |
| 0x45379c | MeisterAssignIdleWorkers (core = `IdleWorkerShouldQueue`) | ai_meister_passes.cpp |

The pure decision **cores** (`AiSlotDeficit`, `RenovateRoomBudget`, `FreeStaffSlotCount`,
`CancelTaskFlagMask`, `TaskMatchesOrder`, `DispatchOrderCount`, `IdleWorkerShouldQueue`,
`ProductionSlotShuffle`, `ResourceRestockTarget`) already lived in
`src/sim/meister_mgmt_recon.{h,cpp}` from wave-18 — **reused, not redefined**. This wave
adds the FULL loop-driver bodies that wrap them.

## Module layout (new)

- `src/sim/ai_meister.h` — public API: the 6 Calc functions + all sub-planners +
  `MeisterCommand`/`MeisterCmdSink` (captured command boundary), `MeisterAiLeaves`
  (engine-leaf hook table), the city-tile danger grid + work-order/stock scratch globals,
  `DispatchMeisterCalc` (the EvaluateMeister handoff), `MeisterDispatchArgs`.
- `src/sim/ai_meister_internal.h` — shared raw little-endian record accessors
  (`rd*`/`wr*`), the **64-bit-safe pointer-column handle model** (`rdptr`/`makeObjHandle`/
  `makePersonHandle`/`resolveHandle`), record field offsets, bounded type-def table reads,
  city-tile helpers, byte-exact FP constants.
- `src/sim/ai_meister_core.cpp` — globals, the type-def base pointers + counts, the
  `DispatchMeisterCalc` dispatch shim, `ResetMeisterAiScratch`.
- 6 body TUs (one per cluster, listed above).

## Data model & fidelity decisions

- **Records by raw byte offset.** The originals address `g_persons` (536-stride),
  `g_personIds`, `g_objects` (169-stride), the building-type def tables (589/65-stride)
  and the Meister's own person record by raw offset. The typed `Person`/`ObjectRec`
  structs (sim/types.h) model only a few fields, so all AI field touches go through
  `rd*/wr*` at the exact decompiled offsets (matches the rest of the codebase).
- **Pointer columns → 64-bit-safe handles.** The 32-bit binary stores *record pointers*
  in some columns (employer rec @Person+0x16C, action-object @+0x184, building rec
  @Meister+0x16C) and dereferences them. A native 8-byte pointer does not fit the 32-bit
  column (and adjacent columns overlap), so — per the index model `sim/entity.h` already
  uses — these columns hold a HANDLE (`1+objIdx` / `0x40000000+personIdx`, 0==null)
  resolved by `rdptr`/`resolveHandle`. The dereferences stay byte-faithful, width-correct
  on any host.
- **Command queue = surfaced boundary (rule 8).** Every Calc that "acts" builds the
  248-byte command record (`VIBE_Light_SetGrayColorThunk(0,248,&buf)` + field writes +
  `QueueRequestSlotReset28`/`EnqueueCmd15`/`QueueRequest17`). The live lockstep
  `CommandQueue` needs session context, so — exactly as the sibling
  `aiaction_dispatch_ai_recon2.h` (`RequestPacket`) — we capture each emit into a
  `MeisterCommand` pushed to `g_meisterCmdSink`. The live bridge drains the sink into the
  real queue. cmdType bytes recovered per site: 6 hire, 9 harvest, 18 fill-slot,
  19 train, 22 free-staff-slot, 28 renovate, 2 transport/upgrade-item, 60 burgle,
  64 spy, 67 patrol, 72 ambush-direction, 73 attack, 97 default-patrol, 98 ambush-target,
  100 escort, 101 countryside-patrol.
- **Engine leaves = hook table (rule 8).** ~35 leaves the brains call
  (`worldToCityTile`, `securityLevel`, `computeAssetWorth`, `sumCurrencyHeld`,
  `marketPrice`, `relationMatrix`, `heFindFirst/Next`, `queryFind/Next`,
  `changePlayerAction`, `amtFind*`, …) are modeled in `src/` with divergent typed/virtual/
  fn-ptr APIs and several need engine context unreachable from a raw-record tick. Per the
  established sibling pattern (`world/event5.h`, `sim/npcaction10.h`) they are surfaced as
  `MeisterAiLeaves` raw-record function pointers the live bridge wires; null hooks take the
  original's null/zero-return path. NEVER faked.
- **City-tile danger grid** (`word_12349A0/A2`, `byte_12349A4`): the 8×8×24 patrol/ambush
  scoring table — a genuine engine input, modeled as `g_cityTileGrid`, zero-init, populated
  by the live engine. Score = `dangerA + dangerB*0.125`.
- **FP:** scores computed in float/double exactly as typed; `<1.0f`/`&0x7FFFFFFF==0` tests
  reproduced; float→int via `(int)` truncation (`VIBE_Coord_ConvertX`). Constants
  byte-verified via `get_bytes`: 0.125 (619528/619780/6198A8), 0.5 (619530/619788),
  0.01 (619538/619790/6198B0), 8.0 (619540/61979C), 4.0 (619798), 0.85
  (619770/6193F8/619898), 0.75 (619778/6198A0).

### Correction to wave-18 pure core (rule 1)
`RenovateRoomBudget` constants in `meister_mgmt_recon.{h,cpp}` were wrong (comments said
0.5/0.0/0.1). Byte-verified `get_bytes @0x619940/44/48` = `0x3BA3D70A / 0x43000000 /
0x3F400000` → **0.005f / 128.0f / 0.75f**. Corrected `kRenovWorthScale`/`kRenovWorthFloor`/
`kRenovCashScale` and the dependent tests (`meister_mgmt_recon_test`, +192 checks still
green). This was a genuine 1:1 fidelity bug, not a convenience change.

## Wiring (rule 13)

`DispatchMeisterCalc(routine, meisterRec, attackBudget)` (ai_meister_core.cpp) is the
handoff for `VIBE_Ai_EvaluateMeister`: it switches the `guild::ai::MeisterRoutine` chosen
by `ClassifyMeisterRoutine` onto the matching Calc (Farming/Wache/Diebe/Ambush; the
Production/Bank routines stay with their own planner module). `CalcAngriff` is invoked
internally by Wache/Diebe/Ambush and also exposed for direct dispatch.
**One-line handoff for the bind site** (`src/ai/aiplayer.cpp`, not owned here): after
`ClassifyMeisterRoutine`, call `guild::sim::DispatchMeisterCalc((int)routine, meisterRec,
attackBudget)` with `g_meisterLeaves`/`g_meisterCmdSink`/`g_meisterGameTime` set by the
engine bridge.

## Tests

7 new golden unit suites (synthetic deterministic NPC/person/object/tile state, injected
leaf hooks, captured command sink, seeded RNG) + the corrected wave-18 core suite —
**all green:**

| suite | checks |
|-------|-------:|
| AiMeisterEquip | 51 |
| AiMeisterAngriff | 39 |
| AiMeisterWache (Wache+Ambush) | 46 |
| AiMeisterDiebe | 37 |
| AiMeisterSub (sub-planners) | 57 |
| AiMeisterPasses (cancel/dispatch/idle) | 54 |
| AiMeisterFarming (Farming+AssignWorkstations) | 39 |
| MeisterMgmtRecon (wave-18 cores, constants corrected) | 193 |
| **total** | **516, 0 failures** |

`guild` static lib builds clean; all 7 ai_meister test executables build, link, and pass.

## Reconstructed vs genuine leaves

- **Reconstructed (this wave):** the 6 Calc brains + 16 sub-planner full bodies + the
  EquipStaffWeapon item flow + the work-order/stock scratch state machine + the city-tile
  patrol/ambush scoring + the command-record construction for every action. All control
  flow, RNG draw order, integer wraparound, FP scoring, and per-tick flag-byte state
  machines are 1:1 with the decompile.
- **Genuine leaves (surfaced via MeisterAiLeaves hooks, NOT reconstructed here — they are
  large independent subsystems already modeled elsewhere in `src/` with their own APIs):**
  GameObject/Person query+iterate, He handler list, market price / cached price,
  security level, asset worth / currency held, relation matrix, world-to-tile,
  weapon-slot compat, avatar lookup, profession sync, inventory capacity, wage,
  ChangePlayerAction, Amt office placement, animal-target-busy.

## Deferred (documented; rule 8 — not faked)

- **`MeisterTradeManageStorage` inner restock loop** (0x45f1e4): needs
  `Inventory_GetSlotCapacity`/`CollectWorkstationSlots`, `Building_LookupCachedMarketPrice`,
  the delta-packet emitters, and a second scene-node arg absent from the public signature.
  The outer stock-scan (flag gates, type-22 half-capacity adjust, "Morsches Holz" skip) is
  reconstructed; the inner restock + the `TradeGeneral` (0x4614d0) path are left to the
  existing `meister_economy_recon_planners` module.
- **Farming DispatchOrders / AssignIdleWorkers call sites:** the loop drivers are
  reconstructed in `ai_meister_passes.cpp`, but the two CalcFarming call sites that invoke
  them are marked DEFERRED in `ai_meister_calc_farming.cpp` pending the full work-order row
  wiring (the `DispatchOrderCount` core is used for the count there).
- **`MeisterDispatchOrders` mode-40 Amt-slot sub-scan** + `Building_FindWorkProductObject`:
  not in the leaf table; the surrounding loop runs, those sub-reads are skipped (documented
  at the call sites).
- **`loc_5CB930`** (farming PFLANZBAR strstr): IDA decompile returned null; reconstructed as
  an inline single-char string search from the disasm (behavior-identical, not byte-confirmed).

## Out-of-scope note (not my files)

A pre-existing ODR clash between `src/sim/building_type.cpp` and
`src/sim/building_lifecycle.cpp` (both define `BuildingType_GroupFromCode` and
`Building_ClassifyTypeFlag`) blocks some unrelated test targets (e.g. `app_wiring2_test`).
Neither file is part of this module; the ai_meister library + tests are unaffected. Flagged
for the owning wave.
