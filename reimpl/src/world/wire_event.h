#pragma once
// wire_event — binds the four world-event He-action bridges (EventHooks /
// Event3Hooks / Event4Hooks / Event5Hooks; src/world/event2.h .. event5.h) to
// their real reconstructed cross-cluster leaves (rule 13: wire up what you build).
//
// Before this installer ran, ALL FOUR bridges were fully inert at runtime —
// nothing in src/ ever called SetEventHooks / SetEvent3Hooks / SetEvent4Hooks /
// SetEvent5Hooks (only the bridges' own .cpp inert-default install and the tests
// did). The event-action "Run" phase machines therefore executed against no-op
// stubs: no handler ever freed against the real He pool, no person/building ever
// resolved against the real entity arrays, no command ever staged onto the real
// queue. This installer connects the bindable leaves to the same shared real
// He pool (sim::RealHandlerTable) + command queue (sim::RealCommandQueue) the
// rest of the sim cluster already uses, so the event bodies observe the real
// world state.
//
// SEED-FROM-DEFAULTS: each bridge table is seeded from its module's inert
// defaults (Get<X>Hooks(), which are NON-null stubs) and only the wireable fields
// are overridden — REQUIRED because several event bodies invoke hook fields
// WITHOUT a null-check (e.g. event4's CancelMyActions calls
// hk.changePlayerAction / hk.personActionCharId directly), so a zero-initialised
// table would segfault. Unbound leaves keep their safe inert stub.
//
// Glue only — no module logic. The four bridges' callee sets are DISTINCT (no
// ODR clash); this TU owns four process-static hook tables.

namespace guild::world {

// Installs the real reconstructed leaves into EventHooks, Event3Hooks,
// Event4Hooks and Event5Hooks. Idempotent. Forces the shared real He pool +
// command queue to exist first (composes with sim::InstallRealSimHooks3).
void InstallRealEventWiring();

} // namespace guild::world
