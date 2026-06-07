// Unit tests for src/sim/npcaction9.cpp — the NpcAction9 evaluator family.
// Golden vectors for the pure cores were computed with python (float32-aware), and
// the evaluators are exercised with recording mock hooks installed via
// SetNpcAction9Hooks.

#include "sim/npcaction9.h"
#include "test.h"

#include <cstring>

using namespace guild;
using namespace guild::sim;

// ---------------------------------------------------------------------------
// Pure cores.
// ---------------------------------------------------------------------------
TEST(NpcAction9Core, TruncTowardZero) {
    CHECK_EQ(NpcAction9_TruncToInt(3.9), 3);
    CHECK_EQ(NpcAction9_TruncToInt(-3.9), -3);
    CHECK_EQ(NpcAction9_TruncToInt(0.0), 0);
}

TEST(NpcAction9Core, ShootTooExpensive) {
    // combined = (wt + ws) * 0.005; budget = (int)(cur * 0.22).
    CHECK(NpcAction9_ShootTooExpensive(100000, 50000, 2000));   // 750 > 440
    CHECK(!NpcAction9_ShootTooExpensive(10000, 10000, 5000));   // 100 < 1100
    CHECK(!NpcAction9_ShootTooExpensive(0, 0, 0));              // 0 > 0 == false
}

TEST(NpcAction9Core, ArrestBounty) {
    CHECK_EQ(NpcAction9_ArrestBounty(10000), 160);   // 100 <= 160 floor
    CHECK_EQ(NpcAction9_ArrestBounty(50000), 499);   // (int)(50000*0.01f-as-double)
    CHECK_EQ(NpcAction9_ArrestBounty(16001), 160);   // 160.01 -> still floored? no: >160
    CHECK_EQ(NpcAction9_ArrestBounty(100000), 999);
}

TEST(NpcAction9Core, RivalBounty) {
    CHECK_EQ(NpcAction9_RivalBounty(1000, 150.0f, 1), 3200);        // lower clamp
    CHECK_EQ(NpcAction9_RivalBounty(100000000, 0.0f, 100), 320);    // upper clamp
    CHECK_EQ(NpcAction9_RivalBounty(5000000, 0.0f, 50), 24999);     // mid range
}

TEST(NpcAction9Core, LoyaltyRoll) {
    CHECK(NpcAction9_LoyaltyRollPasses(20, 10));    // 20 < 47
    CHECK(!NpcAction9_LoyaltyRollPasses(60, 10));   // 60 < 47 false
}

TEST(NpcAction9Core, StrideTableFaithful) {
    const u32 expect[16] = {1,3,5,7,11,13,17,19,237,239,243,245,249,251,253,255};
    for (int i = 0; i < 16; ++i) CHECK_EQ(kSocializeStrideTable[i], expect[i]);
}

