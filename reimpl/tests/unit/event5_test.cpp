// Unit tests for the four giant event5 deferred state machines (gilde.exe
// VIBE_Event_RunGebaeudeBauen / RunProduktion / DiscoveryRaidRun / SlotProcessRun).
// Each test installs scripted Event5Hooks and asserts the coroutine phase
// transitions + the He-record field writes the originals make. CHECK does NOT
// abort — every deref past a CHECK is guarded.
#include "test.h"

#include "world/event5.h"
#include "sim/he.h"
#include "sim/npcaction.h"   // NpcClock / SetNpcClock (shared clock)
#include "sim/gametime.h"

#include <cstring>
#include <vector>

using namespace guild;
using guild::world::Event5Hooks;
using guild::world::SetEvent5Hooks;
using guild::world::GetEvent5Hooks;
using guild::world::RunGebaeudeBauen;
using guild::world::RunProduktion;
using guild::world::DiscoveryRaidRun;
using guild::world::SlotProcessRun;
using guild::sim::HeRecord;
using guild::sim::HeBytes;
using guild::sim::GameTime;
using guild::sim::SetNpcClock;

namespace {
i32&      RD(HeRecord* h, int off) { return *reinterpret_cast<i32*>(HeBytes(h) + off); }
u16&      RW(HeRecord* h, int off) { return *reinterpret_cast<u16*>(HeBytes(h) + off); }
u8&       RB(HeRecord* h, int off) { return *reinterpret_cast<u8*>(HeBytes(h) + off); }
GameTime& RT(HeRecord* h, int off) { return *reinterpret_cast<GameTime*>(HeBytes(h) + off); }
HeRecord MakeHe() { HeRecord h; std::memset(&h, 0, sizeof(h)); return h; }

// Shared capture for hook side effects.
struct Cap {
    int frees = 0;
    int cancels = 0;
    int buildOps = 0;
    int single49 = 0;
    int named53 = 0;
    int entity29 = 0;
    std::vector<i32> queue17qty;
    int heStateUpdates = 0;
    int heMax = 0;
};
Cap* g_cap = nullptr;

// A fake scene-entity record large enough for all the +off accesses the machines
// make (the biggest offset touched is +579*4 in RunGebaeudeBauen).
u8 g_obj[4096];
u8 g_obj2[4096];

void ResetClock(int day = 5, int hour = 12) {
    GameTime c{}; c.day = day; c.hour = static_cast<u16>(hour); c.minute = 0; c.second = 0;
    SetNpcClock(c);
}

// Install a baseline hook set with all the common side-effect counters wired.
Event5Hooks BaseHooks() {
    SetEvent5Hooks(nullptr);
    Event5Hooks h = GetEvent5Hooks();
    h.freeHandlerEntry = [](HeRecord*) -> i32 { if (g_cap) g_cap->frees++; return 7; };
    h.requestBuildOp66 = [](i32) { if (g_cap) g_cap->buildOps++; };
    h.queueRequestSingle49 = [](i32) { if (g_cap) g_cap->single49++; };
    h.queueRequestNamedObject53 = [](i32, i32, i32, i32, i32, const char*) {
        if (g_cap) g_cap->named53++; };
    h.changePlayerAction = [](void*, void*, void*, u16) { if (g_cap) g_cap->cancels++; };
    h.queueRequestEntity29 = [](i32, const void*) -> i32 { if (g_cap) g_cap->entity29++; return 99; };
    h.queueRequest17 = [](i32, i32, i32 qty, i32, i32, i32) -> i32 {
        if (g_cap) g_cap->queue17qty.push_back(qty);
        return 1; };
    h.updateBuildingHeState = [](HeRecord*) { if (g_cap) g_cap->heStateUpdates++; };
    h.queryBuildingHeMax = [](HeRecord*) -> i32 { return g_cap ? g_cap->heMax : 0; };
    return h;
}
}  // namespace

// ---------------------------------------------------------------------------
// RunGebaeudeBauen
// ---------------------------------------------------------------------------

