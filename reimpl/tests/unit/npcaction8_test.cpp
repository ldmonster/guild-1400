// Unit tests for NpcAction8 — AI candidate-scoring / action-evaluation leaves.
// Golden vectors computed with python (see the brief). A recording mock hooks
// struct lets each scored/searched path be exercised in isolation.
#include "sim/npcaction8.h"
#include "tests/framework/test.h"

#include <cstring>
#include <vector>

using namespace guild;
using namespace guild::sim;

// ---------------------------------------------------------------------------
// Recording mock for the cross-module leaves.
// ---------------------------------------------------------------------------
namespace {

struct Mock {
    // Log of which scorer ran and the values it wrote.
    int lawId = -1;
    bool lawUseDistance = false;
    int scoreReturn = 7;
    float writeX = 11.0f, writeY = 13.0f;
    int lastScorer = 0;           // 1=distance 2=weighted 3=own
    int lastKind = -99;
    bool searchSucceeds = false;
    unsigned char searchFillByte = 0xAB;
    int searchFlagSeen = -99;
    int lastSizeParam = -99;      // req[+4] the door variant marshalled
    int searchKind = 0;           // 0=primary 1=secondary
    float demand3 = 0.0f;
    int category = 0;
    int rank = 0;
    bool rivalFound = false;
    u16 rngReturn = 0;
    int shopWord = 0;
    long long sellPrice = 0;
    // sell command capture
    bool sellEmitted = false;
    i32 sellIdA = 0, sellIdB = 0;
    int sellCount = 0, sellKind = 0;
    u8 sellCurrency = 0;
    long long sellPriceSeen = 0;
    int priceLookups = 0;
};

Mock* g_m = nullptr;

int hkDistance(float* x, float* y, const AiScoreCall&) {
    g_m->lastScorer = 1; *x = g_m->writeX; *y = g_m->writeY; return g_m->scoreReturn;
}
int hkWeighted(float* x, float* y, const AiScoreCall& c) {
    g_m->lastScorer = 2; g_m->lastKind = c.kind; *x = g_m->writeX; *y = g_m->writeY; return g_m->scoreReturn;
}
int hkOwn(float* x, float* y, const AiScoreCall&) {
    g_m->lastScorer = 3; *x = g_m->writeX; *y = g_m->writeY; return 1;
}
int hkLaw(int lawId, void* out) {
    g_m->lawId = lawId; std::memset(out, 0, 32); return g_m->lawUseDistance ? 1 : 0;
}
int hkPrimary(u8 flag, int, void*, void* res) {
    g_m->searchFlagSeen = flag; g_m->searchKind = 0;
    if (!g_m->searchSucceeds) return 0;
    std::memset(res, g_m->searchFillByte, kNpc8CoordBlockSize); return 1;
}
int hkSecondary(u8 flag, int, void* req, void* res) {
    g_m->searchFlagSeen = flag; g_m->searchKind = 1;
    g_m->lastSizeParam = *reinterpret_cast<int*>(static_cast<unsigned char*>(req) + 4);
    if (!g_m->searchSucceeds) return 0;
    std::memset(res, g_m->searchFillByte, kNpc8CoordBlockSize); return 1;
}
void hkDemand(float* out13) { for (int i = 0; i < 13; ++i) out13[i] = 0.0f; out13[3] = g_m->demand3; }
int hkCat(const void*, int) { return g_m->category; }
int hkRank(int) { return g_m->rank; }
int hkRival(const void*, void* a, void* b) {
    if (!g_m->rivalFound) return 0;
    std::memset(a, 0x11, kNpc8CoordBlockSize); std::memset(b, 0x22, kNpc8CoordBlockSize); return 1;
}
u16 hkRng(u16) { return g_m->rngReturn; }
int hkShopWord() { return g_m->shopWord; }
double hkPrice(int, u8) { g_m->priceLookups++; return static_cast<double>(g_m->sellPrice); }
void hkSell(i32 a, i32 b, int count, int kind, u8 cur, long long price) {
    g_m->sellEmitted = true; g_m->sellIdA = a; g_m->sellIdB = b; g_m->sellCount = count;
    g_m->sellKind = kind; g_m->sellCurrency = cur; g_m->sellPriceSeen = price;
}

NpcAction8Hooks MakeHooks() {
    NpcAction8Hooks h{};
    h.scoreDistance = hkDistance; h.scoreWeighted = hkWeighted; h.scoreOwn = hkOwn;
    h.getLawRecord = hkLaw;
    h.dispatchPrimarySearch = hkPrimary; h.dispatchSecondarySearch = hkSecondary;
    h.loadDemandSnapshot = hkDemand;
    h.buildingCategoryForObject = hkCat; h.buildingRankWithinGroup = hkRank;
    h.findRivalToConfront = hkRival; h.randomModulo = hkRng; h.shopStateWord = hkShopWord;
    h.lookupCachedMarketPrice = hkPrice; h.queueRequest17 = hkSell;
    h.currencyByte = 2;
    return h;
}

struct HookGuard {
    Mock m; NpcAction8Hooks h;
    HookGuard() { h = MakeHooks(); g_m = &m; SetNpcAction8Hooks(&h); }
    ~HookGuard() { SetNpcAction8Hooks(nullptr); g_m = nullptr; }
};

} // namespace

