#pragma once
// The CONTINUOUS game-clock tick driver of gilde.exe — the proc that turns
// real time into game time during a live session, reconstructed 1:1.
//
// Recovered architecture (all verified in the binary):
//
//   1. winmm timer  — VIBE_App_InitSubsystemsAndMovieDll @0x527de0 calls
//      VIBE_TimeBase_StartTimer(0xE, 0) @0x527e52: a multimedia timer firing
//      fptc @0x44e130 every **14 ms** (a2==0 maps to TIME_PERIODIC). Each fire
//      bumps the master tick counters (dword_62EB44 / dword_62EB38) and runs
//      every registered TimeBase proc whose `dword_62EB38 % interval == 0`
//      and whose pause gate (dword_B537D0 slot) is clear.
//      (Dispatcher already reconstructed: guild::crt::TimeBase, src/crt/time.*.)
//
//   2. clock proc   — VIBE_Clock_ComputeGameTimeOfDay @0x527778, registered by
//      VIBE_Game_InitWorldAndSounds @0x52f2ec:
//        RegisterProc(clockProc, 71)      @0x52f339   (interval = 71 ticks)
//        SetProcInterval(clockProc, 1)    @0x52f345   (starts PAUSED; edx=1)
//      => the clock proc fires every 71 * 14 ms = **994 ms** of real time.
//      Each fire advances the MASTER clock qword_122F840 by a speed-scaled
//      number of game seconds (see ClockScaledGameSeconds below), gated on
//      "not yet 23:00" — see ClockComputeGameTimeOfDay.
//      It is UNPAUSED by the live-scene frame loop prologue
//      (VIBE_Scene_RunMainFrameLoop @0x50f19f: SetProcInterval(clockProc, 0)).
//
//   3. master->world sync — VIBE_GameLogic_RunFrameLoop @0x4c09a0:
//        @0x4c0b8c  if (dword_764CE0 == -1            // single-player/master
//                       && dword_62EB38 > dword_11AA488  // command window open
//                       && byte_63CC40)                  // session live
//                     VIBE_Command_QueueRequestPerm30(&qword_122F840) — the
//                     master clock packed as an opcode-30 packet (the builder
//                     @0x494a50 itself no-ops when master == world, via
//                     VIBE_GameTime_Compare against qword_13CE852);
//        @0x4c0be5  command pump (FlushSendQueue/ReceiveAndQueue/ExecCommands),
//                   then dword_11AA488 = dword_62EB38 + 7  @0x4c0bfe
//                   (=> one sync per 7-tick window ~= 98 ms).
//      The opcode-30 executor VIBE_Command_ExAdvanceGameTick @0x498954
//      (reconstructed in src/sim/command_apply6.cpp) commits the packet time
//      into the WORLD clock qword_13CE852 when newer and runs the per-tick
//      world-update cascade (He handlers, calendar, goods demand, character
//      needs, Meister AI per live person, player turns, production timers...).
//
//   4. day-end gate — VIBE_GameLogic_RunFrameLoop @0x4c1324: once the WORLD
//      clock hour reaches 23 (0x17) in single-player (or the network
//      round-end flag dword_11AA480 is raised), and the latch dword_63CC3C is
//      clear and the tutorial flag (word_63C740 & 0x80) is off:
//        dword_63CC3C = 1; SetProcInterval(clockProc, 1)  @0x4c1338  (pause);
//        modeless "day over" box; queue the mode switch back into
//        VIBE_GameLogic_InitOrLoadSession (the round loop).
//
//   5. day rollover — VIBE_GameLogic_InitOrLoadSession @0x533a54 round loop:
//        @0x53449c  pause clock; if !(word_63C740 & 8): fast-forward a copy of
//                   the world clock to 23:00 in **+30 minute** Perm30-committed
//                   steps (@0x5344ce loop), each commit running the world
//                   cascade; then VIBE_GameLogic_RunTurnTransition @0x534587.
//        @0x5345bf  (single-player) pause; fast-forward to 23:00 again
//                   (@0x5345f7, +30 min steps); GameTime_Advance(+24h)
//                   @0x534634; GameTime_Set(06:00:00) @0x534659; final Perm30
//                   @0x534665; SetProcInterval(clockProc, 0) @0x534671 (resume).
//                   => the next day starts at **06:00**.
//        @0x534474/0x53447a  top of the next round clears dword_11AA480 and
//                   dword_63CC3C before re-entering the live frame loop.
//
// This header owns the pure pieces: the clock proc itself (0x527778), its
// cadence/speed constants, and the day-end gate predicate. The stateful
// session driver that strings them onto the real TimeBase + CommandQueue is
// play::SessionTick (src/play/session_tick.h).
//
// NOTE on gui::Clock_ComputeTimeOfDay (src/gui/hud.cpp, also tagged 0x527778):
// that function is a DISPLAY-ONLY derivative (tick value -> hh:mm:ss) kept for
// the HUD caption; the authoritative 0x527778 — which ADVANCES the master
// GameTime record — is ClockComputeGameTimeOfDay below.

