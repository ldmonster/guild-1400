# 14 — The Per-Frame Game Loop

`VIBE_GameLogic_RunFrameLoop @0x4c09a0` is the engine's beating heart: **one call = one
simulated, rendered frame.** It has **342 callers** and **88 callees**. Understanding it is
the key to understanding the whole game, because every interactive state — the city view,
combat, a court trial, a tavern card game, a modal dialog, a cutscene — is just a loop that
calls `RunFrameLoop` repeatedly until that state's exit condition trips.

```c
// Typical caller shape (hundreds of these exist):
while (!done)
    VIBE_GameLogic_RunFrameLoop(...);   // pump one frame; 'done' set by input/script/timer
```

`size 0xe64`, 807 instructions, 288 basic blocks — a large dispatcher. The flags it reads
each frame (game speed, pause, active modal, fade state) decide which of its 88 callees
actually run.

---

## 1. The frame, in order

The callees group into a fixed pipeline. A single frame runs (conditionally) through:

### 1.1 Input
- `VIBE_Window_PumpMessages @0x4bea64` — drain the OS message queue (Win32→SDL).
- `VIBE_Input_PollMouseDevice @0x40d388`, `VIBE_Input_LatchMouseState @0x40dab8`,
  `VIBE_Input_ResetMouseButtonState @0x40c87c` — sample DirectInput (→SDL) mouse/keyboard
  and latch edge state for this frame.
- `VIBE_Input_HandleGameSpeedKeys @0x4ff800`, `VIBE_Hotkey_HandleKeyPress @0x4ff7a8` —
  speed +/- and user hotkeys.

### 1.2 Deterministic command processing (the netcode spine)
This is what keeps single-player and multiplayer behaving identically (doc 19):
- `VIBE_Command_ReceiveAndQueue @0x493ebc` — pull inbound command packets (network or local).
- `VIBE_Command_QueueRequestPerm30 @0x494a50` — enqueue periodic/permission requests.
- `VIBE_Command_ExecCommands @0x494088` — **execute** all due commands for this tick,
  deterministically and in order.
- `VIBE_Command_FlushSendQueue @0x4934cc` — flush outbound packets to the server/peers.

### 1.3 Simulation step
- `VIBE_Script_StepAllActive @0x445250` — advance every active script coroutine (doc 22).
- `VIBE_Character_Update @0x405148`, `VIBE_Character_ProcessFlaggedLocal @0x40204c`,
  `VIBE_Character_RefreshFlaggedLocal @0x40208c`, `VIBE_Character_FlushPendingMesh @0x426924`
  — advance every actor's action state machine and animation (docs 16–17).
- `VIBE_Character_CollectByOwner @0x4b99ac`, `VIBE_Character_UpdateWorkScripts @0x4b9a5c`
  — per-owner work/behaviour scripts.
- `VIBE_GameObject_DispatchInteractions @0x40e6c0`, `VIBE_GameLogic_Interactions @0x4139a8`,
  `VIBE_Object_UpdateGateContact @0x4b8ba8`, `VIBE_Object_ResolveQuickJumpContact @0x4b950c`
  — world-object interaction resolution (clicks, gates, contact menus).
- `VIBE_GameTick_RunAdvanceGameDialog @0x4c0750` — the round-advance / "next day" driver
  (doc 15); `VIBE_GameLogic_RunPauseLoop @0x56e7e0` — the pause state.

### 1.4 Ambience & audio (doc 30)
- `VIBE_Weather_UpdateSky @0x4c0040`, `VIBE_DayCycle_UpdateBrightness @0x4b2504`,
  `VIBE_Weather_RenderAndThunder @0x4c05ac`, `VIBE_Rain_Render @0x429c38` — sky, day/night
  brightness, weather FX (doc 25).
- `VIBE_Weather_UpdateAmbientLoops @0x57f190`, `VIBE_Ambient_UpdateWildlifeSounds @0x5800f0`,
  `VIBE_VoiceQueue_ProcessNext @0x57eff0`, `VIBE_Music_UpdateOutdoorTrackPlayback @0x581594`
  — ambient loops, wildlife, queued voice lines, the music track.
- `VIBE_Sound3d_UpdateAll @0x424790`, `VIBE_Sound3d_UpdateListener @0x425208`,
  `VIBE_Sound_UpdateVoices @0x445f00`, `VIBE_Audio_ApplyMasterVolume @0x439ddc` — 3D sound.

### 1.5 Camera & selection
- `VIBE_Camera_ComputeWorldTarget @0x4c0864`, `VIBE_Camera_UpdateCombatScroll @0x487b2c` —
  advance the camera.
- `VIBE_Hud_UpdateSelectionAndTargets @0x4ba614`, `VIBE_DragSelect_ApplyToSelection @0x4bdecc`,
  `VIBE_DragSelect_ApplyToUnits @0x4bdc3c`, `VIBE_DragSelect_DrawBox @0x4be154` — unit
  selection and the drag-box.

