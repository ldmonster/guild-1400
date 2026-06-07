// Integration test: world/economy_tick cross-wired against its REAL siblings —
// world/economy (goods demand + price deltas), world/economy_quality (law-score
// cores), world/city (parameter table + global totals), and world/law (the live
// law table). No stubs: every collaborator is the production translation.
#include "test.h"

#include <array>
#include <cmath>
#include <cstring>
#include <vector>

#include "world/city.h"
#include "world/economy.h"
#include "world/economy_quality.h"
#include "world/economy_tick.h"
#include "world/law.h"
#include "world/types.h"

using namespace guild;
using namespace guild::world;

namespace {
bool Near(double a, double b, double eps = 1e-4) { return std::fabs(a - b) <= eps; }
}  // namespace

// The price tick must drive the REAL demand + price-delta cores: after the tick,
// EconomyComputeGoodsDemand has refreshed g_cityTotalMoney/g_cityTotalGoods and
// the per-good price deltas are populated by the real EconomyComputePriceDeltas.
TEST(EconomyTickIT, PriceTickDrivesRealDemandAndDeltas) {
    // Real parameter-table seed (this is VIBE_City_InitParameterTable). It also
    // sets g_capDivisor; we override it to 0 to take the deterministic first-tick
    // branch of the price level EMA.
    CityInitParameterTable(1000.0f);
    g_capDivisor = 0.0f;
    SetSmoothedPriceLevel(0.0f);

    // Drive the snapshot slot[9] (the demand seed the tick multiplies in) and a
    // mixed population so the REAL demand pass produces a non-trivial money total.
    std::array<float, 10> snap = {};
    snap[9] = 0.0f;  // target = (1 + 0)*money
    EconomySetDemandSnapshot(snap);

    std::vector<PersonEcoView> persons[28];
    persons[1] = {{20, false}, {10, false}};  // money-side goods (1,2)
    persons[2] = {{15, false}};
    persons[3] = {{10, false}, {6, false}};

    u8 clock[22];
    for (int i = 0; i < 22; ++i) clock[i] = static_cast<u8>(i + 1);
    SetBroadcastClock(clock);

    int rounded = EconomyTickPriceLevel(persons);

    // The real demand core set the money total from goods 1 & 2.
    CHECK(g_cityTotalMoney != 0.0f);
    // First tick: smoothed price == target == (1+0)*money ; divisor == price*0.75.
    float target = g_cityTotalMoney;  // (1+snap[9]) == 1
    CHECK(Near(GetSmoothedPriceLevel(), target, 1e-2));
    CHECK(Near(g_capDivisor, target * 0.75f, 1e-2));
    // Rounded return == trunc(price).
    CHECK_EQ(rounded, static_cast<int>(std::trunc(GetSmoothedPriceLevel())));
    // The broadcast clock was snapshotted from the injected clock.
    CHECK_EQ(std::memcmp(GetBroadcastClockSnapshot(), clock, 22), 0);
    // The real price-delta core ran: at least one good carries a delta.
    bool anyDelta = false;
    for (int g = 3; g < 28; ++g)
        if (g_goods[g].priceDelta != 0.0f) anyDelta = true;
    CHECK(anyDelta || g_cityTotalGoods == 0.0f);  // deltas may be 0 if goods empty
}

// PopulationTrend must consume the REAL law-score cores (EconomyComputeWeightedLawScore
// + EconomyComputeLawSatisfaction) reading the live g_lawTable. We seed two law
// configurations and verify the score tracks the real cores exactly.
TEST(EconomyTickIT, PopulationTrendTracksRealLawCores) {
    LawTableResetDefaults();
    // Compute the reference law contributions directly from the same real cores.
    double weighted = EconomyComputeWeightedLawScore();
    double lawSat   = EconomyComputeLawSatisfaction();

    g_capDivisor = 100.0f;
    PopulationStats s = {};
    // growth = (prevCount*prevScale + curCount - div)/div = (100 + 10 - 100)/100
    //        = 0.1 (well inside [-1,1], no clamp).
    s.prevCount = 100; s.prevScale = 1.0f; s.curCount = 10;
    s.birthsCmp = 1000; s.deathsCmp = 1000;                    // both terms 0
    SetPopulationStats(s);

    // Expected: trend = 0.1 (no clamp) ; score = weighted*0.30 + 0.1*0.65 ;
    //           result = lawSat*0.05 + score.
    float trendF = 0.1f;
    double expected = lawSat * 0.05 +
                      (weighted * static_cast<double>(0.30f) +
                       static_cast<double>(trendF) * static_cast<double>(0.65f));
    CHECK(Near(EconomyComputePopulationTrend(), expected, 1e-4));

    // Now bump every threshold and confirm the result moves with the real cores.
    for (int i = 0; i < kLawCount; ++i) g_lawTable[i].threshold += 1;
    double weighted2 = EconomyComputeWeightedLawScore();
    double lawSat2   = EconomyComputeLawSatisfaction();
    CHECK(weighted2 != weighted || lawSat2 != lawSat);
    double expected2 = lawSat2 * 0.05 +
                       (weighted2 * static_cast<double>(0.30f) +
                        static_cast<double>(trendF) * static_cast<double>(0.65f));
    CHECK(Near(EconomyComputePopulationTrend(), expected2, 1e-4));
    LawTableResetDefaults();
}

// FillLawRangeRatiosFromTable must read the SAME live law records that
// VIBE_Gesetz_GetRecord serves, using the +0/+4/+20 triple.
TEST(EconomyTickIT, FillLawRangeRatiosUsesLiveTable) {
    LawTableResetDefaults();
    // Hand-set laws 8..14's +0/+4/+20 dwords to known values via raw record bytes.
    for (int i = 0; i < 7; ++i) {
        u8* rec = reinterpret_cast<u8*>(&g_lawTable[8 + i]);
        i32 lo = 0, hi = 100, value = 25 * i;  // ratio = value/100
        std::memcpy(rec + 0, &lo, 4);
        std::memcpy(rec + 4, &hi, 4);
        std::memcpy(rec + 20, &value, 4);
    }
    float out[15];
    for (int i = 0; i < 15; ++i) out[i] = -1.0f;
    EconomyFillLawRangeRatiosFromTable(out);
    for (int i = 0; i < 7; ++i)
        CHECK(Near(out[8 + i], 0.25 * i, 1e-5));
    LawTableResetDefaults();
}