#include "guild/common/types.h"
#include "sim/types.h"

namespace guild::sim {

// ---------------------------------------------------------------------------
// Cadence / scale constants recovered from the binary (addresses inline).
// ---------------------------------------------------------------------------

// flt_622958 @0x622958 — bytes CD CC CC 3B == 0.00625f (1/160).
constexpr float kClockSpeedScale = 0.00625f;
// dbl_622960 @0x622960 — bytes 00 00 00 00 00 00 E0 3F == 0.5.
constexpr double kClockHalfBias = 0.5;
// dword_63CC60 @0x63CC60 — static image 0x64 == 100: game-seconds multiplier.
constexpr i32 kClockDaySecondsScale = 100;
// VIBE_TimeBase_StartTimer arg @0x527e4d: 0xE == 14 ms per TimeBase tick.
constexpr u32 kTimeBaseTickMs = 14;
// VIBE_TimeBase_RegisterProc arg @0x52f329: clock proc fires every 71 ticks.
constexpr i32 kClockProcIntervalTicks = 71;
// VIBE_GameLogic_RunFrameLoop @0x4c0bfe: dword_11AA488 = dword_62EB38 + 7.
constexpr u32 kCommandWindowTicks = 7;
// Hour gate constant 0x17 (0x527778 @0x527788/0x52778e; 0x4c1324 @0x4c12f9).
constexpr u16 kDayEndHour = 23;
// VIBE_GameLogic_InitOrLoadSession @0x534634: GameTime_Advance(+24 hours).
constexpr int kDayRollAdvanceHours = 24;
// VIBE_GameLogic_InitOrLoadSession @0x534649: GameTime_Set(hour=6, 0, 0).
constexpr u8 kDayStartHour = 6;
// Fast-forward step @0x5344ce / 0x5345f7 loops: +30 minutes per Perm30 commit.
constexpr int kDayFastForwardMinutes = 30;
// VIBE_Game_InitWorldAndSounds @0x52f311: LODWORD(qword_13CE852) = 2 — a fresh
// session's world clock starts on day 2.
constexpr i32 kSessionStartDay = 2;

// word_63C740 session-flag bits consumed by the clock chain:
//   bit 0x80 — tutorial session: clock ignores the 23:00 stop (0x527798) and
//              the day-end gate is suppressed (0x4c1324 tail test).
//   bit 0x08 — skip the pre-turn fast-forward to 23:00 (0x5344a1).
constexpr u16 kSessionFlagTutorial = 0x80;
constexpr u16 kSessionFlagSkipPreTurnFastForward = 0x08;

// Game speed: dword_1233558 (raw value read by the clock proc) holds
// 40 * level, level 0..4 — VIBE_Input_HandleGameSpeedKeys @0x4ff800 steps it
// by +-40 with bounds (level < 4 to increase @0x4ff89b, > 0 to decrease
// @0x4ff8dc; writes 40*(level+-1) @0x4ff8b1/0x4ff8ee). The options menu and
// gfx config persist the same raw value. The LEVEL global dword_631284
// (default 2 @ static image) is modeled by sim::g_gameSpeed
// (src/sim/command_apply7.cpp).
constexpr i32 kGameSpeedStep = 40;
constexpr i32 kGameSpeedMaxLevel = 4;

// ---------------------------------------------------------------------------
// Clock globals image — the cluster of globals the clock proc touches.
// ---------------------------------------------------------------------------
struct ClockGlobals {
    GameTime master{};               // qword_122F840 — the MASTER clock
    const GameTime* world = nullptr; // qword_13CE852 — committed WORLD clock
                                     // (live session points this at
                                     //  sim::g_tickClock, command_apply6.cpp)
    i32 gameSpeed = 0;               // dword_1233558 (40*level; image value 0)
    i32 daySeconds = kClockDaySecondsScale; // dword_63CC60
    u16 sessionFlags = 0;            // word_63C740 image
};

// ---------------------------------------------------------------------------
// gilde.exe 0x527778 — the seconds-per-fire computation, exact FP semantics:
//   v1 = ((double)gameSpeed * flt_622958 + dbl_622960) * (double)daySeconds;
//   VIBE_Coord_ConvertX @0x5c6b08   (fldcw CW=0x1F7F -> RC=0b11 round-toward-
//                                    ZERO/truncate, + frndint)
//   fistp dword                      (value already integral)
// => seconds = trunc(v1). The +dbl_622960 (0.5) bias makes this round-half-UP
//    for positive v1 (NOT round-half-even — RC=11 is truncate). For the
//    reachable speeds 40*level the value is an exact integer either way:
//    25*level + 50 (50/75/100/125/150 game-seconds per 994 ms fire).
// ---------------------------------------------------------------------------
i32 ClockScaledGameSeconds(i32 gameSpeed, i32 daySeconds);

// ---------------------------------------------------------------------------
// gilde.exe 0x527778 — VIBE_Clock_ComputeGameTimeOfDay (the TimeBase proc).
//
//   if (!((master.hour < 23 && world.hour < 23) || (flags & 0x80))) return;
//   v2 = ClockScaledGameSeconds(speed, daySeconds);
//   GameTime_Advance(&master, v2/3600 /*hours@edx*/, v2%3600%60 /*sec@ecx*/,
//                    v2%3600/60 /*min@ebx*/);            @0x527820
//
// Returns true when the gate passed and the master clock advanced.
// ---------------------------------------------------------------------------
bool ClockComputeGameTimeOfDay(ClockGlobals& g);

// ---------------------------------------------------------------------------
// gilde.exe 0x4c09a0 @0x4c1324 — the end-of-day gate predicate, verbatim:
//
//   (v54 & 0x10000) == 0
//   && (   (!dword_631614 && !dword_11BC27C && dword_11AA480 && !dword_63CC3C)
//       || (dword_764CE0 == -1 && WORD2(qword_13CE852) >= 0x17 && !dword_63CC3C))
//   && (word_63C740 & 0x80) == 0
//
// Arguments mirror those globals:
//   headlessMask    — (v54 & 0x10000): the frame-loop feature-mask bit that
//                     suppresses the gate (combat/cutscene sub-loops).
//   menuPopPending  — dword_631614;  netWaitBusy — dword_11BC27C;
//   roundEndRequested — dword_11AA480 (raised by ExSysMessage subtype 2);
//   dayEndLatch     — dword_63CC3C;  singlePlayer — dword_764CE0 == -1;
//   worldHour       — WORD2(qword_13CE852);  flags — word_63C740.
// ---------------------------------------------------------------------------
bool ClockDayEndPending(bool headlessMask, bool menuPopPending, bool netWaitBusy,
                        bool roundEndRequested, bool dayEndLatch,
                        bool singlePlayer, u16 worldHour, u16 sessionFlags);

} // namespace guild::sim
