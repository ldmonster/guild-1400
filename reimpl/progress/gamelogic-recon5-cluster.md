# gamelogic-recon5 — command/gamelogic/selection/meister/camera cluster

Status: reconstructed 1:1, golden-tested (82 checks / 21 tests, all passing).

## Reconstructed functions

| addr | name | file |
|------|------|------|
| 0x4fa290 | VIBE_Command_ResolveTargetSelectedStat | src/sim/gamelogic_recon5_resolve_stat.cpp |
| 0x4fa3bc | VIBE_Command_ResolveTargetBuildingStat | src/sim/gamelogic_recon5_resolve_stat.cpp |
| 0x452f38 | VIBE_GameLogic_UpdatePlayerTurns | src/sim/gamelogic_recon5_turns.cpp |
| 0x4b94d8 | VIBE_Selection_ClearAll | src/sim/gamelogic_recon5_turns.cpp |
| 0x5383f4 | VIBE_DebugFlag_SetReload | src/sim/gamelogic_recon5_turns.cpp |
| 0x558c34 | VIBE_Meister_PopulateMasterCertificateFields | src/sim/gamelogic_recon5_certificate.cpp |
| 0x43f4d8 | VIBE_Camera_Flight | src/render/camera_recon5_flight.cpp |

## Notes / faithfulness

- **Stat resolvers** compute `(double)ParseInt(name) * 0.01f * (double)stat` then
  truncate to int. The float `0.01f` is `0x3c23d70a == 0.00999999978`, so e.g.
  50*0.01f*25000 = 12499 (NOT 12500) — preserved exactly; golden tests assert it.
  SelectedStat gates kind==2 / params!=null / index!=8 / active-person record.
  BuildingStat (a __thiscall) scans building child objects for the first whose
  category byte == 9, uses its quantity (0 if none), and renders.
- **UpdatePlayerTurns**: minute-diff * 12.8f -> per-tick count; the small-player
  branch uses a fractional accumulator (`frac += band; if (bits(frac) >= 0x3f800000)
  { ++count; frac += -1.0f; }`) where the threshold compares the float BIT IMAGE
  as a signed int to 1.0f's bits — reproduced via memcpy bit punning. >60 players
  forces full-table (768) processing. Balance delta = `(int)(balance*0.89f)-balance`
  (0.89f under 1 -> 1000 yields -111). Image floats: flt_6191C0=12.8,
  C4=0.0625, C8=0.5, CC=-1.0, D0=0.89.
- **Selection_ClearAll**: original quirk — writes alive-byte offsets 536..411648
  (slots 1..768, never offset 0, one past slot 767); returns 411648. Reproduced.
- **Camera_Flight**: DrawTextLabels3D(a1[0]/17 signed, selector 2, a2[0]); logs
  "CameraFlight(): Could not find one or few dummies" on failure; always returns 1.
  Sibling of camera_recon.{h,cpp} 0x43f528/0x43f5dc (which use /17 sel 6, /14).

## Hooks / deferred leaves (inert-default structs, math is in-scope)

All live-state leaves routed through installable hook structs (Recon5StatHooks,
Recon5TurnHooks, Recon5CertHooks, CameraFlightCtx):
FindRecordById, the 768-slot person/selection table, ComputeTotalWealth,
QueryByGoodType/QueryFind/IterNext building graph, the command-delta queue
(BeginDeltaPacket/AppendRawField/QueueRequestState22), MeisterAi_RefillTavernStock,
Character_UpdateGuardBehavior, GameTime_DiffMinutes, the Form/Text/Object UI
subsystem, ConvertX/Money_ConvertToDisplayCoord, RenderFormattedMessage,
DrawTextLabels3D, Script_ReportError.

Nothing omitted; no untranslatable paths. No non-pre-approved tech encountered
(no rule-6 flags).

## Wiring (real callers — for the integrating agent)

- ResolveTargetSelectedStat / BuildingStat: dispatched from the history/console
  target-resolution table alongside the command_recon4_resolve family
  (VIBE_History_ParseContext 0x4fd44c dispatch at 0x6343d8). Wire into the same
  resolver dispatch as the recon4 siblings.
- GameLogic_UpdatePlayerTurns: called from the per-tick cascade — already enumerated
  as `TickPass::kPlayerTurns` in src/sim/command_apply6.h (the tick orchestrator).
- Selection_ClearAll: called from the HUD/selection teardown path referenced in
  src/world/location2.cpp and exposed as the `selectionClearAll` hook in
  src/gui/gui_dialogs6.h. Point that hook at guild::sim::Selection_ClearAll.
- DebugFlag_SetReload: cheat/debug hotkey handler (key 0x17 / SetReload) — see the
  enum in src/sim/cheat_recon.h; bind the SetReload action to it.
- Camera_Flight: base script camera-flight command (xref via the script command
  table near camera_recon.h's CmdCameraFlight entries).
