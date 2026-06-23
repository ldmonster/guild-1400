# Wave-15 TRUE 1:1 binary diff — game clock / tick / turn pipeline (W15-TICK)

MCP was LIVE. Every function below was decompiled (and disassembled / get_bytes'd
where rounding, constants, or register detail mattered) and compared line-for-line
against the reconstruction. The wave-13 NEEDS-LIVE-MCP queue is fully resolved.

## Headline finding (FIXED divergence)

**`ClockScaledGameSeconds` (0x527778 body @0x5277a9) used the WRONG rounding mode.**
The wave-13 audit assumed `VIBE_Coord_ConvertX` (0x5c6b08) installs RC=00
(round-to-nearest-EVEN) and modeled it with `std::nearbyint`. The live disasm
proves otherwise:

```
0x5c6b08 bytes: 50 9b d9 3c 24  9b ff 34 24  c6 44 24 01 1f  d9 2c 24  d9 fc ...
                push eax; fstcw[esp]; push[esp]; mov byte[esp+1],0x1F; fldcw[esp]; frndint
```
`mov byte[esp+1], 0x1F` overwrites the HIGH byte of the control word -> CW = 0x1F7F.
The RC field (CW bits 10-11) = 0b11 = **round toward ZERO (truncate)**, NOT nearest.
So 0x527778 computes `trunc(v1)`, and the original's `+ dbl_622960 (0.5)` bias makes
the net result **round-half-UP** for the positive scaled-seconds value — not
round-half-even.

Impact: identical on the reachable speed ladder (40*level -> 50/75/100/125/150 are
exact integers, so any RC agrees), but DIVERGES for every off-ladder speed
(verified 200/401 inputs differ over speed 0..400 against an 80-bit x87 emulation).

FIX (this segment, owned files only):
- `src/sim/game_clock_tick.cpp`: `std::nearbyint` -> `std::trunc`; provenance
  comment rewritten with the 0x1F7F / RC=11 byte evidence.
- `src/sim/game_clock_tick.h`: ConvertX docblock corrected (RC=11 truncate, +0.5
  bias => round-half-up).
- `tests/unit/game_clock_tick_test.cpp`: the old `ScaledSecondsRoundsHalfToEven`
  golden encoded the WRONG behavior (0.5->0, 1.5->2, 2.5->2, 3.5->4, 4.5->4) and
  was deleted/replaced with `ScaledSecondsTruncatesTowardZero` (0.5->0, 1.5->1,
  2.5->2, 3.5->3, 4.5->4) plus a new `ScaledSecondsOffLadderMatchesX87Truncate`
  regression (speeds 1/3/4/9/11 -> 50/51/52/55/56, where nearbyint would give
  50/52/53/56/57). The golden was wrong; the source + golden are now both 1:1.

Corroboration: the canonical `guild::util::ConvertX` (src/util/coord.cpp) ALREADY
models ConvertX as `std::trunc` with an RC=truncate provenance comment, and the
large majority of the codebase (building_*, npc_market, illness, terrain_render,
command_recon4, etc.) already truncate. The `nearbyint` clock model was the outlier.

### Cross-segment note (NOT my files — for the owners)
A few non-segment files still model ConvertX as `std::nearbyint`, which my disasm
now proves wrong (RC=11 truncate). They should be fixed by their owners:
- `src/play/input_recon_select.cpp:44` (`Coord_ConvertX`)
- `src/sim/combat_slots2.cpp:25`, `src/sim/charaction_misc.cpp:435`
(These may be genuinely off the live tick flow; flagging for verification only.)

## Wave-13 NEEDS-LIVE-MCP queue — all RESOLVED

1. **0x5320f0 vs 0x5336e1 Character_SyncAllTurnStates** — RESOLVED. The 0x533188
   (BeginPlayerRound) decompile shows `0x5336e1 = the call SITE`; the symbol
   ENTRY is `0x5320f0`. The golden list (game_day.cpp / game_day_test.cpp) and the
   header both use 0x5320f0 (entry) — correct. Added a clarifying provenance
   comment in game_day.cpp. No behavior change.

2. **0x527778 x87-vs-double bit-identity** — RESOLVED. disasm:
   `fild[speed]; fmul flt_622958(single-mem); fadd dbl_622960(double-mem);
    fild[daySeconds]; fxch; fmul; fstp; <ConvertX>; fistp`. Emulated the exact
   80-bit x87 sequence (single-promoted 0.00625f, double-promoted 0.5) and compared
   to the `double` reconstruction: BIT-IDENTICAL over speed 0..200 (ladder yields
   exactly 50/75/100/125/150). `double` is safe; the recon casts the *single*
   `0.00625f` to double (matching the single-precision memory operand). VERIFIED-1:1.

3. **0x5c6b08 ConvertX round mode** — RESOLVED = **RC=11 truncate** (see headline).
   The wave-13 RC=00 assumption was wrong; fixed.

