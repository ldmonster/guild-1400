#include "test.h"

// Integration: drive the caravan cargo valuation/load core against the REAL
// market-price cache sibling (world/market_price.cpp, gilde.exe 0x58f6b8) — no
// mock price model. The caravan price hook forwards into MarketLookupCachedPrice
// exactly as the live wiring (VIBE_Building_LookupCachedMarketPrice) would.
#include "world/caravan_cargo.h"
#include "world/market_price.h" // real MarketLookupCachedPrice sibling

#include <cmath>
#include <vector>

using namespace guild;
using namespace guild::world;

namespace {
bool deq(double a, double b, double eps = 1e-4) { return std::fabs(a - b) <= eps; }

// A real single-currency cache: good 5 -> 12.0, good 8 -> 3.5, good 11 -> 7.0.
MarketCacheEntry g_entries[3];
MarketPriceCache MakeCache() {
    g_entries[0] = {5 << 16, 12.0f};
    g_entries[1] = {8 << 16, 3.5f};
    g_entries[2] = {11 << 16, 7.0f};
    MarketPriceCache c;
    c.entries = g_entries;
    c.entriesPerBlock = 3;
    c.blockCount = 1;
    return c;
}
MarketPriceCache g_cache;

// Caravan price hook -> real cache lookup (currency 0; no fallback => miss==0).
double RealCacheHook(i32 goodId, u8 /*ctx*/) {
    return static_cast<double>(MarketLookupCachedPrice(
        g_cache, static_cast<i16>(goodId), 0, nullptr, nullptr));
}
} // namespace

TEST(CaravanCargoItest, ValueThroughRealMarketCache_Market) {
    g_cache = MakeCache();
    CaravanSetPriceHook(&RealCacheHook);

    CaravanCargoTables t;
    CaravanInitSlotTables(t);
    // Place two real cargo lines into the (now-empty) sell grid + one buy line.
    t.sell[0].goodIdPacked = 5 << 16;
    t.sell[0].objectId = 100;
    t.sell[0].dataPtr = 4; // qty 4
    t.buy[0].goodIdPacked = 8 << 16;
    t.buy[0].objectId = 101;
    t.buy[0].dataPtr = 10; // qty 10

    // ownerIsMarket => unit = price*1.1.
    //   sell: 4 * (12.0*1.1) = 52.8
    //   buy : 10 * (3.5*1.1) = 38.5
    double v = CaravanComputeCargoValue(t, /*ownerIsMarket=*/true, 1.0f, false, 0, 1);
    CHECK(deq(v, 52.8 + 38.5));

    CaravanSetPriceHook(nullptr);
}

TEST(CaravanCargoItest, LoadCoreUsesRealCachePrices) {
    g_cache = MakeCache();
    CaravanSetPriceHook(&RealCacheHook);

    CaravanCargoTables t;
    CaravanInitSlotTables(t);
    t.sell[0].goodIdPacked = 11 << 16;
    t.sell[0].objectId = 200;
    t.sell[0].dataPtr = 99;
    t.sell[0].storageSlot = 1;

    std::vector<i32> sellFree(kCaravanSellSlots, 0);
    sellFree[0] = 5; // 5 units of free space at the destination
    std::vector<i32> buyFree(kCaravanBuySlots, 0);

    auto lines = CaravanLoadFromStorage(t, sellFree, buyFree, /*applyPricing=*/true,
                                        /*ownerIsMarket=*/false, /*priceMul=*/2.0f,
                                        0, 0, 1);
    CHECK_EQ(static_cast<int>(lines.size()), 1);
    CHECK_EQ(lines[0].goodId, 11);
    CHECK_EQ(lines[0].quantity, 5);
    CHECK_EQ(lines[0].toSlot, 1);
    // unit = price(11)*priceMul = 7.0 * 2.0 = 14.0 (not market).
    CHECK(deq(lines[0].unitPrice, 14.0));

    CaravanSetPriceHook(nullptr);
}

// Cache MISS path: a good not in the cache and no fallback resolves to price 0,
// so its cargo contributes nothing — verifies the real lookup's miss semantics.
TEST(CaravanCargoItest, CacheMissContributesZero) {
    g_cache = MakeCache();
    CaravanSetPriceHook(&RealCacheHook);

    CaravanCargoTables t;
    CaravanInitSlotTables(t);
    t.sell[0].goodIdPacked = 999 << 16; // not cached
    t.sell[0].objectId = 1;
    t.sell[0].dataPtr = 7;

    double v = CaravanComputeCargoValue(t, true, 1.0f, false, 0, 1);
    CHECK(deq(v, 0.0));

    CaravanSetPriceHook(nullptr);
}
