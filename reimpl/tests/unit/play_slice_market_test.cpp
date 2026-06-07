// tests/unit/play_slice_market_test.cpp — UNIT: the MARKET/TRADE slice classifier
// + apply, on a SYNTHETIC live world (no assets).
//
//   * ClassifyMarketInteraction (the golden): (side, ware, qty, kind) -> the
//     opcode-17 trade command, with the unit price from the REAL price oracle
//     sim::Building_ComputeMarketPrice(ware,100).
//   * The apply mutates the building's treasury (object+77) and stock (fillLevel
//     @+10) as expected for a buy vs a sell.
//   * The full slice is deterministic (same inputs -> identical world hashes).
#include "test.h"

#include "play/slice_market.h"
#include "sim/building_production.h"   // g_sceneTypes / SceneTypeDefAt / g_sceneTypesLoaded
#include "sim/entity.h"
#include "sim/command.h"
#include "world/city.h"               // g_goods / g_cities / g_capDivisor / totals
#include "world/economy_tick.h"       // SetSmoothedPriceLevel

#include <cstring>

using namespace guild;
using namespace guild::play;

namespace {

// Seed one live building in g_objects and a priceable scene-type for `ware`.
// Returns the live building id.
i32 SeedMarketWorld(i16 ware, u32 baseValue, u16 divisor) {
    // Blank the folded world tables RunEconomyTurn dirties so each run's hashes are
    // a pure function of (seed + interaction), independent of a prior run in-process
    // (the play-layer ZeroWorldGlobals determinism rule).
    std::memset(world::g_goods, 0, sizeof(world::g_goods));
    std::memset(world::g_cities, 0, sizeof(world::g_cities));
    world::g_capDivisor = 0.0f;
    world::g_cityTotalMoney = 0.0f;
    world::g_cityTotalGoods = 0.0f;
    world::SetSmoothedPriceLevel(0.0f);

    sim::ResetEntityArrays();
    std::memset(sim::g_objects, 0, sizeof(sim::g_objects));   // clear pad bytes too
    sim::g_sceneArrayLoaded = true;

    // Clear + seed the scene-type catalog so Building_ComputeMarketPrice prices the
    // ware off a real (non-zero) base. A category-23 ("raw") good takes the simple
    // priced branch: 2.2 * 0.5 * (base/divisor) * 4 (no type remap).
    std::memset(sim::g_sceneTypes, 0, sizeof(sim::g_sceneTypes));
    std::memset(sim::g_sceneTypeRemap, 0, sizeof(sim::g_sceneTypeRemap));
    sim::g_sceneTypesLoaded = true;
    sim::SceneTypeDef* td = sim::SceneTypeDefAt(ware);
    if (td) {
        td->kind = 23;            // raw good -> simple priced branch
        td->baseValue = (i32)baseValue;
        td->divisor = divisor;
        td->cachedPrice = 0;
    }

    const i32 kId = 7001;
    sim::ObjectRec& o = sim::g_objects[2];
    o.alive = 1;
    o.id    = kId;
    // Seed a starting stock (fillLevel @+10) so a sell has something to remove.
    auto* b = reinterpret_cast<sim::BuildingRec*>(&o);
    b->fillLevel = 500;
    // Zero the treasury field (object+77).
    i32 zero = 0;
    std::memcpy(reinterpret_cast<u8*>(&o) + kTreasuryFieldOff, &zero, sizeof zero);
    return kId;
}

} // namespace

// --- classifier golden: a SELL on a contor (kind 2) -> opcode-17 command. -------
TEST(PlaySliceMarketUnit, ClassifySellCommandGolden) {
    i32 id = SeedMarketWorld(/*ware=*/5, /*base=*/80, /*divisor=*/2);

    MarketInteraction mi;
    mi.side = MarketSide::kSell;
    mi.ware = 5;
    mi.qty  = 10;
    mi.buildingId = id;
    mi.buildingKind = 2;       // sellable contor
    mi.player = 0;

    MarketCommand c = ClassifyMarketInteraction(mi);
    CHECK(c.issued);
    CHECK_EQ((int)c.opcode, 17);             // VIBE_Command_QueueRequest17
    CHECK(c.side == MarketSide::kSell);
    CHECK_EQ(c.seller, id);
    CHECK_EQ(c.buyer, -1);                    // market sink
    CHECK_EQ((int)c.proto, 5);
    CHECK_EQ(c.qty, 10);

    // Unit price = trunc(Building_ComputeMarketPrice(5,100)):
    //   raw cat-23: 2.2 * (4*0.5*(80/2)*2.2) ... taken from the real oracle.
    double expect = sim::Building_ComputeMarketPrice(5, 100);
    CHECK_EQ(c.unitPrice, (i32)expect);
    CHECK(c.unitPrice > 0);
    CHECK_EQ(c.totalValue, (i64)c.unitPrice * 10);
}

