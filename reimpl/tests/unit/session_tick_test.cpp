// Unit tests for play::SessionTick (src/play/session_tick.*) — the per-frame
// session adapter over the reconstructed continuous game-clock chain:
// TimeBase fptc @0x44e130 (14 ms), clock proc @0x527778 (interval 71), the
// opcode-30 master->world sync (0x4c0b8c / ExAdvanceGameTick @0x498954), the
// day-end gate @0x4c1324 and the InitOrLoadSession day rollover @0x533a54.
#include "tests/framework/test.h"

#include "play/session_tick.h"
#include "sim/command_apply6.h"

using namespace guild;
using guild::play::SessionTick;

namespace {
sim::GameTime MakeTime(i32 day, u16 hour, i32 minute, i32 second) {
    sim::GameTime t{};
    t.day = day;
    t.hour = hour;
    t.minute = minute;
    t.second = second;
    return t;
}

// Fresh world-clock state for each test (sim::g_tickClock == qword_13CE852).
void SeedWorld(i32 day, u16 hour, i32 minute, i32 second) {
    sim::ResetApply6State();
    sim::g_tickClock = MakeTime(day, hour, minute, second);
}
} // namespace

// The clock proc is registered PAUSED (0x52f345, edx=1) — no fires until
// BeginDay() (the Scene_RunMainFrameLoop prologue unpause @0x50f1a6).
TEST(SessionTick, ClockStartsPausedUntilBeginDay) {
    SeedWorld(2, 6, 0, 0);
    SessionTick st;
    st.SyncClocksToDayStart();
    CHECK(st.clockPaused());

    auto r = st.OnFrame(994);            // 71 ticks — would fire if unpaused
    CHECK_EQ(r.timeBaseTicks, 71);
    CHECK_EQ(r.clockFires, 0);
    CHECK_EQ(st.masterTime().second, 0); // master untouched

    st.BeginDay();
    CHECK(!st.clockPaused());
}

// fptc cadence: the proc fires when the pre-increment tick counter is a
// multiple of 71 — i.e. on tick 1 (counter 0), tick 72 (counter 71), ...
TEST(SessionTick, ClockFiresEverySeventyOneTicks) {
    SeedWorld(2, 6, 0, 0);
    SessionTick st;
    st.SetGameSpeedLevel(0);             // 50 game-seconds per fire
    st.SyncClocksToDayStart();           // master = world = day 2, 06:00:00
    st.BeginDay();

    auto r = st.OnFrame(14);             // tick 1: counter 0 -> fire
    CHECK_EQ(r.timeBaseTicks, 1);
    CHECK_EQ(r.clockFires, 1);
    CHECK_EQ(st.masterTime().second, 50);

    r = st.OnFrame(14 * 70);             // ticks 2..71: counters 1..70 -> none
    CHECK_EQ(r.timeBaseTicks, 70);
    CHECK_EQ(r.clockFires, 0);

    r = st.OnFrame(14);                  // tick 72: counter 71 -> fire
    CHECK_EQ(r.clockFires, 1);
    CHECK_EQ(st.masterTime().minute, 1); // 06:01:40
    CHECK_EQ(st.masterTime().second, 40);
}

// Sub-14ms remainders carry across frames (the winmm timer is asynchronous to
// the render frame).
TEST(SessionTick, MillisecondRemainderCarries) {
    SeedWorld(2, 6, 0, 0);
    SessionTick st;
    st.BeginDay();
    auto r = st.OnFrame(13);
    CHECK_EQ(r.timeBaseTicks, 0);
    r = st.OnFrame(1);                   // 13 + 1 = 14 -> one tick
    CHECK_EQ(r.timeBaseTicks, 1);
    r = st.OnFrame(29);                  // 29 -> two ticks, 1 ms remainder
    CHECK_EQ(r.timeBaseTicks, 2);
}

