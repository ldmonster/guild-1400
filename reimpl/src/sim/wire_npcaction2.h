#pragma once
// wire_npcaction2 — wires the three NpcAction "candidate-scoring / state-machine"
// LEAF bridges of the NpcAction cluster (gilde.exe) into their real reconstructed
// siblings:
//
//   * NpcAction8Hooks  (sim/npcaction8.h)  — AI action-evaluator score/search family
//   * NpcAction9Hooks  (sim/npcaction9.h)  — combat / economy / law / recruitment
//                                            evaluators
//   * NpcAction10Hooks (sim/npcaction10.h) — twelve per-NPC He-record coroutines
//                                            (credit / kidnap / fire / exam / wander …)
//
// Before this installer NOTHING in the live tree set any of the three tables, so
// every evaluator / coroutine ran against the fully INERT default (every query
// absent, every RNG draw 0, every command emit a no-op). The installer SEEDS each
// table from its module's complete inert default — `SetNpcActionNHooks(nullptr)`
// resets the module pointer to that default, `GetNpcActionNHooks()` copies it —
// then overrides ONLY the fields that have a byte-faithful, signature-compatible
// real reconstructed target.
//
// WHAT IS BOUND TO A REAL RECONSTRUCTION
//   RNG (all three):
//     * randomModulo      -> util::RandomModulo        (VIBE_Math_RandomModulo  0x58b89c)
//     * randomFloatScaled -> util::RandomFloatScaled   (VIBE_Math_RandomFloatScaled 0x58b910)
//   building-type table (8 / 9):
//     * buildingRankWithinGroup -> BuildingType_ComputeRankWithinGroup (0x58a560)
//   He handler pool (9 / 10) — the SHARED real pool RealHandlerTable() owns:
//     * heFindFirstHandlerByFilter -> HandlerTable::FindFirstHandlerByFilter (0x4c63f8)
//     * heFindNextMatchingHandler  -> HandlerTable::FindNextMatchingHandler  (0x4c6278)
//     * freeHandlerEntry           -> HandlerTable::FreeHandlerEntry         (0x4c6144)
//   command builders (8 / 10) — onto the SHARED real RealCommandQueue():
//     * queueRequest17  -> QueueRequest17        (0x49465c)
//     * queueEntity29   -> QueueRequestEntity29  (0x4949c4)
//     * requestCoord27  -> QueueRequestCoord27   (0x494878)
//     * queueRequest16  -> QueueRequest16        (0x494630)
//     * requestBuildOp71-> RequestBuildOp71      (0x495508)
//     * requestBuildOp77-> RequestBuildOp77      (0x4956c8)
//     * requestBuildOp90-> RequestBuildOp90      (0x495b58)
//     * requestBuildOp91-> RequestBuildOp91      (0x495b7c)
//     * queueSlotReset28-> QueueRequestArgs25    (0x494810, opcode-28 slot-reset flavour)
//   packet ACK gate (10):
//     * packetStatus    -> CommandQueue::GetPacketStatusById (0x4939d4)
//
// WHAT STAYS INERT (and why) — each a real-reconstructed target that does NOT exist
// or carries AMBIGUOUS record semantics (rule 8: no cheap analogue), reported below
// in the per-bridge notes inside wire_npcaction2.cpp. In brief:
//   * the AiScore relation kernels, SelectBestRecursive / EvalMeisterTarget /
//     TryGroupAttack / FindRivalToConfront / FindOpponentBuilding (ai/ env-hook
//     planners, not byte-faithful standalone leaves over native records),
//   * the polymorphic record FIELD READERS (objId/markerWord/kind/recRank/
//     hasCharacter) and id->record resolves (findPersonById/resolveEntity/
//     gameObjectQueryFind/personQueryBegin/cityPersonRecord/cityId/cityAuxRecord):
//     the SAME reader is applied to Person (id@+4) AND Object (id@+1) records, so
//     no single byte offset is faithful — exactly the ambiguity real_hooks3.h
//     leaves inert for NpcAction3/4,
//   * He_SumPlayerHandlerValues (needs the unported 45-byte He entity table —
//     no real shared instance exists; an empty table is inert-equivalent),
//   * wealth/currency leaves (Person_SumCurrencyHeld / ComputeTotalWealth /
//     GetCurrencyAmount are env-virtual, not free functions),
//   * text/voice/history/event-panel renders, the form-event globals, and all
//     remaining economy/inventory/relation/violation leaves: not reconstructed
//     as callable free leaves.
//
// All command-emit bindings enqueue onto the SAME shared real CommandQueue the
// CharAction / real_hooks waves use (RealCommandQueue(), real_hooks.h); the He-pool
// bindings operate on the ONE shared HandlerTable (RealHandlerTable(), real_hooks3.h),
// so a test can compose InstallRealSimHooks3() + InstallRealNpcAction2Wiring() and
// inspect one queue + one He pool. Idempotent.
namespace guild::sim {

// Install the real NpcAction8/9/10 leaf wiring into the three global hook tables.
void InstallRealNpcAction2Wiring();

} // namespace guild::sim
