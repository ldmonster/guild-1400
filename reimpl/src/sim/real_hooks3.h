#pragma once
// real_hooks3 — THIRD wave of the cross-module "real wiring" installer for the
// sim/ai cluster. Glue only: now that the He/HandlerEntry subsystem
// (handler_entry.{h,cpp}, he.cpp) and the remaining command builders
// (command_builders.{h,cpp}: QueueRequestEntity29 / QueueRequestSingle49 /
// QueueRequestNamedObject53 / QueueRequestPair33 / RequestBuildOp88/91/93 / …)
// are translated, this wave binds the NPC/AI/interaction LEAF hooks that the
// first two waves had to leave at their inert default (no real target then) to
// those real reconstructed targets. See real_hooks.h / real_hooks2.h for the
// earlier waves.
//
// This file contains NO module logic — only the indirection that connects the
// already-translated modules to each other.
//
//   ┌─ category ──────────────┬─ hook (setter) ─────────────────────────┬─ real target ───────────────────────────────┐
//   │ NPC step command emit    │ NpcLeafHooks.queueRequestEntity29        │ sim::QueueRequestEntity29 (command_builders) │
//   │  (npcaction.h)           │ NpcLeafHooks.requestBuildOp93            │ sim::RequestBuildOp93 (command_builders)     │
//   │                          │ NpcLeafHooks.freeHandlerEntry            │ sim::HandlerTable::FreeHandlerEntry (he pool)│
//   │                          │ NpcLeafHooks.packetStatus                │ sim::CommandQueue::GetPacketStatusById       │
//   │ plague step (npcaction2) │ NpcAction2Hooks.queueRequestEntity29     │ sim::QueueRequestEntity29                    │
//   │                          │ NpcAction2Hooks.queueSingle49            │ sim::QueueRequestSingle49                    │
//   │                          │ NpcAction2Hooks.queueNamedObject53       │ sim::QueueRequestNamedObject53               │
//   │                          │ NpcAction2Hooks.queuePair33              │ sim::QueueRequestPair33                      │
//   │                          │ NpcAction2Hooks.requestBuildOp73Str      │ sim::RequestBuildOp73Str                     │
//   │                          │ NpcAction2Hooks.packetStatus/packetSeq   │ CommandQueue::GetPacketStatusById/SeqById    │
//   │                          │ NpcAction2Hooks.freeHandlerEntry         │ sim::HandlerTable::FreeHandlerEntry          │
//   │ burglary/recruit/jail    │ NpcAction3Hooks.queueRequestEntity29     │ sim::QueueRequestEntity29                    │
//   │  (npcaction3)            │ NpcAction3Hooks.queueSingle49            │ sim::QueueRequestSingle49                    │
//   │                          │ NpcAction3Hooks.queueNamedObject53       │ sim::QueueRequestNamedObject53               │
//   │                          │ NpcAction3Hooks.queueRequest16           │ sim::QueueRequest16 (command_codec)          │
//   │                          │ NpcAction3Hooks.requestBuildOp91         │ sim::RequestBuildOp91                        │
//   │                          │ NpcAction3Hooks.enqueueArgs25            │ sim::QueueRequestArgs25 (command_codec)      │
//   │                          │ NpcAction3Hooks.packetStatus/packetSeq   │ CommandQueue::GetPacketStatusById/SeqById    │
//   │                          │ NpcAction3Hooks.freeHandlerEntry         │ sim::HandlerTable::FreeHandlerEntry          │
//   │ patrol/sabotage/raid     │ NpcAction4Hooks.queueRequestEntity29     │ sim::QueueRequestEntity29                    │
//   │  (npcaction4)            │ NpcAction4Hooks.queueSingle49            │ sim::QueueRequestSingle49                    │
//   │                          │ NpcAction4Hooks.queueNamedObject53       │ sim::QueueRequestNamedObject53               │
//   │                          │ NpcAction4Hooks.queuePair33              │ sim::QueueRequestPair33                      │
//   │                          │ NpcAction4Hooks.queueRequest16           │ sim::QueueRequest16                          │
//   │                          │ NpcAction4Hooks.packetStatus/packetSeq   │ CommandQueue::GetPacketStatusById/SeqById    │
//   │                          │ NpcAction4Hooks.freeHandlerEntry         │ sim::HandlerTable::FreeHandlerEntry          │
//   │ command-apply He find    │ sim::SetHeFindHook (command_apply2.h)    │ sim::HandlerTable::FindFirstHandlerByFilter  │
//   │                          │                                          │   (selector 3 = field@+16), via raw helper   │
//   │ combat command sink      │ sim::SetCombatCommandSink (combat.h)     │ sim::QueueRequestPair33 (damage) /           │
//   │                          │                                          │   QueueRequestSingle49 (death) on the queue  │
//   └──────────────────────────┴──────────────────────────────────────────┴──────────────────────────────────────────────┘
//
// All command-emit hooks enqueue onto the SAME shared real CommandQueue the
// first two waves use (RealCommandQueue(), real_hooks.h), and freeHandlerEntry /
// the He-find hook operate on ONE shared real HandlerTable (RealHandlerTable()).
// The three installers compose: a test can call InstallRealSimHooks(),
// InstallRealSimHooks2(), InstallRealSimHooks3() and inspect one queue + one He
// pool.
//
// Hooks STILL left at their inert/record default because the real reconstructed
// target does NOT exist yet (LISTED, not silently skipped):
//   * NpcLeafHooks (npcaction.h): lawBaseTextId (VIBE_Law_GetBaseTextId),
//       findInventorySlot (VIBE_Inventory_FindSlotByItemId), shuffleDwords
//       (VIBE_Util_InitAndShuffleDwordArray) — not reconstructed.
//   * NpcAction2Hooks (npcaction2.h): the object-array probes (objectKindAt /
//       objectAiPlayerType / objectId / objectSusceptible / resolveObjectId /
//       personPresent / personNearDoor — dword_13CE298/_13CE294 object & AiPlayer
//       tables), beginSlotResetPacket (cmd28 accumulate), broadcastOutbreak /
//       broadcastSpreadDone (VIBE_History_BroadcastPlague*), plagueInfectNearby
//       (the undefined-locals float scatter) — not reconstructed.
//   * NpcAction3Hooks (npcaction3.h): requestChrMove (RequestChrMoveToUniverse),
//       queueFlag55 (op55), queuePair36 (op36), queueQuad43 (op43),
//       setEntityField (QueueRequestState22 delta abstraction), queueSlotReset28
//       (op28), enqueueBuildingActionStart/End, and all entity-query / physics
//       fields (personQueryBegin / findPersonById / findBuildingById /
//       findStorableObject / personNearDoor / personId / personKind /
//       personMarkerWord / personHasCharacter / objectIdField / changePlayerAction
//       / recruitProximity / recruitCost / computeWanderPath / sendMessage /
//       evaluateViolation / jailEscapeRoll / burglaryLootValuation /
//       burglaryDetectionRoll) — the builders have no reconstructed opcode-37/55/
//       36/43/28 target, and the field-reader byte offsets are ambiguous between
//       decompiler views (same reason real_hooks.h leaves NpcTarget readers null).
//   * NpcAction4Hooks (npcaction4.h): queueFlag55 (op55), queuePair36 (op36),
//       queueQuad43 (op43), enqueueCmd15 (op15), queueRequest39 (op39),
//       cityIdFromIndex, getFamilyRecord / familyLedgerAdd, and all entity-query /
//       physics fields (personQueryBegin / findPersonById / findStorableObject /
//       objectIdField / personIdField / personMarkerWord / personHasCharacter /
//       personOwnerWord / personKind / personActionActive / changePlayerAction /
//       evaluateViolation / findHandlerByFilter / handlerRecordId / sendMessage /
//       patrolWageRoll / sabotageGestureTarget / sabotageDamageRoll / sabotagePlayFx
//       / combatAttackRoll / combatStrength) — no reconstructed builder/leaf.
//   * command_apply2.h SetSlotFindHook / SetTradeNodeHook / SetEstateTransferHook /
//       SetOffice{Assign,Transfer,Swap}Hook / SetLawApplyHook / Set{Remove,Add,
//       Decrement}ObjektHook / SetBuilding{Free,Remove}Hook / SetSelectionToggleHook
//       — VIBE_Office_* assign/transfer/swap exist in world/office.h but with a
//       DIFFERENT signature (no `const i32* spec` op-form), and the GameObject
//       stock/scene-removal + Gesetz_ApplyAndNotify targets are not reconstructed.
//       Only SetHeFindHook has a real He-pool target (wired here).
//   * command_apply3.h SetCharFindHook / SetPersonCreateHook / SetTargetBusyHook /
//       SetCombatSlotStoreHook — not reconstructed (see real_hooks.h). (SetObject-
//       FindHook is wired by InstallRealSimHooks.)
#include "sim/command.h"

namespace guild::sim {

// Forward decls so this header needs neither he.h nor handler_entry.h (the
// HeRecord/HandlerRecord type definitions conflict with command_apply2.h's own
// 176-byte HeRecord, so the apply2 wiring lives in its own TU).
class HandlerTable;

// Installs the third-wave wireable hooks (the table above) to their real
// reconstructed targets. Idempotent. The command-emit hooks enqueue onto the
// shared CommandQueue returned by RealCommandQueue() (real_hooks.h); the
// He-pool hooks (freeHandlerEntry / SetHeFindHook) operate on the shared
// HandlerTable returned by RealHandlerTable().
void InstallRealSimHooks3();

// The shared real He/HandlerEntry pool the wired freeHandlerEntry / He-find
// hooks operate on. Created + Init()'d on the first InstallRealSimHooks3() call;
// tests inspect it (live_count / high_water) and seed it (AllocHandlerEntry).
// Never null after install.
HandlerTable* RealHandlerTable();

} // namespace guild::sim
