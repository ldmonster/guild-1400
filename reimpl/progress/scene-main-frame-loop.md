# Scene main frame loop — VIBE_Scene_RunMainFrameLoop @0x50f0c0

Full 1:1 translation of the in-city per-frame spine. Unit:
`src/play/scene_main_loop.{h,cpp}` (namespace `guild::play`), tests:
`tests/unit/scene_main_loop_test.cpp` (26 tests / 142 checks, all green).

## Translated (addresses)

| Original | Address range | Reconstruction |
|---|---|---|
| `VIBE_Scene_RunMainFrameLoop` (whole) | 0x50f0c0–0x50f694 | `SceneMainLoop_Run` |
| — pre-loop phase | 0x50f0c0–0x50f1ab | `SceneMainLoop_Begin` |
| — loop head + one body | 0x50f1ad–0x50f5f9 | `SceneMainLoop_StepFrame` |
| — teardown | 0x50f5fe–0x50f694 | `SceneMainLoop_End` |
| `VIBE_GameTime_GetSeasonFromDay` | 0x58339c | `GameTime_GetSeasonFromDay` (`day % 4`, leaf) |
| smoke-window kernel (3 inline expansions) | 0x50f1f7 / 0x50f462 / 0x50f4d3 | `Scene_SmokeWindow` |

The original is a single blocking function; it is factored exactly along its
own seams (`while (RunFrameLoop(...)) { body }`). `StepFrame` evaluates the
loop **condition first** (the original `while` head: `RunFrameLoop(eax=&self,
edx=0x67FFF)`), returns `false` with no body run when the pump says stop.
Loop-carried locals (`ebp` smoke phase, `var_28` selection-flag latch — *not*
reset per frame, the camera-transform snapshot `var_2C/30/40/3C`, the season
byte `v43[4]`) live in `SceneMainLoopRun` so a host can call once per frame.

Register-artifact resolutions verified against the disasm (documented in the
.cpp header): `dword_62D4F0 = -1` (ecx @0x50f0ce), `var_28 = 0` /
`dword_634498 = 0` (edx @0x50f0d8), teardown `word_62D310 = 0` (cx zeroed
@0x50f5fe/0x50f606), `dword_62D314 = 0` (edx @0x50f622), final
`RunFrameLoop(eax=0)` vs in-loop `eax = 0x50f0c0`.

Recovered constants:
* frame mask `0x67FFF` (425983) — bits 0..14 | net-command | day-cycle-music;
* family table: 768 records × 0x218 bytes (`cmp eax, 64800h` @0x50f3e0),
  fields word @0x12CE910 (slot), byte @0x12CE912 (type), dword @0x12CE964
  (owner), dword @0x12CEA80 (master person), byte @0x12CEAC1 (jail flag);
* smoke hour tables as the exact `.data` image 0x6476F0–0x64771F
  (`kSmokeHourImage[12]`): start {8,7,8,9} @0x6476FC, end {20,21,20,19}
  @0x64770C, plus the true adjacent zero floats so a negative `day%4` index
  reads the same bytes the original would.

Behavioral details preserved (each has a dedicated test):
* coat-of-arms scan order (type tests 6,7,5; first match stops the scan; all
  768 records probed on no-match);
* intro-zoom scan skips entries equal to `dword_12CEA80[active]` and zooms the
  first other person only;
* smoke phase machine 0→1→2→3 incl. "enter scene during window ⇒ phase 0
  waits for the window to close before arming";
* `ComputeSelectionFlags` result is `cwde`-sign-extended and latched across
  frames;
* auto-enter request 1: `(67222C || (67221C && word_62D310==11))` OR-gate,
  busy bit `+0x5A&1`, slot60 ops 27/25, the `+0x27` dword re-read before
  `EnterAndDispatch`;
* auto-enter request 2: ext==0 ⇒ ZoomIn + consume; busy ⇒ word copy +
  consume; else LABEL_48 dispatch **without clearing** `dword_11BC27C`, with
  the original's double `IsStorageType` call before the storage ZoomIn;
* teardown order and the `ConvertY(sext word@0x75BF4A, sext word@0x75BF48)`
  operands; camera snapshot restore; return value = final pump's eax.

