// End-to-end test for the Guild world/economy flow (guild::world):
//   build a small city with a mixed-profession population, then run one economy
//   tick: parameter-table init -> goods demand -> price deltas -> production
//   integration, checking the resulting stocks/prices against a hand-computed
//   reference (golden values cross-checked with python).
#include <cmath>
#include <vector>

#include "tests/framework/test.h"
#include "world/city.h"
#include "world/economy.h"
#include "world/production.h"
#include "world/tax.h"
#include "world/types.h"

using namespace guild::world;

namespace {
bool approx(float a, float b) { return std::fabs(a - b) < 1e-3f; }
}

TEST(WorldEconomyE2E, FullTick) {
    // 1) Build a small city from a synthetic .ini.
    const char* ini =
        "[A - ALLGEMEIN]\n"
        "Stadtname=Koeln\n"
        "KartenPosition=10,20\n"
        "Glaube=1\n"
        "MaxPlayer=2\n"
        "[F - GESETZE]\n"
        "Finanzgesetze=8,0,0,0,0,0,0,0,0,0,0,0\n";
    IniDocument* doc = IniParse(ini);
    CityLoadFromIni(doc, 0);
    CHECK_EQ((int)g_cities[0].faith, 1);
    CHECK_EQ((int)g_cities[0].finanz[0], 8);
    IniFree(doc);

    // 2) Seed the economy parameter table (cap divisor = equilibrium scale).
    CityInitParameterTable(1000.0f);

    // 3) Mixed-profession population. persons[g] = views for category g.
    std::vector<PersonEcoView> persons[28];
    persons[3]  = {{10, false}, {6, false}};                 // flat
    persons[5]  = {{8, false}, {12, false}, {4, true}};      // default
    persons[7]  = {{5, false}};                              // service
    persons[16] = {{9, false}, {9, false}};                  // inverted at price step

    // 4) Demand pass.
    EconomyComputeGoodsDemand(persons);
    CHECK(approx(g_goods[3].accum, 24.0f));
    CHECK(approx(g_goods[5].accum, 41.5f));
    CHECK(approx(g_goods[7].accum, 14.0f));
    CHECK(approx(g_goods[16].accum, 34.0f));

    // 5) Price-delta pass — drives prices toward equilibrium.
    EconomyComputePriceDeltas();
    CHECK(approx(g_goods[3].priceDelta, 6.2f));
    CHECK(approx(g_goods[5].priceDelta, 13.525f));
    CHECK(approx(g_goods[7].priceDelta, 7.4f));
    // good 16 is an inverted good -> negative delta despite positive demand.
    CHECK(approx(g_goods[16].priceDelta, -5.8f));
    CHECK(g_goods[16].priceDelta < 0.0f);

    // Apply the deltas to a starting price vector (the engine adds the per-tick
    // drift to each good's price). Demand-heavy normal goods rise; the inverted
    // good falls.
    float price[28];
    for (int g = 0; g < 28; ++g) price[g] = 100.0f;
    for (int g = 3; g < 28; ++g) price[g] += g_goods[g].priceDelta;
    CHECK(price[5] > 100.0f);   // demand surplus pushed price up
    CHECK(price[16] < 100.0f);  // inverted good pushed down
    CHECK(approx(price[5], 113.525f));
    CHECK(approx(price[16], 94.2f));

    // 6) Production pass — integrate one good's output over a work shift.
    //    Weekday 0 window is [8,20]; a 09:00->17:00 shift on day 0 yields the
    //    clamped span 540..1020 = 480 minutes of output.
    ProdTime shiftStart{0, 9, 0}, shiftEnd{0, 17, 0};
    int outputMinutes = ProductionComputeOutputOverTime(shiftStart, shiftEnd, false);
    CHECK_EQ(outputMinutes, 480);

    // 7) Tax pass on the day's trade — uses finance law slot 8's rate (here we
    //    pass the law threshold directly; the engine reads it via Gesetz_GetRecord).
    //    rate 10% of a 4000-gulden account = 399 (single-precision truncation).
    CHECK_EQ(TaxComputeTradeIncome(10, 4000), 399);
}
