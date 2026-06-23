#pragma once
// wire_dialogtut — binds the *bindable* fields of the Dialog/Tutorial/Gesetz-Desc
// hook bridges to their real reconstructed cross-cluster leaves (rule 13). Glue
// only; no module logic.
//
// Bridges surveyed (header / setter):
//   world/location3.h          SetLocationDialogHooks     — ZERO bindable (all GUI /
//                              voice / command-queue, and findExistingRequest is a
//                              LOSSY 2-arg abstraction over a 3-pair He filter +
//                              a +43 field compare it cannot faithfully replay,
//                              rule 8) -> left at its module inert default.
//   world/history_mission.h    SetMissionDialogHooks      — ZERO bindable (all
//                              Form / Text / Voice / Audio / GameLogic frame-loop
//                              glue, rule 3/4/5) -> left inert.
//   world/tutorial_mission.h   SetTutorialMissionHooks    — ONE bindable field:
//                              gameTick() == dword_62EB38 (sim::g_gameTick). All
//                              other fields are GUI / voice / audio / frame-loop.
//   play/tutorial_recon3_stepvoice.h  SetTutorialStepVoiceHooks — HideReminderPanel
//                              is pure GUI glue (rule 3) -> left inert.
//   world/office_law3.h        GesetzSetDescHooks(portrait, render, ctx) — the
//                              `portrait` resolver is bindable (VIBE_Person_FindRecordById
//                              @0x58bc6c -> record word@+0, else -2); `render`
//                              (VIBE_Text_RenderFormattedMessage @0x59f99c) is GUI
//                              text raster (rule 3) -> stays inert (null).
//   gui/mission_load_run.h     SetMissionNameHooks        — ALREADY WIRED by
//                              world/wire_meister_loc.cpp (Menu_SetMissionNameHooks)
//                              -> SKIPPED here.

namespace guild::world {

// Seed-from-defaults installer: copies each touched bridge's current (inert)
// hook table, overrides ONLY the fields with a faithful reconstructed leaf, and
// re-installs. Idempotent; references process-lifetime hook storage.
void InstallRealDialogTutWiring();

} // namespace guild::world
