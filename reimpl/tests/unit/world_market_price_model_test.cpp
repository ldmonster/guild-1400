#include "test.h"

#include "world/market_price_model.h"

#include <cmath>
#include <vector>

using namespace guild;
using namespace guild::world;

namespace {
bool Near(float a, float b, float eps = 1e-2f) {
    return std::fabs(a - b) <= eps;
}
GoodRecord Leaf(u8 cat, u32 value, u16 div, u8 typeIdx = 0,
                u8 need0 = 0, u8 need1 = 0) {
    GoodRecord r;
    r.category = cat;
    r.value = value;
    r.divisor = div;
    r.typeIndex = typeIdx;
    r.typeNeed0 = need0;
    r.typeNeed1 = need1;
    return r;
}
} // namespace

// Category-23 "raw" good with no type need: price = (4 * 0.5 * value/div) * 2.2.
// value=1000 div=10 -> 4*0.5*100*2.2 = 440.0 (exact in float).
TEST(WorldMarketPriceModel, Category23Leaf) {
    std::vector<GoodRecord> recs = { Leaf(23, 1000, 10) };
    GoodTable t{ recs.data(), 1 };
    float p = BuildingComputeMarketPrice(t, 0, 100);
    CHECK_EQ(p, 440.0f);
    // Category 23 does NOT write the cache.
    CHECK_EQ(recs[0].cachedBase, 0);
}

// Non-cached ordinary leaf (no components): computes base, writes cache, returns
// 2.2 * (v28 * currency * 0.01). value=500 div=5 typeIdx=0 cur=100:
//   v28 = 4*0.5*(500/5) = 200; return 2.2*(200*100*0.01) = 440.0;
//   cache = trunc(200 * 2.2 * 0.03125) = trunc(13.75) = 13.
TEST(WorldMarketPriceModel, OrdinaryLeafWritesCache) {
    std::vector<GoodRecord> recs = { Leaf(1, 500, 5) };
    GoodTable t{ recs.data(), 1 };
    float p = BuildingComputeMarketPrice(t, 0, 100);
    CHECK_EQ(p, 440.0f);
    CHECK_EQ(recs[0].cachedBase, 13);

    // Repricing now takes the cached branch: 32 * 13 * 100 * 0.01 = 416.0.
    float p2 = BuildingComputeMarketPrice(t, 0, 100);
    CHECK_EQ(p2, 416.0f);

    // flag == 3 applies the 1.5 multiplier on the cached branch: 416 * 1.5 = 624.
    recs[0].flag = 3;
    float p3 = BuildingComputeMarketPrice(t, 0, 100);
    CHECK_EQ(p3, 624.0f);
}

// Type-need accumulation: typeIdx != 0 sums (i16)need * 28*32/12/60 over two
// bytes. needs 6,3 -> v27 = (6+3)*28*32/12/60 = 9*896/720 = 11.2; value=120 div=4
// cur=50: v23=30, v28=11.2*0.5*30=168, return 2.2*(168*50*0.01)=184.8.
TEST(WorldMarketPriceModel, TypeNeedAccumulation) {
    std::vector<GoodRecord> recs = { Leaf(2, 120, 4, 7, 6, 3) };
    GoodTable t{ recs.data(), 1 };
    float p = BuildingComputeMarketPrice(t, 0, 50);
    CHECK(Near(p, 184.8f));
    // cache = trunc(168 * 2.2 * 0.03125) = trunc(11.55) = 11.
    CHECK_EQ(recs[0].cachedBase, 11);
}

// One-component recipe: good 0 has component good 1 (a cat-23 raw) qty 3.
//   good1 (cat23): 4*0.5*(80/2)*2.2 = 176.0
//   good0: v28 = 4*0.5*(200/8) = 50; comp = 176*3 = 528; v28 += 528/8 = 50+66 = 116
//          return 2.2*(116*100*0.01) = 255.2
TEST(WorldMarketPriceModel, ComponentRecipe) {
    std::vector<GoodRecord> recs(2);
    recs[0] = Leaf(2, 200, 8);
    recs[0].components[0] = { 1, 3 };
    recs[1] = Leaf(23, 80, 2);
    GoodTable t{ recs.data(), 2 };

    float p = BuildingComputeMarketPrice(t, 0, 100);
    CHECK(Near(p, 255.2f));
    CHECK_EQ(recs[0].cachedBase, 7);   // trunc(116 * 2.2 * 0.03125) = trunc(7.975)
}

TEST(WorldMarketPriceModel, OutOfRangeReturnsZero) {
    std::vector<GoodRecord> recs = { Leaf(1, 100, 1) };
    GoodTable t{ recs.data(), 1 };
    CHECK_EQ(BuildingComputeMarketPrice(t, 5, 0), 0.0f);
    CHECK_EQ(BuildingComputeMarketPrice(t, -1, 0), 0.0f);
    GoodTable empty{ nullptr, 0 };
    CHECK_EQ(BuildingComputeMarketPrice(empty, 0, 0), 0.0f);
}

// Component id 0xFFFF is a named ingredient with no price contribution (skipped).
TEST(WorldMarketPriceModel, NamedComponentSkipped) {
    std::vector<GoodRecord> recs(1);
    recs[0] = Leaf(2, 200, 8);
    recs[0].components[0] = { 0xFFFF, 0 };   // named, no price
    GoodTable t{ recs.data(), 1 };
    // No price contribution -> same as the bare leaf: v28 = 4*0.5*(200/8) = 50;
    // return 2.2*(50*100*0.01) = 110.0.
    float p = BuildingComputeMarketPrice(t, 0, 100);
    CHECK_EQ(p, 110.0f);
}
