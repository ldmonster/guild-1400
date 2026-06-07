// Unit tests for ai/desire_table (LookupAttributeIndex, ComputeWeights, the
// intrigue NpcAction "perform" wrappers). Golden vectors computed with python3.
#include "test.h"

#include "ai/desire_table.h"

using namespace guild;
using namespace guild::ai;

// --- LookupAttributeIndex ---------------------------------------------------

TEST(DesireTable, LookupAttributeAllNames) {
    CHECK_EQ(LookupAttributeIndex("APS"), 0);
    CHECK_EQ(LookupAttributeIndex("UNVERSEHRTHEIT"), 1);
    CHECK_EQ(LookupAttributeIndex("WOHNUNG"), 2);
    CHECK_EQ(LookupAttributeIndex("GELD"), 3);
    CHECK_EQ(LookupAttributeIndex("BERUF"), 4);
    CHECK_EQ(LookupAttributeIndex("VERGNUEGEN"), 5);
    CHECK_EQ(LookupAttributeIndex("ANSEHEN"), 6);
    CHECK_EQ(LookupAttributeIndex("AMT"), 7);
    CHECK_EQ(LookupAttributeIndex("BILDUNG"), 8);
    CHECK_EQ(LookupAttributeIndex("RECHTSCHAFFENHEIT"), 9);
    CHECK_EQ(LookupAttributeIndex("GEMEINHEIT"), 10);
    CHECK_EQ(LookupAttributeIndex("SICHERHEIT"), 11);
    CHECK_EQ(LookupAttributeIndex("FORTPFLANZUNG"), 12);
    CHECK_EQ(LookupAttributeIndex("TRAEGHEIT"), 13);
}

TEST(DesireTable, LookupAttributeCaseInsensitive) {
    CHECK_EQ(LookupAttributeIndex("geld"), 3);
    CHECK_EQ(LookupAttributeIndex("Geld"), 3);
    CHECK_EQ(LookupAttributeIndex("vErGnUeGeN"), 5);
}

TEST(DesireTable, LookupAttributeMiss) {
    CHECK_EQ(LookupAttributeIndex("NOTAREALNEED"), -1);
    CHECK_EQ(LookupAttributeIndex(""), -1);
    // Prefix of a real name must NOT match (full-string compare).
    CHECK_EQ(LookupAttributeIndex("GEL"), -1);
    CHECK_EQ(LookupAttributeIndex("GELDX"), -1);
}

// --- ComputeWeights ---------------------------------------------------------

namespace {
// Deterministic price env matching the python golden-vector computation:
//   cached(g) = g % 50 + 10 ;  base(g) = g % 30 + 5
struct GoldenPriceEnv : DesirePriceEnv {
    double CachedMarketPrice(u16 g) override { return (g % 50) + 10; }
    double BaseMarketPrice(u16 g) override { return (g % 30) + 5; }
};
} // namespace

TEST(DesireTable, ComputeWeightsUnknownCategory) {
    GoldenPriceEnv env;
    DesirePlan plan;
    plan.categoryId = 0xFE; // not in the table
    CHECK_EQ(ComputeWeights(plan, env), 0);
    CHECK_EQ(plan.goodCount, 0);
}

TEST(DesireTable, ComputeWeightsEmptyRow) {
    // Category 0x1f (row 21) has exactly one good (0x1cd); category 0x00 (row 22)
    // has none -> success requires count>0, so 0x00 returns 0.
    GoldenPriceEnv env;
    DesirePlan plan;
    plan.categoryId = 0x00;
    CHECK_EQ(ComputeWeights(plan, env), 0);
    CHECK_EQ(plan.goodCount, 0);
}

TEST(DesireTable, ComputeWeightsCategory2a) {
    // Row 1 (cat 0x2a): nonzero goods {0x1d4,0x155,0x156,0x158,0x157}, count 5.
    GoldenPriceEnv env;
    DesirePlan plan;
    plan.categoryId = 0x2a;
    CHECK_EQ(ComputeWeights(plan, env), 1);
    CHECK_EQ(plan.goodCount, 5);
    CHECK_EQ(plan.goodIds[0], 0x1d4);
    CHECK_EQ(plan.goodIds[1], 0x155);
    CHECK_EQ(plan.goodIds[2], 0x156);
    CHECK_EQ(plan.goodIds[3], 0x158);
    CHECK_EQ(plan.goodIds[4], 0x157);

    // Golden: cached={28,51,52,54,53}, base={23,16,17,19,18}.
    CHECK_EQ(plan.cachedPrice[0], 28);
    CHECK_EQ(plan.basePrice[0], 23);
    CHECK_EQ(plan.cachedPrice[1], 51);
    CHECK_EQ(plan.basePrice[1], 16);

    // Golden: max-ratio slot 1 (3.1875), min-ratio slot 0 (1.2174), avg ~2.65005.
    CHECK_EQ(plan.maxRatioSlot, 1);
    CHECK_EQ(plan.minRatioSlot, 0);
    CHECK(plan.avgRatio > 2.650f && plan.avgRatio < 2.651f);
    CHECK(plan.ratio[1] > 3.187f && plan.ratio[1] < 3.188f);
    CHECK(plan.ratio[0] > 1.217f && plan.ratio[0] < 1.218f);
}

TEST(DesireTable, ComputeWeightsSingleGood) {
    // Row 21 (cat 0x1f): single good 0x1cd. min==max==slot 0, avg==ratio[0].
    GoldenPriceEnv env;
    DesirePlan plan;
    plan.categoryId = 0x1f;
    CHECK_EQ(ComputeWeights(plan, env), 1);
    CHECK_EQ(plan.goodCount, 1);
    CHECK_EQ(plan.goodIds[0], 0x1cd);
    CHECK_EQ(plan.maxRatioSlot, 0);
    CHECK_EQ(plan.minRatioSlot, 0);
    CHECK(plan.avgRatio == plan.ratio[0]);
}

// --- Intrigue NpcAction wrappers --------------------------------------------

TEST(DesireTable, PerformWrappersAccept) {
    CHECK_EQ(PerformEnterBuilding(true, 99), 40u);
    CHECK_EQ(PerformOpenDoorLarge(true, 99), 43u);
    CHECK_EQ(PerformOpenDoorSmall(true, 99), 44u);
}

TEST(DesireTable, PerformWrappersReject) {
    // On reject the wrapper returns the supplied reject code verbatim.
    CHECK_EQ(PerformEnterBuilding(false, 7), 7u);
    CHECK_EQ(PerformOpenDoorLarge(false, 8), 8u);
    CHECK_EQ(PerformOpenDoorSmall(false, 9), 9u);
}

namespace {
int g_useBackCalls = 0;
i32 g_useBackTarget = -1;
void UseBackCb(i32 id) { ++g_useBackCalls; g_useBackTarget = id; }
} // namespace

TEST(DesireTable, PerformUseBackEmitsCommandOnAccept) {
    g_useBackCalls = 0;
    g_useBackTarget = -1;
    SetUseBackCmdHook(&UseBackCb);

    CHECK_EQ(PerformUseBack(true, 12345), 42u);
    CHECK_EQ(g_useBackCalls, 1);
    CHECK_EQ(g_useBackTarget, 12345);

    // Reject: returns 0 and does NOT emit a command.
    CHECK_EQ(PerformUseBack(false, 999), 0u);
    CHECK_EQ(g_useBackCalls, 1); // unchanged

    SetUseBackCmdHook(nullptr);
}
