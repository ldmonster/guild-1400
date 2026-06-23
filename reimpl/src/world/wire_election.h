#pragma once
// gilde.exe — election / court-council / create wiring (guild::world).
//
// Binds three previously-INERT hook bridges to their real reconstructed leaves
// (rule 13). Before this pass none of these bridges were installed anywhere in
// src/ or apps/ — they were reconstructed but dangling, so the live office /
// court / building-create call tree ran against inert defaults:
//
//   * CourtCouncilHooks            (world/court_council2.h) — the court-trial
//     verdict scorer + candidate rating bars.
//   * GuildSetElectionHooks         (world/guild_election.h)  — the single-seat
//     guild-master election install + notify.
//   * CanvassSetHooks               (world/election_candidacy.h) — the two-seat
//     guild-master canvass install + notify.
//   * ElectionSetInstallHook        (world/election.h) — the ElectGuildMaster
//     winner-install command.
//   * ICreateHooks                  (sim/building_create2.h) — the CreateGebaeude
//     per-type plant-map allocation leaf.
//
// SEED-FROM-DEFAULTS: CourtCouncilHooks is a struct of nullable function pointers
// whose module owner (court_council2.cpp) null-checks every field, so seeding is
// for symmetry with the rest of the wiring layer; the installer copies the module
// inert defaults and overrides only the fields with a clean reconstructed target.
// The election install/notify and ICreateHooks bridges install REPLACEMENT
// objects (function pointers / a vtable) rather than per-field tables.
//
// Bound leaves:
//   CourtCouncilHooks.guildEligibility -> VIBE_Amt_GetGuildEligibility 0x481bcc
//                                         (world::GuildGetEligibility)
//   CourtCouncilHooks.randomMod4       -> VIBE_Math_RandomModulo(4)  0x58b89c
//                                         (util::RandomModulo)
//   election install hooks (x3)        -> VIBE_Office_AddTableEntry  0x47e750
//                                         (world::OfficeAddTableEntry)
//   ICreateHooks.AllocPlantMap         -> VIBE_Memory_AllocDebug(0x600) 0x438f10
//                                         (mem::MemoryTracker::AllocDebug)
//
// Inert (no clean reconstructed target — documented at the install site):
//   CourtCouncilHooks.buildingSlotCount / buildingSlot  (live object-array read)
//   CourtCouncilHooks.aiNeedsComputeWeights / categoryRating (AiNeeds scratch)
//   CourtCouncilHooks.setBarValue                       (SetValueOrText widget/UI)
//   election notify hooks (x2)                          (message broadcast send)

namespace guild::world {

// Install the real election / court-council / create wiring. Idempotent; safe to
// call from the command-system init point (RealSubsystems::commandQueueInitAndSync).
void InstallRealElectionWiring();

} // namespace guild::world
