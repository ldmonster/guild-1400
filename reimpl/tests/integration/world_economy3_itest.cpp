#include "test.h"

// Integration: the election-candidate rating kernel (world_economy3) wired against
// a REAL reconstructed sibling — guild::world::MarketLookupCachedPrice (the market
// price-cache lookup, gilde.exe 0x58f6b8, world/market_price.cpp). In the live game
// a candidate office's "current output" feeds the rating tier; here we forward the
// world_economy3 buildingCurrentOutput hook into the real cached-price lookup so the
// rating id is computed from a genuine cross-module value (no mock), and we assert
// the kernel + real sibling agree on the tier boundaries.
#include "world/world_economy3.h"
#include "world/market_price.h"   // REAL sibling: MarketLookupCachedPrice

using namespace guild::world;

namespace {
// A small real cache: good 7 priced at 400 (-> tier 1603), good 9 at 80 (-> 1605),
// good 11 at 700 (-> 1602). Key high-word is the good id (key = goodId << 16).
MarketCacheEntry g_entries[] = {
    {7 << 16, 400.0f},
    {9 << 16, 80.0f},
    {11 << 16, 700.0f},
};
MarketPriceCache g_cache{g_entries, 3, 1};

// The good id the hook should price for the candidate under test.
guild::i16 g_goodForCandidate = 7;

// buildingCurrentOutput hook -> REAL MarketLookupCachedPrice (currency 0, no
// fallback). This is the genuine cross-module call the shell would make to value a
// candidate's office.
float OutputViaRealMarketLookup(const void* /*officeRecord*/) {
    return MarketLookupCachedPrice(g_cache, g_goodForCandidate, /*currency*/ 0,
                                   /*fallback*/ nullptr, nullptr);
}
}  // namespace

TEST(WorldEconomy3Integration, RatingFromRealMarketLookup) {
    WorldEconomy3Hooks h{};
    h.buildingCurrentOutput = &OutputViaRealMarketLookup;
    WorldEconomy3Hooks prev = WorldEconomy3SetHooks(h);

    // good 7 -> price 400 (real lookup) -> rating tier 1603.
    g_goodForCandidate = 7;
    CHECK_EQ(GuildCandidateRatingViaHook(nullptr), 1603);

    // good 9 -> price 80 -> tier 1605 (below 100).
    g_goodForCandidate = 9;
    CHECK_EQ(GuildCandidateRatingViaHook(nullptr), 1605);

    // good 11 -> price 700 -> tier 1602 (>= 650).
    g_goodForCandidate = 11;
    CHECK_EQ(GuildCandidateRatingViaHook(nullptr), 1602);

    // A good NOT in the cache, no fallback -> real lookup returns 0.0f -> tier 1605.
    g_goodForCandidate = 99;
    CHECK_EQ(GuildCandidateRatingViaHook(nullptr), 1605);

    WorldEconomy3SetHooks(prev);
}

// Sanity: the real sibling returns exactly the cached price we wired (i.e. the
// cross-module value the rating is keyed off is the genuine lookup result).
TEST(WorldEconomy3Integration, RealLookupReturnsCachedPrice) {
    CHECK_EQ(MarketLookupCachedPrice(g_cache, 7, 0, nullptr, nullptr), 400.0f);
    CHECK_EQ(MarketLookupCachedPrice(g_cache, 11, 0, nullptr, nullptr), 700.0f);
    // miss with no fallback -> 0.0f (the faithful default).
    CHECK_EQ(MarketLookupCachedPrice(g_cache, 99, 0, nullptr, nullptr), 0.0f);
}
