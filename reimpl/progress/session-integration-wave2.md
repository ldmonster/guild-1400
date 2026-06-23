# Session integration — wave 2 (the playable city session)

The wave-2 centerpiece: the 12 wave-1 subsystems wired into ONE live interactive
session. `guild_run --play` now enters a city session that renders the REAL 3D
city through the universe chain, commits the menu's new-game choices into the
live world, runs the continuous game clock, drives picking/selection through the
reconstructed input chain, and composites the HUD/tooltips/info-panel with real
converted gilde.gfx artwork.

Files (this wave): `src/play/sdl_session.{h,cpp}` (the integration),
`apps/guild_run.cpp` (menu-result -> session entry), `src/play/universe_render.cpp`
(live-ambient shade), `tests/e2e/playable_flow_e2e_test.cpp` (TEST E),
`tests/unit/raster_clip_test.cpp` (new), plus two targeted memory-safety fixes
(`src/gui/form_loader.cpp`, `src/render/raster_textured.{h,cpp}` — see below).

Everything is ADDITIVE: every new `SdlSessionConfig`/`SdlSessionTrace` field
defaults to the wave-1 behaviour, so all 13+ pre-existing ScriptedPlatform test
binaries (sdl_session unit/itest/e2e, mode_fsm, input_router, hud_binder,
determinism, run_interactive_app, play_input_command, playable_flow A–D, ...)
pass unchanged. `guild_run --play` turns the new wiring on.

## What got wired (the task list, item by item)