### 1.6 Render the 3D scene (docs 23–25)
- `VIBE_SceneGraph_CullOctreeAgainstFrustum @0x5f09f0` — frustum-cull the scene octree.
- `VIBE_Render_RenderMainViewFrame @0x5b6074` — **the universe render chain**: build the
  draw list from the scene graph, radix-sort it, rasterize (software reference or Vulkan).
  This is the single biggest renderer entry; see doc 23.
- `VIBE_Fade_UpdateAll @0x41f47c` — screen fades/cross-fades.

### 1.7 HUD & overlays (doc 10/11)
- `VIBE_Widget_DispatchMouseClick @0x421594`, `VIBE_Hud_HandleMouseClick @0x4bc280` — route
  clicks to widgets/HUD.
- `VIBE_Hud_DrawSelectedUnitInfo @0x4bae88`, `VIBE_Hud_DrawDamageLabels @0x4bb7a0`,
  `VIBE_Hud_DrawObjectNameLabels @0x4bbaec`, `VIBE_Hud_DrawAnimalLabels @0x4bbbbc`,
  `VIBE_Hud_DrawStatusBanner @0x4bcb74`, `VIBE_Hud_DrawScrollArrows @0x4bbd34`,
  `VIBE_PlayerBar_BuildContent @0x4b11e4` — the HUD layers.
- `VIBE_Widget_SetTooltipText @0x421a24`, `VIBE_Tooltip_DispatchByType @0x4f7424`,
  `VIBE_DragCursor_SetSprite @0x41fcbc`, `VIBE_DragCursor_RenderForState @0x4c094c` — tooltips
  and the drag cursor.
- Many callers are themselves modal windows that `RunFrameLoop` can re-enter: dialogs
  (`VIBE_Dialog_ShowMessageBoxModeless @0x4adc60`), panels
  (`VIBE_Panel_RunInventory @0x54f668`, `VIBE_Panel_RunPlayerStats @0x552234`,
  `VIBE_StatPanel_ShowCityStatistics @0x55f4ac`), the family tree
  (`VIBE_Stammbaum_RunFamilyTreeWindow @0x55ab84`), options
  (`VIBE_Menu_RunOptionsMain @0x56dccc`), chat (`VIBE_ChatConsole_BuildWindow @0x4bfc48`,
  `VIBE_QuickChat_UpdateWindow @0x4bff90`).

### 1.8 Present & housekeeping
- `VIBE_Render_PresentFrame @0x4349e4` — present the back buffer to the screen
  (DirectDraw/Vulkan + SDL).
- `VIBE_TimeBase_SetProcInterval @0x44e3ac` — pace the frame to the game-speed setting.
- `VIBE_Cutscene_ProcessActive @0x4ac31c` — step any active cutscene.
- `VIBE_DecompressState_Blob @0x423500`, `VIBE_Decompression_Finalize @0x4235dc` — despite
  the symbol names, these are **DirectDraw surface lock/unlock** helpers (the former calls
  `IDirectDrawSurface::Lock` via vtable+100 and routes failures to
  `VIBE_Render_ReportDDrawError @0x42e4ec`): they lock the back buffer for the frame's
  software draws and unlock it before present. DDraw → Vulkan boundary (doc 23). The codec
  named in doc 29 is a different cluster (`VIBE_Inflate_*`).
- `VIBE_Net_LoadAndSyncSession @0x56da74`, `VIBE_Net_RunWaitLoopWithStatus @0x4beb80` — for
  network games, keep the session synced.

---

## 2. Why this design matters for the reconstruction

1. **Determinism.** §1.2 runs *before* §1.3, and all state changes flow through the command
   queue. Two machines fed the same command stream produce identical simulations — the basis
   of the lock-step multiplayer (doc 19). The reimplementation must preserve this ordering
   exactly.
2. **Re-entrancy.** `RunFrameLoop` is called from inside dialogs/panels that were themselves
   reached from a `RunFrameLoop` callee. The loop is re-entrant by design; modal UI is "just
   another frame loop with a different exit condition." Global flags (`dword_63CC34/38/48`,
   `word_63C740`) coordinate which modal owns the frame.
3. **Conditional pipeline.** Most callees are gated by per-frame flags (paused? fading? a
   modal active? network game? weather on?). A faithful port reproduces the gates, not just
   the call list.

## 3. Entering and leaving the loop

- **Entered** from `VIBE_Menu_RunMainMenu` (doc 08), from `VIBE_GameLogic_InitOrLoadSession`
  (doc 12) once a session is live, and from every modal window/panel/cutscene/combat routine.
- **Left** when the owning loop's exit flag trips: a menu choice, a dialog OK/Cancel, a
  script `wait` completing, a combat resolving, or a global quit flag (`dword_63CC38` →
  quit to menu, `dword_63CC48` → quit to desktop) observed back in WinMain (doc 01 §2.6).

See [23 — Render engine: universe chain](23-render-universe-chain.md) for what
`RenderMainViewFrame` does, [19 — Commands & netcode](19-commands-netcode.md) for the command
queue, and [15 — Game time & tick](15-game-time-tick.md) for how rounds advance.
