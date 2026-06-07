#pragma once
// real_hooks2 — SECOND wave of the cross-module "real wiring" installer for the
// sim/ai cluster. Glue only: it binds a further set of per-module Set*Hook entry
// points (the abstract command-emit / pamphlet sinks and the character render/
// anim turn bridge) to their REAL reconstructed targets, now that more modules
// (command_codec, charaction_walk) exist. See real_hooks.h for the first wave.
//
// This file contains NO module logic — only the indirection that connects the
// already-translated modules to each other.
//
//   ┌─ category ──────────────┬─ hook (setter) ─────────────────────┬─ real target ─────────────────────────────┐
//   │ interaction command sink│ sim::SetCommandEmitHook             │ sim::QueueRequest17 (command_codec) on the │
//   │   (abstract tag,action) │   (interaction_handlers.h)          │   installer's real sim::CommandQueue       │
//   │ AI pamphlet command emit│ ai::SetPamphletCmdHook (intrigue.h) │ sim::QueueRequest16 (command_codec)        │
//   │ character turn bridge   │ sim::SetCharActionHooks.turnStep    │ sim::WalkRotateTowardHeading (charaction_  │
//   │   (render/anim leaf)    │   (charaction.h)                    │   walk.cpp) — real heading interpolation   │
//   └─────────────────────────┴─────────────────────────────────────┴────────────────────────────────────────────┘
//
// The other CharActionHooks fields (attachAnim / animDone / setCarried /
// setVisible) use inert backends (matching charaction.cpp's own defaults): there
// is still no reconstructed renderer/anim attach leaf, so binding them would be
// inventing behaviour. They are filled (not left null) only because the step
// handlers call every field unconditionally. The turnStep field is the one whose
// effect is a PURE reconstructed math function (WalkRotateTowardHeading), so it
// is the only render/anim-bridge field wired to real code. (LISTED, not silently
// skipped.)
//
// InstallRealSimHooks2() is independent of InstallRealSimHooks(); call both. It
// reuses the same shared real CommandQueue (RealCommandQueue()).
#include "sim/command.h"

namespace guild::sim {

// Installs the second-wave wireable hooks (the table above) to their real
// reconstructed targets. Idempotent. The command-emit hooks enqueue onto the
// shared CommandQueue returned by RealCommandQueue() (see real_hooks.h), so the
// two installers compose: a test can call InstallRealSimHooks() then
// InstallRealSimHooks2() and inspect one queue.
void InstallRealSimHooks2();

} // namespace guild::sim
