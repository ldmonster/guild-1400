#include "test.h"

#include "world/market_price.h"

#include <vector>

using namespace guild;
using namespace guild::world;

namespace {
// The original keys an entry by storing the good id in the HIGH word of the key
// dword (it compares `key >> 16 == goodId`). Mirror that when seeding the cache.
MarketCacheEntry Entry(i16 goodId, float price) {
    MarketCacheEntry e;
    e.key = static_cast<i32>(goodId) << 16;
    e.price = price;
    return e;
}

int g_fallbackCalls = 0;
i16 g_lastFallbackGood = -1;
float Fallback(i16 goodId, void* ctx) {
    ++g_fallbackCalls;
    g_lastFallbackGood = goodId;
    if (ctx)
        *static_cast<int*>(ctx) += 1;
    return 7.5f;   // stand-in for ComputeMarketPrice(goodId, 100)
}
} // namespace

TEST(WorldMarketPrice, CacheHitReturnsStoredPrice) {
    // Two currency blocks of 62 entries each.
    std::vector<MarketCacheEntry> cacheData(2 * kMarketCacheEntries);
    // currency 0, slot 3 -> good 17 @ 12.25
    cacheData[3] = Entry(17, 12.25f);
    // currency 1, slot 0 -> good 17 @ 99.0 (different currency, different price)
    cacheData[kMarketCacheEntries + 0] = Entry(17, 99.0f);

    MarketPriceCache cache;
    cache.entries = cacheData.data();
    cache.entriesPerBlock = kMarketCacheEntries;
    cache.blockCount = 2;

    g_fallbackCalls = 0;
    CHECK_EQ(MarketLookupCachedPrice(cache, 17, 0, Fallback, nullptr), 12.25f);
    CHECK_EQ(MarketLookupCachedPrice(cache, 17, 1, Fallback, nullptr), 99.0f);
    CHECK_EQ(g_fallbackCalls, 0);   // both hit the cache
}

TEST(WorldMarketPrice, CacheMissCallsFallback) {
    std::vector<MarketCacheEntry> cacheData(kMarketCacheEntries);
    cacheData[5] = Entry(3, 4.0f);

    MarketPriceCache cache;
    cache.entries = cacheData.data();
    cache.entriesPerBlock = kMarketCacheEntries;
    cache.blockCount = 1;

    g_fallbackCalls = 0;
    int ctxCounter = 0;
    // good 99 is not cached -> fallback.
    float p = MarketLookupCachedPrice(cache, 99, 0, Fallback, &ctxCounter);
    CHECK_EQ(p, 7.5f);
    CHECK_EQ(g_fallbackCalls, 1);
    CHECK_EQ(g_lastFallbackGood, static_cast<i16>(99));
    CHECK_EQ(ctxCounter, 1);
}

TEST(WorldMarketPrice, OutOfRangeCurrencyFallsBack) {
    std::vector<MarketCacheEntry> cacheData(kMarketCacheEntries);
    cacheData[0] = Entry(1, 1.0f);
    MarketPriceCache cache;
    cache.entries = cacheData.data();
    cache.entriesPerBlock = kMarketCacheEntries;
    cache.blockCount = 1;

    g_fallbackCalls = 0;
    // currency 5 has no block -> fallback path.
    CHECK_EQ(MarketLookupCachedPrice(cache, 1, 5, Fallback, nullptr), 7.5f);
    CHECK_EQ(g_fallbackCalls, 1);
}

TEST(WorldMarketPrice, NoCacheNoFallbackReturnsZero) {
    MarketPriceCache cache;  // entries == nullptr
    CHECK_EQ(MarketLookupCachedPrice(cache, 1, 0, nullptr, nullptr), 0.0f);
}

TEST(WorldMarketPrice, FirstMatchWins) {
    std::vector<MarketCacheEntry> cacheData(kMarketCacheEntries);
    // Same good id in two slots: the linear scan returns the FIRST.
    cacheData[2] = Entry(8, 10.0f);
    cacheData[9] = Entry(8, 20.0f);
    MarketPriceCache cache;
    cache.entries = cacheData.data();
    cache.entriesPerBlock = kMarketCacheEntries;
    cache.blockCount = 1;
    CHECK_EQ(MarketLookupCachedPrice(cache, 8, 0, nullptr, nullptr), 10.0f);
}