// Phase 0 (state -2): spawn path -> He-state update + free.
TEST(Event5RunGebaeudeBauen, Phase0SpawnFreesHandler) {
    Cap cap; g_cap = &cap; ResetClock();
    Event5Hooks h = BaseHooks();
    std::memset(g_obj, 0, sizeof(g_obj));
    RD(reinterpret_cast<HeRecord*>(g_obj), 4) = 0x1234;
    h.resolveEntityById = [](void** a, void**, i32, void**) -> i32 {
        if (a) *a = g_obj;
        return 1; };
    SetEvent5Hooks(&h);

    HeRecord he = MakeHe();
    RD(&he, 132) = -1;          // no pending packet -> gate opens
    RD(&he, 112) = -2;          // phase 0
    RD(&he, 188) = -1;          // no running script
    RD(&he, 196) = 4;           // current level
    RB(&he, 120) = 0;           // flag&2 clear -> no flag blob
    i32 r = RunGebaeudeBauen(&he);

    CHECK_EQ(cap.frees, 1);
    CHECK_EQ(RD(&he, 200), 5);  // +200 = +196 + 1
    CHECK_EQ(cap.heStateUpdates, 1);
    CHECK_EQ(r, 7);             // free's return
}

// Front gate: a pending packet (+132 status 0) blocks the phase entirely.
TEST(Event5RunGebaeudeBauen, PendingPacketBlocks) {
    Cap cap; g_cap = &cap; ResetClock();
    Event5Hooks h = BaseHooks();
    h.packetStatus = [](i32) -> i32 { return 0; };  // unresolved
    SetEvent5Hooks(&h);

    HeRecord he = MakeHe();
    RD(&he, 132) = 55;          // a live handle, status 0 -> blocked
    RD(&he, 112) = -2;
    i32 r = RunGebaeudeBauen(&he);
    CHECK_EQ(cap.frees, 0);     // nothing ran
    CHECK_EQ(r, 0);
    CHECK_EQ(RD(&he, 132), 55); // packet handle untouched (early return)
}

// Phase 6 (state 4): resolve fails -> set counter to -1 (no advance).
TEST(Event5RunGebaeudeBauen, Phase6ResolveFailSetsMinusOne) {
    Cap cap; g_cap = &cap; ResetClock();
    Event5Hooks h = BaseHooks();
    h.resolveEntityById = [](void**, void**, i32, void**) -> i32 { return 0; };  // miss
    SetEvent5Hooks(&h);

    HeRecord he = MakeHe();
    RD(&he, 132) = -1;
    RD(&he, 112) = 4;           // phase 6
    RunGebaeudeBauen(&he);
    CHECK_EQ(RD(&he, 112), -1);
}

// ---------------------------------------------------------------------------
// RunProduktion
// ---------------------------------------------------------------------------

// city index 0xFFFF -> teardown-only: frees immediately.
TEST(Event5RunProduktion, TeardownRecordFreesImmediately) {
    Cap cap; g_cap = &cap; ResetClock();
    Event5Hooks h = BaseHooks();
    h.buildingFindById = [](i32) -> void* { return nullptr; };
    SetEvent5Hooks(&h);

    HeRecord he = MakeHe();
    RW(&he, 8) = 0xFFFF;
    i32 r = RunProduktion(&he);
    CHECK_EQ(cap.frees, 1);
    CHECK_EQ(r, 7);
}

// phase -2: resolve building, build-op, cancel-actions over the table, free.
TEST(Event5RunProduktion, TeardownCancelsAndFrees) {
    Cap cap; g_cap = &cap; ResetClock();
    Event5Hooks h = BaseHooks();
    std::memset(g_obj, 0, sizeof(g_obj));
    RD(reinterpret_cast<HeRecord*>(g_obj), 1 /*+1*/) = 0;  // building id area
    h.resolveEntityById = [](void** a, void**, i32, void**) -> i32 {
        if (a) *a = g_obj;
        return 1; };
    // active-action table: slot 3 belongs to THIS record.
    static HeRecord* s_self = nullptr;
    h.activeActionHe = [](int p) -> void* { return p == 3 ? s_self : nullptr; };
    h.personActionCharId = [](int) -> u16 { return 17; };
    SetEvent5Hooks(&h);

    HeRecord he = MakeHe(); s_self = &he;
    RW(&he, 8) = 5;             // valid city
    RD(&he, 112) = -2;
    i32 r = RunProduktion(&he);
    CHECK_EQ(cap.buildOps, 1);
    CHECK_EQ(cap.cancels, 1);   // exactly one matching slot
    CHECK_EQ(cap.frees, 1);
    CHECK_EQ(r, 7);
}

