// End-to-end tests for the NpcEvent step machines (npcevent_steps.{h,cpp}). Each
// test drives a single NPC through one full timed-event routine tick-by-tick with
// scripted leaves, advancing the global clock to each armed appointment, and
// verifies the phase progression + emitted commands against a reference trace.
#include "tests/framework/test.h"

#include <cstdint>
#include <cstring>
#include <vector>

#include "sim/npcevent_steps.h"
#include "sim/npcaction.h"
#include "sim/gametime.h"
#include "sim/he.h"
#include "crt/rand.h"

using namespace guild;
using namespace guild::sim;

namespace {

struct E2EState {
    int freeCount = 0;
    int entity29Count = 0;
    int lastArg = 0;
    std::vector<int> entity29Args;
    int op49 = 0, op53 = 0, op25 = 0, op28 = 0;
    int status = 2;     // packets apply immediately by default
    int seq = 0;
    int entity = 0;
    int person = 0;
    int nearDoor = 0;
    int pause = 0, resume = 0, detach = 0;
    int moveCode = 0;
    int approach = 1;
};
E2EState s;

NpcLeafHooks MakeLeaf() {
    NpcLeafHooks lh{};
    lh.freeHandlerEntry = [](HeRecord* h) -> i32 { ++s.freeCount; return reinterpret_cast<intptr_t>(h); };
    lh.queueRequestEntity29 = [](int arg, HeRecord*) -> i32 {
        ++s.entity29Count; s.lastArg = arg; s.entity29Args.push_back(arg);
        return 0x1000 + s.entity29Count;
    };
    lh.packetStatus = [](i32) -> i32 { return s.status; };
    return lh;
}
NpcEventHooks MakeEvent() {
    NpcEventHooks ev{};
    ev.resolveEntity = [](i32) -> i32 { return s.entity; };
    ev.entityField = [](i32 e, int off) -> i32 {
        if (!e) return 0;
        if (off == 1) return 0x500 + e;
        if (off == 4) return 0x600 + e;
        if (off == 39) return s.nearDoor ? 3 : 0xFFFF;
        if (off == 1000) return 1;
        return 0;
    };
    ev.findPerson = [](i32) -> i32 { return s.person; };
    ev.personField = [](i32 p, int off) -> i32 {
        if (!p) return 0;
        if (off == 1) return 0x700 + p;
        if (off == 4) return 0x800 + p;
        if (off == 388) return 0x900 + p;
        if (off == 8) return 1;
        return 0;
    };
    ev.isNearDoor = [](i32, i32) -> int { return s.nearDoor; };
    ev.queueRequestSingle49 = [](i32) -> i32 { ++s.op49; return 0x49; };
    ev.queueRequestArgs25 = [](i32, int, int, int, int) -> i32 { ++s.op25; return 0x25; };
    ev.queueRequestNamedObject53 = [](i32, i32, int, int, int, const char*) -> i32 { ++s.op53; return 0x53; };
    ev.queueRequestQuad52 = [](i32, i32, int, int) -> i32 { return 0x52; };
    ev.requestBuildOp87 = [](i32) -> i32 { return 0x87; };
    ev.requestBuildOp77 = [](i32) -> i32 { return 0x77; };
    ev.packetStatus = [](i32) -> i32 { return s.status; };
    ev.packetSeq = [](i32) -> i32 { return s.seq; };
    ev.reaperApproach = [](HeRecord*) -> int { return s.approach; };
    ev.reaperMove = [](HeRecord*) -> int { return s.moveCode; };
    ev.reaperCachePose = [](HeRecord*) -> int { return 1; };
    ev.reaperUpdateSound = [](HeRecord*) -> int { return 1; };
    ev.reaperDetach = [](i32) { ++s.detach; };
    ev.cutscenePause = []() { ++s.pause; };
    ev.cutsceneResume = []() { ++s.resume; };
    ev.loadDemandSnapshot = [](i32 o[3]) -> float { o[0] = 100; o[1] = 50; o[2] = 0; return 0.5f; };
    ev.enqueueObjectInteraction = [](int, int, int, int, int, int, int, int) -> i32 { return 0x99; };
    ev.nodeFieldGet = [](i32, int) -> i32 { return 0; };
    ev.nodeFieldSet = [](i32, int, i32) {};
    return ev;
}

GameTime* Appt(HeRecord* h) { return reinterpret_cast<GameTime*>(reinterpret_cast<u8*>(h) + 82); }
i32* P(HeRecord* h, int off) { return reinterpret_cast<i32*>(reinterpret_cast<u8*>(h) + off); }

} // namespace

