// End-to-end test: drive a synthetic NPC's He/handler record through a small daily
// routine (init -> idle-anim -> action-duration -> reset -> restore) tick by tick,
// with a seeded RNG and recording leaf-hook mocks, and verify the appointment-time
// and state progression against a hand-computed reference (see the golden values
// derived from the GameTimeAdvance arithmetic + the CRT LCG in the comments).
#include "tests/framework/test.h"

#include "sim/he.h"
#include "sim/npcaction.h"
#include "sim/npcevent.h"
#include "sim/gametime.h"
#include "crt/rand.h"

#include <cstring>

using namespace guild;
using namespace guild::sim;

namespace {

struct E2ERecorder { int cmd29 = 0; i32 nextHandle = 0xC0DE; };
E2ERecorder g_e2e;

i32 E2ECmd29(int, HeRecord*) { g_e2e.cmd29++; return g_e2e.nextHandle; }

GameTime Clk(i32 d, u16 h, i32 m, i32 s) {
    GameTime t{}; t.day = d; t.hour = h; t.minute = m; t.second = s; return t;
}

} // namespace

TEST(SimNpcActionE2E, DailyRoutineProgression) {
    HeRecord h;
    std::memset(&h, 0, sizeof(h));

    static NpcLeafHooks hooks;
    hooks = NpcLeafHooks{};
    hooks.queueRequestEntity29 = E2ECmd29;
    SetNpcLeafHooks(&hooks);
    g_e2e = E2ERecorder{};

    // The host clock is held fixed across ticks (the engine re-stamps the live
    // global clock each step; here the wall clock does not advance between our
    // synthetic ticks, isolating the per-step deltas).
    SetNpcClock(Clk(10, 8, 0, 0));
    crt::Srand(42);

    // --- tick 1: NpcEvent_ResetAndQueueEntity (arm the NPC) -------------------
    He_Flags(&h) = 0;
    NpcEvent_ResetAndQueueEntity(&h);
    CHECK_EQ(He_SavedTime(&h).day, 10);     // saved <- clock
    CHECK_EQ(He_ApptTime(&h).day, 10);
    CHECK_EQ(He_ApptTime(&h).minute, 1);    // clock + 1 minute
    CHECK_EQ(He_ReqHandle(&h), (i32)0xC0DE);
    CHECK_EQ(g_e2e.cmd29, 1);

    // --- tick 2: BeginIdleAnim (v2 = mod(30) = 1; +1h-equiv, -19 minutes) -----
    NpcAction_BeginIdleAnim(&h);
    CHECK_EQ(He_ApptTime(&h).day, 10);
    CHECK_EQ(He_ApptTime(&h).hour, 9);      // 8 + addDays(1) hour-accumulator
    CHECK_EQ(He_ApptTime(&h).minute, -19);  // faithful negative-minute artifact

    // --- tick 3: AddTimeToActionDuration (add = mod(21-8)=3; minute=mod(59)=47) -
    NpcAction_AddTimeToActionDuration(&h);
    CHECK_EQ(He_ApptTime(&h).hour, 11);     // 8 + 3
    CHECK_EQ(He_ApptTime(&h).minute, 47);
    CHECK_EQ(He_ApptTime(&h).second, 0);

    // --- tick 4: ResetToState0 (+2 days hour-accumulator, state cleared) ------
    He_State(&h) = 7;
    NpcAction_ResetToState0(&h);
    CHECK_EQ(He_ApptTime(&h).day, 10);
    CHECK_EQ(He_ApptTime(&h).hour, 10);     // 8 + 2
    CHECK_EQ(He_ApptTime(&h).minute, 0);
    CHECK_EQ(He_State(&h), 0);

    // --- tick 5: RestorePoseReset (appt <- saved, state/wait cleared) ---------
    He_WaitCounter(&h) = 9;
    NpcEvent_RestorePoseReset(&h);
    CHECK_EQ(He_ApptTime(&h).day, He_SavedTime(&h).day);
    CHECK_EQ(He_ApptTime(&h).hour, He_SavedTime(&h).hour);   // restored to 8
    CHECK_EQ(He_ApptTime(&h).hour, 8);
    CHECK_EQ(He_State(&h), 0);
    CHECK_EQ(He_WaitCounter(&h), 0);
}

// A second flow: dispatch routing through the recovered jump table while the NPC
// waits out an idle-wait appointment, then re-arms.
TEST(SimNpcActionE2E, DispatchAndReArm) {
    HeRecord h;
    std::memset(&h, 0, sizeof(h));

    static NpcLeafHooks hooks;
    hooks = NpcLeafHooks{};
    hooks.queueRequestEntity29 = E2ECmd29;
    SetNpcLeafHooks(&hooks);
    g_e2e = E2ERecorder{};

    SetNpcClock(Clk(10, 8, 0, 0));
    crt::Srand(5);

    // Dispatch with type 45 (RetZero slot) -> 0; the day>=8 guard is satisfied.
    *reinterpret_cast<u16*>(reinterpret_cast<u8*>(&h) + 4) = 45;
    CHECK_EQ(NpcAction_Dispatch(&h), 0);

    // Arm an idle-wait appointment; deadline +24h, wait counter from RNG.
    He_Flags(&h) = 0;
    NpcAction_BeginIdleWaitState(&h);
    CHECK_EQ(He_ApptTime(&h).day, 10);          // +1 added to hour-accumulator (8->9)
    CHECK_EQ(He_ApptTime(&h).hour, 9);
    CHECK_EQ(He_Deadline(&h).day, 11);          // 10 + 24h hour-accumulator wraps a day
    CHECK_EQ(He_WaitCounter(&h), 22);           // mod(4)+19 (seed 5)
    CHECK_EQ(g_e2e.cmd29, 1);

    // The deadline (+176) is strictly after the appointment-stamp clock: a
    // GameTimeCompare against the live clock reports the clock is earlier (-1).
    GameTime clock = NpcClock();
    CHECK_EQ(GameTimeCompare(&clock, &He_Deadline(&h)), -1);
}
