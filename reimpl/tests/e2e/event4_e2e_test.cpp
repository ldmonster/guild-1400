// e2e flow for src/world/event4.cpp — drive a full ConversationSink coroutine
// from phase 0 to teardown, and a WorkAction tick sequence, across the module's
// own functions with a scripted hook table that simulates the surrounding engine.
#include "test.h"

#include "world/event4.h"
#include "sim/he.h"
#include "sim/npcaction.h"
#include "sim/gametime.h"

#include <cstring>
#include <vector>

using namespace guild;
using guild::world::Event4Hooks;
using guild::world::SetEvent4Hooks;
using guild::world::GetEvent4Hooks;
using guild::sim::HeRecord;
using guild::sim::HeBytes;
using guild::sim::GameTime;
using guild::sim::SetNpcClock;

namespace {
i32&      RD(HeRecord* h, int off) { return *reinterpret_cast<i32*>(HeBytes(h) + off); }
u8&       RB(HeRecord* h, int off) { return *reinterpret_cast<u8*>(HeBytes(h) + off); }
HeRecord MakeHe() { HeRecord h; std::memset(&h, 0, sizeof(h)); return h; }

struct World {
    u8 person[600] = {0};
    u8 script[64]  = {0};   // the rec[97] script-context (its +40 is the run handle)
    int packetStatus = 1;   // request-39 poll result
    int frees = 0;
    int broadcasts = 0;
    int standUps = 0;
};
World* g_w = nullptr;
}  // namespace

TEST(Event4E2E, ConversationCoroutineFullFlow) {
    World w; g_w = &w;
    // Wire the person: rec[130] (+520) == -1 (no live route, runs the machine),
    // rec[97] (+388) points at the script context, class byte (+2) = 3 (-> phase 4),
    // person id mirror at +4.
    *reinterpret_cast<i32*>(w.person + 520) = -1;
    *reinterpret_cast<u8**>(w.person + 388) = w.script;
    *reinterpret_cast<i32*>(w.script + 40) = -1;   // no running script -> phase 1 advances
    w.person[2] = 3;
    *reinterpret_cast<i32*>(w.person + 4) = 1234;

    SetEvent4Hooks(nullptr);
    Event4Hooks h = GetEvent4Hooks();
    h.freeHandlerEntry = [](HeRecord*) -> i32 { if (g_w) g_w->frees++; return 0; };
    h.findPersonById = [](i32) -> void* { return g_w ? g_w->person : nullptr; };
    h.scriptFinishByHandle = [](i32) {};
    h.changePlayerAction = [](void*, void*, void*, u16) {};
    h.characterStandUp = [](void*) { if (g_w) g_w->standUps++; };
    h.insertCollapseAction = [](void*) {};
    h.selectConversationTarget = [](void*) -> void* { static int t; return &t; };
    h.broadcastFamilyNews = [](void*, void*, i32) { if (g_w) g_w->broadcasts++; };
    h.findEmploymentRelation = [](void*) -> i32 { return 7; };
    h.queueRequestPair33 = [](i32, i32) {};
    SetEvent4Hooks(&h);

    GameTime clk{}; clk.day = 2; clk.hour = 6; clk.minute = 0;
    SetNpcClock(clk);

    HeRecord he = MakeHe();
    RD(&he, 112) = 0;
    RD(&he, 172) = 1234;
    RB(&he, 120) = 0;

    // Phase 0 -> 1 (has script context, finishes, ++counter).
    world::ConversationSinkRun(&he);
    CHECK_EQ(RD(&he, 112), 1);
    // Phase 1 -> 2 (running handle is -1, advance).
    world::ConversationSinkRun(&he);
    CHECK_EQ(RD(&he, 112), 2);
    // Phase 2 -> 3 (stand up + collapse action, +30 min).
    world::ConversationSinkRun(&he);
    CHECK_EQ(RD(&he, 112), 3);
    CHECK_EQ(g_w->standUps, 1);
    // Phase 3: class byte 3 -> +2 min, counter:=4.
    world::ConversationSinkRun(&he);
    CHECK_EQ(RD(&he, 112), 4);
    // Phase 4: target found -> broadcast, free.
    world::ConversationSinkRun(&he);
    CHECK_EQ(g_w->broadcasts, 1);
    CHECK_EQ(g_w->frees, 1);

    SetEvent4Hooks(nullptr); g_w = nullptr;
}

TEST(Event4E2E, WorkActionTickSequence) {
    SetEvent4Hooks(nullptr);   // inert hooks throughout

    HeRecord he = MakeHe();
    RD(&he, 112) = 0;
    // 0 -> 1
    CHECK_EQ(world::WorkActionRun(&he), 1);
    CHECK_EQ(RD(&he, 112), 1);
    // phase 1, no work -> 2
    RD(&he, 20) = 0; RD(&he, 24) = 0;
    world::WorkActionRun(&he);
    CHECK_EQ(RD(&he, 112), 2);
    // phase 2, no work, inert resolve gives null station -> -1
    world::WorkActionRun(&he);
    CHECK_EQ(RD(&he, 112), -1);
    // phase -1 -> teardown (inert free returns 0)
    CHECK_EQ(world::WorkActionRun(&he), 0);
}