## Named gaps (rule 8 — hooks left inert, no fakes)

Callees with **no reconstruction anywhere in the tree** — each is a
`SceneMainLoopHooks` slot with an inert default, named with its address:

| Address | Original | Hook slot |
|---|---|---|
| 0x50f028 | `VIBE_Sound3d_UpdateListenerForScene` (gates on `dword_11BBC30[2*dword_631610] == 0x50f0c0` — the owner token this unit publishes) | `sound3dUpdateListenerForScene` |
| 0x50f698 | `VIBE_Building_TeleportPlayerToJail` | `buildingTeleportPlayerToJail` |
| 0x50de7c | `VIBE_Building_OpenGebaeudeBauenWindow` | `buildingOpenGebaeudeBauenWindow` |
| 0x51e88c | `VIBE_Building_EnterForeignShop` | `buildingEnterForeignShop` |
| 0x51defc | `VIBE_Building_EnterAndDispatch` | `buildingEnterAndDispatch` |
| 0x4b60a0 | `VIBE_Object_SpawnChimneySmoke` | `objectSpawnChimneySmoke` |
| 0x421a24 | `VIBE_Widget_SetTooltipText` | `widgetSetTooltipText` |
| 0x4bcdcc | `VIBE_Hud_SetStatusBannerText` | `hudSetStatusBannerText` |
| 0x588dec | `VIBE_Building_ComputeSelectionFlags` (modeled hook in sim/building6 too) | `buildingComputeSelectionFlags` |

