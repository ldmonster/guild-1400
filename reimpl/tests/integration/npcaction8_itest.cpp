#include "test.h"

// Integration: drive npcaction8's RequestSellObjekt against the REAL market-price
// cache sibling (world/market_price.cpp, gilde.exe 0x58f6b8) — no mock price model.
// RequestSellObjekt's NpcAction8Hooks.lookupCachedMarketPrice slot is the live
// VIBE_Building_LookupCachedMarketPrice; we forward it straight into the real
// MarketLookupCachedPrice, exactly as the game wires it, and assert the price the
// real cache returns is the price that reaches the emitted QueueRequest17 command.
#include "sim/npcaction8.h"
#include "world/market_price.h"   // REAL reconstructed sibling

#include <cmath>
#include <cstring>

using namespace guild;
using namespace guild::sim;

namespace {
bool deq(double a, double b, double eps = 1e-4) { return std::fabs(a - b) <= eps; }

// A real single-currency cache: good 7 -> 9.5, good 12 -> 4.0.
world::MarketCacheEntry g_entries[2];
world::MarketPriceCache g_cache;
void MakeCache() {
    g_entries[0] = {7 << 16, 9.5f};
    g_entries[1] = {12 << 16, 4.0f};
    g_cache.entries = g_entries;
    g_cache.entriesPerBlock = 2;
    g_cache.blockCount = 1;
}

// The price hook -> real cache lookup (currency 0; no fallback => miss == 0).
double RealMarketPriceHook(int itemHiword, u8 currency) {
    return static_cast<double>(world::MarketLookupCachedPrice(
        g_cache, static_cast<i16>(itemHiword), currency, nullptr, nullptr));
}

// Capture the QueueRequest17 the sell builder emits.
struct SellCapture {
    bool emitted = false;
    i32 idA = 0, idB = 0; int count = 0, kind = 0; u8 currency = 0; long long price = 0;
} g_cap;
void CaptureQueue17(i32 idA, i32 idB, int count, int kind, u8 currency, long long price) {
    g_cap = {true, idA, idB, count, kind, currency, price};
}

// Lay out the actor/target object records: RequestSellObjekt reads the i32 at +4.
struct ObjRec { i32 pad0; i32 id; };
} // namespace

// descriptor[0]==2 gate passes; itemHiword resolves a cached good; the price from
// the REAL cache lands in the emitted opcode-17 sell command.
TEST(NpcAction8Itest, SellPriceFromRealMarketCache) {
    MakeCache();
    g_cap = SellCapture{};

    NpcAction8Hooks h{};                // zero all slots
    h.lookupCachedMarketPrice = &RealMarketPriceHook;  // -> real sibling
    h.queueRequest17          = &CaptureQueue17;
    h.currencyByte            = 0;
    SetNpcAction8Hooks(&h);

    ObjRec actor{0, 0xA1}, target{0, 0xB2};
    // descriptor: [0]=2 (gate), dword@+2 high word = good id 7, dword@+8 = count.
    u8 desc[16] = {0};
    desc[0] = 2;
    i32 itemDword = 7 << 16;            // HIWORD == 7
    std::memcpy(desc + 2, &itemDword, 4);
    i32 count = 5;
    std::memcpy(desc + 8, &count, 4);

    int rc = NpcAction8_RequestSellObjekt(&actor, desc, &target);
    CHECK_EQ(rc, 12);                   // RequestSellObjekt always returns 12 on the sell path
    CHECK(g_cap.emitted);
    if (g_cap.emitted) {
        CHECK_EQ(g_cap.idA, 0xA1);
        CHECK_EQ(g_cap.idB, 0xB2);
        CHECK_EQ(g_cap.count, 5);
        CHECK_EQ(g_cap.kind, 7);        // itemHiword
        CHECK_EQ(static_cast<u8>(g_cap.currency), 0);
        // The real cache priced good 7 at 9.5 -> truncated to 9 in the long long.
        CHECK_EQ(g_cap.price, 9LL);
    }
    // Cross-check the hook against the real sibling directly.
    CHECK(deq(RealMarketPriceHook(7, 0), 9.5));
    CHECK(deq(RealMarketPriceHook(12, 0), 4.0));

    SetNpcAction8Hooks(nullptr);
}

// A good NOT in the real cache misses (no fallback) -> price 0 reaches the command,
// proving the real lookup's miss semantics flow through the sell builder.
TEST(NpcAction8Itest, SellCacheMissPricesZero) {
    MakeCache();
    g_cap = SellCapture{};

    NpcAction8Hooks h{};
    h.lookupCachedMarketPrice = &RealMarketPriceHook;
    h.queueRequest17          = &CaptureQueue17;
    h.currencyByte            = 0;
    SetNpcAction8Hooks(&h);

    ObjRec actor{0, 1}, target{0, 2};
    u8 desc[16] = {0};
    desc[0] = 2;
    i32 itemDword = 999 << 16;          // good 999 not cached
    std::memcpy(desc + 2, &itemDword, 4);

    int rc = NpcAction8_RequestSellObjekt(&actor, desc, &target);
    CHECK_EQ(rc, 12);
    CHECK(g_cap.emitted);
    if (g_cap.emitted) {
        CHECK_EQ(g_cap.kind, 999);
        CHECK_EQ(g_cap.price, 0LL);     // real cache miss, no fallback => 0
    }

    SetNpcAction8Hooks(nullptr);
}

// descriptor[0] != 2 short-circuits BEFORE any price lookup — no command emitted.
TEST(NpcAction8Itest, NonSellDescriptorEmitsNothing) {
    MakeCache();
    g_cap = SellCapture{};

    NpcAction8Hooks h{};
    h.lookupCachedMarketPrice = &RealMarketPriceHook;
    h.queueRequest17          = &CaptureQueue17;
    SetNpcAction8Hooks(&h);

    ObjRec actor{0, 1}, target{0, 2};
    u8 desc[16] = {0};
    desc[0] = 1;                        // not 2 -> reject
    int rc = NpcAction8_RequestSellObjekt(&actor, desc, &target);
    CHECK_EQ(rc, 0);
    CHECK(!g_cap.emitted);

    SetNpcAction8Hooks(nullptr);
}
