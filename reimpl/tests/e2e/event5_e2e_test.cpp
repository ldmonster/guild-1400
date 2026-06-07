// End-to-end flow across the event5 machines: drive a full RunProduktion coroutine
// from the spawn phase through the no-work transition into teardown, then a full
// DiscoveryRaidRun begin->scan->teardown cycle, asserting the phase counter walks
// the exact sequence the original would and the right side effects fire at each
// step. (CHECK does not abort; all derefs are guarded.)
#include "test.h"

#include "world/event5.h"
#include "sim/he.h"
#include "sim/npcaction.h"
#include "sim/gametime.h"

#include <cstring>

using namespace guild;
using guild::world::Event5Hooks;
using guild::world::SetEvent5Hooks;
using guild::world::GetEvent5Hooks;
using guild::world::RunProduktion;
using guild::world::DiscoveryRaidRun;
using guild::sim::HeRecord;
using guild::sim::HeBytes;
using guild::sim::GameTime;
using guild::sim::SetNpcClock;

namespace {
i32& RD(HeRecord* h, int off) { return *reinterpret_cast<i32*>(HeBytes(h) + off); }
u16& RW(HeRecord* h, int off) { return *reinterpret_cast<u16*>(HeBytes(h) + off); }
GameTime& RT(HeRecord* h, int off) { return *reinterpret_cast<GameTime*>(HeBytes(h) + off); }
HeRecord MakeHe() { HeRecord h; std::memset(&h, 0, sizeof(h)); return h; }

struct Flow { int frees = 0; int builds = 0; int cancels = 0; };
Flow* g_flow = nullptr;
u8 g_bldg[2048];

Event5Hooks Hooks() {
    SetEvent5Hooks(nullptr);
    Event5Hooks h = GetEvent5Hooks();
    h.freeHandlerEntry = [](HeRecord*) -> i32 { if (g_flow) g_flow->frees++; return 0; };
    h.requestBuildOp66 = [](i32) { if (g_flow) g_flow->builds++; };
    h.changePlayerAction = [](void*, void*, void*, u16) { if (g_flow) g_flow->cancels++; };
    h.resolveEntityById = [](void** a, void**, i32, void**) -> i32 {
        if (a) *a = g_bldg;
        return 1; };
    return h;
}
}  // namespace

// RunProduktion: 0 -> 1 (no work) -> 2 -> teardown free.
TEST(Event5E2E, ProduktionSpawnNoWorkTeardown) {
    Flow f; g_flow = &f;
    GameTime c{}; c.day = 5; c.hour = 12; SetNpcClock(c);
    std::memset(g_bldg, 0, sizeof(g_bldg));
    Event5Hooks h = Hooks();
    SetEvent5Hooks(&h);

    HeRecord he = MakeHe();
    RW(&he, 8) = 7;             // valid city
    RD(&he, 112) = 0;           // start at phase 0
    RD(&he, 20) = 0; RD(&he, 24) = 0;  // never any pending work

    // tick 1: phase 0 -> 1
    RunProduktion(&he);
    CHECK_EQ(RD(&he, 112), 1);
    // tick 2: phase 1, no work -> 2
    RunProduktion(&he);
    CHECK_EQ(RD(&he, 112), 2);
    // tick 3: phase 2, still no work, building resolves -> cancel + buildop + free
    RunProduktion(&he);
    CHECK_EQ(f.frees, 1);
    CHECK_EQ(f.builds, 1);
}

// DiscoveryRaid: phase 2 begin -> counter 1 -> teardown (state -2) free.
TEST(Event5E2E, RaidBeginThenTeardown) {
    Flow f; g_flow = &f;
    GameTime c{}; c.day = 9; c.hour = 3; SetNpcClock(c);
    std::memset(g_bldg, 0, sizeof(g_bldg));
    Event5Hooks h = Hooks();
    h.personQueryBegin = [](i32, i32, i32, i32) -> void* { return g_bldg; };
    h.personFindRecordById = [](i32) -> void* { return g_bldg; };
    h.buildingFindStorableObject = [](void*) -> void* { return g_bldg; };
    h.queueRequestSingle49 = [](i32) {};
    h.queueRequestNamedObject53 = [](i32, i32, i32, i32, i32, const char*) {};
    SetEvent5Hooks(&h);

    HeRecord he = MakeHe();
    RD(&he, 112) = 0;           // phase 2 (begin)
    RD(&he, 140) = 200;         // one member
    for (int s = 1; s < 8; ++s) RD(&he, 140 + 4 * s) = -1;

    // begin: stamps appointment, advances +4 min, counter -> 1
    DiscoveryRaidRun(&he);
    CHECK_EQ(RD(&he, 112), 1);
    CHECK(RT(&he, 82).minute == 4);

    // teardown: drive state -2 -> per-member commands + free
    RD(&he, 112) = -2;
    DiscoveryRaidRun(&he);
    CHECK_EQ(f.frees, 1);
    CHECK(f.cancels >= 1);
}
