#include "test.h"

#include <cstdlib>
#include <vector>

#include "sim/production_slots.h"
#include "sim/building.h"
#include "sim/building_production.h"

using namespace guild;
using namespace guild::sim;

// End-to-end flow for the production-slot module (guild::sim) — gilde.exe.
//
// Drives a whole workshop production cycle across the real sibling modules:
//   1. aggregate a workshop's work slots (capacities + worth via the real price
//      model),
//   2. run the per-NPC production-timer countdown to completion, capturing the
//      completion command,
// verifying the running totals and the fire decision against a hand-computed
// reference. GUARDED on GUILD_GAME_DIR so it stays a clean skip without assets;
// the math runs regardless of asset presence (it uses the in-binary tables).

namespace {
struct E2EHooks : IProductionSlotHooks {
    int  diff = 0;
    int  emitted = 0;
    i16  lastType = -1;
    // Real price model for worth; scripted clock + command capture for the timer.
    double MarketPrice(i16 type) override { return Building_ComputeMarketPrice(type, 100); }
    int DiffMinutes(const GameTime&, const GameTime&) override { return diff; }
    void EmitProductionFinished(i32, i16 t) override { ++emitted; lastType = t; }
};
}  // namespace

TEST(SimProdSlotsE2E, WorkshopCycle) {
    // Guard: only run the asset-backed portion when a game dir is configured.
    const bool haveAssets = std::getenv("GUILD_GAME_DIR") != nullptr;

    ResetProductionTables();
    E2EHooks h;
    SetProductionSlotHooks(&h);

    // --- Phase 1: aggregate the workshop work slots. --------------------------
    // A non-high-cap workshop with three graded work slots.
    std::vector<ProdSlotNode> slots = {{300, 1}, {301, 2}, {302, 4}};
    ProdSlotCollect agg;
    int rc = InventoryCollectProductionSlots(slots, agg);
    CHECK_EQ(rc, 1);
    CHECK_EQ(agg.count, 2);              // slots[0] is the ROOT (0x59231d)
    // 0x59234d/0x5923e1: every slot's capacity comes from the ROOT node's
    // {type, level} — root {300,1} -> 20*1 = 20 for both child slots.
    CHECK_EQ(agg.caps[0], 20);
    CHECK_EQ(agg.caps[1], 20);
    CHECK_EQ(agg.capTotal, 40);
    CHECK_EQ(agg.levTotal, 6);           // child levels 2 + 4
    // worth: int accumulation with per-iteration truncation (0x5923b8), level
    // narrowed to float (0x59239e).
    int w = 0;
    w = static_cast<int>(Building_ComputeMarketPrice(301, 100) *
                             static_cast<double>(2.0f) + static_cast<double>(w));
    w = static_cast<int>(Building_ComputeMarketPrice(302, 100) *
                             static_cast<double>(4.0f) + static_cast<double>(w));
    CHECK(agg.worth == static_cast<double>(w));

    // --- Phase 2: run a work order's timer to completion. ---------------------
    ProductionOrder order;
    order.active = true;
    order.timerMinutes = 120;     // 2 hours of work remaining
    order.productType = 302;
    order.personId = 41;

    GameTime now{};
    // First tick: 45 minutes elapse — still running.
    h.diff = 45;
    ProductionTickResult r1 = InventoryTickProductionOrder(order, now, /*owner*/ true);
    CHECK(r1.finished == false);
    CHECK_EQ(order.timerMinutes, 75);
    CHECK_EQ(h.emitted, 0);

    // Second tick: another 90 minutes — order completes and emits the command.
    h.diff = 90;
    ProductionTickResult r2 = InventoryTickProductionOrder(order, now, /*owner*/ true);
    CHECK(r2.finished == true);
    CHECK(r2.emitted == true);
    CHECK(order.active == false);
    CHECK_EQ(order.timerMinutes, -15);
    CHECK_EQ(h.emitted, 1);
    CHECK_EQ(h.lastType, static_cast<i16>(302));

    // A finished, now-inactive order is skipped by subsequent ticks.
    h.diff = 1000;
    ProductionTickResult r3 = InventoryTickProductionOrder(order, now, true);
    CHECK(r3.finished == false);
    CHECK_EQ(h.emitted, 1);

    SetProductionSlotHooks(nullptr);
    (void)haveAssets;  // asset-backed extensions would gate here; math runs always.
}