1. **NEW-GAME COMMIT — wired.** `cfg.applyNewGame` + `cfg.newGame`
   (`gui::NewGameParams`, threaded from `NativeMenuResult::params`). Right after
   the world load — the original's `VIBE_Command_EnqueueInheritanceTransfer
   @0x5336f0` call site at `0x533e03` — the session replays the exact
   `newgame_apply_e2e` sequence: `ResetApply5State` + `ResetPersonCreate` +
   `ResetApply6State`, `RegisterApplyHandlers6(q)` (the opcode-27 relations),
   `g_personNextId = maxLoadedId+1`, `g_personArrayLoaded`, `Srand(cfg.seed)`,
   `ApplyNewGameParams(p, p.difficulty, q, {rateByte=100})`. The player +
   parents + relations + start gold land in the live `sim::g_persons`.
   `guild_run` passes `m.params` and arms it with `m.params.started`.

2. **REAL 3D CITY VIEW — wired.** `cfg.city3d`: `CityView3D::Init(&fs)` →
   `io::LoadWorldEx(cty, world, &sceneBlob)` (the EMBEDDED city scene stream the
   original hands to `Scene_LoadFromStream @0x5e7e38` at PostLoadInitScene) →
   `LoadCityFromWorld(blob)` → `SetHooks({})` (REAL defaults: the
   `RebuildModelByOwner @0x5a8140` owner-id node match + the node's shipped
   mesh) → `BindWorldObjects()` (AUGSBURG: 53 objects at real placements).
   Per frame `CityCamera3D{eye, rot}` comes straight from
   `SessionCamera::pose()` (camera node +76 eye / +132 euler), then
   `RenderFrame(cam, opt)` + `PresentToDevice(device)` (blit + present; the HUD
   composites onto `view.surface()` BEFORE the blit, the DrawUniverseAndStats
   "world, then stats" order). The legacy top-down `RealCityRenderer` remains
   the default (`city3d=false`) and the fallback when the 3D boot fails.

3. **CONTINUOUS GAME CLOCK — wired.** `cfg.continuousClock`: one
   `play::SessionTick`; `SetGameSpeedLevel(cfg.gameSpeedLevel)` →
   `SyncClocksToDayStart()` → `BeginDay()`; per frame
   `OnFrame(elapsedMs)` (own prev-ms accumulator so the legacy camera dt is
   untouched); on `dayEnded`: `FastForwardToDayEnd()` → `play::RunGameDay`
   (the SAME transition SPACE triggers, same seed scheme) → `RollToNextDay()` →
   `SyncClocksToDayStart()` → `BeginDay()`. SPACE day-advance stays. The HUD
   date/time reads `tick.worldTime()`; the hh:mm:ss caption feeds the EXACT
   inverse of `gui::Clock_ComputeTimeOfDay`'s `(tick*0.00625+0.5)*dayLength`
   map so the real splitter displays the world clock (the corrected-provenance
   note in `src/gui/hud.h` says the HUD should read `qword_13CE852`).

4. **REAL INPUT/SELECTION — wired (city3d mode).** `play::SessionInput` is THE
   pick/selection driver: per frame `ClearEntities()` → one `UpdateEntity` per
   bound object with its projected screen box (world pos → `render::WorldToView`
   over `MatrixFromEuler(-rot)` → the engine projection scalars
   `x*scale/z + W/2`, `-y*scale/z + H/2`, scale = `viewScale` = W/2; typeByte 0
   = the city-entity profile) → `Frame(shim mouse/keys/clock)`. `Result.pickedId`
   is the hover/click pick; a left-click edge with a pick issues the order
   through `IssuePickedOrder` (the same real CommandQueue path). Right-click
   deselect and the commit-on-release gate are inside the chain
   (`Selection_CommitContact @0x4b950c`). One driver owns the hooks; the legacy
   mode keeps `SessionSelect` + `rc.Pick` byte-identically.
   `ResetSessionInputGlobals()` (the canonical test reset) runs at 3D session
   start so reruns in one process are deterministic.

5. **REAL HUD ARTWORK — wired.** `cfg.realHudArt` (default on): after
   `SessionHud::Init` loads gfx/gilde.gfx, the session reads the gfx-1403 icon
   record's raw SHAPBANK blob (`archive().record(1403)`, +48/+56 offset/size —
   the `VIBE_State_Helper @0x40e014` d2_LoadObj read) and feeds it through
   `play::SetHudSpriteBankFromGfx` → `ShapeBankConvertNew @0x5d80a8` →
   `ShapeConvertRgbTo16 @0x5d7c0c` → the HUD blits a REAL converted depth-1
   bank. Released (`SetHudSpriteBankFromGfx(nullptr,0)`) at session end.

6. **TOOLTIPS + INFO PANELS — wired.** `cfg.uiPanels` (default on): per frame a
   `play::SessionPanelsInputs` rides `SessionHud::Inputs::panels` — cursor,
   `scenePickActive=1`, `hoveredTooltipId` from a CLICKLESS pick at the cursor
   (3D: the SessionInput hover pick; legacy: `rc.Pick` at the cursor),
   `hoverKind=kObject` + object code/class 32, `selection.sceneObject` from the
   live selection. The REAL dispatch lifecycle (`@0x4f7424`) and
   `InfoPanel_Update @0x4b84c0` run; with nothing selected the engine's
   STANDARD panel builds — visible in the frame dump. The per-entity content
   feeds (`PanelObjectHover` record/economy views) stay null → zeroed defaults
   (named gap below).

7. **ATMOSPHERE LIGHTING — wired.** Per frame (when `cfg.atmosphere`):
   `atmos.Frame(...)` then `play::ApplyAtmosLightingFrame(atmos, nowMs,
   cursor&)` (the `BlendBandLighting @0x5b85e4` ambient store +
   `Light_RefreshAllObjects @0x5c886c` rebuild), bracketed by
   `render::LightAtmosBeginUniverseFrame()` (the 0x5b3982 budget reset) /
   `LightAtmosEndUniverseFrame()` (the 0x5b3c19 frame-end clear) around the
   render. The legacy renderer now sets `Options::atmosRelight = cfg.atmosphere`
   (the `LightAtmosEnsureNodeLit` rebuild arm). In
   `src/play/universe_render.cpp` the fixed `FinalizeVertexShadeLuma(200,200,200)`
   stamp now reads the LIVE ambient triple `render::LightAtmos().ambientR/G/B`
   (flt_64A074/78/7C — the values BlendBandLighting stores; static image
   200/200/200, so default-identical). Decision: the ambient-globals read is the
   `BuildObjectCache @0x5c8218` SEED semantics — the right minimal step for the
   single-mesh universe driver; the full `LightAtmosObject`/EnsureNodeLit
   registration is the RealCityRenderer/per-node route and stays there.

8. **LOAD GAME — wired.** `NativeMenuResult::kLoadGame` → `guild_run` sets
   `cfg.loadSavePath = m.savePath`; the session enters via
   `play::LoadLiveWorld(fs, savePath, seed)` and SKIPS ApplyNewGameParams.
   3D note: `SaveLiveWorld`'s partial .SAV embeds NO scene stream, so the 3D
   view falls back to `LoadCity(cfg.cityName3d)` (scenes.BIN stadt_*.ed3) when
   set, else the legacy renderer (named gap below).

9. **SceneMainLoop bindings — DEFERRED (documented).** `play::SceneMainLoop_*`
   (0x50f0c0) is the full in-city frame spine; adopting it as the session
   skeleton means replacing the whole RunSdlSession loop body (its frame driver
   is `runFrameLoop(0x67FFF)` → `play::RunFrameLoop`, which owns input, audio,
   weather, selection, the HUD click dispatch and the clock pump internally).
   That wholesale swap cannot be additive-with-defaults inside this wave's
   "existing scripted binaries stay green" constraint — the current loop calls
   the same reconstructed pieces (Camera_Update via SessionCamera, RunFrameLoop's
   input ladder via SessionInput, the clock via SessionTick, the day gate via
   RunGameDay) at the same per-frame cadence. The swap is the natural next
   wave once the session loop itself can be replaced rather than extended.

## Per-frame loop order (as wired, city3d mode)

```
(0)  atmos.Frame (Weather_UpdateSky + DayCycle_UpdateBrightness)
     ApplyAtmosLightingFrame (ambient store + RefreshAllObjects, cursor-stepped)
