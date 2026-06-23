#pragma once
// wire_apply_input — binds the cursor-click ORDER-ROUTER and target-pick GUI
// bridges to their real reconstructed cross-cluster leaves (rule 13). Glue only;
// no module logic lives here.
//
// Two of the five bridges this wiring agent surveyed have a clean reconstructed
// leaf to bind; the other three are reported (see wire_apply_input.cpp) as having
// ZERO bindable fields (string-table / live-VFS-handle / live-world-state leaves
// with no standalone reconstructed home), and the input-command apply bridge is
// already REAL by default (its inert-default apply mutates the live entity arrays).
//
//   IssueOnObjectHooks  (sim/command_apply12.h) — labelStrncmp -> util::StrncmpN
//                        (VIBE_Util_StrncmpN @0x5e9ee0).
//   ActionTargetPickHooks (gui/action_target_pickn.h) — lightSetGrayThunk ->
//                        VIBE_Light_SetGrayColorThunk @0x5c6af0 (render::
//                        BroadcastGrayDword), the genuine grey-broadcast effect.
//
// SEED-FROM-DEFAULTS: each installer starts from the module's inert defaults
// (ActionTargetPickHooks_Default() / the empty-std::function table whose unset
// fields the command_apply12.cpp call site guards) and overrides ONLY the wireable
// field, so unbound / unchecked fields keep their safe inert behaviour.
namespace guild::sim {

// Bind the reconstructed order-router / target-pick leaves into their previously
// fully-inert hook bridges. Idempotent; safe to call once at command-system init.
void InstallRealApplyInputWiring();

} // namespace guild::sim
