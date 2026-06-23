// Unit tests for the reconstructed continuous game-clock pieces
// (src/sim/game_clock_tick.*): the clock proc @0x527778, its scaled-seconds
// computation (flt_622958 / dbl_622960 / dword_63CC60), the day-end gate
// @0x4c1324, and VIBE_GameTime_Set @0x5831f0.
#include "tests/framework/test.h"

#include "sim/game_clock_tick.h"
#include "sim/gametime.h"

using namespace guild;
using namespace guild::sim;

namespace {
GameTime MakeTime(i32 day, u16 hour, i32 minute, i32 second) {
    GameTime t{};
    t.day = day;
    t.hour = hour;
    t.minute = minute;
    t.second = second;
    return t;
}
} // namespace

// ---------------------------------------------------------------------------
// ClockScaledGameSeconds — golden vectors for the five reachable speed values
// (dword_1233558 = 40 * level): seconds = (40L * 0.00625 + 0.5) * 100
//                                       = 25L + 50.
// ---------------------------------------------------------------------------
TEST(GameClockTick, ScaledSecondsGoldenSpeeds) {
    CHECK_EQ(ClockScaledGameSeconds(0, kClockDaySecondsScale), 50);
    CHECK_EQ(ClockScaledGameSeconds(40, kClockDaySecondsScale), 75);
    CHECK_EQ(ClockScaledGameSeconds(80, kClockDaySecondsScale), 100);
    CHECK_EQ(ClockScaledGameSeconds(120, kClockDaySecondsScale), 125);
    CHECK_EQ(ClockScaledGameSeconds(160, kClockDaySecondsScale), 150);
}

// VIBE_Coord_ConvertX @0x5c6b08 installs control word 0x1F7F (bytes
// c6 44 24 01 1f -> mov byte[esp+1],0x1F; fldcw) whose RC field (CW bits 10-11)
// is 0b11 == round-toward-ZERO (truncate), then runs frndint. The clock value
// already carries a +0.5 bias, so the net effect is round-half-UP for positive
// values, NOT round-half-even. Exact-half inputs therefore truncate downward
// (x.5 -> x). Verified bit-identical against an 80-bit x87 emulation with
// RC=11/truncl over speed 0..400.
TEST(GameClockTick, ScaledSecondsTruncatesTowardZero) {
    // speed 0 => value = 0.5 * daySeconds; frndint(RC=11) == trunc.
    CHECK_EQ(ClockScaledGameSeconds(0, 1), 0);  // 0.5  -> 0
    CHECK_EQ(ClockScaledGameSeconds(0, 3), 1);  // 1.5  -> 1
    CHECK_EQ(ClockScaledGameSeconds(0, 5), 2);  // 2.5  -> 2
    CHECK_EQ(ClockScaledGameSeconds(0, 7), 3);  // 3.5  -> 3
    CHECK_EQ(ClockScaledGameSeconds(0, 9), 4);  // 4.5  -> 4
}

// Wave-15 binary-diff regression: the +0.5 bias plus RC=11 truncation makes the
// clock value round-half-UP for off-ladder speeds. std::nearbyint (the old,
// wrong model) would double-round and diverge here. Goldens computed against an
// 80-bit x87 emulation (frndint with RC=0b11 / truncl) of the exact 0x527778 op
// sequence: v=((double)speed*0.00625f + 0.5)*100 ; trunc(v).
TEST(GameClockTick, ScaledSecondsOffLadderMatchesX87Truncate) {
    // speed 1: 1*0.00625+0.5 = 0.50625 ; *100 = 50.625 ; trunc = 50.
    CHECK_EQ(ClockScaledGameSeconds(1, kClockDaySecondsScale), 50);
    // speed 3: 0.51875 ; *100 = 51.875 ; trunc = 51 (nearbyint gives 52).
    CHECK_EQ(ClockScaledGameSeconds(3, kClockDaySecondsScale), 51);
    // speed 4: 0.525 ; *100 = 52.5 ; trunc = 52 (nearbyint gives 53).
    CHECK_EQ(ClockScaledGameSeconds(4, kClockDaySecondsScale), 52);
    // speed 9: 0.55625 ; *100 = 55.625 ; trunc = 55 (nearbyint gives 56).
    CHECK_EQ(ClockScaledGameSeconds(9, kClockDaySecondsScale), 55);
    // speed 11: 0.56875 ; *100 = 56.875 ; trunc = 56 (nearbyint gives 57).
    CHECK_EQ(ClockScaledGameSeconds(11, kClockDaySecondsScale), 56);
}

