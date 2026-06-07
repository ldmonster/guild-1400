// End-to-end flow for the Guild economy-quality scalars (guild::world).
// Simulates a per-city "economic health" assessment pass: seed the aggregate
// state (good totals, equilibrium divisor, demand snapshot, law table), then run
// every quality/ratio/law-score scalar the price-tick and AI consume and check
// the combined readout is internally consistent and matches hand-computed
// golden values.
#include <array>
#include <cmath>
#include <vector>

#include "test.h"
#include "world/city.h"
#include "world/economy_quality.h"
#include "world/law.h"

using namespace guild::world;

namespace {
bool Near(double a, double b, double eps = 1e-6) { return std::fabs(a - b) <= eps; }
}  // namespace

// ---------------------------------------------------------------------------
// A prosperous city: goods slightly above equilibrium, money well above, a
// permissive law table, a healthy object mix.
// ---------------------------------------------------------------------------
TEST(EconQualityE2E, ProsperousCityAssessment) {
    // 1. Equilibrium divisor + city totals (the price-tick's flt_641DA8/FD4/FD8).
    g_capDivisor     = 100.0f;
    g_cityTotalGoods = 130.0f;   // (130-100)/100 = 0.30
    g_cityTotalMoney = 100.0f;   // (100-100)/100 = 0.00

    double industry = EconomyComputeIndustryRatio();
    double resident = EconomyComputeResidentialRatio();
    CHECK(Near(industry, 0.30000001192092896, 1e-6));
    CHECK(Near(resident, 0.0, 1e-6));
    // Industry pressure exceeds residential pressure for this city.
    CHECK(industry > resident);

    // 2. Average good quality across the active object mix.
    std::vector<ObjectQualityView> objects = {
        {6, 8}, {4, 6}, {9, 10}, {3, 6}};  // num=22, den=30
    double quality = EconomyComputeAverageQuality(objects);
    CHECK(Near(quality, 22.0 / 30.0, 1e-6));
    CHECK(quality > 0.0 && quality <= 1.0);

    // 3. Demand snapshot block: feed a 10-float block, confirm the cap-divisor
    //    patch lands in slot 6 and the live demand reading (slot 9) round-trips.
    std::array<float, 10> snap = {0.1f, 0.2f, 0.3f, 0.4f, 0.5f,
                                  0.6f, 0.7f, 0.8f, 0.9f, 1.25f};
    EconomySetDemandSnapshot(snap);
    float out[10] = {};
    double demand = EconomyLoadDemandSnapshot(out);
    CHECK(Near(demand, 1.25, 1e-6));
    CHECK(Near(out[6], g_capDivisor, 1e-6));  // patched, NOT the original 0.7

    // 4. Law-driven satisfaction scalars across the three law bands.
    LawTableResetDefaults();
    g_lawTable[0].threshold = 1;  // lenient
    g_lawTable[1].threshold = 0;
    double lawSat = EconomyComputeLawSatisfaction();
    // (4-1)*0.25*0.75 + (1-0)*0.25 = 0.5625 + 0.25 = 0.8125
    CHECK(Near(lawSat, 0.8125, 1e-6));

    for (int i = 0; i < 10; ++i)
        g_lawTable[16 + i].threshold = 2;
    double weighted = EconomyComputeWeightedLawScore();
    // sum of weights (0.06+..+0.15 = 1.0) * threshold 2 ~= 2.0
    CHECK(Near(weighted, 2.0, 1e-4));

    std::array<LawRangeRecord, 7> bands;
    for (auto& b : bands) b = {0, 10, 2};  // t = 0.2, term = w*0.8
    double interp = EconomyComputeInterpolatedLawScore(bands);
    // sum_{i} w[i]*0.8 + last term (w[6]*0.8). sum w = 1.0 -> 0.8 + 0.08 = 0.88
    CHECK(Near(interp, 0.88, 1e-5));

    // 5. Combined health index (a plausible downstream consumer): the three
    //    law scalars and the two pressure ratios should all sit in band.
    CHECK(lawSat   > 0.0);
    CHECK(weighted > 0.0);
    CHECK(interp   > 0.0);
    CHECK(industry >= -1.0 && industry <= 1.0);
    CHECK(resident >= -1.0 && resident <= 1.0);
}

// ---------------------------------------------------------------------------
// A depressed city: goods/money far below equilibrium clamp to the floor; a
// harsh law table drives the satisfaction terms toward (or below) zero.
// ---------------------------------------------------------------------------
TEST(EconQualityE2E, DepressedCityAssessment) {
    g_capDivisor     = 100.0f;
    g_cityTotalGoods = 10.0f;    // (10-100)/100 = -0.9 (in band)
    g_cityTotalMoney = -50.0f;   // (-50-100)/100 = -1.5 -> floor -1.0

    CHECK(Near(EconomyComputeIndustryRatio(), -0.8999999761581421, 1e-6));
    CHECK(Near(EconomyComputeResidentialRatio(), -1.0, 1e-6));

    LawTableResetDefaults();
    g_lawTable[0].threshold = 4;  // maximally strict -> first term zero
    g_lawTable[1].threshold = 1;  // -> second term zero
    CHECK(Near(EconomyComputeLawSatisfaction(), 0.0, 1e-6));
}