// 0x4c0b8c + 0x4c0be5: the master clock is broadcast as an opcode-30 packet
// and committed into the world clock by ExAdvanceGameTick within one 7-tick
// command window.
TEST(SessionTick, TimeSyncCommitsMasterIntoWorld) {
    SeedWorld(2, 6, 0, 0);
    SessionTick st;
    st.SetGameSpeedLevel(0);
    st.SyncClocksToDayStart();
    st.BeginDay();

    auto r = st.OnFrame(14);             // fire 1 (master 06:00:50) + window
    CHECK_EQ(r.clockFires, 1);
    CHECK(r.commandWindowRan);
    CHECK_EQ(r.timeSyncCommits, 1);
    CHECK_EQ(st.worldTime().second, 50); // world == master after the commit
    CHECK_EQ(st.worldTime().day, 2);

    // Window throttle: dword_11AA488 = tick + 7 (@0x4c0bfe) — after the
    // window at tick 1 the threshold is 8, so ticks 2..8 run no window and
    // tick 9 runs the next one.
    int windows = 0;
    for (int i = 0; i < 7; ++i) {        // ticks 2..8
        r = st.OnFrame(14);
        if (r.commandWindowRan)
            ++windows;
    }
    CHECK_EQ(windows, 0);
    r = st.OnFrame(14);                  // tick 9 > 8 -> window
    CHECK(r.commandWindowRan);
}

// No Perm30 is emitted while master == world (the 0x494a50 GameTime_Compare
// gate): windows run but commit nothing.
TEST(SessionTick, NoCommitWhenClocksEqual) {
    SeedWorld(2, 6, 0, 0);
    SessionTick st;
    st.SyncClocksToDayStart();           // master == world
    // Clock stays paused (no BeginDay) so master never moves.
    auto r = st.OnFrame(14 * 16);        // two command windows
    CHECK(r.commandWindowRan);
    CHECK_EQ(r.timeSyncCommits, 0);
}

// 0x4c1324: when the WORLD clock reaches 23:00 in single-player, the day-end
// gate latches (dword_63CC3C) and pauses the clock proc.
TEST(SessionTick, DayEndGateLatchesAndPausesClock) {
    SeedWorld(2, 22, 59, 0);
    SessionTick st;
    st.SetGameSpeedLevel(4);             // 150 game-seconds per fire
    st.clock().master = MakeTime(2, 22, 59, 0);
    st.BeginDay();

    // One fire takes the master to 23:01:30; the following command window
    // commits it into the world; the gate then fires.
    bool ended = false;
    int totalFires = 0;
    for (int i = 0; i < 200 && !ended; ++i) {
        auto r = st.OnFrame(14);
        totalFires += r.clockFires;
        ended = r.dayEnded;
    }
    CHECK(ended);
    CHECK_EQ(totalFires, 1);
    CHECK(st.dayEndLatched());
    CHECK(st.clockPaused());
    CHECK_EQ(st.worldTime().hour, 23);
    CHECK_EQ(st.worldTime().minute, 1);
    CHECK_EQ(st.worldTime().second, 30);

    // Latched: further frames neither advance nor re-raise dayEnded.
    auto r = st.OnFrame(994);
    CHECK_EQ(r.clockFires, 0);
    CHECK(!r.dayEnded);
}

// Tutorial flag (word_63C740 & 0x80): the clock keeps running past 23:00 and
// the day-end gate never fires (0x527798 / 0x4c1324 tail).
TEST(SessionTick, TutorialSessionNeverEndsDay) {
    SeedWorld(2, 23, 30, 0);
    SessionTick st;
    st.SetSessionFlags(sim::kSessionFlagTutorial);
    st.clock().master = MakeTime(2, 23, 30, 0);
    st.BeginDay();
    auto r = st.OnFrame(14);
    CHECK_EQ(r.clockFires, 1);
    CHECK(!r.dayEnded);
    CHECK_EQ(st.masterTime().second, 50);
}

// 0x53449c..0x534581: fast-forward to 23:00 in +30 minute opcode-30 commits.
// From 13:37 that is 19 steps, landing on 23:07.
TEST(SessionTick, FastForwardToDayEndSteps) {
    SeedWorld(2, 13, 37, 0);
    SessionTick st;
    const int before = sim::Apply6_GetLog().tickAdvanceCount;
    int steps = st.FastForwardToDayEnd();
    CHECK_EQ(steps, 19);
    CHECK_EQ(st.worldTime().day, 2);
    CHECK_EQ(st.worldTime().hour, 23);
    CHECK_EQ(st.worldTime().minute, 7);
    CHECK_EQ(sim::Apply6_GetLog().tickAdvanceCount - before, 19); // cascade ran
    CHECK(st.clockPaused());             // 0x53449c pause
}