// ---------------------------------------------------------------------------
// Mock-hook scaffolding.
// ---------------------------------------------------------------------------
namespace {

// Recording state for the mocks (file-static; each test executable owns its own).
struct MockState {
    int currency = 0;
    int wealth = 0;
    int selectResult = 0;        // SelectBestRecursive return
    int selectCalls = 0;
    int rankResult = 0;
    int findNearestHit = 0;      // findNearestEntity returns this; writes id
    int nearestId = 0;
    void* resolved = nullptr;
    int countMatch = 0;
    int matchId = 0;
    int amtRank = 0;
    int opponentHit = 0;
    int rivalHit = 0;
    u16 rngValue = 0;
    float rngFloat = 0.0f;
    const u16* objFind = nullptr;
};
MockState g_ms;

int m_currency(int, u8) { return g_ms.currency; }
int m_wealth(int, const void*) { return g_ms.wealth; }
int m_select(int, u16, void*, int, void*) { g_ms.selectCalls++; return g_ms.selectResult; }
int m_rank(int) { return g_ms.rankResult; }
int m_findNearest(const void*, int, void*, float, float, int* outId) {
    if (outId) *outId = g_ms.nearestId;
    return g_ms.findNearestHit;
}
void m_resolve(void** out, int, int, int) { *out = g_ms.resolved; }
int m_count(void* outDesc, int* outId, const void*) {
    if (outDesc) std::memset(outDesc, 0, 8);
    if (outId) *outId = g_ms.matchId;
    return g_ms.countMatch;
}
int m_amt(const void*) { return g_ms.amtRank; }
int m_opponent(const void*, void* a, void* b) {
    if (g_ms.opponentHit) { std::memset(a, 0, kNpc9ReqBlockSize); std::memset(b, 0, kNpc9ReqBlockSize); }
    return g_ms.opponentHit;
}
int m_rival(const void*, void* a, void* b) {
    if (g_ms.rivalHit) { std::memset(a, 0, kNpc9ReqBlockSize); std::memset(b, 0, kNpc9ReqBlockSize); }
    return g_ms.rivalHit;
}
u16 m_rng(u16) { return g_ms.rngValue; }
float m_rngf() { return g_ms.rngFloat; }
const u16* m_objFind(int, int, int, int, int) { return g_ms.objFind; }
int m_catForObj(int) { return 7; }

NpcAction9Hooks MakeHooks() {
    NpcAction9Hooks h{};
    h.personCurrencyAmount = m_currency;
    h.personTotalWealth = m_wealth;
    h.selectBestRecursive = m_select;
    h.buildingRankWithinGroup = m_rank;
    h.findNearestEntity = m_findNearest;
    h.resolveEntityById = m_resolve;
    h.countInventoryMatch = m_count;
    h.amtCheckGuildRankLevel2 = m_amt;
    h.findOpponentBuilding = m_opponent;
    h.findRivalToConfront = m_rival;
    h.randomModulo = m_rng;
    h.randomFloatScaled = m_rngf;
    h.gameObjectQueryFind = m_objFind;
    h.buildingCategoryForObject = m_catForObj;
    h.currencyByte = 0;
    return h;
}

// A 600-byte zeroed person record (covers every +offset the evaluators read).
struct Rec { u8 b[600]; Rec() { std::memset(b, 0, sizeof(b)); } };

} // namespace

TEST(NpcAction9Eval, ShootBailsOnRelFlag) {
    NpcAction9Hooks h = MakeHooks();
    SetNpcAction9Hooks(&h);
    Rec rec; u8 a[kNpc9ReqBlockSize] = {0}, b[kNpc9ReqBlockSize] = {0};
    CHECK_EQ(NpcAction9_EvaluateShoot(reinterpret_cast<const u16*>(rec.b), 1, a, b), 0);
    SetNpcAction9Hooks(nullptr);
}

TEST(NpcAction9Eval, ShootNoMatchReturnsZero) {
    // No findMatchingColors hook installed -> early 0.
    NpcAction9Hooks h = MakeHooks();
    SetNpcAction9Hooks(&h);
    Rec rec; u8 a[kNpc9ReqBlockSize] = {0}, b[kNpc9ReqBlockSize] = {0};
    CHECK_EQ(NpcAction9_EvaluateShoot(reinterpret_cast<const u16*>(rec.b), 0, a, b), 0);
    SetNpcAction9Hooks(nullptr);
}

TEST(NpcAction9Eval, ThrowGatesOnFlagAndDescriptor) {
    NpcAction9Hooks h = MakeHooks();
    SetNpcAction9Hooks(&h);
    u8 desc[24] = {0}; u8 a[kNpc9ReqBlockSize] = {0};
    desc[0] = 2;
    CHECK_EQ(NpcAction9_EvaluateThrow(desc, 1, 0, a), 0);  // flag != 2
    CHECK_EQ(NpcAction9_EvaluateThrow(desc, 2, 0, a), 0);  // no work slot
    desc[0] = 5;
    CHECK_EQ(NpcAction9_EvaluateThrow(desc, 2, 0, a), 0);  // descriptor[0] != 2
    SetNpcAction9Hooks(nullptr);
}