(0x51defc/0x51e88c/0x50de7c/0x50f698 are listed as DEFERRED in
`src/sim/building.cpp`'s module report — UI/cutscene coupled.)

Also note: `guild::play::scene_recon2::Scene_RunMainFrameLoop`
(src/play/scene_recon2_orchestrator.cpp) is an earlier **coarse model** of
this same address (entity scans delegated wholesale, smoke machine and
request-2 block simplified). This unit supersedes it for wiring purposes;
distinct symbols, no ODR clash. The Begin-callee 0x506df4 remains
reconstructed there and is reached via the hook.

## Tests

`tests/unit/scene_main_loop_test.cpp` — suite `SceneMainLoop`, 26 tests,
142 checks: season leaf goldens (incl. negative-day truncation), smoke-window
goldens per season (incl. the out-of-table negative-index image reads), Begin
ordering/flags/snapshot, slope gate, wappen scan (match/no-match/768 probes),
intro-zoom scan + gates, loop condition + owner token, body ordering, jail
gate, full smoke cycle + in-window phase-0 wait, auto-zoom gate matrix, bauen
gate, selFlags cwde + persistence, request-1 production/non-production/busy,
market stall, request-2 zoom-only/busy/dispatch-keeps-request/storage-double-
check, teardown ordering + register stores + restore, whole-function run
(pump/body/teardown counts).

## Wave-2 session integration contract

The host (`sdl_session` wave-2) drives the scene like this:

```cpp
using namespace guild::play;

SceneMainLoopState sceneSt;     // mirror of the city-scene globals
LiveSceneHooks     hooks;       // SceneMainLoopHooks subclass, bindings below
SceneMainLoopRun   run;

// on entering the city scene (after scene load / SyncWorldOnEnter):
SceneMainLoop_Begin(sceneSt, hooks, run);

// once per host frame while the city scene is active:
if (!SceneMainLoop_StepFrame(sceneSt, hooks, run)) {
    i32 r = SceneMainLoop_End(sceneSt, hooks, run);   // scene exit
    // ... return to menu / next scene; r is the final pump result (0)
}
```

`hooks.runFrameLoop(mask, owner)` is the **frame pump** and must do the real
per-frame work, exactly as 0x4c09a0 does. Bind it to
`guild::play::RunFrameLoop(GameLogicState&, IGameLogicHooks&, mask)`
(src/play/gamelogic_recon.h) with `mask == kSceneFrameLoopMask` asserted, and
publish `owner` (== `kSceneMainLoopProc` in-loop, 0 at exit) into the modeled
frame-proc stack (`dword_11BBC30[2*dword_631610]`) — 0x50f028 gates on it.
GameLogicState's `sessionActive` is this unit's `byte_63CC40` — share the
backing field. Inside that pump live the already-wired pieces: input poll
`Input_PollMouseAndKeyboard` @0x40da88 (src/play/input_recon_select),
`render::RenderMainViewFrame` @0x5B6074 (src/play/universe_render), GameTick
`MainLoop` @0x414a38 / `InitEntityTracking` @0x4146d8
(src/sim/gametick_entityscan_recon), and the selection machinery
(src/play/session_select + sim selection recon).

Hook → existing-reconstruction binding map:

| Hook slot | Original | Bind to |
|---|---|---|
| `sceneActivateAndRefreshCharacters` | 0x506df4 | `scene_recon2::Scene_ActivateAndRefreshCharacters` (src/play/scene_recon2_orchestrator.h) |
| `floorComputeSlopeFlags` | 0x5bbdb0 | render terrain_mesh slope pass (src/render/terrain_mesh.h) |
| `panelRunChooseWappen` | 0x551404 | `gui::Panel_RunChooseWappen(u16)` (src/gui/gui_dialogs5.h) |
| `ambientStartMarketLoop` / `ambientStopMarketLoop` | 0x582858 / 0x5828bc | session_audio market loop (src/play/session_audio.h, app/audio_tick.h) |
| `timeBaseSetClockProc` | 0x44e3ac (proc 0x527778) | crt/time `SetProcInterval` with the clock proc `Clock_ComputeTimeOfDay` (src/play/session_hud uses the same proc) |
| `runFrameLoop` | 0x4c09a0 | `play::RunFrameLoop` (src/play/gamelogic_recon.h) — see above |
| `cameraUpdate` | 0x4b4c68 | `render::Camera_Update` (src/render/camera_update_recon.h) |
| `cameraZoomReset` | 0x4b5250 | `render Camera_ZoomReset` (src/render/camera_recon2.h) |
| `cameraZoomIn` | 0x4b4e24 | camera zoom/dolly (src/play/camera_controls.h `CameraZoomBy`/`CameraDolly` model of 0x4b4e24) |
| `personQueryBegin_1_4` / `personQueryBegin_1_6` / `personIterNext` | 0x586c20 / 0x586a6c | `sim PersonQueryBegin(filters,count)` / `PersonIterNext` (src/sim/entity.h); the raw vararg stack args are (1,4,player) and (1,6) — translate via entity.h's filter-opcode table |
| `playerMasterPerson` / `playerJailFlag` / `family*` | table 0x12CE910.. | the live family/player record array (sim entity layer); offsets documented in the header |
| `scriptFindByHandle` / `scriptFinish` | 0x442174 / 0x443f38 | sim script VM (src/sim/script_import4.h FindByHandle / Script_Finish) |
| `buildingIsProductionType` / `buildingIsStorageType` | 0x587f80 / 0x587f50 | sim building_type (kind∈{11,12,13,16,28} / kind==10) (src/sim/building_type.h) |
| `interactionInvokeHandlerSlot60` | 0x595e74 | `sim InvokeHandlerSlot60(char,int,int,int)` (src/sim/interaction2.h), ebx arg fixed 0 |
| `marketStallRouteContact` | 0x519918 | `world::MarketStallRouteContact` (src/world/market_stall.h) |
| `selectionReset` | 0x4b9444 | `Selection_Reset(int)` (src/play/input_recon_select.h) |
| `coordConvertY` | 0x40da48 | `render::Camera_CursorCoordWrite` (src/render/camera_update_recon.h — 0x40da48 is the cursor-coord write) |
| `buildingComputeSelectionFlags` | 0x588dec | sim/building6 `ComputeSelectionFlags` hook until 0x588dec is translated |
| all NAMED GAPS above | — | leave at inert defaults until their owners are reconstructed |

State-field sharing: `SceneMainLoopState` mirrors live globals that other
reconstructed units also model (notably `byte_63CC40`, `word_63C740`,
`word_63CC5C`, `dword_631730`, `dword_631610/618`). Wave-2 must back these
with one shared store (the session-global block), not per-unit copies.
