#pragma once
// wire_he — cross-cluster "real wiring" installer for the He (handler-pool) leaf
// bridges. Glue only: binds the He entity-query, per-type He handler-step, and the
// group-interaction coroutine bridges to their real reconstructed leaves — the
// command builders (command_builders2.h QueueRequestPair35, command_codec.h
// EnqueueObjectInteraction, command_builders.h QueueRequestEntity29), the shared
// real He/HandlerEntry pool (real_hooks3.h RealHandlerTable()), the shared real
// CommandQueue (real_hooks.h RealCommandQueue()), the NpcAction dispatcher
// (npcaction.h NpcAction_Dispatch) and the Person id->record scan
// (entity.h PersonFindRecordById).
//
// Bridges wired here (grep'd: NOT installed anywhere else in src/):
//   * HeEntityQueryHooks   (he_entity_query.h)
//   * HeHandlerHooks       (he_handlers.h)
//   * GroupInteractHooks   (charaction_misc.h)
//
// (IssueOnObjectHooks — command_apply12.h — is already wired by
//  src/play/input_command.cpp around its IssueOnObject calls, so it is NOT touched
//  here.)
//
// This file contains NO module logic — only the indirection that connects the
// already-translated modules to each other.
//
//   ┌─ bridge ────────────────┬─ hook field ──────────────┬─ real target ──────────────────────────────────┐
//   │ HeEntityQueryHooks       │ personFind                │ sim::PersonFindRecordById (entity)              │
//   │  (he_entity_query.h)     │ queueRequestPair35        │ sim::QueueRequestPair35(q, value, 1)            │
//   │                          │ notifyRivalEvent          │ INERT (no reconstructed History leaf, 0x536070) │
//   │ HeHandlerHooks           │ freeHandlerEntry          │ sim::HandlerTable::FreeHandlerEntry (He pool)   │
//   │  (he_handlers.h)         │ npcActionDispatch         │ sim::NpcAction_Dispatch (npcaction)             │
//   │                          │ packetStatus              │ sim::CommandQueue::GetPacketStatusById          │
//   │                          │ queueRequestEntity29      │ sim::QueueRequestEntity29(q, (i8)arg, h)        │
//   │                          │ charAction/event/building │ INERT — the three subsystem RetZero stubs all   │
//   │                          │   Tick                    │   return 0 in the shipping build (faithful)     │
//   │ GroupInteractHooks       │ findPersonById            │ sim::PersonFindRecordById                       │
//   │  (charaction_misc.h)     │ personReady               │ *(u8*)(person+8) != 0 (record-byte read)        │
//   │                          │ freeHandlerEntry          │ sim::HandlerTable::FreeHandlerEntry             │
//   │                          │ enqueueObjectInteraction  │ sim::EnqueueObjectInteraction(q,8,..,0,0,2)     │
//   │                          │ queueRequest39            │ INERT (no reconstructed op39 builder, 0x494c30) │
//   └──────────────────────────┴───────────────────────────┴─────────────────────────────────────────────────┘

namespace guild::sim {

// Bind the three He bridges to their real reconstructed leaves over the shared He
// pool + command queue. SEEDS each bridge table from its module inert defaults
// (GetXHooks()) first, then overrides only the wireable fields — required because
// the group-interaction step (and others) invoke hook fields WITHOUT a null-check,
// so a zero-initialised table would crash. Idempotent; forces the shared He pool
// and command queue to exist (composes with InstallRealSimHooks3).
void InstallRealHeWiring();

} // namespace guild::sim
