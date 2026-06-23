#pragma once
// wire_ai.{h,cpp} — wires the reconstructed AI decision-math bridges into their
// live engine-leaf couplings (rule 13).
//
// The AI scorers/deciders reconstructed in ai_recon5_decisions.* (AiCardGame_
// PlaceBet, AiPlayer_ExecThreaten, AiMethod_SelectConversationTarget) and the
// recon2/recon3 method/action/meister modules are *pure decision math* that take
// their outbound couplings through an inert-default `AiRecon5Hooks` struct passed
// by reference (there is no global hook pointer — the struct is the bridge).
//
// Before this, every call site fell back to the inert defaults
// (`kInertAiRecon5Hooks`): RNG returns 0, wealth/money/rating oracles return 0.
// `InstallRealAiWiring()` populates a process-lifetime `AiRecon5Hooks` instance,
// binding the leaves that have a *faithful* 1:1 reconstruction with a matching
// shape:
//
//   randNext        -> guild::crt::RandNext               (0x5cb8bc)
//   moneyToDisplay  -> guild::world::MoneyConvertToDisplayCoord (0x58f14c)
//   moneyMulByRate  -> guild::world::AmtMoneyMultiplyByRate      (0x58f19c)
//
// The remaining fields stay null (= inert default) because their original is not
// a nullary/single-arg oracle of the hook's shape and their faithful form needs
// engine leaves not reachable at this binding layer (see wire_ai.cpp):
//
//   personTotalWealth -> 0x591f7c  (recon needs assembled person/building inputs)
//   selectMoodColor   -> 0x4664d8  (__usercall w/ moodClass + global side effects)
//   ratingCurveA      -> 0x58a6e8  (needs two BuildingRec records)
//
// Callers read the populated table via GetRealAiRecon5Hooks() and pass it to the
// scorers exactly where they previously used kInertAiRecon5Hooks.
#include "sim/ai_recon5_decisions.h"

namespace guild::sim {

// Populate & return the process-lifetime real AiRecon5Hooks table. Idempotent.
void InstallRealAiWiring();

// The real (post-install) AiRecon5Hooks bridge. Before InstallRealAiWiring() runs
// this is the all-null inert table; after, the bindable leaves point at their 1:1
// reconstructions. Pass this in place of kInertAiRecon5Hooks at AI call sites.
const AiRecon5Hooks& GetRealAiRecon5Hooks();

} // namespace guild::sim
