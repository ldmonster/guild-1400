# Session tick — CONTINUOUS game time during play

Mission: make game time advance continuously during a live session, as
gilde.exe does (previously the native session only advanced a whole day at a
time via SPACE → `play::RunGameDay`).

## Recovered architecture (the real per-frame clock chain)

```
winmm timer (14 ms)                  StartTimer(0xE,0) @0x527e52, fptc @0x44e130
  └─ TimeBase tick: dword_62EB38++, fire procs where 62EB38 % interval == 0
       └─ clock proc @0x527778 (interval 71 @0x52f339 => every 994 ms):
            gate: (master.hour<23 && world.hour<23) || (word_63C740 & 0x80)
            master(qword_122F840) += round_ne((dword_1233558*0.00625f + 0.5)
                                              * dword_63CC60) game-seconds
frame loop (VIBE_GameLogic_RunFrameLoop @0x4c09a0):
  @0x4c0b8c  single-player: QueueRequestPerm30(&master)   (opcode 30; builder
             @0x494a50 no-ops when master == world via GameTime_Compare)
  @0x4c0be5  command window: Flush/Receive/Exec; dword_11AA488 = 62EB38 + 7
             -> ExAdvanceGameTick @0x498954 commits WORLD clock qword_13CE852
                and runs the per-tick world cascade (He handlers, calendar,
                goods demand, char needs, Meister AI, player turns, production)
  @0x4c1324  day-end gate: world.hour >= 23 (single-player) || round-end flag
             -> dword_63CC3C = 1, pause clock proc, queue the round loop
round loop (VIBE_GameLogic_InitOrLoadSession @0x533a54):
  @0x53449c  pause; unless (word_63C740 & 8): fast-forward world to 23:00 in
             +30-minute Perm30 commits; RunTurnTransition @0x534587
  @0x5345bf  (single-player) FF to 23:00; GameTime_Advance(+24h) @0x534634;
             GameTime_Set(06:00:00) @0x534659 (0x5831f0); final Perm30
             @0x534665; unpause @0x534671  => next day starts 06:00
  @0x534474/0x53447a  next round clears dword_11AA480 / dword_63CC3C
  turn start: v97=world; GameTime_Set(6,0,0); FlagBlob32(3,&v97) -> sysmsg-3
             executor (ExSysMessage case 3 @0x498ba3) writes BOTH clocks,
             pausing/unpausing the clock proc around the write
scene loop (VIBE_Scene_RunMainFrameLoop @0x50f0c0):
  @0x50f1a6  prologue UNPAUSES the clock proc (SetProcInterval(clock, 0))
```

### Cadence constants (all recovered with get_bytes / disasm)
| constant | value | source |
|---|---|---|
| TimeBase tick period | **14 ms** (0xE) | StartTimer arg @0x527e4d (App_InitSubsystemsAndMovieDll 0x527de0) |
| clock proc interval | **71 ticks** = 994 ms | RegisterProc arg @0x52f329 (Game_InitWorldAndSounds 0x52f2ec) |
| speed scale | **0.00625f** | flt_622958 (bytes `CD CC CC 3B`) |
| bias | **0.5** | dbl_622960 (bytes `..E0 3F`) |
| day-seconds scale | **100** | dword_63CC60 (image `64 00 00 00`) |
| game speed raw | **40 × level**, level 0..4 | dword_1233558; HandleGameSpeedKeys 0x4ff8b1/0x4ff8ee, bounds 0x4ff89b/0x4ff8dc |
| seconds per fire | **25·level + 50** (50/75/100/125/150) | the 0x527778 formula |
| rounding | x87 **round-half-even** | VIBE_Coord_ConvertX 0x5c6b08 (fldcw RC=00 + frndint) |
| command window | **7 ticks** (~98 ms) | dword_11AA488 = 62EB38 + 7 @0x4c0bfe |
| day end hour | **23** (0x17) | 0x527788 / 0x52778e / 0x4c12f9 |
| day start hour | **06:00** | GameTime_Set(6,0,0) @0x534649 |
| rollover advance | **+24 h** | @0x534634 |
| fast-forward step | **+30 min** per Perm30 commit | @0x534604 loop |
| session start day | **2** | LODWORD(qword_13CE852)=2 @0x52f311 |
| registered state | **paused** (edx=1) | SetProcInterval @0x52f345 (edx from 0x52f2f5) |

A full game day 06:00→23:00 = 61200 game-seconds: at speed level 0 that is
1224 fires ≈ 20.3 real minutes; level 2 = 612 fires ≈ 10.1 minutes; level 4 =
408 fires ≈ 6.8 minutes.

## New files
- `src/sim/game_clock_tick.{h,cpp}` — 1:1 clock proc @0x527778
  (`ClockComputeGameTimeOfDay` over a `ClockGlobals` image of qword_122F840 /
  qword_13CE852 / dword_1233558 / dword_63CC60 / word_63C740), the scaled
  seconds computation (`ClockScaledGameSeconds`, exact x87 round-half-even),
  the day-end gate predicate @0x4c1324 (`ClockDayEndPending`), and all cadence
  constants with addresses.
- `src/play/session_tick.{h,cpp}` — `play::SessionTick`, the thin per-frame
  session adapter: real `crt::TimeBase` (14 ms ticks pumped from frame ms),
  the clock proc registered at interval 71 (paused, as @0x52f345), the
  master→world sync through the REAL command pipeline
  (`QueueRequestPerm30` → `CommandQueue` standalone loopback →
  `ExAdvanceGameTick` with its cascade hooks), the day-end latch, and the
  InitOrLoadSession day lifecycle (`SyncClocksToDayStart` / `BeginDay` /
  `FastForwardToDayEnd` / `RollToNextDay`).
