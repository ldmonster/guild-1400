#pragma once
// ---------------------------------------------------------------------------
// wire_event_office — installs the reconstructed event / mission-requirement /
// interaction leaf functions into their live hook bridges (rule 13).
//
// Before this, three hook tables were inert at runtime (nothing bound their
// fields to the 1:1 reconstructions even though those reconstructions exist):
//
//   * world::MissionReq3GetHooks()  — the VIBE_MissionReq_Evaluate dispatcher
//     (0x5398c4) routes each objective-type case through this table.  The six
//     check/timer/count leaves whose reconstructed signatures match exactly are
//     bound to world/mission_requirement.cpp (CheckStatThreshold 0x539160,
//     CheckOwnPersonRatio 0x539380, CheckMultiStat 0x5393ec, CheckStatCombo
//     0x53945c, AccumulateTimer 0x539c44, CountGuildMembers 0x539da0).  One
//     more (CheckCumulativeStats 0x539534) binds through a thin person-drop
//     adapter (the native leaf ignores the person arg).
//
//   * sim::g_i4Hooks (interaction4.h) — the determinism-critical RNG leaf
//     randomModulo (VIBE_Math_RandomModulo 0x58b89c) is bound to
//     util::RandomModulo.  The header explicitly notes "game wires it to
//     util::RandomModulo"; its inert default returns a constant 0, which would
//     break every RNG-driven interaction decision.  All other i4 leaves stay
//     at their inert defaults.
//
// Every other field of these tables stays inert: those are cross-module
// subsystem leaves (live person store, economy snapshot, scene query, UI/voice)
// that are not faithfully reachable in a headless build, and whose hook
// signatures do not match any reconstructed function (rule 8 — no fake
// stand-ins).  The dispatchers null-check each field, so unbound fields keep
// the established inert behaviour.
//
// Bridges with no live install point (declared but never consumed / passed by
// parameter, not a process-wide table) are intentionally NOT touched here:
//   * world::AmtWindowHooks (amt_recon_office_window.h) — struct only; no
//     global, setter or consuming function.
//   * world::OfficeAddTableEntryHook / OfficeDestroyActorHook — by-parameter
//     hooks supplied at each call site, not a process-wide table.
//   * world::MissionReqEventGetHooks() — every field is a coupled subsystem
//     leaf (person/economy/building) with no signature-compatible recon.
// ---------------------------------------------------------------------------
namespace guild::world {

// Install the available reconstructed leaves into MissionReq3GetHooks() and
// sim::g_i4Hooks.  Idempotent; safe to call more than once.
void InstallRealEventOfficeWiring();

} // namespace guild::world