TEST(NpcAction9Eval, RecruitWorkerGates) {
    NpcAction9Hooks h = MakeHooks();
    SetNpcAction9Hooks(&h);
    Rec rec; u8 a[kNpc9ReqBlockSize] = {0}, b[kNpc9ReqBlockSize] = {0};
    // record[+358] != 15 -> 0
    CHECK_EQ(NpcAction9_EvaluateRecruitWorker(0, reinterpret_cast<const u16*>(rec.b), a, 0, b), 0);
    rec.b[358] = 15;
    // relFlag set -> 0
    CHECK_EQ(NpcAction9_EvaluateRecruitWorker(0, reinterpret_cast<const u16*>(rec.b), a, 1, b), 0);
    // a3[0] non-zero -> 0
    a[0] = 1;
    CHECK_EQ(NpcAction9_EvaluateRecruitWorker(0, reinterpret_cast<const u16*>(rec.b), a, 0, b), 0);
    a[0] = 0;
    // no nearest entity -> 0
    g_ms.findNearestHit = 0;
    CHECK_EQ(NpcAction9_EvaluateRecruitWorker(0, reinterpret_cast<const u16*>(rec.b), a, 0, b), 0);
    SetNpcAction9Hooks(nullptr);
}

TEST(NpcAction9Eval, RecruitWorkerSelectsCandidate) {
    g_ms = MockState{};
    NpcAction9Hooks h = MakeHooks();
    SetNpcAction9Hooks(&h);
    Rec rec; rec.b[358] = 15;
    static Rec target;            // resolved person record (loyalty top byte = 0)
    g_ms.findNearestHit = 1;
    g_ms.nearestId = 42;
    g_ms.resolved = target.b;
    g_ms.rngValue = 0;            // RandomModulo -> 0, so loyalty 0 < 37 passes
    g_ms.selectResult = 49;       // SelectBestRecursive(15) returns the code
    u8 a[kNpc9ReqBlockSize] = {0}, b[kNpc9ReqBlockSize] = {0};
    int r = NpcAction9_EvaluateRecruitWorker(0, reinterpret_cast<const u16*>(rec.b), a, 0, b);
    CHECK_EQ(r, 49);
    CHECK_EQ(a[0], 4);            // reqA kind byte
    SetNpcAction9Hooks(nullptr);
}

TEST(NpcAction9Eval, ArrestGuildRankGate) {
    g_ms = MockState{};
    NpcAction9Hooks h = MakeHooks();
    SetNpcAction9Hooks(&h);
    Rec rec; u8 a[kNpc9ReqBlockSize] = {0};
    g_ms.amtRank = 0;             // != 1 -> 0
    CHECK_EQ(NpcAction9_EvaluateArrest(0, a, 0, reinterpret_cast<const i16*>(rec.b)), 0);
    g_ms.amtRank = 1;
    g_ms.wealth = 100000;         // bounty 999
    g_ms.currency = 100000;       // budget (int)(100000*0.22)=22000 >= 999 ok
    g_ms.findNearestHit = 0;      // no target -> 0
    CHECK_EQ(NpcAction9_EvaluateArrest(0, a, 0, reinterpret_cast<const i16*>(rec.b)), 0);
    SetNpcAction9Hooks(nullptr);
}

TEST(NpcAction9Eval, ShopInteractOpponentFallback) {
    g_ms = MockState{};
    NpcAction9Hooks h = MakeHooks();
    SetNpcAction9Hooks(&h);
    Rec rec; u8 a[kNpc9ReqBlockSize] = {0}, b[1] = {0};
    g_ms.rankResult = 0;          // cat present (7) but rank <= 2 -> LABEL_11
    g_ms.opponentHit = 1;
    CHECK_EQ(NpcAction9_EvaluateShopInteract(0, a, 0, reinterpret_cast<const u16*>(rec.b), b), 50);
    g_ms.opponentHit = 0;
    CHECK_EQ(NpcAction9_EvaluateShopInteract(0, a, 0, reinterpret_cast<const u16*>(rec.b), b), 0);
    // relFlag set -> 0
    CHECK_EQ(NpcAction9_EvaluateShopInteract(0, a, 1, reinterpret_cast<const u16*>(rec.b), b), 0);
    SetNpcAction9Hooks(nullptr);
}