// ---------------------------------------------------------------------------
// Deterministic cores.
// ---------------------------------------------------------------------------
TEST(NpcAction8Core, MarketGate) {
    CHECK(NpcAction8_MarketUseWeighted(10.0f, 3.0f) == true);   // 3.2 >= 3
    CHECK(NpcAction8_MarketUseWeighted(5.0f, 3.0f) == false);   // 1.2 >= 3
    CHECK(NpcAction8_MarketUseWeighted(7.0f, 2.0f) == true);    // 2.0 >= 2 (boundary)
}

TEST(NpcAction8Core, ScalePair) {
    float x = 4.0f, y = 5.0f;
    NpcAction8_ScalePair(&x, &y, 3.0f, 6.0f);
    CHECK_EQ(x, 12.0f);
    CHECK_EQ(y, 30.0f);
}

TEST(NpcAction8Core, Scatter) {
    CHECK_EQ(NpcAction8_ScatterForward(1.5f, 2.0f), 3.0f);
    CHECK_EQ(NpcAction8_ScatterReverse(1.5f, 2.0f), -1.5f);   // -(1.5*2*0.5)
}

// ---------------------------------------------------------------------------
// Score evaluators.
// ---------------------------------------------------------------------------
TEST(NpcAction8Score, MoveToUsesLaw18Weighted) {
    HookGuard g; g.m.lawUseDistance = false; g.m.scoreReturn = 9;
    float x = -1, y = -1;
    int r = NpcAction8_EvaluateMoveTo(&x, &y, 0, 100, 200);
    CHECK_EQ(g.m.lawId, 18);
    CHECK_EQ(g.m.lastScorer, 2);   // weighted
    CHECK_EQ(r, 9);
    CHECK_EQ(x, 11.0f);
}

TEST(NpcAction8Score, MoveToUsesDistanceWhenLawFlagSet) {
    HookGuard g; g.m.lawUseDistance = true; g.m.scoreReturn = 5;
    float x = 0, y = 0;
    int r = NpcAction8_EvaluateMoveTo(&x, &y, 1, 0, 0);
    CHECK_EQ(g.m.lastScorer, 1);   // distance
    CHECK_EQ(r, 5);
}

TEST(NpcAction8Score, GuardedMovesLawIdsAndDisable) {
    {
        HookGuard g; float x = 0, y = 0;
        NpcAction8_EvaluateMoveGuarded(&x, &y, 0, false, 0, 0);
        CHECK_EQ(g.m.lawId, 3);
    }
    {
        HookGuard g; float x = 0, y = 0;
        NpcAction8_EvaluateMoveToSecondary(&x, &y, 0, false, 0, 0);
        CHECK_EQ(g.m.lawId, 4);
    }
    {
        HookGuard g; float x = 0, y = 0;
        NpcAction8_EvaluateMoveToTertiary(&x, &y, 0, false, 0, 0);
        CHECK_EQ(g.m.lawId, 21);
    }
    {
        HookGuard g; float x = 0, y = 0;
        int r = NpcAction8_EvaluateMoveGuarded(&x, &y, 0, true, 0, 0);  // disabled
        CHECK_EQ(r, 0);
        CHECK(x < -1.0e29f);   // -1e30 sentinel
        CHECK_EQ(g.m.lawId, -1);  // law never fetched
    }
}

// ---------------------------------------------------------------------------
// Search evaluators.
// ---------------------------------------------------------------------------
TEST(NpcAction8Search, IdleStandPrimary) {
    HookGuard g; g.m.searchSucceeds = true;
    unsigned char req[24] = {0}, res[24] = {0};
    int r = NpcAction8_EvaluateIdleStand(0, 0, 1234, req, res);
    CHECK_EQ(r, 39);
    CHECK_EQ(g.m.searchKind, 0);
    CHECK_EQ((int)res[0], 0xAB);
}

