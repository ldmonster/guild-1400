# Session UI feeds — wave 3 (record content, wheel, load-game 3D, rateByte)

Wave-3 agent W3-C: the wave-2 session-integration deferrals around the UI layer
(progress/session-integration-wave2.md "Named gaps"), minus terrain/persons
(other agents' wave-3 clusters). Everything is ADDITIVE: new
`SdlSessionTrace` fields, a new `shim::MouseState` field defaulting to 0, new
`play::session_save` accessors — all 13+ pre-existing ScriptedPlatform test
binaries re-run green unchanged.

Files: `src/play/sdl_session.{h,cpp}` (the wiring), `shim/IPlatform.h` +
`src/shim_impl/{sdl_vulkan_platform,sdl2_backend}.{h,cpp}` +
`src/shim_impl/scripted_platform.h` (the wheel axis),
`src/play/session_save.{h,cpp}` (capture accessors), `apps/guild_run.cpp`
(load-game 3D report line), `tests/unit/shim_wheel_test.cpp` (NEW),
`tests/e2e/playable_flow_e2e_test.cpp` (TESTs F/G/H).

## 1 — Tooltip / info-panel RECORD-CONTENT FEEDS (was: zeroed)

The session now feeds the panel layer (`play::SessionPanelsInputs`) the
hovered/selected entity's REAL live records instead of the wave-2 zeroed
defaults:

* **Type catalogs loaded.** The original's session-start data load —
  `VIBE_World_LoadBuildingAndObjectData @0x5835f8` (`data/A_Geb.dat` 72×589
  `BuildingTypeDef` → dword_13CE294, `data/A_Obj.dat` 731×65 `SceneTypeDef` →
  dword_13CE27C) + `VIBE_City_InitParameterTable @0x577a9c` — now runs in
  `RunSdlSession` right after the world load (the same block `app/wiring.cpp`'s
  Game_InitWorldAndSounds runs; the wave-2 session skipped it, which is WHY the
  panel content was zeroed). Re-run after every `LoadLiveWorld` (session entry
  and F9 quickload), because the load's determinism blank-slate wipes the
  catalogs. Trace: `typeTablesLoaded`.
* **Building hover** (`hoverKind = kBuilding`): a hovered city entity is a live
  record in the shared object/building array (`sim::g_objects` ==
  dword_13CE298) — the table the original widget's +736 sceneRef points into,
  so the REAL classification (`Tooltip_ClassifySubject @0x4f7424`) yields
  kBuilding with code = record byte +0 (72-cap inside the core). The
  `VIBE_Tooltip_BuildBuilding @0x4f78e4` content feed (`PanelBuildingHover`):
  colour selector = type record +583, extra field = `*(i32*)(type record
  +579)` (both from the loaded A_Geb catalog `g_buildingTypes[code]`), and
  `salePrice` through the REAL `VIBE_Building_ComputeSalePrice @0x591480` with
  the building's sale-record quality byte (+358 of the buildings-as-persons
  row resolved by `Person_FindRecordById @0x58bc6c` over the same entity id).
  * 3D mode: the SessionInput hover pick id resolves to its `g_objects` slot
    through `view.boundObjects()` (the same id→record boundary the order path
    uses). Legacy mode: `rc.Pick`'s index IS the `g_objects` slot.
  * (Wave-2 fed `kObject` + class 32 + the entity ID as the object code — a
    placeholder; replaced.)
* **Person hover** (legacy renderer, `hoverKind = kPerson`): the clickless
  hover now arbitrates `rc.PickPerson` against `rc.Pick` nearest-wins (the
  same arbitration the click path runs); a person hover feeds the live
  536-byte `sim::g_persons` record fields `VIBE_Tooltip_BuildPerson @0x4f84ac`
  dereferences — +0 id word, +9 female, +12 class, +13 religion, +356 job,
  +358/+361 traits — plus `cash` through the REAL `Person_GetCashAmount
  @0x58bc9c` and spouse (+92, gated on its byte +8) / children (+104..+120)
  name codes through `Person_FindRecordById` over the live array.
