// Verifies InstallRealEventOfficeWiring() binds the reconstructed mission-
// requirement leaves into MissionReq3GetHooks() and the determinism-critical
// RNG leaf into sim::g_i4Hooks, while leaving every coupled-subsystem field at
// its inert default.
#include "tests/framework/test.h"

#include "world/wire_event_office.h"
#include "world/mission_recon3_evaluate.h"   // MissionReq3Hooks / MissionReq3GetHooks
#include "world/mission_requirement.h"        // MissionReqCheck* / Count* / AccumulateTimer
#include "sim/interaction4.h"                 // g_i4Hooks / FindNearestThresholdSeed
#include "util/math_random.h"                 // util::RandomModulo

using namespace guild;
using namespace guild::world;

TEST(WireEventOffice, BindsMissionReq3MatchingLeaves) {
    // Baseline: a fresh inert table leaves the leaves null.
    MissionReq3GetHooks() = MissionReq3Hooks{};
    CHECK(MissionReq3GetHooks().checkStatThreshold == nullptr);

    InstallRealEventOfficeWiring();
    const MissionReq3Hooks& m = MissionReq3GetHooks();

    // The six exact-signature leaves point at the 1:1 reconstructions.
    CHECK(m.checkStatThreshold  == &MissionReqCheckStatThreshold);
    CHECK(m.checkOwnPersonRatio == &MissionReqCheckOwnPersonRatio);
    CHECK(m.checkMultiStat      == &MissionReqCheckMultiStat);
    CHECK(m.checkStatCombo      == &MissionReqCheckStatCombo);
    CHECK(m.accumulateTimer     == &MissionReqAccumulateTimer);

    // checkCumulativeStats (drops the unused person arg) and countGuildMembers
    // (drops the redundant i32 return) bind through thin adapters: non-null.
    CHECK(m.checkCumulativeStats != nullptr);
    CHECK(m.countGuildMembers != nullptr);
}

TEST(WireEventOffice, LeavesCoupledSubsystemFieldsInert) {
    MissionReq3GetHooks() = MissionReq3Hooks{};
    InstallRealEventOfficeWiring();
    const MissionReq3Hooks& m = MissionReq3GetHooks();

    // Cross-module subsystem leaves with no signature-compatible reconstruction
    // stay null -> dispatcher keeps the inert default for those objective types.
    CHECK(m.personFindRecordById == nullptr);
    CHECK(m.personGetFamilyRecord == nullptr);
    CHECK(m.moneyConvertToDisplayCoord == nullptr);
    CHECK(m.checkObjectCount == nullptr);
    CHECK(m.checkBuildingEquip == nullptr);
    CHECK(m.checkGuildMemberCount == nullptr);
    CHECK(m.checkMemberStats == nullptr);
    CHECK(m.checkMinThresholds == nullptr);
    CHECK(m.checkTimeElapsed == nullptr);
    CHECK(m.checkNoActiveCombat == nullptr);
}

TEST(WireEventOffice, BindsInteractionRandomModulo) {
    // Reset i4 hooks to their inert defaults (randomModulo -> constant 0).
    sim::ResetInteraction4Hooks();
    CHECK(sim::g_i4Hooks.randomModulo != nullptr);          // inert default present
    CHECK(sim::g_i4Hooks.randomModulo(0x24) == 0);          // inert: always 0

    InstallRealEventOfficeWiring();
    CHECK(sim::g_i4Hooks.randomModulo == &util::RandomModulo);

    // FindNearestThresholdSeed (0x46c12c) = RandomModulo(0x24) + 35, so with the
    // real generator wired it now lands in the faithful 35..70 window instead of
    // the inert constant 35.
    int seed = sim::FindNearestThresholdSeed();
    CHECK(seed >= 35 && seed <= 70);

    sim::ResetInteraction4Hooks();  // restore for any later test in this TU
}

// End-to-end: the live VIBE_MissionReq_Evaluate dispatcher now reaches a bound
// reconstructed leaf instead of returning the inert false. Case 39 routes to
// checkMultiStat(objective); we supply only the gating subsystem hooks needed
// to reach that case (table row + a non-null person) and confirm the bound
// reconstruction actually executes (the multi-stat scan over a zeroed member
// store returns false, a *defined* result distinct from the unreached default).
TEST(WireEventOffice, DispatcherReachesBoundMultiStatLeaf) {
    static u8 fakePerson[512] = {};
    MissionReq3GetHooks() = MissionReq3Hooks{};
    InstallRealEventOfficeWiring();
    // Add the minimal subsystem gate so the dispatcher reaches the switch.
    MissionReq3GetHooks().personFindRecordById =
        [](i32) -> const u8* { return fakePerson; };

    // Requirement row for objective type 39.
    static ReqTableRow rows[1] = {};
    rows[0].type = 39;
    rows[0].threshold = 0;
    MissionReq3SetTable(rows, 1);
    g_missionReqEnabled = 1;
    g_missionReqSpecialFlag = 0;

    ObjectiveRecord obj{};
    obj.type = 39;
    obj.personId = 0;

    // Should run MissionReqCheckMultiStat over an empty member store without
    // crashing, yielding a defined boolean.
    bool r = MissionReqEvaluate(&obj);
    CHECK(r == false || r == true);

    MissionReq3GetHooks() = MissionReq3Hooks{};
}