TEST(NpcAction8Search, IdleStandShortCircuits) {
    HookGuard g; g.m.searchSucceeds = true;
    unsigned char req[24] = {0}, res[24] = {0};
    CHECK_EQ(NpcAction8_EvaluateIdleStand(5, 0, 1, req, res), 0);   // prevResult set
    CHECK_EQ(NpcAction8_EvaluateIdleStand(0, 1, 1, req, res), 0);   // relFlag set -> prev(0)
}

TEST(NpcAction8Search, CloseDoorSecondaryCode45) {
    HookGuard g; g.m.searchSucceeds = true;
    unsigned char req[24] = {0}, res[24] = {0};
    int r = NpcAction8_EvaluateCloseDoor(0, 0, 1, req, res);
    CHECK_EQ(r, 45);
    CHECK_EQ(g.m.searchKind, 1);
}

TEST(NpcAction8Search, OpenDoorLargeSmallParamsAndCodes) {
    {
        HookGuard g; g.m.searchSucceeds = true;
        unsigned char req[24] = {0}, res[24] = {0};
        int r = NpcAction8_EvaluateOpenDoorLarge(3, 0, 1, req, res);
        CHECK_EQ(r, 43);
        CHECK_EQ(g.m.lastSizeParam, 8);
        CHECK_EQ(g.m.searchFlagSeen, 3);   // prevResult passed as flag
        CHECK_EQ((int)req[0], 16);
    }
    {
        HookGuard g; g.m.searchSucceeds = true;
        unsigned char req[24] = {0}, res[24] = {0};
        int r = NpcAction8_EvaluateOpenDoorSmall(0, 0, 1, req, res);
        CHECK_EQ(r, 44);
        CHECK_EQ(g.m.lastSizeParam, 4);
    }
    {
        HookGuard g; unsigned char req[24] = {0}, res[24] = {0};
        CHECK_EQ(NpcAction8_EvaluateOpenDoorLarge(0, 1, 1, req, res), 0);  // relFlag
    }
    {
        HookGuard g; g.m.searchSucceeds = false;
        unsigned char req[24] = {0}, res[24] = {0};
        CHECK_EQ(NpcAction8_EvaluateOpenDoorSmall(0, 0, 1, req, res), 0);  // no hit
    }
}

// ---------------------------------------------------------------------------
// Economic approach evaluators.
// ---------------------------------------------------------------------------
static void SetRecF32(unsigned char* rec, int off, float v) { std::memcpy(rec + off, &v, 4); }

TEST(NpcAction8Approach, MarketGateBranches) {
    // record[+358]==15 required; price at +256.
    {
        HookGuard g; g.m.demand3 = 3.0f;
        unsigned char rec[512] = {0}; rec[358] = 15; SetRecF32(rec, 256, 10.0f);
        float x = 0, y = 0;
        g.m.scoreReturn = 8;
        int r = NpcAction8_EvaluateApproachMarket(&x, &y, rec, 0, 0, 0);
        CHECK_EQ(g.m.lastScorer, 2);   // weighted (3.2 >= 3)
        CHECK_EQ(r, 8);
    }
    {
        HookGuard g; g.m.demand3 = 3.0f;
        unsigned char rec[512] = {0}; rec[358] = 15; SetRecF32(rec, 256, 5.0f);
        float x = 0, y = 0;
        int r = NpcAction8_EvaluateApproachMarket(&x, &y, rec, 0, 0, 0);
        CHECK_EQ(g.m.lastScorer, 3);   // own (1.2 < 3)
        CHECK_EQ(r, 1);
        CHECK_EQ(x, 33.0f);            // 11 * 3.0
        CHECK_EQ(y, 39.0f);            // 13 * 3.0
    }
    {
        HookGuard g;
        unsigned char rec[512] = {0}; rec[358] = 7;   // not 15
        float x = 0, y = 0;
        int r = NpcAction8_EvaluateApproachMarket(&x, &y, rec, 0, 0, 0);
        CHECK_EQ(r, 0);
        CHECK(x < -1.0e29f);
    }
}