* **Selection → info panel**: the selected building now rides the
  **`building` handle (dword_631748)** — the handle `VIBE_InfoPanel_Update
  @0x4b84c0`'s builder selection requires for the kBuilding branch (wave-2 fed
  only `sceneObject`/dword_11BC278, which always selected the STANDARD panel).
  `InfoBuildingRecord` is filled from the live record: code (byte +0),
  customName (+5, 32 bytes, NUL-forced defensively), item (`*(u16*)(+39)`),
  upgradeLevel through the REAL `Building_GetUpgradeLevel @0x58fc84` over
  `*(i32*)(+89)`, noSlider (`+90 & 1`). A person selection (legacy person
  pick, kind 3) feeds `InfoPersonRecord` (name code word +0, class byte +2,
  job1 +357, portraitObj `*(i32*)(+396)`) on the `person` handle
  (dword_11BC270).

Trace: `hoverFeedFrames` / `hoverPersonFrames` (frames a real record fed the
layer), `tooltipTextOps` / `panelTextOps` (last frame's composited lines).

### Named gaps (rule 8 — passed as inert, never invented)

* `TooltipPersonEnv.rank` — the `ComputeRankWithinGroup @0x58a560` argument at
  the 0x4f84ac call site is unrecovered (MCP down); left 0.
* `TooltipPersonEnv.wealth` — `ComputeTotalWealth @0x591f7c` needs the
  owned-building worth join (`ContainerView` + per-building worth list); left 0.
* `TooltipPersonEnv.statusCard` / `betrothedNameId` — `ResolveStatusFlags
  @0x553ce8` argument record and the `He_FindFirstHandlerByFilter @0x4c63f8`
  walk are not surfaced; left false / -1.
* `selSelectionFlags` — `VIBE_Building_ComputeSelectionFlags @0x588dec` is
  unreconstructed (hook-only everywhere in the tree); left 0.
* `selCategory` — the @0x4b84c0 category-argument source is unrecovered
  (plausibly `BuildingType_MapToCategoryCode @0x589af0`, unproven); left 0.
* Sale-price tax tier — `IStockHooks::SaleTaxTier` stays the inert default 4
  (no live Gesetz record bound in the session yet).
* **Object/upgrade hover content** (`PanelObjectHover`) — not reachable from
  the city hover (the city entities classify as buildings); the object/upgrade
  builders stay covered by `session_panels_test`'s 18 unit tests.

## 2 — Mouse wheel (was: no wheel axis in the shim)

* `shim::MouseState` gains `int wheel = 0` — notches accumulated since the
  previous `getMouse()` (ADDITIVE; every backend without a wheel reports 0).
* `SdlVulkanPlatform` / `Sdl2Platform`: `SDL_MOUSEWHEEL` (direction-aware) is
  accumulated in `pumpMessages()` and reported read-and-clear by `getMouse()` —
  the WM_MOUSEWHEEL stream replacement (rule 4).
* `ScriptedPlatform`: `setWheel(notches)` (level API) + `scriptAt(...,
  wheelOnce)` (timeline) with the same read-and-clear contract.
* `RunSdlSession` feeds `ms.wheel` into `SessionCamera::Frame`'s `wheelDelta` —
  the REAL wheel-zoom branch (`VIBE_Camera_UpdateMovement @0x4b41a8`'s
  dword_672254 accumulator: one notch = zoom +0.1 + the `Camera_AnchorToTerrain
  @0x4b2900` eye/pitch recompute). Trace: `wheelNotches`, `zoomEnd`
  (flt_6316DC at exit).

## 3 — Load-game 3D (was: needed cfg.cityName3d)

The load path (`cfg.loadSavePath` → `play::LoadLiveWorld`) now derives the 3D
city when the loaded file embeds no scene stream, in evidence order:

1. `cfg.cityName3d` (explicit caller override, unchanged),
2. **the loaded save's header city name** (`SaveHeader +0x05 name[32]` — the
   field the shipped .cty seeds carry, verified `"Augsburg"` on AUGSBURG.cty,
   and the same field `app/wiring.cpp` publishes as the city name) via the new
   `play::SessionLoadedCityName()` capture accessor,