- `src/sim/gametime.{h,cpp}` — added `GameTimeSet` (VIBE_GameTime_Set
  @0x5831f0; hour/minute/second setter used by the rollover).

## Edits to existing files (fidelity fixes)
- `src/crt/time.{h,cpp}` — `PumpFromClock` no longer self-stops when
  StartTimer's second arg is 0: in the binary a2==0 arms a winmm
  **TIME_PERIODIC** timer (@0x44e28c `fuEvent = (a2==0)`), a2!=0 arms a
  one-shot that fptc re-arms (@0x44e209); both modes tick until StopTimer.
  (`tests/e2e/crt_time_e2e_test.cpp` updated accordingly.)
- `src/gui/hud.h` — corrected the provenance note on
  `gui::Clock_ComputeTimeOfDay`: the real 0x527778 ADVANCES the master clock
  (dword_1233558 is the game-speed setting, not a tick accumulator); the gui
  function is kept as a display-only hh:mm:ss splitter.

## Per-frame call contract (wave-2 session wiring)
```cpp
play::SessionTick tick;            // once per session (global-state mirror)
tick.SetGameSpeedLevel(2);         // dword_1233558 = 40*level
tick.SyncClocksToDayStart();       // sysmsg-3: both clocks = world day, 06:00
tick.BeginDay();                   // clears latches; UNPAUSES the clock
// each rendered frame:
auto r = tick.OnFrame(elapsedMs);  // ticks -> clock fires -> world commits
// HUD reads tick.worldTime() (sim::g_tickClock = qword_13CE852)
if (r.dayEnded) {
    tick.FastForwardToDayEnd();    // +30-min commits to 23:00 (skipped: flag 8)
    /* existing play::RunGameDay machinery == RunTurnTransition @0x534587 */
    tick.RollToNextDay();          // +24h, 06:00:00, clock resumes
    tick.SyncClocksToDayStart();
    tick.BeginDay();
}
```
Per-commit world updates (character walk etc.) fan out through the Apply6
cascade hooks: `sim::SetTickCascadeHook` + `sim::Apply6_TickGates`
(dword_63C8E8/E4/E0) — wire them to the live subsystems in the session build.

## Tests (all passing)
- `tests/unit/game_clock_tick_test.cpp` — 12 tests / **66 checks**: scaled
  seconds golden vectors (50/75/100/125/150), round-half-even vectors, clock
  advance + hour carry + midnight wrap, the 23:00 gates, both day-end-gate
  arms with every suppressor, GameTimeSet goldens, cadence constants pin.
- `tests/unit/session_tick_test.cpp` — 14 tests / **69 checks**: paused-until-
  BeginDay, fire every 71 ticks, ms remainder carry, master→world commit in a
  window, 7-tick window throttle (windows at tick 1 then 9), no commit when
  clocks equal, day-end latch+pause, tutorial bypass, fast-forward 19 steps
  from 13:37 → 23:07, flag-8 skip, rollover to next-day 06:00 (two shapes),
  non-single-player no-op, sysmsg-3 dual-clock sync, speed level clamps.
- `tests/integration/session_tick_itest.cpp` — 1 test / **224 checks**: a full
  continuous day at the original cadence (16 ms frames, speed level 2):
  exactly **612 fires**, master and world land on 23:00:00 day 2 exactly,
  fires == floor((ticks−1)/71)+1 identity, day-end latch + pause, rollover to
  day 3 06:00:00, next day spins up and advances (seconds == 6h + fires·100).

## Named gaps / deferred
| item | addr | status |
|---|---|---|
| RunAdvanceGameDialog (end-of-day dialog UI; reads/writes dword_1233558 around the dialog) | 0x4c0750 | still deferred (UI flow); SessionTick reports `dayEnded` instead; the modeless "day over" box (text id 196, dword_8C38B4 @0x4c1356) is not shown |
| HandleGameSpeedKeys (N/J keys; level recovery via flt_620BB0=0.5f round-to-even, plus the networked Command_Increase/DecreaseGameSpeed path) | 0x4ff800 | not translated; `SetGameSpeedLevel` models the local `40*(level±1)` writes with the 0..4 bounds |
| ExSysMessage case-3 wire model | 0x498ab4 | command_apply5's `ExSysMessage` keeps its OWN `g_sysGameTime` and does not touch `g_tickClock`/master or the clock-proc pause; `SessionTick::SyncClocksToDayStart` applies the executor semantics (0x498ba3..0x498bf6) directly. Unify when apply5 is rebased onto the shared clock records |
| menu-stack / net-wait arms of the 0x4c1324 gate (dword_631614, dword_11BC27C) | 0x4c1324 | passed as inert-false by the headless adapter; the live GUI session owns them |
| scene-load clock pausing around interiors (word_63C740 & 8 block) | 0x50f120 | belongs to the Scene_RunMainFrameLoop reconstruction (scene_main_loop agent); SessionTick's clock pause API is what it drives |
| dword_1233558 persistence (gfx config read/write @0x56bc7f/0x56b541, options menu @0x56cdd1) | 0x56b834 | settings module owns it; SessionTick defaults to the static-image value 0 (level 0) |
| HUD time-of-day caption still derives from `gui::Clock_ComputeTimeOfDay` over `hud_render`'s `clockTick` | — | wave-2: read `SessionTick::worldTime()` instead (provenance note added in src/gui/hud.h) |
