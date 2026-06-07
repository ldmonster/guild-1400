// End-to-end: a market panel prices goods. The cached-price lookup (0x58f6b8)
// serves hot goods from its per-currency cache and, on a miss, falls back to the
// full supply/recipe model (0x58f3d0); the resulting price is then rendered with
// thousands separators (0x58f798). This stitches the three market-mechanics
// functions into one flow and checks them against hand-computed references.
#include "test.h"

#include "world/market_price.h"
#include "world/market_price_model.h"
#include "world/money_format.h"

#include <cmath>
#include <string>
#include <vector>

using namespace guild;
using namespace guild::world;

namespace {
std::string Show(const std::string& s) {
    std::string out;
    for (char c : s)
        out += (c == kCurrencyGlyph) ? std::string("<G>") : std::string(1, c);
    return out;
}

// The cache miss fallback wraps the full model over a shared good table.
struct ModelCtx {
    GoodTable* table = nullptr;
    u8 currency = 0;
};
float ModelFallback(i16 goodId, void* ctx) {
    auto* c = static_cast<ModelCtx*>(ctx);
    return BuildingComputeMarketPrice(*c->table, goodId, c->currency);
}
} // namespace

TEST(WorldMarketPriceE2E, CacheHitMissAndFormat) {
    // Good ids are 1-based (id 0 == "none"; it collides with zeroed cache slots,
    // matching the original). Good 5 is a recipe over good 1 (a raw cat-23 good).
    std::vector<GoodRecord> goods(6);
    goods[1].category = 23; goods[1].value = 80; goods[1].divisor = 2;
    goods[5].category = 2; goods[5].value = 200; goods[5].divisor = 8;
    goods[5].components[0] = { 1, 3 };
    GoodTable table{ goods.data(), 6 };

    // ---- Cache: good 1 is hot in currency 0 at a fixed 12.0 -------------------
    std::vector<MarketCacheEntry> cacheData(kMarketCacheEntries);
    cacheData[4].key = static_cast<i32>(1) << 16;   // good id 1 in the high word
    cacheData[4].price = 12.0f;
    MarketPriceCache cache;
    cache.entries = cacheData.data();
    cache.entriesPerBlock = kMarketCacheEntries;
    cache.blockCount = 1;

    ModelCtx ctx{ &table, 100 };

    // good 1 is cached -> 12.0 (no model call).
    float p1 = MarketLookupCachedPrice(cache, 1, 0, ModelFallback, &ctx);
    CHECK_EQ(p1, 12.0f);

    // good 5 is NOT cached -> fall back to the full recipe model.
    //   good1 model price (cur 100) = 4*0.5*(80/2)*2.2 = 176.0
    //   good5 = 2.2*((50 + 176*3/8) * 100 * 0.01) = 255.2
    float p5 = MarketLookupCachedPrice(cache, 5, 0, ModelFallback, &ctx);
    CHECK(std::fabs(p5 - 255.2f) <= 1e-2f);

    // ---- Render a player's quoted total (rounded to display units) -----------
    // Quote ~250 units of good 5 at ~255 each ~= 63800; format with separators.
    i32 quote = 63800;
    CHECK_EQ(Show(MoneyFormatWithSeparators(quote)), "63.800<G>");

    // After the model priced good 5, its cache field was written for next time.
    CHECK_EQ(goods[5].cachedBase, 7);
}
