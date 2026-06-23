#pragma once
// =============================================================================
// guild::world — wire_worldnet: the WORLD-NET / RENDER-TAIL hook-bridge audit.
//
// This installer is the wiring counterpart for FOUR inert hook-bridge tables that
// sit on the boundary between the reconstructed game logic and the host:
//
//   WorldHistory2Hooks   (world/world_history2.h)  — the VIBE_He_* history/event
//                          engine tail: per-player news dispatch + the floating
//                          3D event-icon pool (universe-node / mesh teardown).
//   NetModeHooks         (gui/netfile_run.h)        — VIBE_Menu_ChooseNetworkMode:
//                          the multiplayer mode-chooser sub-screen (form build,
//                          frame-loop pump, host/search/profile child screens).
//   TransitionHooks      (play/menu_recon_transition.h) — VIBE_Transition_FadeOut/
//                          In: the screen fade register/pump/scene-render spine.
//   RenderLeaves9Hooks   (render/render_leaves9.h)  — VIBE_Shape_* batch-9: the
//                          frame rasterizer + 8/24->16bpp shape converters.
//
// AUDIT RESULT — every field of all four tables is a HOST-BOUNDARY leaf (Vulkan/
// SDL render + window + menu frame-loop + universe/object graph + the not-yet-
// reconstructed per-player news dispatch) OR a reconstructed sibling whose
// signature does NOT faithfully match the hook field shape:
//
//   * RenderLeaves9Hooks.FrameDataProcess — the reconstructed FrameDataProcess
//     (render/animation_decode) is `bool(int,int,const u8*,const FrameBlitState&)`;
//     the hook is `int(*)(int,int,void*,int)`. Binding it would require fabricating
//     a FrameBlitState from a raw int — NOT byte-faithful (rule 8). INERT.
//   * RenderLeaves9Hooks.ConvertRgbTo16/Convert8To16 — REAL since the
//     render/shape_convert16 reconstruction (VIBE_Shape_ConvertRgbTo16 @0x5d7c0c
//     / VIBE_Shape_Convert8To16 @0x5d7924): the installer now binds them via
//     render::InstallShapeConvertersIntoLeaves9(), so Shape_ConvertToNew
//     @0x5d8080 and ShapeBankConvertNew @0x5d80a8 run the real conversion.
//   * TransitionHooks.fadeRegister — the reconstructed Fade_Register takes a u8
//     FLAGS byte (`*v13 = a7`, verified @0x41f0e8) and a back-buffer SOURCE ptr;
//     the hook passes a `const char* tag` ("BLACK") string. Shapes disagree.
//     fadeUnregister: reconstructed `i32 Fade_Unregister(i32)` vs the hook's
//     `void(*)(i32,void*)`. The rest (runFrameLoop/render*/groundplan*) are
//     window/scene render. INERT.
//   * NetModeHooks.RunHostNetworkSetup/SearchNetworkGames/ChooseNetworkProfile —
//     net/lobby only reconstructs the advert-BUILD slice (BuildHostAdvert(city,
//     count,scenario,out)->LobbyAdvert); the hook is the full GUI-driven `bool()`
//     sub-screen (player/city pickers + frame loop). Binding the slice to the
//     full screen would be a cheap analogue (rule 8). The rest are form-build /
//     frame-loop / edge-source render. INERT.
//   * WorldHistory2Hooks — processPlayerNews (VIBE_He_ProcessPlayerNews @0x4c4be8)
//     is NOT reconstructed anywhere in src/; every other field is universe-slot /
//     object-handle / icon-mesh render or an entity-record host pointer write.
//     INERT.
//
// So this is a ZERO-BINDABLE audit: InstallRealWorldNetWiring() SEEDS each POD
// bridge from its module inert defaults and re-installs them unchanged (a true
// no-op that pins the live state to the documented inert defaults and reserves the
// wiring point), and leaves the NetMode vtable on its module default. Nothing is
// bound to a fabricated target. When a future agent reconstructs a faithful
// per-player-news dispatch / fade / converter / net sub-screen, the binding lands
// here.
//
// Two SIBLING world/net bridges that this audit covered are ALREADY wired by
// parallel agents and are deliberately NOT touched here:
//   * SetElectionHooks    — wired by world/wire_election.cpp (GuildSetElectionHooks).
//   * SetSupervisionHooks — wired by world/wire_economy2.cpp.
// =============================================================================

namespace guild::world {

// Seed-from-defaults audit installer for the four world-net / render-tail bridges
// (WorldHistory2 / NetMode / Transition / RenderLeaves9). Idempotent; binds no
// fabricated targets — every field stays its documented inert default. Safe to
// call from the boot wiring alongside the sibling InstallReal*Wiring() installers.
void InstallRealWorldNetWiring();

} // namespace guild::world
