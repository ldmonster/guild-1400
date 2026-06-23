# WIRE-GUI — wire the reconstructed GUI into the live call tree (rule 13)

Agent: WIRE-GUI. Date: 2026-06-22. Owned bind files only
(`src/app/wiring.{cpp,h}`, `src/play/gamelogic_recon.{cpp,h}`, play-side HUD/menu
bind files, `src/gui/wire_*.cpp`). No git, no `build/` delete, targeted builds only.

## Method / tracing (MCP-verified call edges)

- `xrefs_to 0x4c09a0` (VIBE_GameLogic_RunFrameLoop) — the per-frame tick is the
  modal pump invoked by the whole GUI spine: `RunFrameLoopWrapper` (0x50c720), the
  combat/council/court loops, every cutscene, **every dialog/message-box**
  (`VIBE_Dialog_ShowMessageBox*` 0x4ad6f0/0x4acbd0/0x4ad9dc/0x4adc60/0x4adea4), the
  building/trade/upgrade windows, the chat console, hotkey-assign, history scrolls,
  and `VIBE_Scene_RunMainFrameLoop` (0x50f0c0). Confirms the architecture: dialogs/
  panels run their own modal loop that re-enters RunFrameLoop.
- `xrefs_to 0x4146ac` (VIBE_Window_RenderUpdates) — the window-update tail is called
  by progress/loading screens and `VIBE_Text_RenderRichString`, not the main per-
  frame draw. Already reachable from the reconstructed dialog siblings (gui_dialogs7).

## State of the GUI wiring found (already wired — verified, not changed)

The GUI subsystem is far more wired than the brief implied. All confirmed already on
the live tree:

- **Spine `GameApp::RunFrameLoop`** (src/app/frameloop.cpp) routes EVERY GUI hook to
  REAL reconstructed code via `RealSubsystems` (src/app/wiring.cpp):
  `widgetDispatchMouseClick`→gui::ResolveClickedSlot/RouteClick;
  `hudHandleMouseClick`→gui::Hud_DispatchClick; `hudSelectionAndTargets`→
  gui::StatusText_Register/DamageLabel_Register; `tooltipDispatch`→
  gui::Tooltip_ClassifySubject/SelectBuilder; `hudLabelsAndCaption`→
  gui::Hud_LabelLayout/Hud_ButtonRowLayout; `optionsChatHotkeyPanels`→gui::ChatConsole;
  `renderMainViewFrame`/`presentFrame`→render leaves. (Kind::Real recorded.)
- **`--play` main menu**: `play::RunNativeMainMenu` drives the BYTE-FAITHFUL
  `gui::Menu_RunMainMenu` (0x529d08) with real sub-screen RunXxx (choosecity/
  loadgame/options/credits/mission) via gui::main_menu_wire.
- **`--play` session HUD**: `play::SessionHud::Render` runs each frame (caption/
  player-bar/markers/status) on the real draw leaves; `play::InstallRealHudBridge`
  de-inerts the sprite blit (render::ShapeShowFromBank 0x5d861c) — installed from the
  live session_hud.cpp / render_binder.cpp AND the spine's EnableRealRenderBridges.
- **`--play` session panels/tooltips**: `play::session_panels` drives the real
  tooltip lifecycle (gui::Tooltip_Dispatch 0x4f7424 + content builders) and the
  selected-entity info panel (gui::InfoPanel_Update 0x4b84c0 + builders).
- **Scene-walk + terrain + HUD** de-inerted via `InstallRealSceneBridge` /
  `InstallRealHudBridge` in `RealSubsystems::EnableRealRenderBridges`.

## WIRED this pass

### `play::InstallRealRenderBridge()` added to `RealSubsystems::EnableRealRenderBridges`
File: `src/app/wiring.cpp` (added include + one install call in the render-bridge cohort).

The character attach-offset / head-variant render cohort (`sim::CharRenderHooks`) was
reconstructed and de-inertable (src/play/wire_render_bridge.cpp) but **never installed
on any live path** — only its own tests called it. It belongs to the same "real render
bridges" feature the engine enables for the in-game frame:
- VIBE_Character_ComputeAttachOffset @0x404860 → util::PointThroughBoneChainPivot
  (0x5c8d0c) + mesh-root translation (mesh+132/136/140), replacing the inert
  identity-copy / zero-offset defaults.
- VIBE_Character_ApplyHeadVariant @0x57c548 → sim::ObjectSelectTextureSet (0x5b3f54),
  replacing the inert return-0 default.