3. the INI `[General] Stadt` (`assets.stadt` — the `ReturnedString @0x122EE50`
   seed the `"%s/%s.cty"` load @0x533d4b formats from).

The scene loads from `scenes.BIN Staedte/stadt_<city>.ed3`
(`VIBE_Scene_LoadStadtScene @0x500218`; case-insensitive mount). Trace:
`city3dCityName` (the name actually used; empty = embedded scene).

### Named gaps

* **The engine's own saves embed the scene** — `VIBE_WorldIo_SaveSceneObjects
  @0x5e65b8` appended by `VIBE_Save_RelinkPersonExtraData @0x5a3f14`, read back
  by `VIBE_Save_PostLoadInitScene @0x5a7ef8 → Scene_LoadFromStream @0x5e7e38`.
  Its WRITE side is unreconstructed (progress/session-save.md), so
  `SaveLiveWorld` saves carry no scene and this derivation is the bridge.
* **0 bound objects on the fallback scene** — the standalone `stadt_*.ed3` is
  the city scene WITHOUT the per-node +512 owner-object ids (those are stamped
  only in the save-embedded stream), so `RebuildModelByOwner @0x5a8140` binds
  no live objects → no pickable entities on this path until the sidecar lands.
  The city itself renders fully (926 instances on AUGSBURG). Pinned by TEST F.
* Engine-written .SAV header names are the SAVE name (the browser's slot-rule
  field), not the city — for those files the derivation falls to the INI Stadt.

## 4 — rateByte (byte_6477A1)

* **Provenance corrected** at the `ApplyNewGameParams` call site: 100 is the
  FRESH-CITY rate per the InitOrLoadSession reconstruction
  (`app/session_init.cpp`, the 0x533fXX.. purse block: `cityRate = 100`;
  `gold = Money_MultiplyByRate(base, cityRate) @0x58f19c`) — no longer "the
  e2e working value".
* **Finding:** the .cty scalar block's byte_6477A1 slot (`SaveScalarBlock
  .season`, @0x5a76d6..) carries **0** in the shipped AUGSBURG.cty (decoded
  byte-exactly against the io/save layout), so the loaded scalar is NOT the
  commit-time value — something between the load and the purse seeding sets it
  (un-resolvable without MCP; the session_init recon pins 100).
* **Live home:** the loaded scalar now has one — `g_cap.scalars.season` from
  `LoadLiveWorld`, exposed additively as `play::SessionLoadedRateByte()` for
  the load path (where `ApplyNewGameParams` never runs). Future consumers of
  byte_6477A1 (cmd-15 senders, `command_recon4_senders`' `seasonByte6477A1`
  hook) should bind to this on loaded sessions.

## 5 — SceneMainLoop adoption (re-assessed: still NOT adoptable; plan)

`play::SceneMainLoop_{Begin,StepFrame,End}` (`VIBE_Scene_RunMainFrameLoop
@0x50f0c0`, scene_main_loop.h) was re-assessed against the wave-3 session. It
remains un-adoptable WITHOUT breaking the additive/determinism constraint,
because `StepFrame`'s loop head is `runFrameLoop(0x67FFF, owner)` — and
`play::RunFrameLoop` (gamelogic_recon) owns input, audio, weather, selection
and the clock pump INTERNALLY, while the current session calls those same
reconstructions AROUND the frame in a pinned order (every scripted trace —
frames/picks/orders/hashes — asserts that order).

Concrete adoption plan (binding → existing session step), for the wave that may
replace rather than extend the loop:

| SceneMainLoopHooks slot | session piece today |
|---|---|
| Begin: `sceneActivateAndRefreshCharacters` (0x506df4) | persons3d cluster (wave-3 sibling agent) once stitched |
| Begin: `ambientStartMarketLoop` (0x582858) | `SessionAudio` market-ambience start |
| Begin: `timeBaseSetClockProc` (0x44e3ac→0x527778) | `SessionTick.SetGameSpeedLevel/SyncClocksToDayStart/BeginDay` |
| `runFrameLoop(0x67FFF, owner)` | ONE session frame: atmos.Frame → tick.OnFrame (+day gate) → render+HUD/panels → present → pumpMessages → SessionInput.Frame → SessionAudio.Frame → frame cap; return 0 on ESC/close |
| body: `cameraUpdate` (0x4b4c68) | `SessionCamera::Frame` — note the body calls it AFTER the frame driver; the session today runs it at step (4e). Same callee, one-step phase shift to re-pin |
| body: `buildingComputeSelectionFlags` (0x588dec) | NAMED GAP (unreconstructed) — blocks the auto-enter arm |
| body: jail/auto-enter/Gebaeude-bauen/foreign-shop/smoke/sound3d leaves | named-gap hooks (0x50f698/0x51e88c/0x51defc/0x50de7c/0x4b60a0/0x50f028) — inert until their clusters land |
| End: `selectionReset` (0x4b9444) | `ResetSessionInputGlobals` / `SessionSelect::Clear` |
| End: `ambientStopMarketLoop` (0x5828bc) | `sa.Shutdown()` |
| End: `coordConvertY` cursor rewrite | Camera_CursorCoordWrite (camera_update_recon) |

Blockers to clear first: (a) hand the session's subsystems INTO RunFrameLoop's
hook set (an order swap that re-baselines every scripted trace), (b)
`ComputeSelectionFlags @0x588dec`, (c) live producers for the body's gates
(word_63C740 bits, dword_75BF38 build requests, dword_11BC27C scripted enters).

## Tests / results

* `tests/unit/shim_wheel_test.cpp` (NEW) — **4 tests / 13 checks**: MouseState
  default, setWheel read-and-clear, timeline wheelOnce, notches → the REAL
  camera zoom branch (2 notches → zoom 0.2).
* `tests/e2e/playable_flow_e2e_test.cpp` — now **8 tests / 119 checks** (with
  assets):
  * TEST F `LoadGameEntersCity3D`: `loadSavePath` (partial save) → 3D city up,
    `city3dCityName == "Augsburg"` (header derivation), 926 instances,
    non-trivial frame, hash-deterministic; the 0-bound-objects sidecar gap
    PINNED.
  * TEST G `WheelZoomChangesCamera3D`: no wheel → zoom 0; 2 scripted notches →
    `zoomEnd == 0.2` exactly (the 0x4b41a8 wheel branch), deterministic.
  * TEST H `HoverTooltipShowsRealBuildingContent`: cursor parked on a live
    object's projected point (no click) → real record feed (`hoverFeedFrames`),
    tooltip built+shown with composited content lines, no world mutation
    (`hashStart == hashEnd`); CLICK variant → the real selection commits on
    release and the info panel composites the selected building's record
    content (`panelTextOps > 0`).
  * TEST E additionally asserts `typeTablesLoaded`.
* Re-run green (counts): sdl_session 25/17/22 (unit/itest/e2e), session_camera
  71, session_hud 58 + e2e 35, session_panels 207, session_save 283 + e2e 166,
  mode_fsm 51, input_router 48, hud_binder 63, determinism 22,
  run_interactive_app 11/18, play_input_command 18, session_input 119,
  sdl_loadgame_screen 244, menu_loadgame_flow 26, native_main_menu 10,
  sdl_menu 28/22, newgame_apply_e2e 23, session_tick 69, session_audio 99,
  session_atmos 2216, gui_loadgame_run 40.
* Full ctest: 1398/1415 pass; the 17 failures (multi_city, atmos_lighting,
  full_session, object_mesh_render, playable_slice, real_city_render,
  world_render, object_transform, session_persons3d/_render) are in the
  render/world/persons3d clusters OTHER wave-3 agents are actively modifying
  (their uncommitted WIP in src/render/raster.cpp, texture.cpp,
  universe_render.cpp, city_view3d.cpp, session_persons3d.*) — none touch this
  wave's call paths, and every suite over this wave's files is green.
* Real backends (`build-vk`, Vulkan/llvmpipe + SDL dummy): `guild_run --play
  --frames 40` headless smoke green — 40 frames presented, 796 instances,
  3 clock fires, frame dumped.
