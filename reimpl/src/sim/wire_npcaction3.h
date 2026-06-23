#pragma once
// wire_npcaction3 — wires the THREE remaining late-batch NPC bridge tables
// (NpcAction11Hooks, NpcAction12Hooks, NpcMarketHooks) into their REAL
// reconstructed cross-cluster leaves (rule 13). Before this installer nothing in
// the live call tree ever called SetNpcAction11/12Hooks / SetNpcMarketHooks, so
// all three batches of step coroutines / the market supervisor ran fully inert
// (every He free/find/emit a no-op against the zero-initialised default table).
//
// This is glue ONLY — no module logic. It follows the established real-wiring
// pattern (cf. sim/real_hooks3.cpp and sim/wire_charaction.cpp): every command
// emit stages onto the SAME shared real CommandQueue (RealCommandQueue(),
// real_hooks.h) and every handler free / find scan operates on the SAME shared
// real HandlerTable (RealHandlerTable(), real_hooks3.h) the NpcAction/CharAction
// bridges already share. The three tables compose with InstallRealSimHooks3.
//
// Bound leaves (a genuine reconstructed callable target exists):
//   * VIBE_He_FreeHandlerEntry           @0x4c6144 -> HandlerTable::FreeHandlerEntry
//   * VIBE_He_FindFirstHandlerByFilter   @0x4c63f8 -> HandlerTable::FindFirstHandlerByFilter
//   * VIBE_He_FindNextMatchingHandler    @0x4c6278 -> HandlerTable::FindNextMatchingHandler
//       (the findConflictingHandler / scanFilterHasForeignMatch adapters replay the
//        originals' filter-by-kind scan + +180/+172 byte-offset compare loop 1:1)
//   * VIBE_Person_FindRecordById         @0x58bc6c -> PersonFindRecordById (entity.h)
//   * VIBE_GameObject_ResolveEntityById  @0x583b44 -> GameObjectResolveEntityById (entity.h)
//   * VIBE_Math_RandomModulo             @0x58b89c -> util::RandomModulo
//   * VIBE_Money_MultiplyByRate          @0x..(MoneyMultiplyByRate, session_init)
//   * VIBE_Building_SumFlaggedSlotsWorth @0x5913e0 -> Building_SumFlaggedSlotsWorth
//   * VIBE_Building_ComputeSlotYield     @0x584ec8 -> Building_ComputeSlotYield
//   * VIBE_BuildingType_GroupFromCode    -> BuildingType_GroupFromCode
//   * VIBE_BuildingType_ComputeRankWithinGroup -> BuildingType_ComputeRankWithinGroup
//   * VIBE_Util_InitAndShuffleDwordArray @0x58ba98 -> util::InitAndShuffleDwordArray
//   * command emits (opcode 25/27/17/58/59/53/71/72/77/84/44/15) -> the real
//     reconstructed builders on the shared queue.
//
// Leaves left INERT (no clean reconstructed target — listed, never faked, rule 8):
//   - personQueryBegin / gameObjectQueryFind / gameObjectIterFirst / gameObjectIterNext
//     / buildingFindWorkProduct / amtFindRecordByKey: the real PersonQueryBegin /
//     GameObjectQueryFind (entity.h) consume a structured (op,value) filter-array;
//     the hook surface is bare ints whose op/value pairing the originals build from
//     a stack buffer the decompile does not unambiguously expose — binding it would
//     require GUESSING the pairing (rule 8).
//   - buildingFindById (VIBE_Building_FindById): not reconstructed as a standalone
//     callable (only referenced in comments across the tree).
//   - buildingFindOfficeStorage: the reconstructed Building_FindOfficeStorage
//     returns an i16 object handle (entity id), not the opaque record* the hook
//     hands back to the step machine; the record-vs-id shapes do not match without
//     a further (unreconstructed) id->record resolve. Inert.
//   - the opaque record FIELD readers (objId/markerWord/kind/rank/equipFlags/
//     field101/field364/familyDur/spouseId/equipState/field) and the per-city grid
//     arrays (cityId/cityMarker/cityKind/cityPersonRecord/cityIdShifted/
//     cityAllianceEligible/cityRecord — word_12CE910/byte_12CE912/dword_12CE914/
//     byte_12CEA76): process-global tables / ambiguous byte offsets, not modeled as
//     standalone callable leaves (same reason real_hooks.h leaves the NpcTarget
//     readers null). Inert.
//   - relationLookup (VIBE_Relation_LookupMatrixEntry), computeOfficeRank
//     (VIBE_Person_ComputeOfficeRank), computeTotalWealth (Person_ComputeTotalWealth),
//     lookupMarketPrice (Building_LookupCachedMarketPrice): only abstracted through
//     other hooks elsewhere, no standalone reconstructed definition. Inert.
//   - evaluateViolation (VIBE_Gesetz_EvaluateViolation), selectBestRecursive /
//     tryRangedAttack / combatPickActiveTarget / aiLoadBuildingGraphic /
//     animalBuildWanderPath / heightmapWorldToTile / charActionInsert: AI / physics /
//     animation leaves with no clean reconstructed target. Inert.
//   - render / voice / panel / dialog UI leaves (rules 3-5 backend seam):
//     sendEntity / sendQuickjump / runOfficeOverviewWindow / panelShowAlliance /
//     eventPanelCreate/Destroy / playExamVoice / renderExamResult / dialogOpenBuilding
//     / changePlayerAction / createSoundAction. Inert.
//   - requestSlotReset28 / requestState22 / queueTransform64 / beginDelta /
//     appendCopiedField / appendRawField / buildingActionStart/End / requestState23 /
//     requestBuildOp84(npcaction12 key form): the reconstructed builders carry
//     PendingState / DeltaWriter / SlotResetScratch session-global staging the hook
//     shape collapses away (the hooks pass a bare slot image / no args). Faithfully
//     binding them needs the process-global packet-staging state, which is not
//     modeled as a free leaf here. Inert.
//   - npc_market: marketEnabled (dword_6477A4) / marketIndex (byte_6477A1) /
//     treasury (Person_GetCurrencyAmount) / enqueueTopUp / slotCount / slot /
//     commitSlot / slotIsActiveWorkable / effectiveStock / worldSyncSuppressed
//     (word_63C740): process-global market state / Inventory_GetEffectiveStock (not
//     reconstructed). The pricing RULE (MarketRecomputeSlot) is exercised directly
//     by its own golden tests; here only computeYield / queueRequest17 /
//     freeHandlerEntry have real targets. Inert otherwise.
//
// SEED-FROM-DEFAULTS: each table is copied from its module getter (the inert,
// zero-initialised default) and only the bindable fields are overridden, so unbound
// fields keep their inert default exactly as the bridges document ("nullptr installs
// an inert default"). Composes with InstallRealSimHooks3 (shared He pool + queue).
namespace guild::sim {

// Install the real bindings into NpcAction11Hooks, NpcAction12Hooks and
// NpcMarketHooks. Idempotent; the three tables are process-lifetime storage the
// global hook pointers reference.
void InstallRealNpcAction3Wiring();

} // namespace guild::sim