Grounding (IDA): decompile of 0x404860 / 0x57c548 shows the original wires exactly
those three CharRenderHooks slots to these reconstructed leaves. The install is the
correct rule-13 grouping (cohort with InstallRealSceneBridge / InstallRealHudBridge).

OBSERVABLE-BEHAVIOR NOTE: this is a correct binding but does NOT change the current
playable output, because the CONSUMING functions (sim::Character_ComputeAttachOffset /
ApplyHeadVariant) are not yet reached by the spine's scene-walk draw leaf
(render::ProcessSceneNodeAppend does not call the Character attach subsystem) nor by
the `--play` session (play::person_render / scene_recon2_orchestrator are not driven
by RunSdlSession today). So no golden shifted (correctly — verified by re-running the
suites + guild_run, output byte-identical to baseline). The leaves are now de-inerted
the instant a live attached-actor caller is wired (the natural next step), instead of
sitting orphaned. No golden was changed because none should change.

## DEFERRED (documented handoffs — not faked, per rules 8/13)

- **`play::InstallCharAnimDriver` (wire_char_anim.cpp)** — DEFERRED. Drives posed
  (morph-blended) NPC meshes via the real leaves VIBE_Mesh_InterpolateMorphVertices
  (0x5c953c) + VIBE_Anim_AdvanceFrameIndex (0x5ccf18). Requires a per-entity→.baf
  clip binding that is a DOCUMENTED UNRECONSTRUCTED gap: the portable entity arrays
  (ObjectRec/Person/SceneNode) carry only id+type, NOT the engine's per-object anim-
  track handle (record+380). Auto-wiring it live would require fabricating that
  binding (rule-8 violation). Left installable + test-bound; the live city_view3d
  already does native per-segment morph blend at its line ~2244 for the meshes it
  owns. HANDOFF: needs the entity→clip handle field modeled before a live install.
- **`play::InstallInertAtmosBridge` (wire_atmos_bridge.cpp)** — DEFERRED by design
  (named "Inert"): the atmos/sky bridge is intentionally inert; no real leaf to
  point its slots at on this path.
- **`play::InstallInertTerrainBridge` (wire_terrain_bridge.cpp)** — DEFERRED by
  design (named "Inert"); the live frame uses FrameRenderTerrain (the real floor
  leaf) wired in EnableRealRenderBridges instead.
- **`play::RunFrameLoop` + `IGameLogicHooks` (gamelogic_recon.cpp, OWNED)** —
  intentionally NOT live-wired. It is the MORE-faithful 1:1 RunFrameLoop (0x4c09a0)
  that KEEPS the run-state predicates the app/frameloop.cpp twin drops; the live
  spine uses the app/ twin (GameApp::RunFrameLoop) and the `--play` path uses
  RunSdlSession. Its inert hooks are by-design for isolated verification (see the
  module header). Re-wiring it as the live driver is a spine-architecture change
  outside a pure GUI-wiring pass; left as-is (no regression, no orphan — it is the
  reference twin). Documented here for the next spine-consolidation pass.

## VERIFY — build + suites + boot (all green)

- `guild` lib: builds clean (serial `-j1`; the shared `build/` tree had transient
  archive races + a concurrent agent's mid-edit compile breaks in
  src/audio/audio_recon_engine.cpp and src/ai/meister_workstation.cpp — NOT my files;
  both cleared on retry).
- Affected suites (10/10 pass): wire_render_bridge_e2e/_test/_itest,
  playable_flow_e2e_test, app_full_wired_playthrough_e2e_test, session_hud_e2e_test,
  app_wiring_real_bridges_e2e_test, gui_hud_e2e_test, app_menu_loop_e2e_test,
  gui_core_e2e_test. Spot-checked gui_form_real_e2e_test (2/0) + wire_hud_bridge_e2e
  (skips on asset dir, OK).
- `build-vk/guild_run` rebuilt with the change.
- BOOT: `guild_run --play --game-dir europe_guild_1400_original --frames 60`
  (headless, lavapipe + dummy SDL) → exit 0; loaded=1, liveObjects=55,
  view3d=1(796 inst, 786432 px), framesPresented=60, drives the menu/render/HUD path.
  Output byte-identical to pre-change baseline (expected: latent de-inert).

## Net

GUI is comprehensively wired already. This pass added the one missing render-cohort
install (CharRenderHooks attach/head leaves) at the correct spine bind site, and
documented the three remaining installers as deferred-with-reason (one rule-8 gap:
entity→clip handle; two intentionally inert; plus the gamelogic_recon reference twin).
No golden changed (none should). Suite + playable boot green.
