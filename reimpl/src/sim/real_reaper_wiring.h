#pragma once
// Wires the reconstructed full Reaper event functions (npcevent_reaper_full) into
// the live NpcEventHooks bridge (sim/npcevent_steps.h). Before this, NpcEventHooks
// was fully inert at runtime (nothing installed it), so the plague-NPC reaper steps
// were no-ops. This binds reaperApproach/Move/CachePose/UpdateSound to the 1:1
// reconstructions; the remaining NpcEventHooks fields stay null (every call site in
// npcevent_steps.cpp null-checks, so unbound fields keep the inert default).
//
// The reaper functions' own engine-leaf sub-callees (scene-node resolve, bone
// transform, heightmap, 3D sound) route through ReaperFullHooks (npcevent_reaper_full.h)
// and remain inert by default — the reconstructed CONTROL FLOW now runs over them,
// exactly the established headless-faithful wiring pattern.
namespace guild::sim {

// Install the reaper bindings into the global NpcEventHooks. Idempotent.
void InstallRealReaperWiring();

} // namespace guild::sim
