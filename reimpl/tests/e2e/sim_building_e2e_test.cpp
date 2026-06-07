// End-to-end flow for the buildings module (guild::sim) — gilde.exe.
//
// Builds a synthetic production building, advances game-time, drives the fill
// level up over a working window, and accumulates the per-step output through
// the recovered ComputeCurrentOutput/MaxOutput math — verifying the running
// total and the production rating against a hand-computed reference.
#include "test.h"

#include "sim/building.h"
#include "sim/building_type.h"
#include "sim/building_value.h"
#include "sim/entity.h"
#include "sim/gametime.h"

#include <cmath>

using namespace guild;
using namespace guild::sim;

namespace {
bool feq(double a, double b, double eps = 1e-4) { return std::fabs(a - b) < eps; }
}  // namespace

TEST(SimBuildingE2E, ProductionBuildingOverTime) {
    // --- Set up a one-type catalog: a production building (kind 11). ----------
    ResetBuildings();
    ResetEntityArrays();
    g_buildingTypes[5] = BuildingTypeDef{};
    g_buildingTypes[5].kind = 11;          // production
    g_buildingTypes[5].security = 2;
    g_buildingTypes[5].outputFactor[0] = 4;
    g_buildingTypes[5].outputFactor[1] = 4;
    g_buildingTypesLoaded = true;

    // --- Register the building in the shared object array (alive @+0, id @+1)
    //     and hold its value-math overlay separately (typeIndex @+0). ----------
    g_objects[0].alive = 1;
    g_objects[0].id = 7;

    BuildingRec building{};
    BuildingRec* b = &building;
    b->typeIndex = 5;
    b->activeFlag = 1;
    b->fillLevel = 0;
    b->fillCap = 200.0f;
    b->outZeroFill = 1.0f;
    b->outFullFill = 21.0f;
    b->outBonus = 0;

    // Resolve it the way the game does (id -> object record) and classify it.
    ObjectRec* found = BuildingFindRecordById(7);
    CHECK(found == &g_objects[0]);
    CHECK(Building_IsProductionType(b));
    CHECK_EQ(Building_GetSecurityLevel(b), 2);

    // --- Run it over game-time: each step the building fills by 40 units and we
    //     advance the clock by 90 minutes; accumulate the output. --------------
    GameTime clk{};
    clk.day = 0; clk.hour = 8; clk.minute = 0; clk.second = 0;

    const u16 fills[] = {0, 40, 80, 120, 160, 200, 240};
    const float expected[] = {1.0f, 5.0f, 9.0f, 13.0f, 17.0f, 21.0f, 21.0f};
    double total = 0.0;
    int steps = 0;
    for (u16 f : fills) {
        b->fillLevel = f;
        float out = Building_ComputeCurrentOutput(b);
        CHECK(feq(out, expected[steps]));
        total += out;
        // advance the clock 90 minutes (carry into hours/days).
        GameTimeAdvance(&clk, 0, 0, 90);
        ++steps;
    }

    // Hand-computed running total: 1+5+9+13+17+21+21 = 87.
    CHECK(feq(total, 87.0));

    // Clock: started 08:00 day0, +90min * 7 = +630min = 10h30m -> day0 18:30.
    CHECK_EQ(clk.day, 0);
    CHECK_EQ((int)clk.hour, 18);
    CHECK_EQ(clk.minute, 30);

    // --- Output ratio at full fill is 1.0 (current == max, bonus 0). ----------
    b->fillLevel = 200;
    CHECK(feq(Building_ComputeMaxOutput(b), 21.0));
    CHECK(feq(Building_ComputeOutputRatio(b), 1.0));

    // --- Production rate from the type table (price 500, divisor 8). ----------
    //   sum = (4+4)*28*32*(1/12)*(1/60) ; rate = 500*sum/8.
    double sum = 0.0;
    for (int i = 0; i < 2; ++i)
        sum += g_buildingTypes[5].outputFactor[i] * 28.0 * 32.0 * (1.0 / 12.0) * (1.0 / 60.0);
    float sumf = static_cast<float>(sum);
    double expectRate = 500.0 * sumf / 8.0;
    CHECK(feq(Building_ComputeProductionRate(g_buildingTypes[5], 500, 8), expectRate, 1e-2));

    // --- Production rating (stat 0) with a quality stat level. ----------------
    SetBuildingRatingHooks(nullptr);
    b->statLevel[0] = 126;       // 126/252 = 0.5 exactly
    b->staffBits = 0;
    CHECK(feq(Building_EvalProductionRating(b, 0), 0.5));
    CHECK_EQ(Building_ComputeProductionPixels(0, b), 126);   // trunc(0.5*252)
}