// phase 0: ++counter.
TEST(Event5RunProduktion, Phase0Increments) {
    Cap cap; g_cap = &cap; ResetClock();
    Event5Hooks h = BaseHooks();
    SetEvent5Hooks(&h);
    HeRecord he = MakeHe();
    RW(&he, 8) = 5;
    RD(&he, 112) = 0;
    i32 r = RunProduktion(&he);
    CHECK_EQ(RD(&he, 112), 1);
    CHECK_EQ(r, 1);
}

// phase 1 with no pending work (+20/+24 both 0): ++counter to 2.
TEST(Event5RunProduktion, Phase1NoWorkAdvancesToTwo) {
    Cap cap; g_cap = &cap; ResetClock();
    Event5Hooks h = BaseHooks();
    SetEvent5Hooks(&h);
    HeRecord he = MakeHe();
    RW(&he, 8) = 5;
    RD(&he, 112) = 1;
    RD(&he, 20) = 0; RD(&he, 24) = 0;
    i32 r = RunProduktion(&he);
    CHECK_EQ(RD(&he, 112), 2);
    CHECK_EQ(r, 2);
}

// phase 2 with work re-appeared: -> counter 1.
TEST(Event5RunProduktion, Phase2WorkReappearsToOne) {
    Cap cap; g_cap = &cap; ResetClock();
    Event5Hooks h = BaseHooks();
    SetEvent5Hooks(&h);
    HeRecord he = MakeHe();
    RW(&he, 8) = 5;
    RD(&he, 112) = 2;
    RD(&he, 20) = 4;            // work present
    i32 r = RunProduktion(&he);
    CHECK_EQ(RD(&he, 112), 1);
    CHECK_EQ(r, 2);
}

// ---------------------------------------------------------------------------
// DiscoveryRaidRun
// ---------------------------------------------------------------------------

// phase 0/1 (state -2): per-member raid commands then free.
TEST(Event5DiscoveryRaid, Phase0QueuesRaidAndFrees) {
    Cap cap; g_cap = &cap; ResetClock();
    Event5Hooks h = BaseHooks();
    std::memset(g_obj, 0, sizeof(g_obj));
    h.personQueryBegin = [](i32, i32, i32, i32) -> void* { return g_obj; };  // leader
    h.buildingFindStorableObject = [](void*) -> void* { return g_obj; };
    h.personFindRecordById = [](i32) -> void* { return g_obj; };
    SetEvent5Hooks(&h);

    HeRecord he = MakeHe();
    RD(&he, 112) = -2;          // phase 0
    RD(&he, 140) = 100;         // member slot 0 filled
    RD(&he, 144) = 101;         // member slot 1 filled
    for (int s = 2; s < 8; ++s) RD(&he, 140 + 4 * s) = -1;
    i32 r = DiscoveryRaidRun(&he);
    CHECK_EQ(cap.single49, 2);
    CHECK_EQ(cap.named53, 2);
    CHECK_EQ(cap.cancels, 2);
    CHECK_EQ(cap.frees, 1);
    CHECK_EQ(r, 7);
}

// phase 2 (state 0): begin raid, set counter:=1, advance.
TEST(Event5DiscoveryRaid, Phase2BeginsRaid) {
    Cap cap; g_cap = &cap; ResetClock();
    Event5Hooks h = BaseHooks();
    std::memset(g_obj, 0, sizeof(g_obj));
    h.personQueryBegin = [](i32, i32, i32, i32) -> void* { return g_obj; };
    h.personFindRecordById = [](i32) -> void* { return g_obj; };
    SetEvent5Hooks(&h);

    HeRecord he = MakeHe();
    RD(&he, 112) = 0;           // phase 2
    RD(&he, 140) = 100;
    for (int s = 1; s < 8; ++s) RD(&he, 140 + 4 * s) = -1;
    DiscoveryRaidRun(&he);
    CHECK_EQ(RD(&he, 112), 1);
    CHECK_EQ(cap.single49, 1);
}