// ---------------------------------------------------------------------------
// ClockComputeGameTimeOfDay @0x527778 — advance + carries.
// ---------------------------------------------------------------------------
TEST(GameClockTick, ClockAdvancesMasterAtSpeedZero) {
    GameTime world = MakeTime(2, 6, 0, 0);
    ClockGlobals g{};
    g.master = MakeTime(2, 6, 0, 0);
    g.world = &world;
    g.gameSpeed = 0;

    CHECK(ClockComputeGameTimeOfDay(g));      // fire 1: +50 s
    CHECK_EQ(g.master.hour, 6);
    CHECK_EQ(g.master.minute, 0);
    CHECK_EQ(g.master.second, 50);

    CHECK(ClockComputeGameTimeOfDay(g));      // fire 2: 06:01:40
    CHECK_EQ(g.master.minute, 1);
    CHECK_EQ(g.master.second, 40);
    CHECK_EQ(g.master.day, 2);                // day untouched
}

TEST(GameClockTick, ClockHourCarryAtSpeedTwo) {
    GameTime world = MakeTime(2, 6, 0, 0);
    ClockGlobals g{};
    g.master = MakeTime(2, 6, 59, 30);
    g.world = &world;
    g.gameSpeed = 80;                          // level 2 -> 100 s per fire

    CHECK(ClockComputeGameTimeOfDay(g));       // 06:59:30 + 100 s = 07:01:10
    CHECK_EQ(g.master.hour, 7);
    CHECK_EQ(g.master.minute, 1);
    CHECK_EQ(g.master.second, 10);
}

// 0x52777c..0x52779f: gate — master hour < 23 AND world hour < 23, or the
// tutorial flag (word_63C740 & 0x80).
TEST(GameClockTick, ClockGateStopsAtElevenPm) {
    GameTime world = MakeTime(2, 6, 0, 0);
    ClockGlobals g{};
    g.master = MakeTime(2, 23, 0, 0);          // master at 23:00
    g.world = &world;
    CHECK(!ClockComputeGameTimeOfDay(g));
    CHECK_EQ(g.master.minute, 0);              // unchanged

    g.master = MakeTime(2, 22, 59, 0);         // master ok, world at 23:00
    world = MakeTime(2, 23, 0, 0);
    CHECK(!ClockComputeGameTimeOfDay(g));
    CHECK_EQ(g.master.minute, 59);             // unchanged
}

TEST(GameClockTick, TutorialFlagBypassesElevenPmGate) {
    GameTime world = MakeTime(2, 23, 30, 0);
    ClockGlobals g{};
    g.master = MakeTime(2, 23, 30, 0);
    g.world = &world;
    g.sessionFlags = kSessionFlagTutorial;     // word_63C740 & 0x80
    CHECK(ClockComputeGameTimeOfDay(g));
    CHECK_EQ(g.master.minute, 30);
    CHECK_EQ(g.master.second, 50);             // +50 s at speed 0
}

// Hour 23 carries into the day via GameTime_Advance's 24-wrap when the
// tutorial flag keeps the clock running.
TEST(GameClockTick, TutorialClockWrapsMidnight) {
    GameTime world = MakeTime(2, 0, 0, 0);
    ClockGlobals g{};
    g.master = MakeTime(2, 23, 59, 40);
    g.world = &world;
    g.gameSpeed = 0;
    g.sessionFlags = kSessionFlagTutorial;
    CHECK(ClockComputeGameTimeOfDay(g));       // +50 s -> next day 00:00:30
    CHECK_EQ(g.master.day, 3);
    CHECK_EQ(g.master.hour, 0);
    CHECK_EQ(g.master.minute, 0);
    CHECK_EQ(g.master.second, 30);
}

// ---------------------------------------------------------------------------
// ClockDayEndPending — the 0x4c1324 gate, both arms.
// ---------------------------------------------------------------------------
TEST(GameClockTick, DayEndGateClockArm) {
    // single-player, world hour >= 23, latch clear -> pending.
    CHECK(ClockDayEndPending(false, false, false, false, false, true, 23, 0));
    // hour below 23 -> not pending.
    CHECK(!ClockDayEndPending(false, false, false, false, false, true, 22, 0));
    // latch set (dword_63CC3C) suppresses.
    CHECK(!ClockDayEndPending(false, false, false, false, true, true, 23, 0));
    // not single-player (dword_764CE0 != -1) -> clock arm dead.
    CHECK(!ClockDayEndPending(false, false, false, false, false, false, 23, 0));
    // tutorial flag (word_63C740 & 0x80) suppresses.
    CHECK(!ClockDayEndPending(false, false, false, false, false, true, 23,
                              kSessionFlagTutorial));
    // headless feature-mask bit 0x10000 suppresses.
    CHECK(!ClockDayEndPending(true, false, false, false, false, true, 23, 0));
}