TEST(NpcAction9Eval, ShopInteractRivalBranch) {
    g_ms = MockState{};
    NpcAction9Hooks h = MakeHooks();
    SetNpcAction9Hooks(&h);
    Rec rec; u8 a[kNpc9ReqBlockSize] = {0}, b[1] = {0};
    g_ms.rankResult = 5;          // rank > 2 -> rival branch
    g_ms.rivalHit = 1;
    CHECK_EQ(NpcAction9_EvaluateShopInteract(0, a, 0, reinterpret_cast<const u16*>(rec.b), b), 50);
    SetNpcAction9Hooks(nullptr);
}

TEST(NpcAction9Eval, EnterBuildingBailsOnFlag) {
    g_ms = MockState{};
    NpcAction9Hooks h = MakeHooks();
    SetNpcAction9Hooks(&h);
    Rec rec; u8 a[kNpc9ReqBlockSize] = {0}, b[kNpc9ReqBlockSize] = {0};
    CHECK_EQ(NpcAction9_EvaluateEnterBuilding(reinterpret_cast<const u16*>(rec.b), a, 1, b), 0);
    SetNpcAction9Hooks(nullptr);
}

TEST(NpcAction9Eval, EnterBuildingNoBuildingSelectsBest) {
    g_ms = MockState{};
    NpcAction9Hooks h = MakeHooks();
    SetNpcAction9Hooks(&h);
    Rec rec;  // activeBuilding(+368)=0, typeByte(+2)=0 -> first branch
    g_ms.selectResult = 40;
    u8 a[kNpc9ReqBlockSize] = {0}, b[kNpc9ReqBlockSize] = {0};
    CHECK_EQ(NpcAction9_EvaluateEnterBuilding(reinterpret_cast<const u16*>(rec.b), a, 0, b), 40);
    CHECK_EQ(a[0], 5);   // reqA kind byte from the first branch
    SetNpcAction9Hooks(nullptr);
}

TEST(NpcAction9Eval, ThrowAtRivalClockGate) {
    g_ms = MockState{};
    NpcAction9Hooks h = MakeHooks();
    h.clockLow = 0; h.clockWord2 = 0;
    SetNpcAction9Hooks(&h);
    Rec rec; u8 a[kNpc9ReqBlockSize] = {0}; u8 sub = 0;
    // record[+4] = 1 -> (1&3)=1 != clockLow%4=0 -> bail
    rec.b[4] = 1;
    CHECK_EQ(NpcAction9_EvaluateThrowAtRival(reinterpret_cast<const u16*>(rec.b), a, 0, 0, &sub), 0);
    // relFlag set -> 0
    CHECK_EQ(NpcAction9_EvaluateThrowAtRival(reinterpret_cast<const u16*>(rec.b), a, 1, 0, &sub), 0);
    SetNpcAction9Hooks(nullptr);
}

TEST(NpcAction9Eval, InertDefaultsAllReturnZero) {
    // With no hooks installed, every evaluator takes a "not applicable" exit (0)
    // except where a gate inspects a record field; verify the common ones.
    SetNpcAction9Hooks(nullptr);
    Rec rec; u8 a[kNpc9ReqBlockSize] = {0}, b[kNpc9ReqBlockSize] = {0}; u8 sub = 0;
    CHECK_EQ(NpcAction9_EvaluateShoot(reinterpret_cast<const u16*>(rec.b), 0, a, b), 0);
    CHECK_EQ(NpcAction9_EvaluateHirePersonnel(0, reinterpret_cast<const u16*>(rec.b), a, 0, b), 0);
    CHECK_EQ(NpcAction9_EvaluateBribeJailed(0, reinterpret_cast<const u16*>(rec.b), a, 0, b), 0);
    CHECK_EQ(NpcAction9_EvaluateSocializeGroup(0, reinterpret_cast<const u16*>(rec.b), a, 0, b), 0);
    CHECK_EQ(NpcAction9_EvaluateUseItemOnTarget(0, a, &sub), 0);
}