(0b) SessionTick.OnFrame(elapsedMs)        [continuousClock]
       dayEnded -> FastForwardToDayEnd; RunGameDay; RollToNextDay;
                   SyncClocksToDayStart; BeginDay
(1)  LightAtmosBeginUniverseFrame
     build SessionHud::Inputs (player cash from g_persons, world clock,
       selection) + SessionPanelsInputs (hover id from last frame's pick)
     CityView3D::RenderFrame(SessionCamera::pose())     [the universe chain]
(1b) SessionHud::Render onto view.surface()             [HUD + tooltip + panel]
(2)  CityView3D::PresentToDevice(device)                [blit + present]
     LightAtmosEndUniverseFrame
(3)  plat.pumpMessages()                                [false -> quit]
(4)  getMouse; ESC check
(4b) SessionInput: ClearEntities -> UpdateEntity (projected boxes of every
       bound object) -> Frame(mouse/clock)   [latch @0x40dab8 -> MainLoop scan
       @0x414a38 -> commit @0x4b950c]; click edge + pickedId -> IssuePickedOrder
       (the real CommandQueue order)
(4c) SPACE edge -> RunGameDay              (unchanged)
(4d) F5/F9 quicksave/quickload             (unchanged; quickload re-runs
                                            BindWorldObjects in 3D mode)
(4e) SessionCamera::Frame (arrows/edge pan, drag pan/rotate, wheel-less zoom)
(4f) SessionAudio::Frame                   (unchanged)
(5)  frame cap; latch lastMouse
```

Legacy mode (`city3d=false`) keeps the wave-1 order byte-identically; the only
additions are the panels feed (clickless `rc.Pick` hover), the atmos-lighting
apply, `atmosRelight`, and the real HUD bank — pixels only, every existing
trace assertion (frames/picks/orders/hashes) unchanged (suites re-run green).

## Camera (the "fix camera" core)

* `SessionCamera` is the one camera: `pose()` = camera node +76 eye / +132
  euler; the 3D view builds the engine basis `MatrixFromEuler(-rot)` — no
  invented math.
* `BindTerrain`: a `render::Heightmap` built over the REAL city world bounds
  with the 1:1 `DeriveGridScaleXZ` (the testable core of
  `VIBE_Heightmap_BuildTerrainMesh @0x5c5610`), `originY = minY + 1.0`. The
  HEIGHT BYTES are zero — **named gap**: the full BuildTerrainMesh height fill
  from the "boden" node mesh is not reconstructed, so the eye follows the
  city's ground plane (terrain + 450 + span*zoom through the real
  `Camera_AnchorToTerrain @0x4b2900` / `ClampToTerrainHeight @0x4b2a0c`).
* Initial seat (host default, like `OverviewCamera`): eye behind the city
  centre at the distance where the anchored zoom-0 pitch (`baseAngle`,
  fwd = (0, sin p, cos p)) looks at the centre.
* Edge-scroll/arrows/drag/rotate run through the real cluster
  (`Camera_Update @0x4b4c68`, `EdgeScroll @0x4b2c34` inside). NOTE: a cursor
  parked at (0,0) (NullPlatform headless) is inside the edge band and pans the
  camera off the city — correct behaviour; the e2e parks the cursor mid-screen.

## Additive config / trace fields

`SdlSessionConfig`: `city3d`, `cityName3d`, `continuousClock`, `gameSpeedLevel`,
`applyNewGame`, `newGame`, `loadSavePath`, `realHudArt`, `hudIconGfxId`,
`uiPanels`, `dumpFramePath` — all defaulted to the wave-1 behaviour.

`SdlSessionTrace`: `view3dActive/Instances/NonClear/BoundObjects`,
`newGameApplied`, `playerId`, `playerCashEnd`, `loadedFromSave`, `clockActive`,
`clockFires`, `timeSyncCommits`, `worldDay`, `worldHour`, `hudRealArt`,
`tooltipFrames`, `panelVisible`, `atmosLightRebuilds`, `frameDumped`.

## Memory-safety fixes the integration surfaced (ASAN over the live session)

* `src/gui/form_loader.cpp Form_InitTables` — the original stamps 512 widget
  records (`0x41b888`), but the reimpl's `g_widgets` carries `kMaxWidgets`
  (511, load-bearing in zorder.cpp's recovered arithmetic): the 512th stamp
  wrote 4 bytes past the array into the adjacent global (hit by every
  `SessionHud::Init`). Clamped; `gui_form_loader_test` updated (its own
  `g_widgets[511]` read was equally OOB).
* `src/render/raster_textured.{h,cpp}` — the RGBZ textured rasterizer
  (`@0x5F6C30`) wrote spans UNCLIPPED (the original trusted the upstream poly
  clip + guard band); plane-clip float rounding in the whole-city frame wrote
  past the framebuffer (caught as heap corruption + cross-run pixel
  nondeterminism). Added the reconstruction-only surface-clip clamp — the same
  defensive clip the shaded path (`FillTexturedSpansShaded`) always had:
  horizontal clamp in `FillSpanLoop` (zero-init disables it, so direct callers/
  tests are unchanged), vertical row clamp in `RasterizeTexturedTriangleRgbz`
  with exact k-scaled accumulator pre-stepping. Pixel-neutral for in-bounds
  content — pinned by the new `tests/unit/raster_clip_test.cpp` golden
  (clipped draw == crop of an unclipped draw).

## Named gaps / deferrals (rule 8)

* **SceneMainLoop adoption** — deferred, reasons in item 9 above.
* **City heightmap height bytes** — flat (the BuildTerrainMesh height fill from
  the boden mesh is not reconstructed); the grid-scale math is real.
* **Persons in the 3D view** — CityView3D renders scene nodes + bound objects;
  the character render pass (person meshes/poses) is bound to the legacy
  RealCityRenderer and is not drawn in 3D mode yet (`tr.personsRendered = 0`).
* **Tooltip/panel content feeds** — `PanelObjectHover` record/economy views are
  null (zeroed defaults); the dispatch/lifecycle/composition are real. Feeding
  the live record fields is a follow-up.
* **Load-game 3D scene** — partial .SAV quicksaves embed no scene stream;
  3D needs `cfg.cityName3d` (stadt_*.ed3 fallback) else the session drops to
  the legacy view.
* **Wheel axis** — `shim::MouseState` still has no wheel (pre-existing gap);
  wheel zoom is parked at 0.
* **`rateByte` (byte_6477A1)** — passed as 100 (the `newgame_apply_e2e` working
  value); the loaded scalar-block byte is not yet surfaced by `LoadWorld`.
* **HUD time-of-day** — displayed through the real `Clock_ComputeTimeOfDay`
  splitter fed with the exact inverse mapping of the world clock (the
  authoritative display source per `src/gui/hud.h`'s provenance note).
* **guild_run headless drift** — NullPlatform's (0,0) cursor edge-scrolls the
  camera off-city over a long bounded run; real-window play is unaffected.

## Tests / results

* `tests/e2e/playable_flow_e2e_test.cpp` TEST E `IntegratedCity3DSession`
  (guarded): 320x240, 66 frames — asserts `view3dActive`, 53 bound objects at
  real placements, 796 instances walked, non-trivial framebuffer (~39.5k
  non-sky pixels), `newGameApplied` + the kind-6 player in `sim::g_persons`
  with name "Test", `clockFires >= 1` + the 06:00 sync, `hudRealArt` + caption
  glyphs, the `/tmp/guild_session_city3d.ppm` dump, and FULL determinism
  across reruns (frames/instances/pixels/playerId/clockFires/hashStart/hashEnd).
  Suite now 5 tests / 76 checks with assets.
* `tests/unit/raster_clip_test.cpp` (new) — 3 tests / 9 checks: overhang ==
  crop golden, fully-offscreen no-op, zero-init legacy span behaviour.
* `tests/unit/gui_form_loader_test.cpp` — updated to the clamped stamp.
* Full suite: **1409/1409 ctest targets pass** (portable Debug,
  GUILD_BACKEND=OFF, with `GUILD_GAME_DIR` pointing at the real install).
* Real backends: `cmake --preset vulkan-sdl-system` builds `guild_run`; the
  headless smoke (`SDL_VIDEODRIVER=dummy ... --play --frames 70`) runs the 3D
  session through Vulkan/llvmpipe: 70 frames presented, 796 instances, 4 clock
  fires, frame dumped.

Visual artifact: `/tmp/guild_session_city3d.ppm` (320x240, P6) — the real 3D
AUGSBURG with the HUD ("16$" player purse, "DAY 0 06:03" world clock) and the
standard info panel.
