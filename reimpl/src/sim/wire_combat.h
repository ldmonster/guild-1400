#pragma once
// Wires the COMBAT-cluster hook bridges into the live call tree (rule 13). Before
// this, the five combat/charaction leaf bridges below were FULLY INERT at runtime
// (nothing installed them), so every hook field defaulted to null and the routed
// cross-module leaves were no-ops:
//
//   * BrawlHooks         (sim/charaction_brawl.h)  — VIBE_CharAction_BrawlStep 0x4d201c
//   * CombatSlots3Hooks  (sim/combat_slots3.h)     — object-def / window / order leaves
//   * CombatSlots4Hooks  (sim/combat_slots4.h)     — escape / damage-number / pursuit
//   * CombatSlots5Hooks  (sim/combat_slots5.h)     — find-unit / cutscene / bomb leaves
//   * CombatDriversHooks (sim/combat_drivers.h)    — result / setup / deployment drivers
//
// This installer binds every hook field whose reconstructed sibling has a DIRECTLY
// COMPATIBLE function-pointer signature, exactly the established headless-faithful
// wiring pattern (cf. real_reaper_wiring.cpp). Concretely, the only such field across
// these five bridges is:
//
//   CombatDriversHooks.randomModulo  ->  sim::Math_RandomModulo   (gilde.exe 0x58b89c)
//
//   (`int (*)(u16)` == `int Math_RandomModulo(u16)`. Binding it makes the deployment
//    AI roll / auto-resolve strength bonus / pursuit attack-roll drivers run their
//    REAL CRT-LCG RNG instead of the deterministic-0 inert default — the observable
//    effect the headless build needs to exercise the driver control flow over a
//    real-RNG stream.)
//
// Every OTHER field on all five bridges stays NULL (= inert default; each call site
// null-checks before use, so unbound fields keep the inert behavior). Those fields
// are deliberately-abstracted cross-module leaves — they take `void*` native-record
// pointers or primitives that resolve global game tables (`word_12CE910[268*id]`,
// the GameObject query iterators, Window/Widget handles, audio/script contexts). The
// reconstructed siblings for those leaves exist but over the engine's NATIVE record
// forms / their own sub-hook bridges (e.g. NpcAdjustRelationByMood(HeRecord*,i8),
// CommandQueue::GetPacketStatusById, QueueRequestEntity29(int,HeRecord*),
// GetSoundRangeScale(const CombatUnitAI&), BuildOrderForUnit(const CombatUnitAI&,...)),
// none of which is assignable as a raw `void*`/primitive function pointer to the
// abstracted field without a host-side table-resolving adapter. They are therefore
// the host integration test's responsibility (the same posture as the sibling
// GroupInteractHooks bridge, which is also left inert) and are listed in the .cpp.
//
// CombatApplyHooks (play/slice_combat.h) is NOT touched here: it already has REAL
// installers (play::SetCombatApplyHooks in src/play/slice_combat.cpp and
// src/play/playthrough.cpp), so it is intentionally out of scope.
namespace guild::sim {

// Install the combat-cluster bindings into the global hook tables. Idempotent;
// process-lifetime backing storage. After this, CombatDriversHooks.randomModulo
// points at the 1:1 Math_RandomModulo; all other combat-bridge fields stay inert.
void InstallRealCombatWiring();

} // namespace guild::sim
