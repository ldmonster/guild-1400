// tests/integration/session_tick_itest.cpp — a FULL CONTINUOUS GAME DAY driven
// through the reconstructed tick chain at the original cadence:
//   winmm 14 ms ticks (StartTimer(0xE,0) @0x527e52) -> clock proc @0x527778
//   every 71 ticks (994 ms) -> opcode-30 master->world commits every 7-tick
//   command window (0x4c0b8c/0x4c0bfe, executor @0x498954) -> day-end gate
//   @0x4c1324 at 23:00 -> InitOrLoadSession rollover @0x533a54 to 06:00 next
//   day. Verifies the EXACT original arithmetic: at speed level 2
//   (dword_1233558 = 80) each fire adds (80*0.00625+0.5)*100 = 100 game
//   seconds, so the 17 game-hours from 06:00 to 23:00 take exactly 612 fires.
#include "test.h"

#include "play/session_tick.h"
#include "sim/command_apply6.h"

using namespace guild;
using guild::play::SessionTick;

TEST(SessionTickIntegration, FullDayAtOriginalCadence) {
    sim::ResetApply6State();
    sim::g_tickClock = sim::GameTime{};
    sim::g_tickClock.day = sim::kSessionStartDay;  // 0x52f311: world day = 2
    sim::g_tickClock.hour = sim::kDayStartHour;    // 06:00:00
    sim::g_tickClock.minute = 0;
    sim::g_tickClock.second = 0;

    SessionTick st;
    st.SetGameSpeedLevel(2);                       // dword_1233558 = 80
    st.SyncClocksToDayStart();                     // master = world = 2/06:00
    st.BeginDay();                                 // clock unpaused

    // Drive 16 ms render frames until the day ends.
    long totalTicks = 0;
    long totalFires = 0;
    bool ended = false;
    int frames = 0;
    for (; frames < 100000 && !ended; ++frames) {
        auto r = st.OnFrame(16);
        totalTicks += r.timeBaseTicks;
        totalFires += r.clockFires;
        ended = r.dayEnded;
    }
    CHECK(ended);

    // 06:00:00 -> 23:00:00 is 61200 game seconds; 100 per fire => 612 fires,
    // and the master lands on 23:00:00 EXACTLY (no overshoot: the 613th fire
    // is blocked by the 0x527778 hour gate).
    CHECK_EQ(totalFires, 612);
    CHECK_EQ(st.masterTime().day, 2);
    CHECK_EQ((int)st.masterTime().hour, 23);
    CHECK_EQ(st.masterTime().minute, 0);
    CHECK_EQ(st.masterTime().second, 0);

    // The world clock was committed to the same instant before the gate fired.
    CHECK_EQ(st.worldTime().day, 2);
    CHECK_EQ((int)st.worldTime().hour, 23);
    CHECK_EQ(st.worldTime().minute, 0);
    CHECK_EQ(st.worldTime().second, 0);

    // Cadence identity: fires happen when the pre-increment tick counter is a
    // multiple of 71 (fptc @0x44e180: dword_62EB38 % interval == 0), i.e.
    // fires == floor((ticks - 1) / 71) + 1.
    CHECK_EQ(totalFires, (totalTicks - 1) / sim::kClockProcIntervalTicks + 1);

    // Real-time sanity at the original cadence: fire 612 occurs on tick
    // 611*71 + 1 = 43382, i.e. 43382 * 14 ms ~= 10.1 real minutes at speed 2.
    CHECK(totalTicks >= 611L * sim::kClockProcIntervalTicks + 1);

    // Day-end state: latch set (dword_63CC3C) and clock proc paused (0x4c1338).
    CHECK(st.dayEndLatched());
    CHECK(st.clockPaused());

    // --- the InitOrLoadSession round tail ---------------------------------
    int ffSteps = st.FastForwardToDayEnd();        // already 23:00 -> 0 steps
    CHECK_EQ(ffSteps, 0);

    // (The host runs the play::RunGameDay machinery here — the original's
    //  VIBE_GameLogic_RunTurnTransition @0x534587.)

    st.RollToNextDay();                            // +24h, 06:00:00, resume
    CHECK_EQ(st.worldTime().day, 3);
    CHECK_EQ((int)st.worldTime().hour, 6);
    CHECK_EQ(st.worldTime().minute, 0);
    CHECK_EQ(st.worldTime().second, 0);
    CHECK(!st.clockPaused());

    // --- next day spins up exactly the same way ---------------------------
    st.SyncClocksToDayStart();                     // master snaps to 3/06:00
    st.BeginDay();
    CHECK_EQ(st.masterTime().day, 3);

    long fires2 = 0;
    for (int i = 0; i < 200; ++i) {                // ~2.8 s of real time
        auto r = st.OnFrame(14);
        fires2 += r.clockFires;
        CHECK(!r.dayEnded);
    }
    CHECK(fires2 >= 2);                            // clock is running again
    // master advanced by fires2 * 100 game seconds from 06:00:00.
    long secs = 3600L * st.masterTime().hour + 60L * st.masterTime().minute
                + st.masterTime().second;
    CHECK_EQ(secs, 6L * 3600 + fires2 * 100);
    // world tracks master within one command window.
    CHECK_EQ(st.worldTime().day, 3);
}
