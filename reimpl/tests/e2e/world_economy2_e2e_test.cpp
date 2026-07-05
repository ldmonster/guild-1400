// E2E: a full office-action flow across world_economy2's kernels, mimicking the
// player issuing an "appoint then dismiss" sequence the original dialogs drive.
//   1. BuildGuildOfficeMenu: scan candidate ranks, compute the appointment price.
//   2. BuildOfficeActionDialog: roll the action direction + the payment.
//   3. BuildElectionDialog: dismiss the holder, splitting severance across the
//      dependent staff and fanning out the per-head commands.
// Driven with scripted RNG/rating hooks so the whole flow is deterministic.
#include "test.h"

#include "world/world_economy2.h"

#include <cmath>

using namespace guild;
using namespace guild::world;

namespace {
double g_roll = 0.0;   // RandomFloatScaled (0x58b910) returns double
float g_rating = 0.0f;
double ScriptRoll() { return g_roll; }
float ScriptRating(const void*, int) { return g_rating; }
} // namespace

TEST(WorldEconomy2E2E, AppointActDismissFlow) {
    // Captor for the severance fan-out.
    static int g_payCalls = 0; static i32 g_lastValue = 0;
    g_payCalls = 0; g_lastValue = 0;

    WorldEconomy2Hooks h{};
    h.randFloatScaled = ScriptRoll;
    h.productionRating = ScriptRating;
    h.queueCoord27 = [](i32, i32, i32 v) { g_payCalls++; g_lastValue = v; };
    WorldEconomy2SetHooks(h);

    // --- Step 1: appointment menu ---------------------------------------------
    g_roll = 0.25; g_rating = 0.5f;
    u8 candidateRanks[4] = {3, 7, 2, 7};
    int topRank = ScanMaxCandidateRank(candidateRanks, 4);
    CHECK_EQ(topRank, 7);                       // best candidate's rank

    float appointPrice = ComputeAppointmentPrice(g_rating, /*objectKind*/ 1);
    CHECK(std::fabs(appointPrice - 1.4500000476837158f) < 1e-5f);

    // --- Step 2: office action (promotion) ------------------------------------
    g_roll = 0.5;                               // > 0.3 -> direction +1
    int dir = RollActionDirection();
    CHECK_EQ(dir, 1);

    g_roll = 0.25; g_rating = 0.5f;
    i32 payment = ComputeOfficePaymentAmount(nullptr);
    CHECK_EQ(payment, 36);                      // ((1.25)*(7.5)+5)*2.55 = 36.65 -> 36

    // --- Step 3: dismissal severance ------------------------------------------
    // Six staff; four serve office 77. Holder office-id 4 (index 4 is entity 99,
    // not a dependent, so nothing is self-excluded).
    i32 personEntity[6] = {77, 77, 12, 77, 99, 77};
    i32 personObjIds[6] = {800, 801, 802, 803, 804, 805};
    bool present[6]     = {true, true, true, true, true, true};
    i32 perHead = -1;
    int deps = CountOfficeDependents(/*target*/ 77, personEntity, personObjIds,
                                     present, /*holderOfficeId*/ 4, 6,
                                     /*totalAmount*/ payment * 100,
                                     /*paymentAmount*/ payment, &perHead);
    WorldEconomy2ResetHooks();

    CHECK_EQ(deps, 4);                          // four dependents
    CHECK_EQ(g_payCalls, 4);                    // a command per dependent
    CHECK_EQ(g_lastValue, -payment);            // commands carry -payment (v27)
    CHECK_EQ(perHead, (payment * 100) / 4 + 32);// trunc(3600/4)+32 = 932 (0x482390)

    // Grid layout sanity: a slot at screen-x 300 with camera at 40 lands at 324.
    CHECK_EQ(ComputeGridLabelOffset(300, 40), 324);
}
