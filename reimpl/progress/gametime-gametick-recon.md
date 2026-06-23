# Game clock / tick / calendar cluster (VIBE_Time / VIBE_GameTime / VIBE_GameTick)

Cluster worklists: `/tmp/wl/VIBE_Time.txt`, `VIBE_GameTime.txt`, `VIBE_GameTick.txt`.

## New files
- `src/sim/gametime_recon.{h,cpp}` — GameTime record (de)serialize + thunk + scaled-delay.
- `src/sim/gametick_entityscan_recon.{h,cpp}` — per-frame entity hit-test scans.
- `src/crt/tzparse_recon.{h,cpp}` — CRT POSIX-TZ rule parsing + DST transition helpers.
- `tests/unit/gametime_recon_test.cpp`, `tests/unit/tzparse_recon_test.cpp`,
  `tests/unit/gametick_entityscan_recon_test.cpp` — 88 checks, all passing.

## Done (addr -> symbol -> file)
| addr | symbol | file | notes |
|------|--------|------|-------|
| 0x58320c | VIBE_GameTime_InitDefault | gametime_recon.cpp | zero 14-byte template (qword_13CE852 all-zero), returns 14 |
| 0x58334c | VIBE_GameTime_UnpackFromRecord | gametime_recon.cpp | (head>>16)-1400 (Europa **1400**), byte fields zero-ext, +8 dword |
| 0x583374 | VIBE_GameTime_AdvanceThunk | gametime_recon.cpp | reg-shuffle -> GameTimeAdvance (reused, 0x583150) |
| 0x43c680 | VIBE_GameTick_GetScaledDelay | gametime_recon.cpp | uDelay * dword_62EB38 (inert-default clock state) |
| 0x4146d8 | VIBE_GameTick_InitEntityTracking | gametick_entityscan_recon.cpp | 512-slot entity hit-test, child ptr deref |
| 0x414a38 | VIBE_GameTick_MainLoop | gametick_entityscan_recon.cpp | dual scan + sub-node branch + SelectEntity fallback functor |
| 0x6070b0 | VIBE_Time_ParseDecimal | tzparse_recon.cpp | digit-run accumulator |
| 0x6070dc | VIBE_Time_ParseTzName | tzparse_recon.cpp | tz abbrev + signed hh:mm:ss offset |
| 0x6071f4 | VIBE_Time_ParseTzRule | tzparse_recon.cpp | J / n / Mm.w.d + /time; default 02:00:00 |
| 0x606f10 | VIBE_Time_ClearTzInitFlag | tzparse_recon.cpp | dword_64AFD0 low-2-bit clear |
| 0x606f2c | VIBE_Time_SetTzInitFlag | tzparse_recon.cpp | dword_64AFD0 set bit0 |
| 0x60693c | VIBE_Time_DstTransitionDayOfYear | tzparse_recon.cpp | J/n/M rules; month tables dword_62CEC6/62CEE0 (exact bytes), MakeTimeUtc round-trip |
| 0x606a34 | VIBE_Time_IsAfterDstStart | tzparse_recon.cpp | month short-circuit + day-of-year compare |

Reused (extern, NOT redefined): `guild::sim::GameTimeAdvance` (0x583150,
src/sim/gametime.cpp); `guild::crt::IsLeapYear` / `MakeTimeUtc` / `Gmtime`
(src/crt/time.cpp).

## Continuation: the CONTINUOUS clock driver (see progress/session-tick.md)
The live per-frame clock chain has since been reconstructed:
- 0x527778 VIBE_Clock_ComputeGameTimeOfDay — the REAL function is a TimeBase
  proc that ADVANCES the master clock qword_122F840 (dword_1233558 is the game
  SPEED setting, 40×level, not a tick accumulator) → `sim::ClockComputeGameTimeOfDay`
  (src/sim/game_clock_tick.cpp). Registered at interval 71 (@0x52f339) over the
  14 ms TimeBase (@0x527e52) ⇒ fires every 994 ms.
- 0x5831f0 VIBE_GameTime_Set → `sim::GameTimeSet` (src/sim/gametime.cpp).
- Frame-loop sync/day-end blocks of 0x4c09a0 (@0x4c0b8c, 0x4c0be5, 0x4c1324)
  and the InitOrLoadSession day rollover (0x533a54 @0x53449c/0x5345bf..0x534671)
  → `play::SessionTick` (src/play/session_tick.cpp).
- NOTE: qword_13CE852 is the LIVE WORLD CLOCK (committed by ExAdvanceGameTick
  @0x498954), not just a serialization template; qword_122F840 is the master
  clock the clock proc advances.

## Deferred (addr -> reason)
| addr | symbol | reason |
|------|--------|--------|
| 0x52f66c | VIBE_GameTick_BeginRound | entangled with Person/Building/MeisterAi/Command/Net/Text — not self-contained; needs the whole sim. (Distinct from already-done turn driver 0x533188.) |
| 0x4c0750 | VIBE_GameTick_RunAdvanceGameDialog | UI flow: Interaction/Form/GameLogic/Text rich-string dialog loop. (`SessionTick` reports `dayEnded` in its place.) |
| 0x579530 | VIBE_GameTick_RequestStartTurn | builds a Command packet (VIBE_Command_RequestBuildOp86); belongs to the command/codec module. |
| 0x606eec | VIBE_Time_GmtimeToTlsBuffer | TLS-coupled (off_64A90C per-thread buffer); CRT runtime, not game logic. |
| 0x606f54 | VIBE_Time_LoadTimeZoneFromSystem | **platform boundary**: Win32 GetTimeZoneInformation; host supplies tz. |
| 0x142216c | VIBE_Time_GetLocalTime | addr >= 0x140b000 (SKIP rule) + Win32 GetSystemTime/tz thunks. |

## Cross-module deps / wiring (xrefs_to)
- 0x583374 AdvanceThunk <- VIBE_CharAction_DuelInit (0x4cfed8).
- 0x43c680 GetScaledDelay <- VIBE_Script_RegisterCommands (0x43c850) as the
  script "GetTime" command (registration site 0x43c8d4).
- 0x4146d8 InitEntityTracking <- VIBE_GameTick_MainLoop (0x414a38) only.
- 0x414a38 MainLoop <- VIBE_Widget_DispatchMouseClick (0x421594).
- 0x58320c InitDefault / 0x58334c UnpackFromRecord: no direct xrefs (called via
  save/load dispatch tables).

## Fidelity notes
- GameTime record offsets (+0 day dword, +4 hour word, +6 minute dword, +10
  second dword) match `guild::sim::GameTime` (types.h, 14 bytes packed).
- Entity-scan box edges are read as `*(int*)(rec+off) >> 16` at **overlapping**
  unaligned offsets (the engine's 16.16 windows); reproduced byte-exactly. Child
  (+60) and sub-node (+44) are real pointers, dereferenced faithfully.
- DST month-length tables stored as the exact binary bytes; the day-count is the
  original's overlapping 32-bit-window `>>16` difference.
- GetScaledDelay multiply wraps as a 32-bit DWORD (verified by test).
