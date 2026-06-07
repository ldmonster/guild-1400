// End-to-end flow for src/world/event2.cpp: schedule an event action, advance it
// through its phase machine (trigger -> apply effect -> teardown), and verify the
// scheduled appointment times and the message/free side effects across ticks.
#include "test.h"

#include "world/event2.h"
#include "sim/npcaction.h"   // SetNpcClock
#include "crt/rand.h"        // Srand

#include <cstring>

using namespace guild;
using guild::world::HeRecord;
using guild::sim::GameTime;

namespace {
HeRecord* MakeRecord() {
    HeRecord* h = new HeRecord();
    std::memset(h, 0, sizeof(*h));
    return h;
}
void SetClock(int day, int hour, int minute, int second) {
    GameTime t{};
    t.day = day; t.hour = static_cast<u16>(hour); t.minute = minute; t.second = second;
    guild::sim::SetNpcClock(t);
}

int g_sends = 0;
int g_frees = 0;
i32 RecFree(HeRecord*) { ++g_frees; return 0; }
void RecSend(i32, i32, i32, const void*, i32, i32) { ++g_sends; }
}  // namespace

// Full help-text playback flow:
//   schedule (table of 2 steps) -> tick fires step 0 (send + schedule step 1)
//   -> tick fires step 1 (send, table exhausted -> state=1)
//   -> tick with state=1 tears down (free).
TEST(EventSys, E2E_HelpTextPlaybackFlow) {
    guild::world::EventHooks hooks{ &RecFree, &RecSend };
    world::SetEventHooks(&hooks);

    // A two-step schedule: each step has a valid text id so both send.
    static const guild::world::HelpStep kSteps[] = {
        { /*day*/ 11, /*hour*/ 9,  /*minute*/ 0,  /*textId*/ 100 },
        { /*day*/ 12, /*hour*/ 14, /*minute*/ 30, /*textId*/ 200 },
    };
    world::SetHelpScheduleTable(kSteps, 2);
    SetClock(10, 8, 30, 0);

    HeRecord* h = MakeRecord();
    guild::sim::He_State(h) = 0;     // armed (state+2 == 2)
    guild::sim::He_Counter(h) = 0;   // cursor at step 0
    g_sends = 0; g_frees = 0;

    // --- Tick 1: fire step 0, schedule step 1 ---
    int r1 = world::HelpTextPlaybackRun(h, /*helpEnabled*/ true);
    CHECK_EQ(g_sends, 1);                       // step 0 had text id -> sent
    CHECK_EQ((int)guild::sim::He_Counter(h), 1); // cursor advanced
    GameTime& a1 = guild::sim::He_ApptTime(h);
    // After firing step 0 the cursor is 1; the next appointment is scheduled from
    // the NEW cursor's row (step 1 = day 12, hour 14, minute 30).
    CHECK_EQ(a1.day, 12);
    CHECK_EQ((int)a1.hour, 14);
    CHECK_EQ(a1.minute, 30);
    CHECK_EQ(r1, 30);                           // returns step 1 minute
    CHECK_EQ(guild::sim::He_State(h), 0);       // still armed

    // --- Tick 2: fire step 1, table exhausted -> state = 1 ---
    int r2 = world::HelpTextPlaybackRun(h, true);
    CHECK_EQ(g_sends, 2);                       // step 1 sent
    CHECK_EQ((int)guild::sim::He_Counter(h), 2);
    CHECK_EQ(guild::sim::He_State(h), 1);       // exhausted -> teardown armed
    CHECK_EQ(r2, 1);

    // --- Tick 3: state 1 (state+2 == 3? no: state 1 -> key 3 -> default idle) ---
    // state==1 -> key 3 -> default passthrough returns 3 (no free yet). The engine
    // re-enters with state set to a teardown sentinel only via the abort path; the
    // table-exhausted state 1 simply idles until the owner aborts it. Verify idle.
    int r3 = world::HelpTextPlaybackRun(h, true);
    CHECK_EQ(r3, 3);
    CHECK_EQ(g_frees, 0);

    // --- Abort: state -1 -> key 1 -> teardown (free) ---
    guild::sim::He_State(h) = -1;
    int r4 = world::HelpTextPlaybackRun(h, true);
    CHECK_EQ(g_frees, 1);
    CHECK_EQ(r4, 0);                            // RecFree returns 0

    delete h;
    world::SetHelpScheduleTable(nullptr, 0);
    world::SetEventHooks(nullptr);
}

// Advice-loop flow: enabled gate broadcasts and reschedules with an RNG wait;
// after the cursor passes 26 the state flips to teardown.
TEST(EventSys, E2E_HelpAdviceLoopFlow) {
    guild::world::EventHooks hooks{ &RecFree, &RecSend };
    world::SetEventHooks(&hooks);
    static const i32 kAdvice[] = { 1, 2, 3 };
    world::SetAdviceIdTable(kAdvice, 3);
    SetClock(10, 8, 30, 0);
    crt::Srand(12345);  // RandomModulo(4) -> 0 (golden)

    HeRecord* h = MakeRecord();
    guild::sim::He_State(h) = 0;
    guild::sim::He_Counter(h) = 0;
    g_sends = 0; g_frees = 0;

    int r = world::HelpAdviceLoopRun(h, /*adviceEnabled*/ true);
    CHECK_EQ(g_sends, 1);
    CHECK_EQ((int)guild::sim::He_Counter(h), 1);   // cursor advanced
    GameTime& a = guild::sim::He_ApptTime(h);
    // rng=0 -> advance(8,0,0): clock hour 8 + 8 = 16
    CHECK_EQ(a.day, 10);
    CHECK_EQ((int)a.hour, 16);
    CHECK_EQ(r, 16);
    CHECK_EQ(guild::sim::He_State(h), 0);          // cursor 1 <= 26, still looping

    // Drive the cursor past 26 and verify state flips to teardown.
    guild::sim::He_Counter(h) = 26;
    crt::Srand(12345);
    world::HelpAdviceLoopRun(h, true);
    CHECK_EQ((int)guild::sim::He_Counter(h), 27);
    CHECK_EQ(guild::sim::He_State(h), 1);          // 27 > 26 -> teardown armed

    delete h;
    world::SetAdviceIdTable(nullptr, 0);
    world::SetEventHooks(nullptr);
}

// Initializer scheduling chain: AllocActionAnim4 sets an appointment, then a
// later ResetActionStateZero re-stamps and re-arms from the clock — verifying the
// schedule-time helpers compose as the scheduler uses them.
TEST(EventSys, E2E_InitializerScheduleChain) {
    HeRecord* h = MakeRecord();
    GameTime& saved = guild::sim::He_SavedTime(h);
    saved.day = 10; saved.hour = 8; saved.minute = 30; saved.second = 0;
    crt::Srand(12345);  // RandomModulo(60) -> 48

    int r1 = world::AllocActionAnim4(h);            // +4h +48min -> 13:18
    GameTime& a = guild::sim::He_ApptTime(h);
    CHECK_EQ((int)a.hour, 13);
    CHECK_EQ(a.minute, 18);
    CHECK_EQ(r1, 13);

    // Re-arm from the live clock (a later scheduler tick).
    SetClock(11, 6, 0, 0);
    int r2 = world::ResetActionStateZero(h);        // clock + 20 minutes
    CHECK_EQ(a.day, 11);
    CHECK_EQ((int)a.hour, 6);
    CHECK_EQ(a.minute, 20);
    CHECK_EQ(r2, 6);

    delete h;
}
