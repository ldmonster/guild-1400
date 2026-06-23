// Verifies InstallRealHistoryAmtWiring() binds the AmtEconomy2 and MissionReqEvent
// world bridges to their real reconstructed leaves — previously both were inert at
// runtime (nothing installed them). Also exercises a representative bound path on
// each so the wired control flow actually executes. Suite prefix: WireHistoryAmt.
#include "tests/framework/test.h"

#include "world/wire_history_amt.h"
#include "world/amt_economy2.h"
#include "world/mission_requirement_event_recon.h"
#include "world/economy_quality.h"

#include <array>

using namespace guild;
using namespace guild::world;

// --- field binding -----------------------------------------------------------

TEST(WireHistoryAmt, BindsRealLeavesIntoBothBridges) {
    // Start from a clean inert baseline for both tables.
    AmtEconomy2ResetHooks();
    MissionReqEventGetHooks() = MissionReqEventHooks{};

    InstallRealHistoryAmtWiring();

    // AmtEconomy2: RNG / coord-trunc / three command builders are now real.
    const AmtEconomy2Hooks& amt = AmtEconomy2GetHooks();
    CHECK(amt.randFloatScaled != nullptr);
    CHECK(amt.truncate        != nullptr);
    CHECK(amt.queueRequest16  != nullptr);
    CHECK(amt.queueArgs26     != nullptr);
    CHECK(amt.queueCoord27    != nullptr);
    // unbound coupled leaves stay inert (null)
    CHECK(amt.officeAddTableEntry == nullptr);
    CHECK(amt.personFind          == nullptr);
    CHECK(amt.queueDeltaFlag      == nullptr);
    CHECK(amt.sendEntityMessage   == nullptr);

    // MissionReqEvent: money-convert / weighted-law / demand-snapshot / building map.
    const MissionReqEventHooks& req = MissionReqEventGetHooks();
    CHECK(req.moneyConvertToDisplayCoord     != nullptr);
    CHECK(req.economyComputeWeightedLawScore != nullptr);
    CHECK(req.economyLoadDemandSnapshot      != nullptr);
    CHECK(req.buildingTypeMapToActionCode    != nullptr);
    // unbound coupled person/object-store leaves stay inert (null)
    CHECK(req.personGetCurrencyAmount  == nullptr);
    CHECK(req.personQueryOwnedObjects  == nullptr);
    CHECK(req.personIterNext           == nullptr);
    CHECK(req.gameObjectQueryFind      == nullptr);

    AmtEconomy2ResetHooks();
    MissionReqEventGetHooks() = MissionReqEventHooks{};
}

// --- real execute over the bound leaves ---------------------------------------

TEST(WireHistoryAmt, BoundAmtTruncateAndRngExecute) {
    AmtEconomy2ResetHooks();
    InstallRealHistoryAmtWiring();

    const AmtEconomy2Hooks& amt = AmtEconomy2GetHooks();

    // truncate -> util::ConvertX (round toward zero): exact integral results.
    CHECK_EQ(amt.truncate(3.9), 3);
    CHECK_EQ(amt.truncate(-3.9), -3);
    CHECK_EQ(amt.truncate(7.0), 7);

    // randFloatScaled -> util::RandomFloatScaled in [0, 1): defined, in range.
    float r = amt.randFloatScaled();
    CHECK(r >= 0.0f);
    CHECK(r < 1.0f);

    // The command builders stage onto the real shared queue without crashing and
    // return defined ring-slot handles (>= 0). queueArgs26 reinterprets the float.
    amt.queueRequest16(-1, 5, 100, 0);
    amt.queueArgs26(5, 460, 0.05f);
    amt.queueCoord27(5, 6, 0);

    AmtEconomy2ResetHooks();
}

TEST(WireHistoryAmt, BoundMissionReqLeavesExecute) {
    MissionReqEventGetHooks() = MissionReqEventHooks{};
    InstallRealHistoryAmtWiring();

    const MissionReqEventHooks& req = MissionReqEventGetHooks();

    // moneyConvertToDisplayCoord: unseeded city rate defaults to 1 -> amount + 0.5
    // truncated == amount.
    CHECK_EQ(req.moneyConvertToDisplayCoord(250, 0), 250);

    // economyLoadDemandSnapshot: fills 10 floats and returns out[9]; the recon
    // overwrites out[6] with the cap divisor. Seed a known snapshot first.
    std::array<float, 10> block{};
    for (int i = 0; i < 10; ++i) block[i] = static_cast<float>(i + 1);
    EconomySetDemandSnapshot(block);
    float out[10] = {0};
    double last = req.economyLoadDemandSnapshot(out);
    CHECK_EQ(out[0], 1.0f);
    CHECK_EQ(out[9], 10.0f);
    CHECK(last == 10.0);

    // economyComputeWeightedLawScore: defined (a1/a2 ignored by the native loop).
    double law = req.economyComputeWeightedLawScore(0, 0);
    (void)law;

    // buildingTypeMapToActionCode: the opaque value IS the code byte. Group code 0
    // (the "none" row) maps to action code 0.
    CHECK_EQ(static_cast<int>(req.buildingTypeMapToActionCode(reinterpret_cast<const void*>(0))), 0);

    MissionReqEventGetHooks() = MissionReqEventHooks{};
}