// ===========================================================================
// E2E: drive ObjectInteractionStep through a full market routine tick-by-tick.
//   phase 0 (init) -> phase 1 (demand snapshot) -> haggle -> ... -> day boundary.
// At each tick we advance the clock to the armed appointment and re-enter.
// ===========================================================================
TEST(SimNpcEventE2E, ObjectInteractionRoutine) {
    s = E2EState();
    NpcLeafHooks lh = MakeLeaf();
    NpcEventHooks ev = MakeEvent();
    SetNpcLeafHooks(&lh);
    SetNpcEventHooks(&ev);
    crt::Srand(99);

    HeRecord rec;
    std::memset(&rec, 0, sizeof(rec));
    GameTime clk{}; clk.day = 10; clk.hour = 9; clk.minute = 0;
    SetNpcClock(clk);

    // Tick 1: phase 0 -> resets cursors, advances +5s, arms phase 1.
    *P(&rec, 112) = 0;
    NpcEvent_ObjectInteractionStep(&rec);
    CHECK_EQ(*P(&rec, 172), 0);
    CHECK_EQ(s.entity29Args.back(), 1);   // armed phase 1
    CHECK_EQ(Appt(&rec)->second, 5);

    // Host marks the record now in phase 1 (the cmd29 callback sets +112 = arg).
    *P(&rec, 112) = 1;
    // Tick 2: phase 1 reads the demand snapshot (ratio 0.5 -> 0.25..0.75 -> arm 7,
    // hour forced to 15) and advances +1 hour first.
    NpcEvent_ObjectInteractionStep(&rec);
    CHECK_EQ(s.entity29Args.back(), 7);   // mid-supply path -> phase 7
    CHECK_EQ((int)Appt(&rec)->hour, 15);

    // Tick 3: phase 7 does the hour bookkeeping. Clock hour 9 -> appt advanced +10
    // min; since appt hour (15) >= 0xF and < 0x16 -> re-arms phase 7.
    *P(&rec, 112) = 7;
    NpcEvent_ObjectInteractionStep(&rec);
    CHECK_EQ(s.entity29Args.back(), 7);

    // The routine kept emitting cmd29 each tick (one per re-arm) and never freed.
    CHECK_EQ(s.freeCount, 0);
    CHECK(s.entity29Count >= 3);

    SetNpcLeafHooks(nullptr);
    SetNpcEventHooks(nullptr);
}

// ===========================================================================
// E2E: drive the Reaper/plague routine from pick -> move -> arrive -> finish.
// ===========================================================================
TEST(SimNpcEventE2E, ReaperPlagueRoutine) {
    s = E2EState();
    NpcLeafHooks lh = MakeLeaf();
    NpcEventHooks ev = MakeEvent();
    SetNpcLeafHooks(&lh);
    SetNpcEventHooks(&ev);
    crt::Srand(7);

    HeRecord rec;
    std::memset(&rec, 0, sizeof(rec));
    GameTime clk{}; clk.day = 12; clk.hour = 0;
    SetNpcClock(clk);

    *P(&rec, 200) = 0xAAAA;   // avatar attached
    *P(&rec, 192) = 2;        // two more plague hops remaining
    s.entity = 5;             // targets resolve
    s.nearDoor = 1;           // owner valid

    // Tick 1: phase 0, move "still moving" (code != 1,2) -> phase set -1? No: code 0
    // means lost -> phase -1. Use moveCode 1 (arrived near) -> stays phase 0.
    *P(&rec, 112) = 0;
    s.moveCode = 1;
    NpcEvent_ReaperPlagueStep(&rec);
    CHECK_EQ(*P(&rec, 112), 0);
    CHECK_EQ(s.pause, 1);

    // Tick 2: phase 2 caches pose -> drops to phase 0.
    *P(&rec, 112) = 2;
    NpcEvent_ReaperPlagueStep(&rec);
    CHECK_EQ(*P(&rec, 112), 0);

    // Tick 3: phase 0, move "arrived, advance to next target" (code 2). With
    // remaining (+192) > 0 it picks the next plague target and decrements.
    *P(&rec, 112) = 0;
    s.moveCode = 2;
    int before = *P(&rec, 192);
    NpcEvent_ReaperPlagueStep(&rec);
    CHECK(*P(&rec, 192) <= before);

    // Tick 4: teardown -> detach + resume + free.
    *P(&rec, 112) = -1;
    NpcEvent_ReaperPlagueStep(&rec);
    CHECK_EQ(s.detach, 1);
    CHECK_EQ(s.resume, 1);
    CHECK_EQ(s.freeCount, 1);

    SetNpcLeafHooks(nullptr);
    SetNpcEventHooks(nullptr);
}

// ===========================================================================
// E2E: drive the Politician driver across a wake + a daily-reset wake.
// ===========================================================================
TEST(SimNpcEventE2E, SimPoliticiansRoutine) {
    s = E2EState();
    NpcLeafHooks lh = MakeLeaf();
    NpcEventHooks ev = MakeEvent();
    SetNpcLeafHooks(&lh);
    SetNpcEventHooks(&ev);
    crt::Srand(3);

    HeRecord rec;
    std::memset(&rec, 0, sizeof(rec));
    GameTime clk{}; clk.day = 5; clk.hour = 8; clk.minute = 0;
    SetNpcClock(clk);

    // Initialise all 10 slots empty.
    for (int i = 0; i < 10; ++i) *P(&rec, 172 + 16 * i) = -1;
    *P(&rec, 132) = -1;
    s.person = 0;     // no candidate found -> FindTarget returns -1 each try

    // Tick 1: clock hour 8 stamped + 5 min -> appt hour 8 (< 0x11), runs the slot
    // pass and 10 FindTarget tries, all -1, then re-arms cmd29(0).
    *P(&rec, 112) = 0;
    NpcEvent_SimPoliticiansStep(&rec);
    CHECK_EQ(s.lastArg, 0);
    CHECK_EQ(s.freeCount, 0);

    // Tick 2: bump the appt to the 17:00 boundary by setting the clock to 17:00 so
    // the stamped appt hour (>= 0x11) takes the daily-reset branch.
    clk.hour = 17; SetNpcClock(clk);
    *P(&rec, 112) = 0;
    *P(&rec, 132) = -1;
    NpcEvent_SimPoliticiansStep(&rec);
    CHECK_EQ(s.lastArg, 0);
    // all slots empty -> keptCount 0 -> hour reset to 0 and day advanced.
    CHECK_EQ((int)Appt(&rec)->hour, 0);

    SetNpcLeafHooks(nullptr);
    SetNpcEventHooks(nullptr);
}