// phase 2 with no leader -> free.
TEST(Event5DiscoveryRaid, Phase2NoLeaderFrees) {
    Cap cap; g_cap = &cap; ResetClock();
    Event5Hooks h = BaseHooks();
    h.personQueryBegin = [](i32, i32, i32, i32) -> void* { return nullptr; };
    SetEvent5Hooks(&h);
    HeRecord he = MakeHe();
    RD(&he, 112) = 0;
    i32 r = DiscoveryRaidRun(&he);
    CHECK_EQ(cap.frees, 1);
    CHECK_EQ(r, 7);
}

// ---------------------------------------------------------------------------
// SlotProcessRun
// ---------------------------------------------------------------------------

// state -2 reseeds the target-city ref via the hook.
TEST(Event5SlotProcess, StateMinus2ReseedsCityRef) {
    Cap cap; g_cap = &cap; ResetClock();
    Event5Hooks h = BaseHooks();
    static int s_seedCalls = 0; s_seedCalls = 0;
    h.npcSetTargetCityRef = [](HeRecord*, u16) { s_seedCalls++; };
    h.playerCitySentinelWord = []() -> u16 { return 42; };
    h.resolveEntityById = [](void**, void**, i32, void**) -> i32 { return 0; };  // dest/source miss
    SetEvent5Hooks(&h);

    HeRecord he = MakeHe();
    RD(&he, 112) = -2;
    RD(&he, 232) = -1; RD(&he, 236) = -1;
    i32 r = SlotProcessRun(&he);
    CHECK_EQ(s_seedCalls, 1);
    CHECK_EQ(RD(&he, 112), 0);  // state cleared to 0
    CHECK_EQ(cap.frees, 1);     // dest/source missing -> free
    CHECK_EQ(r, 7);
}

// a pending packet (+232 status 0) idles +1 min instead of running.
TEST(Event5SlotProcess, PendingPacketIdles) {
    Cap cap; g_cap = &cap; ResetClock(8, 6);
    Event5Hooks h = BaseHooks();
    h.packetStatus = [](i32) -> i32 { return 0; };  // unresolved
    SetEvent5Hooks(&h);

    HeRecord he = MakeHe();
    RD(&he, 112) = 0;
    RD(&he, 232) = 77;          // pending
    RD(&he, 236) = -1;
    RT(&he, 82).day = 8; RT(&he, 82).hour = 6; RT(&he, 82).minute = 0;
    SlotProcessRun(&he);
    // GameTimeAdvance(+82, 0,0,1) adds 1 minute (arg "addDays"==0; the 4th arg is
    // addMinutes per the GameTimeAdvance signature).
    CHECK_EQ(RT(&he, 82).minute, 1);
    CHECK_EQ(cap.frees, 0);
}

// dest/source resolved, no sibling, no carry flag (+241) -> free (owner-chain gate).
TEST(Event5SlotProcess, NoCarryFlagFreesViaOwnerChain) {
    Cap cap; g_cap = &cap; ResetClock();
    Event5Hooks h = BaseHooks();
    std::memset(g_obj, 0, sizeof(g_obj));
    std::memset(g_obj2, 0, sizeof(g_obj2));
    h.resolveEntityById = [](void** a, void** b, i32, void** c) -> i32 {
        if (a) *a = g_obj;
        if (b) *b = g_obj2;
        if (c) *c = nullptr;
        return 1; };
    h.findFirstHandler5 = [](i32, i32, i32, i32, i32) -> HeRecord* { return nullptr; };
    h.findNextHandler = []() -> HeRecord* { return nullptr; };
    SetEvent5Hooks(&h);

    HeRecord he = MakeHe();
    RD(&he, 112) = 0;
    RD(&he, 232) = -1; RD(&he, 236) = -1;
    RB(&he, 241) = 0;           // no carry flag -> owner-chain free path
    i32 r = SlotProcessRun(&he);
    CHECK_EQ(cap.frees, 1);
    CHECK_EQ(r, 7);
}
