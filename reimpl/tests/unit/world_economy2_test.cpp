// Unit tests for world_economy2 — the office-action dialog deterministic kernels.
// Golden vectors computed with python (see the brief). The RNG / rating hooks are
// driven with fixed values so every formula is exactly reproducible.
#include "test.h"

#include "world/world_economy2.h"

#include <cmath>

using namespace guild;
using namespace guild::world;

namespace {

// Fixed-value hooks so the formulas are deterministic. The roll hook returns
// DOUBLE — VIBE_Math_RandomFloatScaled (0x58b910) returns st0 (double).
double g_roll = 0.0;
float g_rating = 0.0f;
double FixedRoll() { return g_roll; }
float FixedRating(const void*, int) { return g_rating; }

void InstallFixed(double roll, float rating) {
    g_roll = roll; g_rating = rating;
    WorldEconomy2Hooks h{};
    h.randFloatScaled = FixedRoll;
    h.productionRating = FixedRating;
    WorldEconomy2SetHooks(h);
}

bool FloatClose(float a, float b) { return std::fabs(a - b) < 1e-5f; }

} // namespace

// payment = trunc(((roll+1)*(rating*15)+5)*2.55)
TEST(WorldEconomy2, PaymentFormulaGolden) {
    InstallFixed(/*roll*/ 0.25f, /*rating*/ 0.5f);
    CHECK_EQ(ComputeOfficePaymentAmount(nullptr), 36);   // 36.65625 -> 36
    WorldEconomy2ResetHooks();
}

TEST(WorldEconomy2, PaymentFormulaZeroRating) {
    InstallFixed(/*roll*/ 0.0f, /*rating*/ 0.0f);
    CHECK_EQ(ComputeOfficePaymentAmount(nullptr), 12);   // ((1)*0+5)*2.55=12.75 -> 12
    WorldEconomy2ResetHooks();
}

// Inert default: no rating hook, no roll hook -> rating 0, roll 0 -> 12.
TEST(WorldEconomy2, PaymentFormulaInertDefault) {
    WorldEconomy2ResetHooks();
    CHECK_EQ(ComputeOfficePaymentAmount(nullptr), 12);
}

// Appointment price, high branch (objectKind == 1): (roll+1)*(rating*0.4)+1.2
TEST(WorldEconomy2, AppointmentPriceHighBranch) {
    InstallFixed(/*roll*/ 0.25f, /*rating*/ 0.5f);
    float p = ComputeAppointmentPrice(0.5f, /*objectKind*/ 1);
    CHECK(FloatClose(p, 1.4500000476837158f));
    WorldEconomy2ResetHooks();
}

// Appointment price, low branch (objectKind != 1): 1.0 - ((roll+1)*(rating*0.3)+0.2)
TEST(WorldEconomy2, AppointmentPriceLowBranch) {
    InstallFixed(/*roll*/ 0.25f, /*rating*/ 0.5f);
    float p = ComputeAppointmentPrice(0.5f, /*objectKind*/ 0);
    CHECK(FloatClose(p, 0.612500011920929f));
    WorldEconomy2ResetHooks();
}

// Direction roll: <= 0.3 -> -1, else +1.
TEST(WorldEconomy2, ActionDirectionNegative) {
    InstallFixed(/*roll*/ 0.20f, /*rating*/ 0.0f);
    CHECK_EQ(RollActionDirection(), -1);
    WorldEconomy2ResetHooks();
}
TEST(WorldEconomy2, ActionDirectionPositive) {
    InstallFixed(/*roll*/ 0.50f, /*rating*/ 0.0f);
    CHECK_EQ(RollActionDirection(), 1);
    WorldEconomy2ResetHooks();
}
TEST(WorldEconomy2, ActionDirectionAtGate) {
    InstallFixed(/*roll*/ 0.30000001192092896f, /*rating*/ 0.0f);
    CHECK_EQ(RollActionDirection(), -1);  // roll <= gate -> -1
    WorldEconomy2ResetHooks();
}