4. **0x498ba3 ExSysMessage case 3 (dual-clock executor)** — RESOLVED, VERIFIED-1:1.
   Decompile: `IsProcActive = IsProcActive(clockProc); if(!IsProcActive) SetProcInterval(1);`
   write WORLD clock (qword_13CE852) from packet, write MASTER (qword_122F840) =
   same lodword + world's hidword; `if(!IsProcActive) SetProcInterval(0)`.
   Polarity check: `IsProcActive` (0x44e3e4) returns `dword_B537D0[slot] != 0`, and
   the clock proc is registered with `SetProcInterval(.., 1)` to PAUSE, so
   `dword_B537D0==1`==paused. Thus `IsProcActive`==paused and `if(!active)` ==
   `if(!paused) pause` — exactly what `SessionTick::SyncClocksToDayStart` does
   (`wasPaused=IsProcPaused(); if(!wasPaused) SetProcPaused(1); ...write both...;
   if(!wasPaused) SetProcPaused(0)`). VERIFIED-1:1.

5. **0x4c1324 menu-pop / net-wait arms** — RESOLVED, VERIFIED-1:1. The live
   0x4c1324 predicate decompiles to exactly:
   `(v54&0x10000)==0 && ((!dword_631614 && !dword_11BC27C && dword_11AA480 &&
     !dword_63CC3C) || (dword_764CE0==-1 && WORD2(qword_13CE852)>=0x17 &&
     !dword_63CC3C)) && (word_63C740&0x80)==0` — identical to `ClockDayEndPending`.
   The headless adapter passes menuPop/netWait inert-false; the predicate logic is
   byte-for-byte the binary.

## VERIFIED-1:1 (decompiled and matched, no change needed)

| function | addr | result |
|---|---|---|
| `VIBE_GameTime_Advance` | 0x583150 | matches: v6 carry chain, v7 i64 sign-ext, %60 chains, 24-wrap loop, negative-borrow path, returns hour. |
| `VIBE_GameTime_Compare` | 0x583230 | matches: day-dword first, then 3600*h+60*m+s; -1/0/1. |
| `VIBE_GameTime_Set` | 0x5831f0 | matches: `*(a1+10)=second; v5=second; LOBYTE(v5)=hour; *(a1+4)=v5; *(a1+6)=minute; return minute` (hour-word quirk pinned). |
| `VIBE_Clock_ComputeGameTimeOfDay` gate+idiv | 0x527778 | gate `(mh<23 && wh<23)||(flags&0x80)`; idiv decomposition into hours/seconds/minutes mapped to Advance(edx=hours, ecx=seconds, ebx=minutes). matches (i32 vs decompile i64 is value-equivalent for 50..150). |
| `VIBE_Command_ExAdvanceGameTick` | 0x498954 | world-commit gated on `Compare(world, packet)<0`; cascade He/calendar/demand/threat(%6)/needs/Meister(536-stride, 411648 end)/turns/production/markowned; `*v4=1` ack. matches command_apply6. |
| `VIBE_GameTick_GetScaledDelay` | 0x43c680 | `uDelay * dword_62EB38` (32-bit wrap). matches. |
| `VIBE_GameTick_BeginPlayerRound` pass order | 0x533188 | 26-step golden name+addr+owner verified against the live call sites (City_ComputeWealthGrid 0x577e74 ... SyncAllTurnStates 0x5320f0). matches. |
| `dword_63CC60` (daySeconds=100) | 0x63cc60 | get_bytes = 64 00 00 00; only one xref (read in clock proc), no writes => constant. matches kClockDaySecondsScale. |
| `flt_622958 / dbl_622960` | 0x622958 / 0x622960 | get_bytes = CD CC CC 3B (0.00625f) / 00..E0 3F (0.5). matches constants + golden. |

## Tests

`tests/unit/game_clock_tick_test.cpp`:
- REPLACED `ScaledSecondsRoundsHalfToEven` (wrong) with `ScaledSecondsTruncatesTowardZero`.
- ADDED `ScaledSecondsOffLadderMatchesX87Truncate` (the regression that catches the
  nearbyint/trunc divergence at speeds 1/3/4/9/11).
- Suite: 72 -> **77 checks, 0 failures**.

Full segment build (against the now-green `guild` lib) and run:
- `game_clock_tick_test` 77, `gametime_recon_test` 31, `game_day_test` 19,
  `session_tick_test` 69, `session_tick_itest` 224 — all PASS.

## Files changed (segment-owned only)
- src/sim/game_clock_tick.cpp  (nearbyint -> trunc + provenance)
- src/sim/game_clock_tick.h    (ConvertX RC=11 docblock)
- src/play/game_day.cpp        (0x5336e1/0x5320f0 disambiguation comment)
- tests/unit/game_clock_tick_test.cpp  (golden fix + regression)

No commits. Did not touch sdl_session or any other segment's files.
