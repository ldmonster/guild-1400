// E2E for NpcAction8 — drive a small per-tick "action evaluation" flow across the
// scoring leaves the way VIBE_NpcAction_Dispatch would: each candidate action is
// evaluated, the dispatcher keeps the best-scoring code, and a search/economy leaf
// supplies the concrete action when the actor settles on one.
#include "sim/npcaction8.h"
#include "tests/framework/test.h"

#include <cstring>

using namespace guild;
using namespace guild::sim;

namespace {

// A scoring world: the score kernels emit a fixed magnitude so the dispatcher's
// pick is deterministic; the law fetch always selects the weighted kernel.
float g_scoreX = 0.0f, g_scoreY = 0.0f;
int e2eWeighted(float* x, float* y, const AiScoreCall&) { *x = g_scoreX; *y = g_scoreY; return 1; }
int e2eOwn(float* x, float* y, const AiScoreCall&) { *x = g_scoreX; *y = g_scoreY; return 1; }
int e2eLaw(int, void* out) { std::memset(out, 0, 32); return 0; }   // -> weighted

bool g_searchHit = false;
int e2eSecondary(u8, int, void*, void* res) {
    if (!g_searchHit) return 0;
    std::memset(res, 0x5A, kNpc8CoordBlockSize); return 1;
}
void e2eDemand(float* o) { for (int i = 0; i < 13; ++i) o[i] = 0.0f; o[3] = 1.0f; }
int e2eRank(int) { return 7; }

} // namespace

TEST(NpcAction8E2E, ScoreThenCommitToDoor) {
    NpcAction8Hooks h{};
    h.scoreWeighted = e2eWeighted; h.scoreOwn = e2eOwn; h.getLawRecord = e2eLaw;
    h.dispatchSecondarySearch = e2eSecondary; h.loadDemandSnapshot = e2eDemand;
    h.buildingRankWithinGroup = e2eRank;
    SetNpcAction8Hooks(&h);

    // --- Phase 1: evaluate three movement-ish candidates; the dispatcher picks the
    //     one with the highest X score. ---
    struct Cand { int code; float score; };
    Cand best{0, -1.0e30f};

    g_scoreX = 5.0f; g_scoreY = 0.0f;
    float x, y;
    int moveTo = NpcAction8_EvaluateMoveTo(&x, &y, 0, 0, 0);
    if (x > best.score) best = {moveTo ? 100 : 0, x};   // give MoveTo a synthetic code

    // A disabled guarded move should not displace the winner (sentinel score).
    int guarded = NpcAction8_EvaluateMoveGuarded(&x, &y, 0, /*disabled=*/true, 0, 0);
    CHECK_EQ(guarded, 0);
    CHECK(x < -1.0e29f);
    if (x > best.score) best = {0, x};   // no-op, sentinel loses

    // A tavern approach (rank 7 => own + scale (2,3)) scores higher.
    g_scoreX = 6.0f; g_scoreY = 6.0f;
    unsigned char tavern[512] = {0}; tavern[2] = 5;
    int tav = NpcAction8_EvaluateApproachTavern(&x, &y, tavern, 0, false, 0, 0);
    CHECK_EQ(tav, 1);
    CHECK_EQ(x, 12.0f);   // 6 * 2.0
    if (x > best.score) best = {tav ? 200 : 0, x};

    CHECK_EQ(best.code, 200);   // tavern (score 12) beats MoveTo (score 5)

    // --- Phase 2: the actor now wants to act on a door; the search dispatcher
    //     resolves a concrete request when a target exists. ---
    unsigned char req[24] = {0}, res[24] = {0};
    g_searchHit = false;
    CHECK_EQ(NpcAction8_EvaluateOpenDoorLarge(0, 0, 999, req, res), 0);  // no target yet

    g_searchHit = true;
    int act = NpcAction8_EvaluateOpenDoorLarge(0, 0, 999, req, res);
    CHECK_EQ(act, 43);                 // commit: open large door
    CHECK_EQ((int)req[0], 16);         // request marshalled
    int sizeParam; std::memcpy(&sizeParam, req + 4, 4);
    CHECK_EQ(sizeParam, 8);
    CHECK_EQ((int)res[0], 0x5A);       // result coords copied out

    SetNpcAction8Hooks(nullptr);
}

TEST(NpcAction8E2E, SellAfterAim) {
    // A turret/use action fires (aim scatter written), then the actor lists the
    // item for sale via the market-sell command — a cross-leaf flow.
    static float tx[4] = {1.0f, 0, 0, 0}, ty[4] = {0}, tz[4] = {0}, tw[4] = {0};
    NpcAction8_SetAimTables(tx, ty, tz, tw, /*stride=*/1);

    static u16 objWord = 0x77;
    static unsigned char speedRec[32] = {0};
    float speed = 4.0f; std::memcpy(speedRec + 12, &speed, 4);

    struct AimWorld {
        static const u16* find(int, int, int, int) { return &objWord; }
        static int inv(int) { return 1; }
        static int use(int, const u16*, int* dir, const void** rec) {
            *dir = 1; *rec = speedRec; return 1;
        }
    };

    bool sold = false; long long soldPrice = -1;
    static bool* s_sold; static long long* s_price;
    s_sold = &sold; s_price = &soldPrice;
    struct SellWorld {
        static double price(int, u8) { return 17.0; }
        static void cmd(i32, i32, int, int, u8, long long p) { *s_sold = true; *s_price = p; }
    };

    NpcAction8Hooks h{};
    h.gameObjectQueryFind = AimWorld::find;
    h.inventoryFindSlotByItemId = AimWorld::inv;
    h.itemUseObjectAction = AimWorld::use;
    h.lookupCachedMarketPrice = SellWorld::price;
    h.queueRequest17 = SellWorld::cmd;
    h.currencyByte = 1;
    SetNpcAction8Hooks(&h);

    NpcAction8_ResetAimAccumulators();
    i16 params[8] = {0, 1, 2, 0, 0, 0, 0, 0};   // row 0
    int aimCode = NpcAction8_AimTurretToward(1, 2, params);
    CHECK_EQ(aimCode, 36);
    AimAccumulators a = NpcAction8_GetAimAccumulators();
    CHECK_EQ(a.x, 4.0f);   // 1.0 * 4.0

    unsigned char actor[16] = {0}; *reinterpret_cast<i32*>(actor + 4) = 7;
    unsigned char target[16] = {0}; *reinterpret_cast<i32*>(target + 4) = 8;
    unsigned char desc[16] = {0}; desc[0] = 2;
    int dd = (3 << 16); std::memcpy(desc + 2, &dd, 4);
    int sellCode = NpcAction8_RequestSellObjekt(actor, desc, target);
    CHECK_EQ(sellCode, 12);
    CHECK(sold);
    CHECK_EQ(soldPrice, 17LL);

    SetNpcAction8Hooks(nullptr);
    NpcAction8_SetAimTables(nullptr, nullptr, nullptr, nullptr, 37);
}
