// tests/integration/play_slice_market_itest.cpp — INTEGRATION: the MARKET/TRADE
// slice over a small real-format world. A BUY then a SELL each route through the
// REAL sim::CommandQueue codec (the opcode-17 QueueRequest17 trade packet) and
// mutate a real folded record:
//   * treasury (object+77, the field ExComputeSellableAmount credits),
//   * stock (BuildingRec.fillLevel @+10),
//   * the market-price cache (g_sceneTypes[ware].cachedPrice, folded by HashFullWorld).
// Asserts HashFullWorld differs pre/post the trade AND the game-day, and the whole
// run is byte-identical on rerun (the determinism oracle).
#include "test.h"

#include "play/slice_market.h"
#include "play/world_digest.h"
#include "sim/building_production.h"
#include "sim/entity.h"
#include "sim/command.h"
#include "world/city.h"
#include "world/economy_tick.h"

#include <cstring>

using namespace guild;
using namespace guild::play;

namespace {

// Build a small real-format world: a few live buildings in g_objects, a priced
// scene-type catalog (real Building_ComputeMarketPrice inputs), the folded economy
// tables blanked so the run is reproducible in-process. Returns the trading
// building's id.
i32 SeedRealFormatWorld(i16 ware) {
    // Blank the folded world tables (the ZeroWorldGlobals determinism rule).
    std::memset(world::g_goods, 0, sizeof(world::g_goods));
    std::memset(world::g_cities, 0, sizeof(world::g_cities));
    world::g_capDivisor = 0.0f;
    world::g_cityTotalMoney = 0.0f;
    world::g_cityTotalGoods = 0.0f;
    world::SetSmoothedPriceLevel(0.0f);

    sim::ResetEntityArrays();
    std::memset(sim::g_objects, 0, sizeof(sim::g_objects));
    sim::g_sceneArrayLoaded = true;

    // Scene-type catalog: a couple of priced raw goods (category 23 -> simple branch).
    std::memset(sim::g_sceneTypes, 0, sizeof(sim::g_sceneTypes));
    std::memset(sim::g_sceneTypeRemap, 0, sizeof(sim::g_sceneTypeRemap));
    sim::g_sceneTypesLoaded = true;
    if (auto* td = sim::SceneTypeDefAt(ware)) {
        td->kind = 23; td->baseValue = 120; td->divisor = 4; td->cachedPrice = 0;
    }
    if (auto* td = sim::SceneTypeDefAt(ware + 1)) {
        td->kind = 23; td->baseValue = 64; td->divisor = 2; td->cachedPrice = 0;
    }

    // A handful of live buildings (real-format records).
    const i32 kTraderId = 9100;
    for (int i = 0; i < 4; ++i) {
        sim::ObjectRec& o = sim::g_objects[i];
        o.alive = 1;
        o.id    = 9100 + i;
        auto* b = reinterpret_cast<sim::BuildingRec*>(&o);
        b->fillLevel = (u16)(300 + 50 * i);
        i32 zero = 0;
        std::memcpy(reinterpret_cast<u8*>(&o) + kTreasuryFieldOff, &zero, sizeof zero);
    }
    return kTraderId;
}

// Run a BUY (qty 30) then a SELL (qty 50) of `ware` on the trader, returning the
// two slice results. The world stays live between the two (a real session would).
void RunBuyThenSell(i16 ware, u32 econSeed,
                    MarketSliceResult& buyOut, MarketSliceResult& sellOut) {
    i32 id = SeedRealFormatWorld(ware);

    MarketInteraction buy;
    buy.side = MarketSide::kBuy; buy.ware = ware; buy.qty = 30;
    buy.buildingId = id; buy.buildingKind = 2; buy.player = 0;
    sim::CommandQueue q1; q1.Init(); q1.set_standalone(true);
    buyOut = RunMarketSlice(buy, econSeed, q1);

    MarketInteraction sell;
    sell.side = MarketSide::kSell; sell.ware = ware; sell.qty = 50;
    sell.buildingId = id; sell.buildingKind = 2; sell.player = 0;
    sim::CommandQueue q2; q2.Init(); q2.set_standalone(true);
    sellOut = RunMarketSlice(sell, econSeed, q2);
}

} // namespace

// A buy then a sell each move treasury + stock + the folded price cache, and the
// hashes step at each trade and game-day.
TEST(PlaySliceMarketItest, BuyThenSellMutateFoldedRecordsAndHashSteps) {
    SetMarketApplyHooks(nullptr);

    MarketSliceResult buy, sell;
    RunBuyThenSell(/*ware=*/5, /*econSeed=*/0xC0FFEE, buy, sell);

    // --- BUY: stock up, treasury down, hashes step ---
    CHECK(buy.command.issued);
    CHECK_EQ((int)buy.command.opcode, 17);    // QueueRequest17 trade packet
    CHECK(buy.enqueued);
    CHECK(buy.applied);
    CHECK_EQ(buy.stockAfter - buy.stockBefore, 30);
    CHECK_EQ(buy.treasuryBefore - buy.treasuryAfter, buy.command.totalValue);
    CHECK(buy.tradeChangedWorld());           // the trade moved HashFullWorld
    CHECK(buy.dayChangedWorld());             // the game-day moved it again
    CHECK(buy.priceAfter != buy.priceBefore); // the folded price cache drifted

    // --- SELL: stock down, treasury up, hashes step ---
    CHECK(sell.command.issued);
    CHECK(sell.applied);
    CHECK_EQ(sell.stockBefore - sell.stockAfter, 50);
    CHECK_EQ(sell.treasuryAfter - sell.treasuryBefore, sell.command.totalValue);
    CHECK(sell.tradeChangedWorld());
    CHECK(sell.dayChangedWorld());

    // Both trades used the REAL price oracle (non-zero unit price).
    CHECK(buy.command.unitPrice > 0);
    CHECK(sell.command.unitPrice > 0);
}

// The full buy-then-sell run is byte-identical on rerun (determinism).
TEST(PlaySliceMarketItest, RunDeterministicAcrossReruns) {
    MarketSliceResult a1, a2, b1, b2;
    RunBuyThenSell(/*ware=*/5, /*econSeed=*/0x1357, a1, a2);
    RunBuyThenSell(/*ware=*/5, /*econSeed=*/0x1357, b1, b2);

    CHECK_EQ(a1.hashBefore, b1.hashBefore);
    CHECK_EQ(a1.hashAfterCommand, b1.hashAfterCommand);
    CHECK_EQ(a1.hashAfterDay, b1.hashAfterDay);
    CHECK_EQ(a2.hashBefore, b2.hashBefore);
    CHECK_EQ(a2.hashAfterCommand, b2.hashAfterCommand);
    CHECK_EQ(a2.hashAfterDay, b2.hashAfterDay);
    CHECK_EQ(a1.treasuryAfter, b1.treasuryAfter);
    CHECK_EQ(a2.treasuryAfter, b2.treasuryAfter);
    CHECK_EQ(a1.priceAfter, b1.priceAfter);
    CHECK_EQ(a2.priceAfter, b2.priceAfter);

    // A different seed produces a different world trajectory (the day diverges).
    MarketSliceResult c1, c2;
    RunBuyThenSell(/*ware=*/5, /*econSeed=*/0x2468, c1, c2);
    CHECK(c1.hashAfterDay != a1.hashAfterDay);
}
