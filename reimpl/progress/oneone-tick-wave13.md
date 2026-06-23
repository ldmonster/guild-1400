# Wave-13 1:1 audit — game clock / tick / turn pipeline (W13-TICK)

MCP was DOWN for this wave: this is the MCP-free part of the 1:1 comparison —
cross-check each reconstructed function against the in-tree evidence (provenance
comments + progress docs), PIN its recovered 1:1 values with golden tests, and
produce a confidence map. No invented constants; every pin traces to the
source's own recovery or `progress/session-tick.md` / `progress/gametime-gametick-recon.md`.

## Segment inventory (function -> address -> file)

### src/sim/game_clock_tick.{h,cpp} — the continuous clock proc
| symbol | addr | file | provenance? |
|---|---|---|---|
| `ClockScaledGameSeconds` | 0x527778 (body 0x5277a9..0x5277d8) | game_clock_tick.cpp | YES |
| `ClockComputeGameTimeOfDay` | 0x527778 (VIBE_Clock_ComputeGameTimeOfDay) | game_clock_tick.cpp | YES |
| `ClockDayEndPending` | 0x4c09a0 @0x4c1324 (in RunFrameLoop) | game_clock_tick.cpp | YES |
| cadence constants (kTimeBaseTickMs … kGameSpeedMaxLevel) | inline addrs | game_clock_tick.h | YES |

### src/sim/gametime.{h,cpp} — calendar arithmetic
| symbol | addr | file | provenance? |
|---|---|---|---|
| `GameTimeAdvance` | 0x583150 | gametime.cpp | YES |
| `GameTimeCompare` | 0x583230 | gametime.cpp | YES |
| `GameTimeDiffMinutes` | 0x5832bc | gametime.cpp | YES |
| `GameTimeSet` | 0x5831f0 | gametime.cpp | YES |

### src/sim/gametime_recon.{h,cpp} — record (de)serialize + year bias
| symbol | addr | file | provenance? |
|---|---|---|---|
| `GameTimeInitDefault` | 0x58320c | gametime_recon.cpp | YES |
| `GameTimeUnpackFromRecord` (year bias `(head>>16)-1400`) | 0x58334c | gametime_recon.cpp | YES |
| `GameTimeAdvanceThunk` | 0x583374 | gametime_recon.cpp | YES |
| `GameTickGetScaledDelay` | 0x43c680 | gametime_recon.cpp | YES |

### src/play/session_tick.{h,cpp} — per-frame session adapter (orchestration)
| translated site | addr | provenance? |
|---|---|---|
| StartTimer(0xE,0) | 0x527e52 | YES |
| RegisterProc(clock,71) / SetProcInterval(clock,1 paused) | 0x52f339 / 0x52f345 | YES |
| clock unpause (scene prologue) | 0x50f1a6 | YES |
| master broadcast QueueRequestPerm30(&master) | 0x4c0b8c | YES |
| command window + `dword_11AA488 = 62EB38 + 7` | 0x4c0be5 / 0x4c0bfe | YES |
| day-end gate -> latch + pause | 0x4c1324 / 0x4c1332 / 0x4c1338 | YES |
| sysmsg-3 dual-clock sync (executor semantics) | 0x498ba3 (built @0x533a54) | YES |
| fast-forward to 23:00 in +30 min commits | 0x53449c..0x534581 | YES |
| day rollover (+24h, set 06:00, resume) | 0x5345bf..0x534671 | YES |
| CommitTimePacket (Perm30 -> Flush -> Exec -> ExAdvanceGameTick) | 0x494a50 / 0x4934cc / 0x494088 / 0x498954 | YES |

### src/play/game_day.{h,cpp} — RunGameDay (BeginPlayerRound composition)
| symbol | addr | file | provenance? |
|---|---|---|---|
| `GameDayPassOrder` / `GameDayStepCount` (golden 26-step day order) | 0x533188 (+ pre-round 0x498954) | game_day.cpp | YES |
| `SeedGameDay` / `RunGameDay` / `RunGameDays` | composition glue | game_day.cpp | glue (callees carry provenance) |

### src/play/turn_{driver,economy,events,ai}.{h,cpp} — turn glue / oracles
These are PLAYABLE_PLAN P4 glue / self-consistency oracles, not direct binary
function reconstructions. `turn_driver.cpp` carries NO addresses by design (it
wires `InstallRealSimHooks{,2,3,4}` + `RegisterApplyHandlers3` and calls the
already-provenanced cores: `GameTimeAdvance` 0x583150, economy/production/fire
handlers in their own modules). NOT a red flag — documented as glue in the headers.

