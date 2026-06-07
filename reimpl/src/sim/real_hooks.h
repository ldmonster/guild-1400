#pragma once
// real_hooks — the cross-module "real wiring" installer for the sim/ai cluster.
//
// Many src/sim/* and src/ai/* modules route their cross-module leaf calls
// (command emission, entity queries, etc.) through forward-declared / mockable
// HOOKS that default to an inert no-op/record backend, so each module stays
// translatable and unit-testable in isolation (see the Set*Hook / *LeafHooks /
// Register*Hooks declarations in each module header).
//
// This is GLUE ONLY: it does not contain or modify any module logic. It binds
// the hooks whose REAL reconstructed targets now exist to those targets:
//
//   ┌─ category ──────────┬─ hook (setter) ───────────────────┬─ real target ───────────────────────────┐
//   │ AI command emit     │ ai::SetBetCmdHook                 │ sim::QueueRequest16 (command_codec) on    │
//   │                     │   (cardgame.h)                    │   the installer's real sim::CommandQueue  │
//   │ command-apply entity│ sim::SetObjectFindHook            │ sim::BuildingFindById (entity.cpp)        │
//   │   query             │   (command_apply3.h)              │                                           │
//   │ entity query (NPC   │ sim::SetNpcTargetHooks.            │ sim::PersonFindRecordById (entity.cpp)    │
//   │   target select)    │   findPersonById                  │                                           │
//   │                     │ sim::SetNpcTargetHooks.            │ &sim::g_persons[index] (entity.cpp)       │
//   │                     │   personByIndex                   │                                           │
//   │ entity query (NPC   │ sim::SetNpcCityIdResolver         │ sim::g_personIds[] id column (entity.cpp) │
//   │   city ref resolve) │   (npcaction.h)                   │                                           │
//   │ charaction leaves   │ sim::RegisterHandlers()           │ registers the real sim::character.cpp     │
//   │                     │   (charaction.h)                  │   step handlers (Turn/Take/Drop/LoadAnim) │
//   └─────────────────────┴───────────────────────────────────┴───────────────────────────────────────────┘
//
// NB: a SECOND-wave installer (real_hooks2.h, InstallRealSimHooks2) now wires
// three more of the hooks listed below to real reconstructed targets:
//   * sim::SetCommandEmitHook (interaction_handlers.h) -> sim::QueueRequest17
//   * ai::SetPamphletCmdHook   (intrigue.h)            -> sim::QueueRequest16
//   * sim::SetCharActionHooks.turnStep (charaction.h)  -> sim::WalkRotateToward-
//       Heading (charaction_walk.cpp). The other CharActionHooks fields stay
//       inert (still no reconstructed renderer). Call both installers.
//
// NB: a THIRD-wave installer (real_hooks3.h, InstallRealSimHooks3) now wires the
// NPC/AI/interaction LEAF hooks listed below to real reconstructed targets, now
// that the He/HandlerEntry pool (handler_entry.{h,cpp}) and the remaining command
// builders (command_builders.{h,cpp}) exist:
//   * SetNpcLeafHooks / SetNpcAction2/3/4Hooks command-builder fields
//       (queueRequestEntity29 / queueSingle49 / queueNamedObject53 / queuePair33 /
//       requestBuildOp73Str/91/93 / queueRequest16 / enqueueArgs25) -> the real
//       sim::QueueRequest* / RequestBuildOp* builders on the shared queue.
//   * their freeHandlerEntry  -> sim::HandlerTable::FreeHandlerEntry (real He pool)
//   * their packetStatus/packetSeq -> CommandQueue::GetPacketStatusById/SeqById
//   * sim::SetHeFindHook (command_apply2.h) -> HandlerTable::FindFirstHandlerByFilter
//   * sim::SetCombatCommandSink (combat.h)  -> QueueRequestPair33 / Single49
// The leaf fields with no reconstructed target (object/AiPlayer table probes,
// history broadcasts, the op55/op36/op43/op28/op15/op39 builders, the ambiguous
// entity-field readers, the office/stock/scene leaves, the float-physics blocks)
// stay inert; real_hooks3.h LISTS them. Call all three installers.
//
// Hooks left at their inert/record default because the real reconstructed target
// does NOT exist yet (these are LISTED, not silently skipped):
//   * sim::SetCharActionHooks (charaction.h) attachAnim/animDone/setCarried/
//       setVisible — render/anim bridge (no reconstructed renderer). turnStep is
//       now wired by InstallRealSimHooks2 (see note above).
//   * sim::SetNpcLeafHooks (npcaction.h) / sim::SetNpcAction2/3/4Hooks: the
//       command-builder + freeHandlerEntry + packetStatus/Seq fields are NOW WIRED
//       by InstallRealSimHooks3 (see the third-wave NB above). What remains inert:
//       lawBaseTextId (VIBE_Law_GetBaseTextId) / findInventorySlot (VIBE_Inventory_
//       FindSlotByItemId) / shuffleDwords (VIBE_Util_InitAndShuffleDwordArray) /
//       broadcastOutbreak+broadcastSpreadDone (VIBE_History_*) / the op55/op36/op43/
//       op28/op15/op39 builders / the object & AiPlayer table probes / the ambiguous
//       entity-field readers / the float-physics sub-blocks — not reconstructed.
//   * sim::SetNpcTargetHooks office/ai fields: personKind/personId/officeCatA/B/C/
//       subMethod/officeRank/officeHolderRank/collectSuccessors/collectByCategory/
//       favorability/findNearestVisible — VIBE_Office_* / VIBE_Ai_ComputePerson-
//       Favorability / VIBE_ObjectSearch_FindMatchingColors not reconstructed (and
//       the field-reader byte offsets are ambiguous between the decompiler views).
//   * sim::SetPrivilegeLeafHook / SetCurrencyHook (interaction_handlers.h) —
//       VIBE_Privilege_* panel leaves; no reconstructed Privilege cluster. (NB:
//       SetCommandEmitHook is now wired by InstallRealSimHooks2; and the
//       interaction handlers already call the REAL sim::PersonFindRecordById /
//       BuildingFindById for entity queries directly — that path needs no wiring.)
//   * sim::SetSlotFindHook / SetTradeNodeHook / SetEstateTransferHook /
//       SetOffice{Assign,Transfer,Swap}Hook / SetLawApplyHook / Set{Remove,Add,
//       Decrement}ObjektHook / SetBuilding{Free,Remove}Hook / SetSelectionToggleHook
//       (command_apply2.h) — VIBE_Office_* exists in world/office.h but with a
//       different (non-`const i32* spec`) signature; VIBE_Gesetz_* / VIBE_GameObject_*
//       stock + scene-removal targets not reconstructed. (SetHeFindHook is NOW
//       WIRED to the real He pool by InstallRealSimHooks3 — see the third-wave NB.)
//   * sim::SetBeweisAllocHook / SetTurnControlHook / SetCityAckHook (command_apply.h)
//       — VIBE_Beweis_FindOrAllocSlot / VIBE_GameTick_HandleTurnControlCommand /
//       VIBE_City_ApplyStatsFromAck not reconstructed.
//   * sim::SetCharFindHook / SetPersonCreateHook / SetTargetBusyHook /
//       SetCombatSlotStoreHook (command_apply3.h) — VIBE_Character_FindByPredicate /
//       VIBE_Person_CreateAndSpawn / VIBE_Command_CheckTargetNotInUse / combat slot
//       tables not reconstructed.
//   * sim::PersonSetOfficeDefinitionHook / PersonSetTargetUnderfullHook (person.h) —
//       VIBE_Office_GetDefinition / VIBE_Combat_IsTargetUnderfull not reconstructed.
//   * sim::SetBuildingRatingHooks (building_value.h) — VIBE_He_* / VIBE_Inventory_
//       IsObjectSlotActive not reconstructed.
//   * sim::InventorySetCommandHook (inventory.h) — abstract serialize/veto sink.
//   * sim::PersonnelSet*Hook (personnel.h) — building-value / action-category leaves.
//   * ai::SetHandStrengthHook (cardgame.h) — VIBE_Building_EvalProductionRating.
//   * (ai::SetPamphletCmdHook is now wired by InstallRealSimHooks2 to the generic
//       opcode-16 builder — the closest reconstructed command builder for the op.)
//   * ai::RunWorkerMoodPass's TurnCmdFn (meisterai.h) — a per-call command sink, not
//       a global Set*Hook, so it is wired by the caller, not by this installer.
#include "sim/command.h"

namespace guild::sim {

// Installs every wireable hook (the table above) to its real reconstructed
// target. Idempotent: safe to call more than once. The command-emit hooks emit
// onto the shared CommandQueue returned by RealCommandQueue() (Init()'d on first
// install). Charaction step handlers are registered via RegisterHandlers().
void InstallRealSimHooks();

// The real CommandQueue the wired command-emit hooks enqueue onto. Created and
// Init()'d on the first InstallRealSimHooks() call; tests inspect it to verify a
// real packet was enqueued (send_count / pending_head). Never null after install.
CommandQueue* RealCommandQueue();

} // namespace guild::sim
