#pragma once
// Wires the reconstructed in-world contact-menu / action-menu builders
// (gui/contact_actions.* and gui/contact_loops.*) into the live runtime by
// installing real command-sink / gate implementations.
//
// Before this, the ContactActionSink / ContactLoopSink / ContactGate /
// ContactLoopGate DI bridges were exercised only by tests: at app runtime the
// global pointers fell back to their default no-op instances, so every clicked
// contact-menu entry dispatched to an inert stub.  This installs real sinks that
// route each clicked verb to its 1:1-reconstructed leaf where a faithful,
// signature-matching reconstruction exists, and leave the remainder at the inert
// default (= the established headless-faithful wiring pattern: the reconstructed
// CONTROL FLOW of each builder/dispatcher runs, and a bound leaf executes its own
// reconstruction over its own inert sub-hooks).
//
// Faithfully bound (exact entry-point match):
//   ContactLoopSink::RunFeast(obj)    -> guild::gui::Panel_RunGelage(obj)   0x54e940
//
// Every other ContactActionSink / ContactLoopSink verb stays at its inert
// virtual default: the remaining leaves are reconstructed as Build/Dispatch
// rule-cores or rule kernels with non-matching signatures (no monolithic
// void(int) entry point to call), so binding them would require fabricating a
// driver — which rule 8 forbids.  Those are listed (with addresses) in the .cpp.
//
// The two gates (ContactGate / ContactLoopGate) are left at their permissive
// default (all gates open / no extra objects), which matches the inert default
// the rest of the live wiring uses; the real gate predicates
// (Interaction_TestHandlerFlagWord / Tutorial_IsInactive / the treasury-active
// byte) live in io/sim and are out of scope here.
namespace guild::world {

// Install the real contact-menu sinks into the global contact-action / contact-loop
// bridges.  Idempotent (uses process-lifetime sink objects).
void InstallRealLocationWiring();

} // namespace guild::world