// --- non-trades classify as not issued. -----------------------------------------
TEST(PlaySliceMarketUnit, ClassifyRejectsNonTrades) {
    i32 id = SeedMarketWorld(/*ware=*/5, /*base=*/80, /*divisor=*/2);

    MarketInteraction base;
    base.side = MarketSide::kSell; base.ware = 5; base.qty = 10;
    base.buildingId = id; base.buildingKind = 2; base.player = 0;

    // No side -> no command.
    { MarketInteraction mi = base; mi.side = MarketSide::kNone;
      CHECK(!ClassifyMarketInteraction(mi).issued); }
    // Not a contor (kind != 2) -> no command (RequestSellObjekt gate).
    { MarketInteraction mi = base; mi.buildingKind = 6;
      CHECK(!ClassifyMarketInteraction(mi).issued); }
    // Zero quantity -> no command.
    { MarketInteraction mi = base; mi.qty = 0;
      CHECK(!ClassifyMarketInteraction(mi).issued); }
}

// --- the apply: a SELL adds money, removes stock; a BUY does the inverse. --------
TEST(PlaySliceMarketUnit, ApplySellAndBuyMutateTreasuryAndStock) {
    i32 id = SeedMarketWorld(/*ware=*/5, /*base=*/80, /*divisor=*/2);
    SetMarketApplyHooks(nullptr);   // inert-default record mutation

    i64 t0 = ReadBuildingTreasury(id);
    i32 s0 = ReadBuildingStock(id);
    CHECK_EQ(t0, 0);
    CHECK_EQ(s0, 500);

    // SELL 10 of ware 5.
    MarketInteraction sell;
    sell.side = MarketSide::kSell; sell.ware = 5; sell.qty = 10;
    sell.buildingId = id; sell.buildingKind = 2; sell.player = 0;

    sim::CommandQueue q;
    q.Init(); q.set_standalone(true);
    MarketSliceResult rs = RunMarketSlice(sell, /*econSeed=*/0x1234, q);

    CHECK(rs.command.issued);
    CHECK(rs.enqueued);
    CHECK(rs.applied);
    // Treasury rose by unitPrice*qty; stock fell by qty.
    CHECK(rs.treasuryAfter > rs.treasuryBefore);
    CHECK_EQ(rs.treasuryAfter - rs.treasuryBefore, rs.command.totalValue);
    CHECK_EQ(rs.stockBefore - rs.stockAfter, 10);
    CHECK(rs.treasuryMoved());
    CHECK(rs.stockMoved());

    // Now a BUY 20 of ware 5 (re-seed for a clean slate).
    id = SeedMarketWorld(/*ware=*/5, /*base=*/80, /*divisor=*/2);
    MarketInteraction buy;
    buy.side = MarketSide::kBuy; buy.ware = 5; buy.qty = 20;
    buy.buildingId = id; buy.buildingKind = 2; buy.player = 0;

    sim::CommandQueue q2;
    q2.Init(); q2.set_standalone(true);
    MarketSliceResult rb = RunMarketSlice(buy, /*econSeed=*/0x1234, q2);

    CHECK(rb.command.issued);
    CHECK(rb.applied);
    // Treasury fell by unitPrice*qty; stock rose by qty (the buy inverse).
    CHECK(rb.treasuryAfter < rb.treasuryBefore);
    CHECK_EQ(rb.treasuryBefore - rb.treasuryAfter, rb.command.totalValue);
    CHECK_EQ(rb.stockAfter - rb.stockBefore, 20);
}

// --- the slice is deterministic across reruns + the day reprices the ware. ------
TEST(PlaySliceMarketUnit, SliceDeterministicAndDayRepricesWare) {
    auto runOnce = [](MarketSliceResult& out) {
        i32 id = SeedMarketWorld(/*ware=*/5, /*base=*/80, /*divisor=*/2);
        MarketInteraction mi;
        mi.side = MarketSide::kSell; mi.ware = 5; mi.qty = 10;
        mi.buildingId = id; mi.buildingKind = 2; mi.player = 0;
        sim::CommandQueue q;
        q.Init(); q.set_standalone(true);
        out = RunMarketSlice(mi, /*econSeed=*/0xBEEF, q);
    };

    MarketSliceResult a, b;
    runOnce(a);
    runOnce(b);

    // The trade mutated the world; the day mutated it again.
    CHECK(a.tradeChangedWorld());
    CHECK(a.dayChangedWorld());
    // The day repriced the ware (the folded scene-type cache moved).
    CHECK(a.priceAfter != a.priceBefore);
    CHECK(a.economyPasses > 0);

    // Byte-identical run on rerun (the determinism oracle).
    CHECK_EQ(a.hashBefore, b.hashBefore);
    CHECK_EQ(a.hashAfterCommand, b.hashAfterCommand);
    CHECK_EQ(a.hashAfterDay, b.hashAfterDay);
    CHECK_EQ(a.treasuryAfter, b.treasuryAfter);
    CHECK_EQ(a.stockAfter, b.stockAfter);
    CHECK_EQ(a.priceAfter, b.priceAfter);
}