// word_63C740 & 8 skips the pre-turn fast-forward (0x5344a1).
TEST(SessionTick, FastForwardSkippedByFlag8) {
    SeedWorld(2, 13, 37, 0);
    SessionTick st;
    st.SetSessionFlags(sim::kSessionFlagSkipPreTurnFastForward);
    int steps = st.FastForwardToDayEnd();
    CHECK_EQ(steps, 0);
    CHECK_EQ(st.worldTime().hour, 13);   // untouched
    CHECK(st.clockPaused());             // still pauses the clock first
}

// 0x5345bf..0x534671: the rollover — fast-forward, +24 h, set 06:00:00,
// commit, resume the clock. Next day starts at 06:00.
TEST(SessionTick, RollToNextDayLandsOnSixAm) {
    SeedWorld(2, 23, 7, 0);              // already past the FF loop condition
    SessionTick st;
    st.RollToNextDay();
    CHECK_EQ(st.worldTime().day, 3);
    CHECK_EQ(st.worldTime().hour, 6);
    CHECK_EQ(st.worldTime().minute, 0);
    CHECK_EQ(st.worldTime().second, 0);
    CHECK(!st.clockPaused());            // 0x534671 resume
}

TEST(SessionTick, RollToNextDayFromMidDay) {
    SeedWorld(2, 22, 40, 0);
    SessionTick st;
    st.RollToNextDay();                  // one +30 step (23:10), then +24h/6:00
    CHECK_EQ(st.worldTime().day, 3);
    CHECK_EQ(st.worldTime().hour, 6);
    CHECK_EQ(st.worldTime().minute, 0);
}

// The network host owns the rollover when not single-player (0x5345bf guard).
TEST(SessionTick, RollToNextDayNoopWhenNotSinglePlayer) {
    SeedWorld(2, 23, 7, 0);
    SessionTick st;
    st.SetSinglePlayer(false);
    st.RollToNextDay();
    CHECK_EQ(st.worldTime().day, 2);     // untouched
    CHECK_EQ(st.worldTime().hour, 23);
}

// Turn-start sysmsg-3 (0x533a54 + ExSysMessage case 3 @0x498ba3): both clocks
// snap to the world day at 06:00:00.
TEST(SessionTick, SyncClocksToDayStartWritesBothClocks) {
    SeedWorld(3, 6, 0, 0);
    SessionTick st;
    st.clock().master = MakeTime(2, 23, 1, 30);   // stale master from day 2
    st.SyncClocksToDayStart();
    CHECK_EQ(st.masterTime().day, 3);
    CHECK_EQ(st.masterTime().hour, 6);
    CHECK_EQ(st.masterTime().minute, 0);
    CHECK_EQ(st.masterTime().second, 0);
    CHECK_EQ(st.worldTime().day, 3);
    CHECK_EQ(st.worldTime().hour, 6);
}

// Game-speed plumbing: dword_1233558 = 40 * level, clamped 0..4 (0x4ff800).
TEST(SessionTick, GameSpeedLevels) {
    SeedWorld(2, 6, 0, 0);
    SessionTick st;
    st.SetGameSpeedLevel(3);
    CHECK_EQ(st.GameSpeedLevel(), 3);
    CHECK_EQ(st.clock().gameSpeed, 120);
    st.SetGameSpeedLevel(7);             // clamps to 4 (0x4ff89b bound)
    CHECK_EQ(st.clock().gameSpeed, 160);
    st.SetGameSpeedLevel(-2);            // clamps to 0 (0x4ff8dc bound)
    CHECK_EQ(st.clock().gameSpeed, 0);

    st.SetGameSpeedLevel(1);             // 75 game-seconds per fire
    st.SyncClocksToDayStart();
    st.BeginDay();
    auto r = st.OnFrame(14);
    CHECK_EQ(r.clockFires, 1);
    CHECK_EQ(st.masterTime().minute, 1);
    CHECK_EQ(st.masterTime().second, 15);
}
