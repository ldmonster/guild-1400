#pragma once
// wire_npcaction1 — binds the three previously-inert NpcAction5/6/7 hook bridges
// (npcaction5.h / npcaction6.h / npcaction7.h) to their real reconstructed
// cross-cluster leaves (rule 13). Glue only — no module logic lives here.
//
// These three bridges sit on the VIBE_NpcAction_* dispatch family (the AI
// debug-command / NpcEvent-tail leaves @0x4d877c..0x576618). Each was reconstructed
// 1:1 but NEVER installed, so the dispatch ran against the modules' INERT defaults.
// This installer SEEDS each table from its module inert defaults (non-null stubs)
// and overrides ONLY the wireable fields — required because the NpcAction bodies
// (and the dispatcher) invoke several hook fields WITHOUT a null-check; a zeroed
// table would crash. Unbound fields keep their safe inert stub.
//
// COMPOSES WITH InstallRealSimHooks3(): NpcAction5_CountdownTickEntity /
// _ResolveTargetAndReset and NpcAction6_NotifyWanderPair reach the shared real
// He pool + the NpcLeafHooks (freeHandlerEntry / queueRequestEntity29 /
// findInventorySlot), which InstallRealSimHooks3 owns. Install that first.
//
//   ┌─ bridge ────────┬─ wired field ─────────┬─ real target (addr) ───────────────────────┐
//   │ NpcAction5Hooks │ resolveEntityField97  │ GameObjectResolveEntityById +97 (0x583b44) │
//   │ NpcAction6Hooks │ findRecordById        │ PersonFindRecordById            (0x58bc6c) │
//   │                 │ moneyMultiplyByRate   │ app::MoneyMultiplyByRate        (0x58f19c) │
//   │                 │ pickNeedAndClearGroup │ ai::PickRandomNeedAndClearGroup (0x58aea8) │
//   │                 │ pickNeedAndClearGroupB│ ai::PickRandomNeedAndClearGroupB(0x58b0cc) │
//   │                 │ pickFlagFromFourA/B   │ ai::PickRandomFlagFromFourA/B   (0x58b4e8/0x58b614)│
//   │                 │ requestBuildOp93      │ RequestBuildOp93                (0x495bd0) │
//   │                 │ queueRequest16        │ QueueRequest16                  (0x494630) │
//   │                 │ queueRequestEntity29  │ QueueRequestEntity29            (0x4949c4) │
//   │                 │ currentSeason         │ gui::text::SeasonFromYear       (0x583384) │
//   │                 │ wanderScanBegin/Next  │ HandlerTable::Find*ByFilter(1,0,53)        │
//   │                 │ personSlotByIndex     │ &g_persons[index] (word_12CE910[268*i])    │
//   │ NpcAction7Hooks │ findRecordById        │ PersonFindRecordById            (0x58bc6c) │
//   │                 │ mapTypeToCategory     │ Building_MapKindToCategory      (0x5878b0) │
//   │                 │ personTableBase/Cap   │ g_persons / kPersonCapacity                │
//   │                 │ adjustRelationByMood  │ NpcAdjustRelationByMood         (0x56840c) │
//   │                 │ shuffleDwords         │ util::InitAndShuffleDwordArray  (0x58ba98) │
//   │                 │ requestBuildOp67      │ RequestBuildOp67                (0x495434) │
//   │                 │ requestBuildOp93      │ RequestBuildOp93                (0x495bd0) │
//   │                 │ queueRequest17        │ QueueRequest17                  (0x49465c) │
//   └─────────────────┴───────────────────────┴────────────────────────────────────────────┘
//
// All command emits stage onto the SAME shared real CommandQueue real_hooks owns
// (RealCommandQueue()); the wander scan operates on the SAME shared real
// HandlerTable real_hooks3 owns (RealHandlerTable()).
namespace guild::sim {

// Installs the real wiring for NpcAction5Hooks, NpcAction6Hooks and NpcAction7Hooks.
// Seed-from-defaults; idempotent. Composes with InstallRealSimHooks3().
void InstallRealNpcAction1Wiring();

} // namespace guild::sim
