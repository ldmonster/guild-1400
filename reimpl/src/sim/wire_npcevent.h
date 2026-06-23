#pragma once
// ===========================================================================
// wire_npcevent — REAL wiring installer for the NpcEvent step bridges
// (sim/npcevent_steps.h NpcEventHooks and sim/npcevent_steps2.h NpcEventHooks2).
//
// Before this, NpcEventHooks was only partially wired: real_reaper_wiring.cpp
// (InstallRealReaperWiring) bound the 4 Reaper render/transform/sound leaves and
// left every other field null/inert. NpcEventHooks2 was fully inert (nothing
// installed it). This module SUPERSEDES InstallRealReaperWiring: it binds the
// reaper 4 (re-using the existing guild::sim::Reaper* full reconstructions) AND
// every other NpcEventHooks field that maps onto an already-reconstructed leaf —
// in one process-static table — then installs NpcEventHooks2 with its bindable
// subset.
//
// Bound to real reconstructions (rule 13 — connect finished functions to the
// live call tree):
//   NpcEventHooks:
//     resolveEntity   -> entity-array resolve (VIBE_Building_FindById @0x587b20)
//     entityField     -> byte read off the resolved ObjectRec
//     findPerson      -> VIBE_Person_FindRecordById @0x58bc6c
//     personField     -> byte read off the resolved Person
//     findBuilding    -> VIBE_Building_FindById @0x587b20
//     queueRequestPair33        -> VIBE_Command_QueueRequestPair33 @0x494b04
//     queueRequestSingle49      -> VIBE_Command_QueueRequestSingle49 @0x494e4c
//     queueRequestArgs25        -> VIBE_Command_QueueRequestArgs25 @0x494810
//     queueRequestQuad52        -> VIBE_Command_QueueRequestQuad52 @0x494ee4
//     queueRequestNamedObject53 -> VIBE_Command_QueueRequestNamedObject53 @0x494f0c
//     requestBuildOp87          -> VIBE_Command_RequestBuildOp87 @0x495ac0
//     queueGestureFlag55        -> VIBE_Command_QueueRequestFlag55 @0x495040
//     enqueueObjectInteraction  -> VIBE_Command_EnqueueObjectInteraction @0x4944f0
//     packetStatus    -> CommandQueue::GetPacketStatusById @0x4939d4
//     packetSeq       -> CommandQueue::GetPacketSeqById   @0x4939fc
//     reaperApproach/Move/CachePose/UpdateSound -> guild::sim::Reaper* (0x4d8c34..)
//   NpcEventHooks2:
//     queryBegin (id filter) -> VIBE_Person_QueryBegin @0x586c20
//     findPerson / personField / objectField  (as above)
//     queueRequestSingle49 / queueRequestNamedObject53 / queueRequestArgs25
//     queueRequest16 -> VIBE_Command_QueueRequest16 @0x494630
//     packetStatus / packetSeq (as above)
//
// Left INERT (genuinely unreconstructed leaf, still behind a documented seam — no
// plain callable reconstruction exists; see report):
//   NpcEventHooks:  isNearDoor (0x.. Object_IsNearDoor), requestBuildOp77,
//     loadDemandSnapshot (Economy_LoadDemandSnapshot), reaperDetach,
//     nodeFieldGet/Set, cutscenePause/Resume, eventPanelCreate/Destroy,
//     dialogResult.
//   NpcEventHooks2: buildingUpgradeLevel, computeRoomWorth, sumCurrencyHeld,
//     patrolBrawlEligible, changePlayerAction, queueRequest39, applyTitleDelta,
//     cutsceneActive, sendEntityMessage, sendQuickjumpMessage, compareAwardTime,
//     eventPanelCreate/Destroy, renderAwardText, playAwardVoice,
//     awardDialogResult, awardActivePlayerGate.
//
// LP64 note: the NpcEvent hook prototypes pass entity/person records as opaque
// i32 handles (the 32-bit original stored a record pointer in eax). A real record
// pointer is 8 bytes on LP64, so the resolve hooks return a small i32 TOKEN from a
// process-static pointer registry and the field-read hooks resolve the token back
// to the pointer — the established LP64 reconciliation (cf. ReaperFull node slot).
// ===========================================================================
#include "sim/command.h"  // CommandQueue (the shared real request ring)

namespace guild::sim {

// Install the FULL real NpcEventHooks table (reaper + entity/person/command
// leaves) and call SetNpcEventHooks. Idempotent. SUPERSEDES InstallRealReaperWiring
// — call this instead; it covers the reaper 4 too.
void InstallRealNpcEventWiring();

// Install the bindable subset of NpcEventHooks2 and call SetNpcEventHooks2.
// Idempotent.
void InstallRealNpcEvent2Wiring();

// The shared real command queue the wired emit hooks build onto (Init()'d on
// first use). Exposed so tests/integration can inspect the emitted packets.
CommandQueue* NpcEventCommandQueue();

} // namespace guild::sim