TEST(NpcAction8Approach, TavernRankGates) {
    // rec[2]==5, rec[361]==0, typeCode=rec[356].
    {
        HookGuard g; g.m.rank = 7;     // >=6 => own + scale (2,3)
        unsigned char rec[512] = {0}; rec[2] = 5;
        float x = 0, y = 0;
        int r = NpcAction8_EvaluateApproachTavern(&x, &y, rec, 0, false, 0, 0);
        CHECK_EQ(r, 1);
        CHECK_EQ(g.m.lastScorer, 3);
        CHECK_EQ(x, 22.0f);            // 11 * 2.0
        CHECK_EQ(y, 39.0f);            // 13 * 3.0
    }
    {
        HookGuard g; g.m.rank = 4; g.m.rngReturn = 0;   // 3<=rank<6, roll==0 => 0
        unsigned char rec[512] = {0}; rec[2] = 5;
        float x = 0, y = 0;
        int r = NpcAction8_EvaluateApproachTavern(&x, &y, rec, 0, false, 0, 0);
        CHECK_EQ(r, 0);
    }
    {
        HookGuard g; g.m.rank = 4; g.m.rngReturn = 1; g.m.scoreReturn = 6;  // roll!=0 => weighted
        unsigned char rec[512] = {0}; rec[2] = 5;
        float x = 0, y = 0;
        int r = NpcAction8_EvaluateApproachTavern(&x, &y, rec, 0, false, 0, 0);
        CHECK_EQ(r, 6);
        CHECK_EQ(g.m.lastScorer, 2);
    }
    {
        HookGuard g; g.m.rank = 2;     // rank<3 => 0
        unsigned char rec[512] = {0}; rec[2] = 5;
        float x = 0, y = 0;
        CHECK_EQ(NpcAction8_EvaluateApproachTavern(&x, &y, rec, 0, false, 0, 0), 0);
    }
    {
        HookGuard g; unsigned char rec[512] = {0}; rec[2] = 9;  // wrong type
        float x = 0, y = 0;
        CHECK_EQ(NpcAction8_EvaluateApproachTavern(&x, &y, rec, 0, false, 0, 0), 0);
    }
}

TEST(NpcAction8Approach, ShopCategoryRivalPath) {
    {
        HookGuard g; g.m.category = 5; g.m.rank = 3; g.m.rivalFound = true;
        unsigned char rec[512] = {0};
        unsigned char a[24] = {0}, b[24] = {0};
        float x = 0, y = 0;
        int r = NpcAction8_EvaluateApproachShop(&x, &y, rec, 0, false, a, b, 0, 0);
        CHECK_EQ(r, 1);
        CHECK_EQ(g.m.lastScorer, 3);
        CHECK_EQ(x, 66.0f);   // 11 * 6.0
        CHECK_EQ(y, 26.0f);   // 13 * 2.0
        CHECK_EQ((int)a[0], 0x11);
        CHECK_EQ((int)b[0], 0x22);
    }
    {
        HookGuard g; g.m.category = 5; g.m.rank = 3; g.m.rivalFound = false;  // rival path, none found
        unsigned char rec[512] = {0}, a[24] = {0}, b[24] = {0};
        float x = 0, y = 0;
        CHECK_EQ(NpcAction8_EvaluateApproachShop(&x, &y, rec, 0, false, a, b, 0, 0), 0);
    }
    {
        HookGuard g; g.m.category = 0; g.m.shopWord = 0x14; g.m.scoreReturn = 4;  // state-word path
        unsigned char rec[512] = {0}, a[24] = {0}, b[24] = {0};
        float x = 0, y = 0;
        int r = NpcAction8_EvaluateApproachShop(&x, &y, rec, 0, false, a, b, 0, 0);
        CHECK_EQ(r, 4);
        CHECK_EQ(g.m.lastScorer, 2);
    }
    {
        HookGuard g; g.m.category = 0; g.m.shopWord = 0x13;  // below threshold
        unsigned char rec[512] = {0}, a[24] = {0}, b[24] = {0};
        float x = 0, y = 0;
        CHECK_EQ(NpcAction8_EvaluateApproachShop(&x, &y, rec, 0, false, a, b, 0, 0), 0);
    }
    {
        HookGuard g; unsigned char rec[512] = {0}, a[24] = {0}, b[24] = {0};
        float x = 0, y = 0;
        CHECK_EQ(NpcAction8_EvaluateApproachShop(&x, &y, rec, 0, true, a, b, 0, 0), 0);  // disabled
    }
}

// ---------------------------------------------------------------------------
// AimTurretToward.
// ---------------------------------------------------------------------------
namespace {
const u16* g_obj = nullptr;
u16 g_objWord = 0x55;
int g_useDir = 1;
const void* g_speedRec = nullptr;
int g_useReturn = 1;
const u16* objFind(int, int, int, int) { g_obj = &g_objWord; return g_obj; }
int invFind(int) { return 1; }
int useAction(int, const u16*, int* dir, const void** rec) { *dir = g_useDir; *rec = g_speedRec; return g_useReturn; }
}