## 1:1 VALUE PINNING — every brief-named recovered value is now golden

| recovered 1:1 value | source | pinned by |
|---|---|---|
| 994 ms tick (71 × 14 ms) | kClockProcIntervalTicks / kTimeBaseTickMs | game_clock_tick_test `CadenceConstants` (71*14==994) |
| round-half-even seconds `(speed*0.00625+0.5)*100` | ClockScaledGameSeconds | `ScaledSecondsGoldenSpeeds` (50/75/100/125/150) + `ScaledSecondsRoundsHalfToEven` (0.5→0, 1.5→2, 2.5→2, 3.5→4, 4.5→4) |
| flt_622958 = 0.00625f, dbl_622960 = 0.5 | game_clock_tick.h | **NEW** `RecoveredConstantBytes` |
| gameSpeed = 40 × level (clamp 0..4) | kGameSpeedStep / SetGameSpeedLevel | `CadenceConstants` + session_tick `GameSpeedLevels` |
| opcode-30 master→world sync | CommitTimePacket / OnFrame | session_tick `TimeSyncCommitsMasterIntoWorld`, `NoCommitWhenClocksEqual`, integration itest |
| day-end at world hour ≥ 23 | ClockDayEndPending | `DayEndGateClockArm` / `DayEndGateNetArm` + session_tick `DayEndGateLatchesAndPausesClock` |
| next-day 06:00 rollover (+24h, set 06:00:00) | RollToNextDay | session_tick `RollToNextDayLandsOnSixAm` / `RollToNextDayFromMidDay` + itest |
| fast-forward +30 min steps to 23:00 | FastForwardToDayEnd | session_tick `FastForwardToDayEndSteps` (13:37→23:07, 19 steps) |
| BeginPlayerRound pass order (26 steps) | GameDayPassOrder | game_day_test `PassOrderMatchesBeginPlayerRoundGolden` (name+addr+owner byte-for-byte) |
| GameTime year bias `-1400` (Europa 1400) | GameTimeUnpackFromRecord | gametime_recon_test `BasicFields` / `BaseYearIsZero` / `AllOnesRecordZeroExtendsBytes` |
| GameTime advance carry chain | GameTimeAdvance | gametime_recon_test `CarryGolden` / `MatchesDirectAdvance` |
| GetScaledDelay 32-bit wrap | GameTickGetScaledDelay | gametime_recon_test `Multiplies` |
| session-flag bits 0x80 / 0x08 | game_clock_tick.h | **NEW** `RecoveredConstantBytes` |
| GameTimeSet hour-word quirk `(second&0xFF00)|hour` | GameTimeSet | **NEW** `GameTimeSetSecondNeverBleedsIntoHourWord` |

### Tests added this wave (all in my segment, no existing golden touched)
- `tests/unit/game_clock_tick_test.cpp`:
  - `GameTimeSetSecondNeverBleedsIntoHourWord` — pins the 0x5831f0 word
    composition edge (high byte of the hour field stays clear even with second=0xFF).
  - `RecoveredConstantBytes` — pins flt_622958 (0.00625f), dbl_622960 (0.5),
    kSessionFlagTutorial (0x80), kSessionFlagSkipPreTurnFastForward (0x08).
  - Suite grew 66 → **72 checks**, all passing.

## INTERNAL CONSISTENCY — drift found

1. **game_day.h header comment vs. source-of-record (FIXED).** The prose day
   list in `src/play/game_day.h` step `[25] CharacterSyncAllTurnStates` quoted
   address **0x5336e1**, but both the live golden list in `game_day.cpp` and the
   golden test in `game_day_test.cpp` (two agreeing authoritative sources) use
   **0x5320f0**. Aligned the header comment to 0x5320f0. This is a comment-only
   doc fix; no behavior, no test, no golden value changed. (If MCP later shows
   0x5336e1 is the BeginPlayerRound *call site* and 0x5320f0 the *function*,
   re-annotate — but the golden the test enforces is 0x5320f0.)

