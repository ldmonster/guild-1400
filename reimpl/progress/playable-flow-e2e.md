# Playable-flow e2e harness

End-to-end test harness that drives the **whole native game flow headlessly with
scripted input**, proving the `menu -> new game -> city session -> back-to-menu ->
quit` loop works. Pure shim backends — `shim::MemoryGraphicsDevice` +
`shim::ScriptedPlatform` (no SDL/Vulkan, default portable preset) — exercising the
exact same code paths `guild_run --play` runs on the real window.

- Test: `tests/e2e/playable_flow_e2e_test.cpp` (suite `PlayableFlowE2E`, 4 tests,
  35 checks with assets / 9 with clean skips)
- Harness extension: `src/shim_impl/scripted_platform.h` — additive pump-indexed
  **timeline scripting** (`scriptAt`)

## Scripting model

Every play-layer screen (the menu loop's `InitStateReader`, the 3D city pick, the
difficulty/history radio panels, the player wizard, charcreate, options, credits,
the city session) calls `IPlatform::pumpMessages()` **exactly once per frame**. So
one global pump-indexed input timeline deterministically steers a whole
multi-screen flow: `ScriptedPlatform::scriptAt(p, x, y, left, keys, textOnce)`
installs the full input state that becomes visible right after the p-th pump
(0-based) and persists until a later step fires; `textOnce` feeds the
`pollText()` (WM_CHAR-substitute) stream once. Clicks are scheduled as a single
`left=true` pump so a held button never leaks a spurious click edge into the next
screen; ESC inside nested screens is held for exactly one pump so the menu never
mistakes it for its own quit signal. `quitAfterPumps(N)` doubles as a watchdog: a
desynced script ends in a window-close instead of a hang, and the tests assert the
exact pump count to prove the script stayed in sync.

The extension is purely additive — all 13 pre-existing `ScriptedPlatform` test
binaries (sdl_session, mode_fsm, input_router, hud_binder, determinism,
run_interactive_app, play_input_command suites) were rebuilt and re-run green.

## What each test proves

### A `FullNewGameFlow` (guarded on the real game dir)
One `play::RunNativeMainMenu` call, one 41-pump timeline, 800x600 (the design
resolution, so sub-screen layouts equal their design coordinates):

| pumps | screen | scripted action |
|---|---|---|
| 0–3 | real main menu (`gui::Menu_RunMainMenu` @0x529d08) | hover + click **New Game** (`MenuButtonScreenRect`) |
| 4–5 | 3D ChooseCity (`RunCityScreen3D`, Menu/ChooseCity.ed3) | Enter skips the A_Stadtwahl intro flight, Enter confirms the default tower = **AUGSBURG** (cities list AUGSBURG-first) |
| 6–7 | difficulty (`RunCharIntroScreen`, `_M0_DIFFICULTY`) | click row 2 (**normal**) via `CharIntroComputeLayout` |
| 8–9 | history (`RunChooseHistoryScreen`, `_M0_HISTORIE`) | click row 0 (**factual** -> History flag 1) |
| 10–36 | player wizard (`RunChoosePlayerScreen`, 6 pages) | 7 Backspace edges clear the seeded "Spieler", `pollText` types **"Test"**, Enter; types **"Player"**, Enter; click gender row 0, faith row 0, wappen cell 0; page 5 auto-commits |
| 37–40 | charcreate (`RunCharCreateScreen`) | click profession 0 (PATRIZIER), click **CONFIRM** |

Asserts: `action == kPlayCity`, `cityPath ==
"Resources/gamedata/Cities/AUGSBURG.cty"`, menu `framesPresented == 4` (the whole
chain runs nested in menu iteration 3's dispatch), total pumps `== 41` (no
watchdog), device presents `> 4` (every sub-screen presented through the one
device).

NOTE: asserts are written against the **current** `NativeMenuResult` public API
(`action` / `cityPath` / `framesPresented`). Once the concurrent
`NewGameParams`-on-result work lands, the chosen difficulty / history flag /
names ("Test"/"Player") / profession / wappen can be asserted here too.

### B `SessionDeterministic` (guarded)
`play::RunSdlSession` on the city TEST A picked (AUGSBURG), 320x240x16,
`maxFrames=5`, seed 0x4711, run **twice**: asserts `mounted`, `loaded`,
`liveObjects > 0`, `framesPresented == 5`, `hashStart != 0`, and that
`framesPresented` / `liveObjects` / `hashStart` / `hashEnd` are identical across
the two runs (full `HashFullWorld` reproducibility).

### C `MenuRoundTrip` (asset-free, always runs)
Menu -> **Game Options** -> ESC back -> **Credits** -> ESC back -> **Quit**, one
16-pump timeline. Asserts clean `kQuit` (via the armed close, not the window),
menu `framesPresented == 12`, total pumps `== 16`, device presents `== 16`
(options and credits each presented 2 frames of their own).

### D `EscFromSessionThenMenuAgain` (guarded)
ESC held from frame 0 quits the session (`quitByEsc && cleanQuit &&
!quitByWindow`, city `loaded`); then a **second** `RunNativeMainMenu` pass in the
same process (after `sim::ResetEntityArrays()` + `io::VfsShutdown()`) still runs
the full menu and quits cleanly — the in-process back-to-menu loop.

## How to run

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DGUILD_BACKEND=OFF
cmake --build build --target playable_flow_e2e_test -j8
GUILD_GAME_DIR=$PWD/europe_guild_1400_original ./build/playable_flow_e2e_test
```

Headless, deterministic, ~2 s with the real assets. Without `GUILD_GAME_DIR` (or
with the dir absent) tests A/B/D skip cleanly with a `[skip]` line; C always runs.

## Status / findings

All 4 tests pass against the real install (35 checks, 0 failures). No flow
breakage found: the New-Game chain (city -> difficulty -> history -> player ->
charcreate), the cancel-free commit path, the options/credits round-trip, the
session ESC path and a second in-process menu pass all behave as wired in
`src/play/native_main_menu.cpp`.
