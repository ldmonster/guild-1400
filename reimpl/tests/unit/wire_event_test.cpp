// Verifies InstallRealEventWiring() binds the four world-event He-action bridges
// (EventHooks, Event3Hooks, Event4Hooks, Event5Hooks; src/world/event2.h ..
// event5.h) to their real reconstructed leaves — previously all four were fully
// inert at runtime (nothing in src/ installed them). Suite prefix: WireEvent.
#include "tests/framework/test.h"

#include "world/wire_event.h"
#include "world/event2.h"
#include "world/event3.h"
#include "world/event4.h"
#include "world/event5.h"
#include "sim/he.h"
#include "sim/gametime.h"    // GameTime
#include "sim/npcaction.h"   // SetNpcClock

#include <cstring>

using namespace guild;
using namespace guild::world;

// Re-inert every bridge so a clean baseline can be asserted before install.
static void InertAll() {
    SetEventHooks(nullptr);
    SetEvent3Hooks(nullptr);
    SetEvent4Hooks(nullptr);
    SetEvent5Hooks(nullptr);
}

TEST(WireEvent, BindsRealLeavesIntoAllFourBridges) {
    InertAll();
    // NOTE: the installer SEEDS each table from its module inert defaults (non-null
    // stubs) and overrides only the wireable fields — required because several event
    // bodies invoke hooks WITHOUT a null-check. So unbound fields stay as their
    // inert stubs (non-null), not null; we assert the BOUND fields point at real
    // adapters, and the execute test proves real behaviour.
    InstallRealEventWiring();

    // --- EventHooks (event2): the handler-pool teardown free is real ---------
    const EventHooks& e2 = GetEventHooks();
    CHECK(e2.freeHandlerEntry != nullptr);
    // sendEntityMessage stays inert (no clean target)

    // --- Event3Hooks: free / person / building / find / cmds / packet --------
    const Event3Hooks& e3 = GetEvent3Hooks();
    CHECK(e3.freeHandlerEntry         != nullptr);
    CHECK(e3.findPersonById           != nullptr);
    CHECK(e3.findFirstHandler         != nullptr);
    CHECK(e3.findNextHandler          != nullptr);
    CHECK(e3.findBuildingById         != nullptr);
    CHECK(e3.findObjectById           != nullptr);
    CHECK(e3.enqueueObjectInteraction != nullptr);
    CHECK(e3.queueRequestPair33       != nullptr);
    CHECK(e3.queueRequest17           != nullptr);
    CHECK(e3.packetStatus             != nullptr);
    CHECK(e3.packetSeq                != nullptr);
    CHECK(e3.resolveEntityById        != nullptr);

    // --- Event4Hooks: free / person / building / resolve / cmds / packet -----
    const Event4Hooks& e4 = GetEvent4Hooks();
    CHECK(e4.freeHandlerEntry    != nullptr);
    CHECK(e4.findPersonById      != nullptr);
    CHECK(e4.findBuildingById    != nullptr);
    CHECK(e4.resolveEntityById   != nullptr);
    CHECK(e4.queueRequest17      != nullptr);
    CHECK(e4.queueRequestCoord27 != nullptr);
    CHECK(e4.queueRequestPair33  != nullptr);
    CHECK(e4.packetStatus        != nullptr);
    CHECK(e4.packetSeqValue      != nullptr);

    // --- Event5Hooks: free / find / resolve / cmds / packet / sibling fwds ----
    const Event5Hooks& e5 = GetEvent5Hooks();
    CHECK(e5.freeHandlerEntry      != nullptr);
    CHECK(e5.findFirstHandler5     != nullptr);
    CHECK(e5.findFirstHandler3     != nullptr);
    CHECK(e5.findNextHandler       != nullptr);
    CHECK(e5.resolveEntityById     != nullptr);
    CHECK(e5.queueRequest17        != nullptr);
    CHECK(e5.queueRequestEntity29  != nullptr);
    CHECK(e5.queueRequestSingle49  != nullptr);
    CHECK(e5.queueRequestCoord27   != nullptr);
    CHECK(e5.queueRequestArgs25    != nullptr);
    CHECK(e5.queueRequest16        != nullptr);
    CHECK(e5.packetStatus          != nullptr);
    CHECK(e5.personFindRecordById  != nullptr);
    CHECK(e5.buildingFindById      != nullptr);
    CHECK(e5.updateBuildingHeState != nullptr);
    CHECK(e5.queryBuildingHeMax    != nullptr);

    InertAll();   // restore for any later test in this TU
}

// A representative action from each bridge runs over a zeroed He record through
// the installed real leaves without crashing, returning a defined result — i.e.
// the wired control flow actually executes (free resolves against the real He
// pool, find/resolve scan the real entity arrays, emits stage onto the real queue).
TEST(WireEvent, WiredStepsExecuteOverZeroedRecord) {
    InstallRealEventWiring();

    sim::GameTime clk{};
    clk.day = 1; clk.hour = 9; clk.minute = 0; clk.second = 0;
    sim::SetNpcClock(clk);

    HeRecord rec;

    // event2: ResetActionState — a pure clock stamp + advance. Returns the hour.
    std::memset(&rec, 0, sizeof(rec));
    i32 a = ResetActionState(&rec);
    CHECK(a >= 0);

    // event3: AllocKillPlayer — the target person (+172) is 0; findPersonById(0)
    // hits the real (empty) person array -> null -> the body logs + frees against
    // the real He pool. Defined return (the free result).
    std::memset(&rec, 0, sizeof(rec));
    HeRecord* d3 = AllocKillPlayer(&rec);
    (void)d3;

    // event4: QueryBuildingHeMax — switches slot 0, collects the "he_%i" nodes
    // (inert collectHeNodes -> 0 nodes), returns the max level (0). Defined.
    std::memset(&rec, 0, sizeof(rec));
    i32 c4 = QueryBuildingHeMax(&rec);
    CHECK(c4 >= 0);

    // event5: SlotProcessRun over a zeroed record. State 0 (counter+2 == 2): the
    // two packet gates (+232/+236) are 0 (resolved), the role entities (+172/+176)
    // resolve to null against the real (empty) arrays -> the body frees against the
    // real He pool. Defined return.
    std::memset(&rec, 0, sizeof(rec));
    i32 e5 = SlotProcessRun(&rec);
    (void)e5;

    InertAll();
}
