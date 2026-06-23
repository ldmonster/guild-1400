// Verifies InstallRealNpcAction3Wiring() binds the three late-batch NPC bridges
// (NpcAction11Hooks, NpcAction12Hooks, NpcMarketHooks) to their real reconstructed
// leaves — previously all three were fully inert at runtime (nothing installed
// them). Suite prefix: WireNpcAction3.
#include "tests/framework/test.h"

#include "sim/wire_npcaction3.h"
#include "sim/real_hooks3.h"   // InstallRealSimHooks3 (binds the sibling He pool + queue)
#include "sim/npcaction11.h"
#include "sim/npcaction12.h"
#include "sim/npc_market.h"
#include "sim/he.h"

#include <cstring>

using namespace guild;
using namespace guild::sim;

static void InertAll() {
    SetNpcAction11Hooks(nullptr);
    SetNpcAction12Hooks(nullptr);
    SetNpcMarketHooks(nullptr);
}

TEST(WireNpcAction3, BindsRealLeavesIntoAllThreeBridges) {
    InertAll();
    InstallRealNpcAction3Wiring();

    // --- NpcAction11Hooks: He / resolve / scalar / command emits are real -----
    const NpcAction11Hooks& h11 = GetNpcAction11Hooks();
    CHECK(h11.freeHandlerEntry             != nullptr);
    CHECK(h11.findConflictingHandler       != nullptr);
    CHECK(h11.findPersonById               != nullptr);
    CHECK(h11.resolveEntity                != nullptr);
    CHECK(h11.randomModulo                 != nullptr);
    CHECK(h11.moneyMultiplyByRate          != nullptr);
    CHECK(h11.buildingSumFlaggedSlotsWorth != nullptr);
    CHECK(h11.requestArgs25                != nullptr);
    CHECK(h11.requestCoord27               != nullptr);
    CHECK(h11.request17                    != nullptr);
    CHECK(h11.requestSingle58              != nullptr);
    CHECK(h11.requestBuildOp71             != nullptr);
    CHECK(h11.requestBuildOp72             != nullptr);
    CHECK(h11.requestBuildOp77             != nullptr);
    CHECK(h11.enqueueCmd15                 != nullptr);
    // unbound (no clean target) stays inert
    CHECK(h11.personQueryBegin             == nullptr);
    CHECK(h11.requestState23               == nullptr);

    // --- NpcAction12Hooks -----------------------------------------------------
    const NpcAction12Hooks& h12 = GetNpcAction12Hooks();
    CHECK(h12.freeHandlerEntry          != nullptr);
    CHECK(h12.findConflictingHandler    != nullptr);
    CHECK(h12.scanFilterHasForeignMatch != nullptr);
    CHECK(h12.findPersonById            != nullptr);
    CHECK(h12.resolveEntity             != nullptr);
    CHECK(h12.randomModulo              != nullptr);
    CHECK(h12.computeRankWithinGroup    != nullptr);
    CHECK(h12.groupFromCode             != nullptr);
    CHECK(h12.sumFlaggedSlotsWorth      != nullptr);
    CHECK(h12.shuffleDwords             != nullptr);
    CHECK(h12.requestCoord27            != nullptr);
    CHECK(h12.request17                 != nullptr);
    CHECK(h12.requestSingle59           != nullptr);
    CHECK(h12.requestArgs25             != nullptr);
    CHECK(h12.requestBuildOp77          != nullptr);
    CHECK(h12.enqueueCmd15              != nullptr);
    CHECK(h12.queueRequestMixed44       != nullptr);
    CHECK(h12.personQueryBegin          == nullptr);
    CHECK(h12.lookupMarketPrice         == nullptr);

    // --- NpcMarketHooks -------------------------------------------------------
    const NpcMarketHooks& hm = GetNpcMarketHooks();
    CHECK(hm.freeHandlerEntry != nullptr);
    CHECK(hm.computeYield     != nullptr);
    CHECK(hm.queueRequest17   != nullptr);
    CHECK(hm.marketEnabled    == nullptr);   // process-global, inert
    CHECK(hm.effectiveStock   == nullptr);

    InertAll();
}

// A representative action from each bridge runs through the installed real leaves
// over a zeroed He record without crashing, returning a defined result — i.e. the
// wired control flow actually executes (free/find resolve against the real pool,
// emits stage onto the real queue).
TEST(WireNpcAction3, WiredStepsExecuteOverZeroedRecord) {
    // Match the real boot order: InstallRealSimHooks3 creates/Init's the shared He
    // pool + command queue that these bridges reach through RealHandlerTable() /
    // RealCommandQueue().
    InstallRealSimHooks3();
    InstallRealNpcAction3Wiring();

    HeRecord rec;

    // npcaction11: the busy-target scan. A zeroed record (state 0) scans the real
    // (empty) pool for a filter-60 conflict, finds none, and advances. Void return;
    // exercising the wired findConflictingHandler + free against the real pool.
    std::memset(&rec, 0, sizeof(rec));
    NpcAction11_CheckTargetBusyState(&rec);

    // npcaction12: BeginScanType50 — scanFilterHasForeignMatch over the empty pool
    // reports "self is the only match" and stamps the appointment. Defined return.
    std::memset(&rec, 0, sizeof(rec));
    i32 a = NpcAction12_BeginScanType50(&rec);
    (void)a;

    // npcaction12: QueueRandomActions with a fixed seed draws a count and emits
    // that many slot-resets through the wired emit path. Returns the drawn count.
    g_lcgState = 1u;
    i32 n = NpcAction12_QueueRandomActions(/*maxCount=*/4, /*arg=*/0);
    CHECK(n >= 0);

    // npc_market: the supervisor step over a zeroed record. State 0 with the inert
    // (disabled) market gate proceeds through the daily window and re-arms / frees
    // against the real pool. Defined return.
    std::memset(&rec, 0, sizeof(rec));
    HeRecord* r = NpcMarket_RunMarktSupervisorStep(&rec);
    (void)r;

    InertAll();
}
