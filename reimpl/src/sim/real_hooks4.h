#pragma once
// real_hooks4 — FOURTH wave of the cross-module "real wiring" installer for the
// sim/ai cluster. Glue only: now that the object module (object.{h,cpp},
// operating on the shared g_sceneNodes node array), the building lifecycle
// (building_lifecycle.{h,cpp}, operating on g_buildingPersons), and the person
// create module (person_create.{h,cpp}, operating on the shared g_persons array)
// are translated, this wave binds the command-apply2 / command-apply3 Set*Hook
// entry points whose REAL reconstructed leaf targets now exist to those targets.
// The first three waves (real_hooks{,2,3}.{h,cpp}) wired the command-emit / He /
// charaction / NPC-leaf hooks; this wave wires the command-APPLY leaves that
// actually MUTATE the real entity records when a packet is applied.
//
// This file contains NO module logic — only the indirection that connects the
// already-translated modules to each other.
//
//   ┌─ category ──────────────┬─ hook (setter) ─────────────────────────┬─ real target ───────────────────────────────┐
//   │ object stock mutation    │ sim::SetRemoveObjektHook (apply2)        │ sim::GameObjectRemoveObjektAmount (object)   │
//   │  (ExRemapObjectPair 0x0F,│ sim::SetAddObjektHook    (apply2)        │ sim::GameObjectAddObjektToParent  (object)   │
//   │   ExUpdateObjectPair 0x10)│ sim::SetDecrementObjektHook (apply2)    │ sim::GameObjectDecrementObjektStock(object)  │
//   │ building free / remove   │ sim::SetBuildingFreeHook   (apply2)      │ sim::Building_RemoveAndCleanup (lifecycle)   │
//   │  (ExFreeBuildingOrScene  │ sim::SetBuildingRemoveHook (apply2)      │ sim::Building_RemoveAndCleanup (lifecycle)   │
//   │   0x0E)                  │                                          │                                              │
//   │ person create / spawn    │ sim::SetPersonCreateHook (apply3)        │ sim::Person_CreateAndSpawn (person_create)   │
//   │  (ExSpawnAndPlaceChar    │                                          │   on the shared g_persons array              │
//   │   0x49)                  │                                          │                                              │
//   │ object resolve-by-id     │ sim::SetObjectFindHook (apply3)          │ sim::GameObjectResolveEntityById (entity)    │
//   │  (ExDeselectObject 0x4A) │                                          │   (object/scene/person record probe)         │
//   └──────────────────────────┴──────────────────────────────────────────┴──────────────────────────────────────────────┘
//
// The object-stock leaves operate on the SAME shared g_sceneNodes node array the
// object module owns (so an applied move-object command really removes a stack
// from the array). The building free/remove leaves operate on the SAME shared
// g_buildingPersons table the lifecycle owns (so an applied building-free command
// really marks the record destroyed). The person-create leaf allocates a record
// in the SAME shared g_persons array entity.cpp owns (so an applied spawn command
// really creates a person). No table is duplicated; the wiring is byte-faithful.
//
// Hooks STILL left at their inert/record default because the real reconstructed
// target does NOT exist yet (LISTED, not silently skipped):
//   * command_apply2.h SetSlotFindHook / SetTradeNodeHook / SetEstateTransferHook /
//       SetOffice{Assign,Transfer,Swap}Hook / SetLawApplyHook / SetSelectionToggleHook
//       — VIBE_Building_FindSlotByProt / VIBE_GameObject_QueryFind+AddObjekt trade
//       node, VIBE_Person_TransferEstateOwnership, VIBE_Office_* (different signature
//       in world/office.h), VIBE_Gesetz_ApplyAndNotify, VIBE_GameObject_RemoveById
//       selection toggle — not reconstructed in a form matching the hook signature.
//       (SetHeFindHook is wired by InstallRealSimHooks3.)
//   * command_apply3.h SetCharFindHook / SetTargetBusyHook / SetCombatSlotStoreHook —
//       VIBE_Character_FindByPredicate / VIBE_Command_CheckTargetNotInUse / combat
//       slot tables not reconstructed.
//   * command_apply.h SetBeweisAllocHook / SetTurnControlHook / SetCityAckHook —
//       VIBE_Beweis_* / VIBE_GameTick_HandleTurnControlCommand / VIBE_City_Apply-
//       StatsFromAck not reconstructed.
//   * command_apply4.h / command_apply5.h Set*Hook leaves — the apply5 building
//       create/free leaves already reach real code through DIRECT calls
//       (Building_CreateGebaeude) or a record-faithful default, and the remaining
//       leaves (office/chat/straftat/cutscene/script-finish) are not reconstructed.
//
// InstallRealSimHooks4() is independent of the earlier installers; call all four.
#include "guild/common/types.h"

namespace guild::sim {

// Installs the fourth-wave wireable command-apply leaf hooks (the table above) to
// their real reconstructed module targets. Idempotent: safe to call more than
// once. After this call, applying a move-object / building-free / spawn-person
// packet mutates the real shared entity arrays (g_sceneNodes / g_buildingPersons /
// g_persons), which a test can inspect directly.
void InstallRealSimHooks4();

} // namespace guild::sim
