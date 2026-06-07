// Unit tests for the Guild world/economy_quality module (guild::world).
// Golden vectors computed with python3 (IEEE-754 single/double, matching the
// original's float accumulators).
#include <array>
#include <cmath>
#include <vector>

#include "test.h"
#include "world/city.h"
#include "world/economy_quality.h"
#include "world/law.h"
#include "world/types.h"

using namespace guild::world;

namespace {
bool Near(double a, double b, double eps = 1e-6) { return std::fabs(a - b) <= eps; }
}  // namespace

// ---------------------------------------------------------------------------
// 0x579a38 — ComputeAverageQuality: numerator/denominator over occupied objects.
// ---------------------------------------------------------------------------
TEST(EconQuality, AverageQuality) {
    std::vector<ObjectQualityView> objs = {
        {5, 10}, {3, 7}, {8, 12}, {2, 2}};  // num=18, den=31
    CHECK(Near(EconomyComputeAverageQuality(objs), 0.5806451439857483));

    // Zero denominator -> 0.0 (original's !v2 guard).
    std::vector<ObjectQualityView> zeros = {{0, 0}, {5, 0}};
    CHECK(Near(EconomyComputeAverageQuality(zeros), 0.0));

    // Empty array -> 0.0.
    CHECK(Near(EconomyComputeAverageQuality({}), 0.0));
}

// ---------------------------------------------------------------------------
// 0x57a3c8 / 0x57a474 — Industry / Residential ratio clamps.
// ---------------------------------------------------------------------------
TEST(EconQuality, IndustryRatio) {
    g_capDivisor = 100.0f;

    g_cityTotalGoods = 300.0f;  // (300-100)/100 = 2 -> upper clamp 1.0
    CHECK(Near(EconomyComputeIndustryRatio(), 1.0));

    g_cityTotalGoods = 120.0f;  // 0.2 passthrough
    CHECK(Near(EconomyComputeIndustryRatio(), 0.20000000298023224));

    g_cityTotalGoods = -300.0f;  // -4 -> lower clamp -1.0
    CHECK(Near(EconomyComputeIndustryRatio(), -1.0));

    g_cityTotalGoods = 10.0f;  // -0.9 passthrough
    CHECK(Near(EconomyComputeIndustryRatio(), -0.8999999761581421));
}

TEST(EconQuality, ResidentialRatio) {
    g_capDivisor = 200.0f;
    g_cityTotalMoney = 250.0f;  // (250-200)/200 = 0.25
    CHECK(Near(EconomyComputeResidentialRatio(), 0.25));

    g_cityTotalMoney = 1000.0f;  // 4 -> upper clamp 1.0
    CHECK(Near(EconomyComputeResidentialRatio(), 1.0));
}

// ---------------------------------------------------------------------------
// 0x57a5dc — LoadDemandSnapshot: copy block, patch slot 6, return slot 9.
// ---------------------------------------------------------------------------
TEST(EconQuality, LoadDemandSnapshot) {
    std::array<float, 10> block = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
    EconomySetDemandSnapshot(block);
    g_capDivisor = 42.5f;

    float out[10] = {};
    double ret = EconomyLoadDemandSnapshot(out);
    CHECK(Near(ret, 10.0));         // returns out[9]
    CHECK(Near(out[6], 42.5));      // slot 6 overwritten by cap divisor
    CHECK(Near(out[0], 1.0));
    CHECK(Near(out[9], 10.0));
    CHECK(Near(out[5], 6.0));

    // null pointer -> 0.0, no crash.
    CHECK(Near(EconomyLoadDemandSnapshot(nullptr), 0.0));
}

// ---------------------------------------------------------------------------
// 0x57a520 — ComputeLawSatisfaction: two-term score from law records 0 and 1.
// ---------------------------------------------------------------------------
TEST(EconQuality, LawSatisfaction) {
    LawTableResetDefaults();
    g_lawTable[0].threshold = 2;
    g_lawTable[1].threshold = 0;
    // (4-2)*0.25*0.75 + (1-0)*0.25 = 0.375 + 0.25 = 0.625
    CHECK(Near(EconomyComputeLawSatisfaction(), 0.625));

    g_lawTable[0].threshold = 4;
    g_lawTable[1].threshold = 1;
    CHECK(Near(EconomyComputeLawSatisfaction(), 0.0));

    g_lawTable[0].threshold = 0;
    g_lawTable[1].threshold = 0;
    CHECK(Near(EconomyComputeLawSatisfaction(), 1.0));
}

// ---------------------------------------------------------------------------
// 0x57a580 — ComputeWeightedLawScore: weighted sum of law 16..25 thresholds.
// ---------------------------------------------------------------------------
TEST(EconQuality, WeightedLawScore) {
    LawTableResetDefaults();
    for (int i = 0; i < 10; ++i)
        g_lawTable[16 + i].threshold = i + 1;  // thresholds 1..10
    CHECK(Near(EconomyComputeWeightedLawScore(), 6.440000057220459, 1e-5));

    for (int i = 0; i < 10; ++i)
        g_lawTable[16 + i].threshold = 10;
    CHECK(Near(EconomyComputeWeightedLawScore(), 9.999999046325684, 1e-5));
}

// ---------------------------------------------------------------------------
// 0x57a990 — ComputeInterpolatedLawScore: weighted (1 - t) with tail double-add.
// ---------------------------------------------------------------------------
TEST(EconQuality, InterpolatedLawScore) {
    // All ranges [0,10] value 5 -> t = 0.5, term = w*0.5; last term re-added.
    std::array<LawRangeRecord, 7> recs;
    for (auto& r : recs) r = {0, 10, 5};
    CHECK(Near(EconomyComputeInterpolatedLawScore(recs), 0.5500000007450581, 1e-6));

    std::array<LawRangeRecord, 7> varied = {{
        {0, 4, 1}, {0, 4, 2}, {0, 4, 3}, {0, 8, 2}, {0, 5, 1}, {0, 10, 9}, {0, 2, 1}}};
    CHECK(Near(EconomyComputeInterpolatedLawScore(varied), 0.603000033646822, 1e-6));
}