// Max candidate rank scan keeps the maximum.
TEST(WorldEconomy2, ScanMaxCandidateRank) {
    u8 ranks[5] = {2, 5, 3, 5, 1};
    CHECK_EQ(ScanMaxCandidateRank(ranks, 5), 5);
    CHECK_EQ(ScanMaxCandidateRank(ranks, 0), 0);  // empty -> 0
    CHECK_EQ(ScanMaxCandidateRank(nullptr, 5), 0);
}

// Dependent count + severance split: 4 matching live dependents of 1000.
// Per-head = trunc(1000/4) + 32 = 282 — the binary adds 32 to the truncated
// quotient before queueing (add ebp, 20h @0x482390); old pin 250 was proven
// wrong against gilde.exe 0x482218.
TEST(WorldEconomy2, DependentCountAndSeverance) {
    WorldEconomy2ResetHooks();
    // 6 people; entity 77 is the dismissed office. People 0,1,3,5 serve it (present).
    i32 personEntity[6] = {77, 77, 12, 77, 99, 77};
    i32 personObjIds[6] = {900, 901, 902, 903, 904, 905};
    bool present[6]     = {true, true, true, true, true, true};
    i32 perHead = -1;
    // holderOfficeId = 4 (index 4 does not match entity 77) -> no self-exclude.
    int deps = CountOfficeDependents(/*targetEntity*/ 77, personEntity,
                                     personObjIds, present,
                                     /*holderOfficeId*/ 4, 6,
                                     /*totalAmount*/ 1000,
                                     /*paymentAmount*/ 1000, &perHead);
    CHECK_EQ(deps, 4);        // indices 0,1,3,5
    CHECK_EQ(perHead, 282);   // trunc(1000/4) + 32 (0x482390)
}

// Self-exclusion: holderOfficeId equals a matching person index -> excluded.
TEST(WorldEconomy2, DependentSelfExclusion) {
    WorldEconomy2ResetHooks();
    i32 personEntity[3] = {77, 77, 77};
    i32 personObjIds[3] = {910, 911, 912};
    bool present[3]     = {true, true, true};
    i32 perHead = -1;
    // holderOfficeId == 1 excludes person index 1.
    int deps = CountOfficeDependents(77, personEntity, personObjIds, present,
                                     /*holderOfficeId*/ 1, 3, 900,
                                     /*paymentAmount*/ 900, &perHead);
    CHECK_EQ(deps, 2);        // indices 0 and 2
    CHECK_EQ(perHead, 482);   // trunc(900/2) + 32 (0x482390)
}

// Absent (not present) people are skipped.
TEST(WorldEconomy2, DependentPresenceGate) {
    WorldEconomy2ResetHooks();
    i32 personEntity[4] = {77, 77, 77, 77};
    i32 personObjIds[4] = {920, 921, 922, 923};
    bool present[4]     = {true, false, true, false};
    i32 perHead = -1;
    // holderOfficeId 1: index 1 is absent anyway, so no self-exclusion effect.
    int deps = CountOfficeDependents(77, personEntity, personObjIds, present,
                                     1, 4, 1000, /*paymentAmount*/ 1000, &perHead);
    CHECK_EQ(deps, 2);        // only the two present
    CHECK_EQ(perHead, 532);   // trunc(1000/2) + 32 (0x482390)
}

// No dependents -> per-head 0.
TEST(WorldEconomy2, DependentNoneZeroPerHead) {
    WorldEconomy2ResetHooks();
    i32 personEntity[2] = {1, 2};
    i32 personObjIds[2] = {930, 931};
    bool present[2]     = {true, true};
    i32 perHead = -1;
    int deps = CountOfficeDependents(/*target*/ 77, personEntity, personObjIds,
                                     present, 0, 2, 1000,
                                     /*paymentAmount*/ 1000, &perHead);
    CHECK_EQ(deps, 0);
    CHECK_EQ(perHead, 0);     // no +32 when there are no dependents (block skipped)
}

// Grid label x-offset: slotX + 64 - camX.
TEST(WorldEconomy2, GridLabelOffset) {
    CHECK_EQ(ComputeGridLabelOffset(/*slotX*/ 200, /*camX*/ 50), 214);  // 200+64-50
    CHECK_EQ(ComputeGridLabelOffset(0, 0), 64);
}