TEST(GameClockTick, DayEndGateNetArm) {
    // round-end requested (dword_11AA480), no menu pop / net wait, latch clear.
    CHECK(ClockDayEndPending(false, false, false, true, false, false, 6, 0));
    // menu pop pending (dword_631614) suppresses the net arm.
    CHECK(!ClockDayEndPending(false, true, false, true, false, false, 6, 0));
    // net wait busy (dword_11BC27C) suppresses the net arm.
    CHECK(!ClockDayEndPending(false, false, true, true, false, false, 6, 0));
    // latch suppresses.
    CHECK(!ClockDayEndPending(false, false, false, true, true, false, 6, 0));
}

// ---------------------------------------------------------------------------
// GameTimeSet @0x5831f0 — sets hour/minute/second, keeps the day, returns the
// minute argument.
// ---------------------------------------------------------------------------
TEST(GameClockTick, GameTimeSetDayStart) {
    GameTime t = MakeTime(5, 23, 41, 7);
    int r = GameTimeSet(&t, kDayStartHour, 0, 0);   // 0x534659 call shape
    CHECK_EQ(r, 0);
    CHECK_EQ(t.day, 5);
    CHECK_EQ(t.hour, 6);
    CHECK_EQ(t.minute, 0);
    CHECK_EQ(t.second, 0);
}

TEST(GameClockTick, GameTimeSetArbitraryFields) {
    GameTime t = MakeTime(9, 1, 2, 3);
    int r = GameTimeSet(&t, 23, 59, 58);            // dl=hour cl=second bl=minute
    CHECK_EQ(r, 58);
    CHECK_EQ(t.day, 9);
    CHECK_EQ(t.hour, 23);
    CHECK_EQ(t.minute, 58);
    CHECK_EQ(t.second, 59);
}

// 0x5831f0 hour-word composition quirk: the original builds the hour WORD as
// v5 = (i16)second; LOBYTE(v5) = hour  =>  word = (second & 0xFF00) | hour.
// Because `second` arrives as a byte its high byte is always 0, so the stored
// hour word is plain `hour` and second NEVER bleeds into it. Pin that the high
// byte of the hour field stays clear even with the maximum byte `second`.
TEST(GameClockTick, GameTimeSetSecondNeverBleedsIntoHourWord) {
    GameTime t = MakeTime(1, 0, 0, 0);
    GameTimeSet(&t, /*hour=*/0x07, /*second=*/0xFF, /*minute=*/0x00);
    CHECK_EQ(t.hour, static_cast<u16>(0x0007));  // (0xFF & 0xFF00) | 0x07 == 7
    CHECK_EQ(t.second, 0xFF);                     // second stored full in +10
}

// Cadence constants — pin the recovered values so a regression is loud.
TEST(GameClockTick, CadenceConstants) {
    CHECK_EQ(kTimeBaseTickMs, 14u);            // StartTimer(0xE, 0) @0x527e52
    CHECK_EQ(kClockProcIntervalTicks, 71);     // RegisterProc @0x52f339
    CHECK_EQ(kClockProcIntervalTicks * (int)kTimeBaseTickMs, 994); // ms per fire
    CHECK_EQ(kCommandWindowTicks, 7u);         // 0x4c0bfe
    CHECK_EQ(kClockDaySecondsScale, 100);      // dword_63CC60
    CHECK_EQ(kDayEndHour, 23);                 // 0x17 gates
    CHECK_EQ((int)kDayStartHour, 6);           // 0x534649
    CHECK_EQ(kDayFastForwardMinutes, 30);      // 0x534604 loop step
    CHECK_EQ(kDayRollAdvanceHours, 24);        // 0x534634
    CHECK_EQ(kSessionStartDay, 2);             // 0x52f311
    CHECK_EQ(kGameSpeedStep, 40);              // 0x4ff8b1
    CHECK_EQ(kGameSpeedMaxLevel, 4);           // 0x4ff89b bound
}

// Recovered FP / flag-bit constants (the raw image bytes behind the cadence).
TEST(GameClockTick, RecoveredConstantBytes) {
    // flt_622958 @0x622958 — bytes CD CC CC 3B == 0.00625f (1/160).
    CHECK_EQ(kClockSpeedScale, 0.00625f);
    // dbl_622960 @0x622960 — bytes 00 00 00 00 00 00 E0 3F == 0.5.
    CHECK_EQ(kClockHalfBias, 0.5);
    // word_63C740 flag bits the clock chain consumes.
    CHECK_EQ((int)kSessionFlagTutorial, 0x80);              // 0x527798 / 0x4c1324
    CHECK_EQ((int)kSessionFlagSkipPreTurnFastForward, 0x08); // 0x5344a1 test bh,8
}