TEST(NpcAction8Aim, ForwardAndReverseScatter) {
    // 4 tables, stride 4 for the test; row index = aimParams[4].
    static float tx[8] = {0, 0, 1.0f, 0, 0, 0, 0, 0};   // row 0 col? stride 4 -> row0=tx[0]
    static float ty[8] = {0, 0, 0, 0, 0, 0, 0, 0};
    static float tz[8] = {0, 0, 0, 0, 0, 0, 0, 0};
    static float tw[8] = {0, 0, 0, 0, 0, 0, 0, 0};
    tx[0] = 2.0f;   // row 0
    NpcAction8_SetAimTables(tx, ty, tz, tw, /*stride=*/4);

    // speed record: float at +12.
    static unsigned char rec[32] = {0};
    float speed = 3.0f; std::memcpy(rec + 12, &speed, 4);
    g_speedRec = rec;

    NpcAction8Hooks h{};
    h.gameObjectQueryFind = objFind; h.inventoryFindSlotByItemId = invFind;
    h.itemUseObjectAction = useAction;
    SetNpcAction8Hooks(&h);

    i16 params[8] = {0, 10, 20, 0, /*row=*/0, 0, 0, 0};

    g_useDir = 1; g_useReturn = 1;
    NpcAction8_ResetAimAccumulators();
    int r = NpcAction8_AimTurretToward(1, 2, params);
    CHECK_EQ(r, 36);
    AimAccumulators a = NpcAction8_GetAimAccumulators();
    CHECK_EQ(a.x, 6.0f);   // 2.0 * 3.0

    g_useDir = -1;
    NpcAction8_ResetAimAccumulators();
    NpcAction8_AimTurretToward(1, 2, params);
    a = NpcAction8_GetAimAccumulators();
    CHECK_EQ(a.x, -3.0f);  // -(2*3*0.5)

    g_useReturn = 0;       // use fails -> 0, no scatter
    NpcAction8_ResetAimAccumulators();
    CHECK_EQ(NpcAction8_AimTurretToward(1, 2, params), 0);
    a = NpcAction8_GetAimAccumulators();
    CHECK_EQ(a.x, 0.0f);

    SetNpcAction8Hooks(nullptr);
    NpcAction8_SetAimTables(nullptr, nullptr, nullptr, nullptr, 37);
}

TEST(NpcAction8Aim, NoObjectReturnsZero) {
    SetNpcAction8Hooks(nullptr);   // inert: queryFind null -> 0
    i16 params[8] = {0};
    CHECK_EQ(NpcAction8_AimTurretToward(1, 2, params), 0);
}

// ---------------------------------------------------------------------------
// RequestSellObjekt.
// ---------------------------------------------------------------------------
TEST(NpcAction8Sell, EmitsCommand) {
    HookGuard g; g.m.sellPrice = 42;
    unsigned char actor[16] = {0}; *reinterpret_cast<i32*>(actor + 4) = 111;
    unsigned char target[16] = {0}; *reinterpret_cast<i32*>(target + 4) = 222;
    unsigned char desc[16] = {0};
    desc[0] = 2;
    int descDword = (7 << 16) | 3; std::memcpy(desc + 2, &descDword, 4);   // hiword=7
    int count = 9; std::memcpy(desc + 8, &count, 4);

    int r = NpcAction8_RequestSellObjekt(actor, desc, target);
    CHECK_EQ(r, 12);
    CHECK(g.m.sellEmitted);
    CHECK_EQ(g.m.sellIdA, 111);
    CHECK_EQ(g.m.sellIdB, 222);
    CHECK_EQ(g.m.sellCount, 9);
    CHECK_EQ(g.m.sellKind, 7);
    CHECK_EQ((int)g.m.sellCurrency, 2);
    CHECK_EQ(g.m.sellPriceSeen, 42LL);
    CHECK_EQ(g.m.priceLookups, 2);   // looked up twice, like the original
}

TEST(NpcAction8Sell, WrongDescriptorTypeReturnsZero) {
    HookGuard g;
    unsigned char actor[16] = {0}, target[16] = {0}, desc[16] = {0};
    desc[0] = 3;   // != 2
    CHECK_EQ(NpcAction8_RequestSellObjekt(actor, desc, target), 0);
    CHECK(!g.m.sellEmitted);
}