2. **`GameTimeAdvance(&local, 24, 0, 0)` for "+24 h" (NOT a drift — verified
   1:1).** `RollToNextDay` passes 24 as `addDays`, while the comment says
   "+24 hours". This is correct: `GameTimeAdvance` folds `addDays` into the hour
   total (`result = addDays + hour`) before the 24-wrap carry, so +24 there is
   exactly +24 hours (day 2/23:00 → day 3/23:00, then GameTimeSet 06:00). The
   integration itest already proves day 2→3 with 06:00. Left as-is.

3. **`gameSpeed*0.00625+0.5` evaluated in `double` vs the x87 80-bit original.**
   `ClockScaledGameSeconds` uses `double` and `std::nearbyint`. The source notes
   this is exact for the reachable range (speed 0..160, daySeconds 100) — the
   golden vectors confirm 50/75/100/125/150 and the half-even ladder. Consistent.

No other constant/offset/control-flow contradiction found between source,
provenance comments, and the two progress docs.

## CONFIDENCE MAP

### GOLDEN-PINNED (high 1:1 confidence — constants + control flow tested)
- `ClockScaledGameSeconds` (0x527778) — speed ladder + round-half-even + raw FP bytes.
- `ClockComputeGameTimeOfDay` (0x527778) — advance, hour carry, 23:00 gate (both
  clocks), tutorial bypass, midnight wrap.
- `ClockDayEndPending` (0x4c1324) — both arms + every suppressor.
- `GameTimeSet` (0x5831f0) — day-start shape, arbitrary fields, hour-word quirk.
- `GameTimeAdvance` (0x583150) — carry chain (via thunk + direct goldens).
- `GameTimeUnpackFromRecord` (0x58334c) — year bias, zero-extend, SAR, wrap.
- `GameTimeInitDefault` (0x58320c), `GameTimeAdvanceThunk` (0x583374),
  `GameTickGetScaledDelay` (0x43c680).
- `SessionTick` whole chain (0x527e52/0x52f339/0x4c0b8c/0x4c1324/0x533a54…) —
  paused-until-BeginDay, 71-tick cadence, ms remainder carry, 7-tick window
  throttle, commit-only-when-clocks-differ, day-end latch+pause, FF +30 min,
  rollover to 06:00, full-day integration (612 fires @ speed 2).
- `GameDayPassOrder` (0x533188) — 26-step name+addr+owner golden + sub-turn placement.

### UNDER-VERIFIED → now pinned
- The two raw FP constants (flt_622958 / dbl_622960) and the two session-flag
  bit values (0x80 / 0x08) were *used* but not *value-asserted*; added
  `RecoveredConstantBytes`. The GameTimeSet hour-word composition edge was only
  implicitly covered; added `GameTimeSetSecondNeverBleedsIntoHourWord`. No
  remaining under-verified items in this segment.

### NEEDS-LIVE-MCP (cannot confirm from in-tree evidence — binary-diff targets)
- **0x5320f0 vs 0x5336e1** for `Character_SyncAllTurnStates` — confirm which is
  the BeginPlayerRound call site vs. the function entry; re-annotate if needed.
- **0x527778 x87 evaluation order** — confirm the original `fild;fmul;fadd;fild;
  fmul` truly evaluates in 80-bit and that `double` is bit-identical for all
  reachable inputs (we believe so; a live diff over speed 0..160 closes it).
- **0x5c6b08 VIBE_Coord_ConvertX RC=00** — confirm the fldcw mask installs round-
  to-nearest-EVEN (assumed in `std::nearbyint` under FE_TONEAREST).
- **0x498ba3 ExSysMessage case 3** vs command_apply5's separate `g_sysGameTime`
  (documented gap in session-tick.md) — confirm the dual-clock write + clock-proc
  pause/unpause sequence when apply5 is rebased onto the shared clock records.
- **0x4c1324 menu-pop / net-wait arms** (dword_631614, dword_11BC27C) — passed
  inert-false by the headless adapter; confirm the live GUI session semantics.
- **Deferred (not on the in-tree flow):** 0x52f66c BeginRound, 0x4c0750
  RunAdvanceGameDialog, 0x579530 RequestStartTurn, 0x4ff800 HandleGameSpeedKeys
  — see gametime-gametick-recon.md / session-tick.md "named gaps" tables.

## Build status
`game_clock_tick_test` (72 checks), `session_tick_test` (69), `session_tick_itest`
(224), `gametime_recon_test` (31), `game_day_test` (19) — all rebuilt and PASS.
Only edits: 2 test additions in `tests/unit/game_clock_tick_test.cpp` + 1
comment address fix in `src/play/game_day.h`. No commits.
