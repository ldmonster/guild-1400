// Integration: drive world_economy2's office-payment / appointment-price kernels
// against THREE REAL reconstructed siblings, wired exactly as the live engine
// forwards them:
//   * the production rating routes through WorldEconomy2Hooks.productionRating,
//     which the original implements as VIBE_Building_EvalProductionRating
//     (0x58a794, sim/building_value.cpp) — a genuine BuildingRec evaluated, NOT a
//     stubbed constant;
//   * the RNG roll routes through WorldEconomy2Hooks.randFloatScaled, which the
//     original implements as VIBE_Math_RandomFloatScaled (0x58b910,
//     util/math_rng_float.cpp) == (double)VIBE_Util_RandNext()/32767;
//   * the coord truncation routes through WorldEconomy2Hooks.truncate, the
//     original VIBE_Coord_ConvertX (0x5c6b08, util/coord.cpp) round-toward-zero.
// We construct a real building whose stat-4 rating is exactly 0.5, seed the shared
// CRT LCG, independently predict the draw, and assert the cross-module payment the
// binary would compute — proving the kernel composes with the real rating math,
// real RNG, and real truncation.
//
// The remaining leaves (Command_QueueRequest16 / QueueRequestCoord27 — the network
// commit) have no reconstructed sibling; those hooks use captors. The dependent
// fan-out test drives the real CountOfficeDependents table walk and observes its
// queueCoord27 fan-out through a captor.
#include "test.h"

#include "world/world_economy2.h"
#include "sim/building_value.h"
#include "sim/building_types.h"
#include "util/math_rng_float.h"
#include "util/coord.h"
#include "crt/rand.h"

#include <cmath>

using namespace guild;
using namespace guild::world;

namespace {

// REAL EvalProductionRating sibling, forwarded as the productionRating hook.
float RealRating(const void* b, int stat) {
    return sim::Building_EvalProductionRating(
        static_cast<const sim::BuildingRec*>(b), stat);
}
// REAL Math_RandomFloatScaled / Coord_ConvertX siblings.
float RealRollScaled() { return static_cast<float>(util::RandomFloatScaled()); }
i32   RealConvertX(double v) { return static_cast<i32>(util::ConvertX(v)); }

} // namespace

// Office payment computed through the REAL rating + REAL RNG + REAL ConvertX.
TEST(WorldEconomy2Itest, PaymentUsesRealRatingRngAndConvertX) {
    // Build a real building whose stat-4 rating is exactly statLevel[4]/252.
    // staffBits == 0 -> no staff penalty; default handler hook -> 0 weight.
    sim::SetBuildingRatingHooks(nullptr);  // inert rating sub-hooks (weight 0)
    sim::BuildingRec b{};
    b.staffBits = 0;
    b.statLevel[4] = 126;                  // 126/252 == 0.5 exactly
    float rating = sim::Building_EvalProductionRating(&b, 4);
    CHECK(std::fabs(rating - 0.5f) < 1e-6f);

    WorldEconomy2Hooks h{};
    h.productionRating = RealRating;
    h.randFloatScaled  = RealRollScaled;
    h.truncate         = RealConvertX;
    WorldEconomy2SetHooks(h);

    // Predict the exact first draw from a known seed, then re-seed so the module
    // draws the same value.
    crt::Srand(7u);
    double predictedRoll = static_cast<double>(crt::RandNext()) / 32767.0;

    crt::Srand(7u);
    i32 pay = ComputeOfficePaymentAmount(&b);
    WorldEconomy2ResetHooks();

    double expected = ((predictedRoll + 1.0) * (0.5 * 15.0) + 5.0) * 2.55;
    i32 expectedTrunc = static_cast<i32>(util::ConvertX(expected));
    CHECK_EQ(pay, expectedTrunc);
}

// Appointment price (high branch) through the REAL rating + REAL RNG. The price is
// a float; we reproduce the single-precision chain and compare closely.
TEST(WorldEconomy2Itest, AppointmentPriceUsesRealRatingAndRng) {
    sim::SetBuildingRatingHooks(nullptr);
    sim::BuildingRec b{};
    b.staffBits = 0;
    b.statLevel[2] = 63;                   // 63/252 == 0.25 -> rating for stat 2
    float rating = sim::Building_EvalProductionRating(&b, 2);
    CHECK(std::fabs(rating - 0.25f) < 1e-6f);

    WorldEconomy2Hooks h{};
    h.randFloatScaled = RealRollScaled;
    WorldEconomy2SetHooks(h);

    crt::Srand(11u);
    double predictedRoll = static_cast<double>(crt::RandNext()) / 32767.0;

    crt::Srand(11u);
    float price = ComputeAppointmentPrice(rating, /*objectKind*/ 1);
    WorldEconomy2ResetHooks();

    float r1 = static_cast<float>(predictedRoll) + 1.0f;
    float expected = r1 * (rating * kMenuPriceScaleHi) + kMenuPriceBaseHi;
    CHECK(std::fabs(price - expected) < 1e-4f);
}

// Dependent severance fan-out: the real CountOfficeDependents table walk drives the
// queueCoord27 hook (the original's VIBE_Command_QueueRequestCoord27) once per live
// dependent. We capture the fan-out and assert the split.
TEST(WorldEconomy2Itest, DependentFanOutThroughQueueCoord) {
    static int g_calls = 0; static i32 g_lastValue = 0;
    g_calls = 0; g_lastValue = 0;

    WorldEconomy2Hooks h{};
    h.truncate = RealConvertX;
    h.queueCoord27 = [](i32, i32, i32 value) { g_calls++; g_lastValue = value; };
    WorldEconomy2SetHooks(h);

    i32 personEntity[5] = {77, 12, 77, 77, 99};
    bool present[5]     = {true, true, true, true, true};
    i32 perHead = -1;
    int deps = CountOfficeDependents(/*target*/ 77, personEntity, nullptr, present,
                                     /*holderOfficeId*/ 50, 5,
                                     /*totalAmount*/ 600, &perHead);
    WorldEconomy2ResetHooks();

    CHECK_EQ(deps, 3);          // indices 0,2,3
    CHECK_EQ(g_calls, 3);       // one queueCoord27 per dependent
    CHECK_EQ(g_lastValue, -600);// commands carry -totalAmount
    CHECK_EQ(perHead, 200);     // 600/3
}
