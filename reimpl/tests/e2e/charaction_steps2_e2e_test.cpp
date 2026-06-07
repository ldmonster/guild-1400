// End-to-end flow for charaction_steps2 — drives a full CharAction coroutine
// across the translated leaves: an "extortion / repeat-command" appointment that
// arms via ExtortInit, fires its repeat-command emits over several ticks, then
// reaches a terminal state and is finalized/freed. Wires recording leaf hooks so
// the whole appointment lifecycle is observable. Suite prefix: CharActionX.
#include "test.h"

#include "sim/charaction_steps2.h"
#include "sim/he.h"
#include "sim/npcaction.h"
#include "crt/rand.h"

#include <cstring>

using namespace guild;
using namespace guild::sim;

namespace {

struct Flow {
    int freeCalls = 0;
    int cmd29Calls = 0;
    int cmd15Calls = 0;
    i32 lastCity = 0, lastVal = 0;
    i32 handleSeq = 1000;
};
Flow g_flow;

i32 FlowFree(HeRecord*) { ++g_flow.freeCalls; return 0; }
i32 FlowCmd29(int, HeRecord*) { ++g_flow.cmd29Calls; return ++g_flow.handleSeq; }
i32 FlowPacketStatus(i32) { return 1; }
void FlowCmd15(i32 city, i32 val) { ++g_flow.cmd15Calls; g_flow.lastCity = city; g_flow.lastVal = val; }
i32 FlowResolveCity(u16 idx) { return 2000 + idx; }

void Install() {
    g_flow = Flow{};
    static NpcLeafHooks lh;
    lh = NpcLeafHooks{};
    lh.freeHandlerEntry = FlowFree;
    lh.queueRequestEntity29 = FlowCmd29;
    lh.packetStatus = FlowPacketStatus;
    SetNpcLeafHooks(&lh);

    static CharActionStep2Hooks sh;
    sh = CharActionStep2Hooks{};
    sh.enqueueCmd15 = FlowCmd15;
    sh.resolveCityId = FlowResolveCity;
    SetCharActionStep2Hooks(&sh);

    GameTime t{}; t.day = 5; t.hour = 12; t.minute = 0; t.second = 0;
    SetNpcClock(t);
}

} // namespace

// A racketeer arms an extortion appointment, then runs a repeat-command schedule
// that fires N times (one per "tick"), re-arming each time, and finally frees the
// handler entry on the last iteration. Then a terminal finalize confirms cleanup.
TEST(CharActionX, E2E_ExtortThenRepeatThenFinalize) {
    Install();
    HeRecord rec;
    std::memset(&rec, 0, sizeof(rec));

    // --- arm the extortion appointment ---
    Cas2_Flags(&rec) = 0;                 // not yet spawned
    He_CityIndex(&rec) = 7;               // +8
    Cas2_Counter(&rec) = 500;             // +172 racket payload
    i32 handle = ExtortInit(&rec);
    CHECK(handle > 1000);                 // got a cmd29 packet handle
    CHECK_EQ(Cas2_State(&rec), 1);        // armed -> state 1
    CHECK_EQ(Cas2_SuccId(&rec), -1);
    CHECK_EQ(Cas2_Packet(&rec), handle);
    CHECK_EQ(g_flow.cmd29Calls, 1);

    // --- the repeat-command schedule: 3 iterations, re-arming twice then free ---
    Cas2_State(&rec) = 0;                 // active emit state
    Cas2_RemIter(&rec) = 3;               // +176 remaining iterations

    // tick 1: emit + re-arm (rem 3 -> 2)
    RepeatCommandStep(&rec);
    CHECK_EQ(g_flow.cmd15Calls, 1);
    CHECK_EQ(g_flow.lastCity, 2007);      // resolveCityId(7)
    CHECK_EQ(g_flow.lastVal, 500);
    CHECK_EQ(Cas2_RemIter(&rec), 2);
    CHECK_EQ(g_flow.freeCalls, 0);

    // tick 2: emit + re-arm (2 -> 1)
    RepeatCommandStep(&rec);
    CHECK_EQ(g_flow.cmd15Calls, 2);
    CHECK_EQ(Cas2_RemIter(&rec), 1);
    CHECK_EQ(g_flow.freeCalls, 0);

    // tick 3: emit, last iteration (1 -> 0) -> free
    RepeatCommandStep(&rec);
    CHECK_EQ(g_flow.cmd15Calls, 3);
    CHECK_EQ(Cas2_RemIter(&rec), 0);
    CHECK_EQ(g_flow.freeCalls, 1);

    // --- a follow-on finalize on a terminal record frees again ---
    HeRecord rec2;
    std::memset(&rec2, 0, sizeof(rec2));
    Cas2_State(&rec2) = -2;               // terminal
    i32 fr = FinalizeEntityStep(&rec2);
    CHECK_EQ(fr, 0);
    CHECK_EQ(g_flow.freeCalls, 2);
}

// A second flow: a group action retarget path that, finding no members, frees;
// and a copy-goal step that arms an appointment from the saved pose.
TEST(CharActionX, E2E_CopyGoalAndRestore) {
    Install();
    HeRecord rec;
    std::memset(&rec, 0, sizeof(rec));

    // Saved pose at +68 = day 5, 12:00:00.
    GameTime saved{}; saved.day = 5; saved.hour = 12; saved.minute = 0; saved.second = 0;
    Cas2_SavedTime(&rec) = saved;

    int hr = CopyGoalToTargetState2(&rec);   // +68 -> +82, +2 days
    CHECK_EQ(hr, 14);                         // 12 + 2*24h folded -> result hour 14
    CHECK_EQ(Cas2_ApptTime(&rec).day, 5);
    CHECK_EQ(static_cast<int>(Cas2_ApptTime(&rec).hour), 14);

    // RestorePosFinish with a deterministic RNG (seed 1 -> no free).
    crt::Srand(1);
    Cas2_SavedTime(&rec) = saved;
    int rv = RestorePosFinish(&rec);
    CHECK_EQ(rv, 0);
    CHECK_EQ(g_flow.freeCalls, 0);
    CHECK_EQ(Cas2_ApptTime(&rec).day, 5);     // restored from +68
    CHECK_EQ(static_cast<int>(Cas2_ApptTime(&rec).hour), 12);
}
