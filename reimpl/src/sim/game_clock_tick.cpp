// gilde.exe — continuous game-clock tick driver (see game_clock_tick.h for the
// recovered architecture). Namespace guild::sim.
//
// PROVENANCE:
//   0x527778 VIBE_Clock_ComputeGameTimeOfDay  -> ClockComputeGameTimeOfDay
//   0x5c6b08 VIBE_Coord_ConvertX — the helper installs control word 0x1F7F via
//            `mov byte[esp+1], 0x1F; fldcw`. The RC field (CW bits 10-11) is
//            therefore 0b11 == round-toward-ZERO (truncate), NOT round-to-
//            nearest. frndint then truncates toward zero, and the original's
//            `+ 0.5` bias makes the net result round-half-up for the positive
//            scaled-seconds value. Reconstructed with std::trunc (NOT
//            std::nearbyint — that double-rounds and diverges for non-ladder
//            speeds; verified against an 80-bit x87 emulation, see
//            tests/unit/game_clock_tick_test.cpp ScaledSeconds*).
//   0x4c1324 (in VIBE_GameLogic_RunFrameLoop 0x4c09a0) -> ClockDayEndPending
#include "sim/game_clock_tick.h"

#include <cmath>

#include "sim/gametime.h"

namespace guild::sim {

// gilde.exe 0x527778 (0x5277a9..0x5277d8) — the seconds-per-fire computation.
i32 ClockScaledGameSeconds(i32 gameSpeed, i32 daySeconds) {
    // fild [speed]; fmul flt_622958; fadd dbl_622960; fild [daySeconds];
    // fmul — all exact in double for the reachable range (speed 0..160,
    // daySeconds 100), so double matches the original's x87 80-bit evaluation.
    double v1 = (static_cast<double>(gameSpeed) * static_cast<double>(kClockSpeedScale)
                 + kClockHalfBias)
                * static_cast<double>(daySeconds);
    // VIBE_Coord_ConvertX installs control word 0x1F7F -> RC=0b11 (round toward
    // ZERO / truncate), then frndint. So the integral result is trunc(v1); the
    // original's +0.5 bias makes this round-half-up for positive v1. fistp then
    // stores the (already integral) value as i32. (RC=11 truncate verified from
    // the 0x5c6b08 bytes c6 44 24 01 1f -> mov byte[esp+1],0x1F; fldcw.)
    double rounded = std::trunc(v1);
    return static_cast<i32>(rounded);
}

// gilde.exe 0x527778 — VIBE_Clock_ComputeGameTimeOfDay.
bool ClockComputeGameTimeOfDay(ClockGlobals& g) {
    // 0x52777c..0x52779f: gate — both clocks before 23:00, or tutorial flag.
    const u16 masterHour = g.master.hour;                       // WORD2(qword_122F840)
    const u16 worldHour = g.world ? g.world->hour : u16{0};     // WORD2(qword_13CE852)
    const bool gateOpen = (masterHour < kDayEndHour && worldHour < kDayEndHour)
                          || (g.sessionFlags & kSessionFlagTutorial) != 0;
    if (!gateOpen)
        return false;                                           // 0x5277a5 retn

    // 0x5277a9..0x5277d8: v2 = scaled game seconds (see header).
    const i32 v2 = ClockScaledGameSeconds(g.gameSpeed, g.daySeconds);

    // 0x5277de..0x527820: signed idiv decomposition, then
    // GameTime_Advance(&master, hours@edx, seconds@ecx, minutes@ebx).
    GameTimeAdvance(&g.master, v2 / 3600, v2 % 3600 % 60, v2 % 3600 / 60);
    return true;
}

// gilde.exe 0x4c09a0 @0x4c1324 — end-of-day gate predicate (see header).
bool ClockDayEndPending(bool headlessMask, bool menuPopPending, bool netWaitBusy,
                        bool roundEndRequested, bool dayEndLatch,
                        bool singlePlayer, u16 worldHour, u16 sessionFlags) {
    if (headlessMask)                                       // (v54 & 0x10000)
        return false;
    const bool armNet = !menuPopPending && !netWaitBusy && roundEndRequested
                        && !dayEndLatch;                    // 0x4c12e5..0x4c130b
    const bool armClock = singlePlayer && worldHour >= kDayEndHour
                          && !dayEndLatch;                  // 0x4c12f0..0x4c131c
    if (!(armNet || armClock))
        return false;
    if ((sessionFlags & kSessionFlagTutorial) != 0)         // word_63C740 & 0x80
        return false;
    return true;
}

} // namespace guild::sim
