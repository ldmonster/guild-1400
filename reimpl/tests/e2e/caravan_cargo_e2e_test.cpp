#include "test.h"

// E2E: a whole caravan trade-transport flow across REAL world siblings —
//   init slot tables -> load cargo from storage -> value the cargo through the
//   real market-price cache -> charge the real cart cost rule.
// Guarded (GUILD_RUN_CARAVAN_E2E=1); the math is deterministic so the guarded
// run asserts the exact end-to-end totals.
#include "world/caravan_cargo.h"
#include "world/market_price.h"     // real cached-price lookup (0x58f6b8)
#include "world/tradetransport.h"   // real cart-cost rule (0x592220)

#include <cmath>
#include <cstdlib>
#include <vector>

using namespace guild;
using namespace guild::world;

namespace {
bool deq(double a, double b, double eps = 1e-3) { return std::fabs(a - b) <= eps; }

MarketCacheEntry g_e[3];
MarketPriceCache g_cache;
double CacheHook(i32 good, u8 /*ctx*/) {
    return static_cast<double>(
        MarketLookupCachedPrice(g_cache, static_cast<i16>(good), 0, nullptr, nullptr));
}
} // namespace

TEST(CaravanCargoE2E, FullTransportFlowGuarded) {
    if (!std::getenv("GUILD_RUN_CARAVAN_E2E")) {
        CHECK(true); // guarded skip
        return;
    }

    // Real price cache: good 5 -> 20.0, good 8 -> 100.0, good 11 -> 50.0.
    g_e[0] = {5 << 16, 20.0f};
    g_e[1] = {8 << 16, 100.0f};
    g_e[2] = {11 << 16, 50.0f};
    g_cache.entries = g_e;
    g_cache.entriesPerBlock = 3;
    g_cache.blockCount = 1;
    CaravanSetPriceHook(&CacheHook);

    // 1) Empty cargo grids.
    CaravanCargoTables t;
    CaravanInitSlotTables(t);
    CHECK_EQ(static_cast<int>(t.sell.size()), kCaravanSellSlots);

    // 2) Populate: two sell-grid goods, one buy-grid good, all with destinations.
    // CaravanSlot field order: {goodIdPacked, objectId, storageSlot, dataPtr}.
    t.sell[0] = {5 << 16, 10, 0, 4};  // good5, obj10, slot0, qty4
    t.sell[1] = {8 << 16, 11, 1, 2};  // good8, obj11, slot1, qty2
    t.buy[0] = {11 << 16, 12, 2, 6};  // good11, obj12, slot2, qty6

    // 3) Load from storage (free space fully available; not market, priceMul 1.0).
    std::vector<i32> sellFree(kCaravanSellSlots, 0);
    sellFree[0] = 4;
    sellFree[1] = 2;
    std::vector<i32> buyFree(kCaravanBuySlots, 0);
    buyFree[0] = 6;
    auto lines = CaravanLoadFromStorage(t, sellFree, buyFree, /*applyPricing=*/true,
                                        /*ownerIsMarket=*/false, /*priceMul=*/1.0f,
                                        0, 0, /*mode=*/1);
    CHECK_EQ(static_cast<int>(lines.size()), 3);
    // unit prices == cache prices (priceMul 1.0): 20, 100, 50.
    CHECK(deq(lines[0].unitPrice, 20.0));
    CHECK(deq(lines[1].unitPrice, 100.0));
    CHECK(deq(lines[2].unitPrice, 50.0));

    // 4) Value the loaded cargo (not market, priceMul 1.0):
    //    sell0 4*20 + sell1 2*100 + buy0 6*50 = 80 + 200 + 300 = 580.
    double value = CaravanComputeCargoValue(t, /*ownerIsMarket=*/false, 1.0f, false,
                                            0, /*mode=*/1);
    CHECK(deq(value, 580.0));

    // 5) Charge the real cart-cost rule for that cargo value (Fast mode = 0.15;
    //    value 580 < floor 32000 so it clamps up to 32000; fee = trunc(32000*0.15)
    //    = 4800; cost = trunc(4800 + 0.5) = 4800).
    i32 cost = TradeTransportComputeCartCost(static_cast<i32>(value),
                                             TransportMode::Fast);
    CHECK_EQ(cost, 4800);

    CaravanSetPriceHook(nullptr);
}
