#pragma once
// wire_combat2 — second-wave combat / character-cluster bridge wiring (rule 13).
//
// Binds the directly-bindable fields of two more inert hook bridges to their real
// reconstructed cross-cluster leaves, on the SAME shared real He pool /
// CommandQueue the existing real-hooks waves own:
//
//   * BrawlHooks (charaction_brawl.h, VIBE_CharAction_BrawlStep @0x4d201c) —
//       packetStatus     -> CommandQueue::GetPacketStatusById (0x4939d4)
//       freeHandlerEntry -> HandlerTable::FreeHandlerEntry     (0x4c6144)
//       findPersonById   -> PersonFindRecordById               (0x58bc6c)
//     (the remaining aggressor-record / relation-mood / AP-event / entity-message /
//      family-defeat / saved-pose-requeue leaves stay inert — same rationale the
//      already-present wire_combat.cpp documents for BrawlHooks.)
//
//   * Recon2Hooks (character_recon2_cmds.h, the take/drop/look/ani Cmd cluster) —
//       strCmp -> guild::util::ReconStrCmp (VIBE_Util_StrCmp @0x5d3f10)
//       strLen -> CRT strlen (the original leaf is literally strlen)
//     (every other Recon2 leaf is a deep scene-graph / matrix-transform / object /
//      action-queue-builder / script-error engine leaf with no clean reconstructed
//      target -> inert.)
//
// ZERO-BINDABLE bridges (every field is a render / heightmap / window / widget /
// command / GameObject-iterator / person-QueryBegin / office-record engine leaf
// with no clean reconstructed target) get NOTHING wired here, by design:
//   * CharQueryHooks      (character_query.h)   — setVisible / worldToTile / terrainAt
//   * CombatSlots3Hooks   (combat_slots3.h)     — GameObject iterators / window / widget / frame-loop
//   * CombatSlots4Hooks   (combat_slots4.h)     — screen-project / text-label / command / blood-pool mesh
//   * CheckHooks          (command_apply9.h)    — Person_QueryBegin (rule 8) / Office records
//
// Glue only — no module logic. Idempotent; the Recon2 table has process lifetime
// (SetRecon2Hooks stores the pointer, not a copy).

namespace guild::sim {

// Install the second-wave combat/character bridge bindings. Composes with
// InstallRealSimHooks3() (which owns the shared He pool) — call that first if a
// brawl step must resolve against a populated pool.
void InstallRealCombat2Wiring();

}  // namespace guild::sim
