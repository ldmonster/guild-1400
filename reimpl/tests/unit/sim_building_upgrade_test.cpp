// Unit (golden-vector) tests for the building-upgrade application module.
// Covers: the level-up math (ExGebUpgrade core), the scaffold predicate, the
// shared upgrade-cost pricing, and the interaction/AI dispatch result codes.
#include "test.h"

#include "sim/building.h"           // g_buildingTypes, BuildingTypeDefAt, ResetBuildings
#include "sim/building_stock.h"     // ResetStockHooks
#include "sim/building_upgrade.h"

using namespace guild;
using namespace guild::sim;

namespace {

// Seed a type def with a worth that has NO flagged rooms, so the worth is purely
// 3840 * roomWorthMul (independent of the market-price scene table).  Returns the
// deterministic worth.
int SeedTypeForCost(u8 typeIndex, i32 roomWorthMul, u8 security, u8 maxLevel) {
    ResetBuildings();
    g_buildingTypesLoaded = true;
    BuildingTypeDef& td = g_buildingTypes[typeIndex];
    td.kind = 4;
    td.security = security;
    td.maxUpgradeLevel = maxLevel;
    td.roomWorthMul = roomWorthMul;
    // roomList left all-zero => no flagged rooms => worth == 3840 * mul.
    return 3840 * roomWorthMul;
}

}  // namespace

// --- scaffold predicate -----------------------------------------------------
TEST(SimBuildingUpgrade, ScaffoldFinalStatesSkipRetexture) {
    // VIBE_Object_HideUpgradeScaffold: state 2 or 4 => no-op (no retexture).
    CHECK(!Object_HideUpgradeScaffoldNeedsRetexture(2));
    CHECK(!Object_HideUpgradeScaffoldNeedsRetexture(4));
    // Any other state => the texture set is re-selected.
    CHECK(Object_HideUpgradeScaffoldNeedsRetexture(0));
    CHECK(Object_HideUpgradeScaffoldNeedsRetexture(1));
    CHECK(Object_HideUpgradeScaffoldNeedsRetexture(3));
    CHECK(Object_HideUpgradeScaffoldNeedsRetexture(255));
}

// --- level-up guard + math --------------------------------------------------
TEST(SimBuildingUpgrade, ApplyLevelGuardAtMax) {
    ResetBuildings();
    g_buildingTypesLoaded = true;
    // security(+583) >= maxUpgradeLevel(+584) => "already highest level".
    g_buildingTypes[7].security = 3;
    g_buildingTypes[7].maxUpgradeLevel = 3;
    UpgradeApplyResult r = Building_ApplyUpgradeLevel(7, 0x05000000);
    CHECK(r.atMaxLevel);
    CHECK_EQ(static_cast<int>(r.newTypeByte), 7);   // unchanged
}

TEST(SimBuildingUpgrade, ApplyLevelBelowMaxAdvances) {
    ResetBuildings();
    g_buildingTypesLoaded = true;
    g_buildingTypes[7].security = 1;
    g_buildingTypes[7].maxUpgradeLevel = 3;
    // packed = 0x32000000 => high byte = 0x32 = 50.
    //   HIBYTE = 50; level = (0x32000000 >> 24) = 50 (positive).
    //   cond = 50 + (100 - 50)/2 = 50 + 25 = 75.
    UpgradeApplyResult r = Building_ApplyUpgradeLevel(7, 0x32000000);
    CHECK(!r.atMaxLevel);
    CHECK_EQ(static_cast<int>(r.newTypeByte), 8);    // ++ type
    CHECK_EQ(static_cast<int>(r.newCondition), 75);
}

TEST(SimBuildingUpgrade, ApplyLevelConditionGoldenVectors) {
    ResetBuildings();
    g_buildingTypesLoaded = true;
    g_buildingTypes[2].security = 0;
    g_buildingTypes[2].maxUpgradeLevel = 9;
    // packed high byte 0 => cond = 0 + (100-0)/2 = 50.
    CHECK_EQ(static_cast<int>(Building_ApplyUpgradeLevel(2, 0x00000000).newCondition), 50);
    // high byte 100 (0x64) => 100 + (100-100)/2 = 100.
    CHECK_EQ(static_cast<int>(Building_ApplyUpgradeLevel(2, 0x64000000).newCondition), 100);
    // high byte 80 (0x50) => 80 + (100-80)/2 = 80 + 10 = 90.
    CHECK_EQ(static_cast<int>(Building_ApplyUpgradeLevel(2, 0x50000000).newCondition), 90);
    // odd half: high byte 11 (0x0B) => 11 + (100-11)/2 = 11 + 44 = 55 (int /).
    CHECK_EQ(static_cast<int>(Building_ApplyUpgradeLevel(2, 0x0B000000).newCondition), 55);
}

TEST(SimBuildingUpgrade, ApplyLevelSignedShiftHighByte) {
    ResetBuildings();
    g_buildingTypesLoaded = true;
    g_buildingTypes[2].security = 0;
    g_buildingTypes[2].maxUpgradeLevel = 9;
    // High byte 0x80 (128): HIBYTE = 128 (unsigned). The (packed >> 24) arithmetic
    // shift of 0x80000000 (negative) yields -128. cond = 128 + (100 - (-128))/2
    //   = 128 + 228/2 = 128 + 114 = 242 (fits a byte: 0xF2).
    UpgradeApplyResult r = Building_ApplyUpgradeLevel(2, static_cast<int>(0x80000000));
    CHECK_EQ(static_cast<int>(r.newCondition), 242);
}

// --- upgrade cost -----------------------------------------------------------
TEST(SimBuildingUpgrade, ComputeUpgradeCostGolden) {
    ResetStockHooks();
    // worth = 3840 * 50 = 192000. cost = trunc(192000 * 0.3) = trunc(57600.00..) .
    SeedTypeForCost(5, 50, 0, 9);
    int cost = Building_ComputeUpgradeCost(5);
    // 0.3f == 0.30000001192092896; 192000*that = 57600.002288... -> trunc 57600.
    CHECK_EQ(cost, 57600);
}

TEST(SimBuildingUpgrade, ComputeUpgradeCostZeroWorth) {
    ResetStockHooks();
    SeedTypeForCost(5, 0, 0, 9);   // worth 0 => cost 0
    CHECK_EQ(Building_ComputeUpgradeCost(5), 0);
}

// --- interaction dispatch ---------------------------------------------------
namespace {
struct RecordingSink : IUpgradeSink {
    int charges = 0, buys = 0;
    i32 lastPayer = 0, lastTarget = 0; int lastCost = 0; u8 lastMode = 0;
    i32 buyBuilding = 0, buySeller = 0, buyBuyer = 0;
    void EnqueueUpgradeCharge(i32 p, i32 t, int c, u8 m) override {
        ++charges; lastPayer = p; lastTarget = t; lastCost = c; lastMode = m;
    }
    void EnqueueBuyBuilding(i32 b, i32 s, i32 buyer) override {
        ++buys; buyBuilding = b; buySeller = s; buyBuyer = buyer;
    }
};
UpgradeInteraction MakeInteraction(u8 hk, u8 objKind, u8 typeIndex) {
    UpgradeInteraction in{};
    in.handlerKind = hk;
    in.targetObjKind = objKind;
    in.buildingTypeId = typeIndex;
    in.payerId = 0x1111;
    in.targetId = 0x2222;
    in.secondaryId = 0x3333;
    return in;
}
}  // namespace

TEST(SimBuildingUpgrade, PerformUpgradeHandlerKind4Charges) {
    ResetStockHooks();
    ResetUpgradeModule();
    SeedTypeForCost(5, 50, 0, 9);     // worth 192000 -> cost 57600
    RecordingSink sink; SetUpgradeSink(&sink);
    SetUpgradePriceMode(7);

    int rc = Interaction_PerformBuildingUpgrade(MakeInteraction(4, 0, 5));
    CHECK_EQ(rc, 17);
    CHECK_EQ(sink.charges, 1);
    CHECK_EQ(sink.lastCost, 57600);
    CHECK_EQ(sink.lastPayer, 0x1111);
    CHECK_EQ(sink.lastTarget, 0x2222);
    CHECK_EQ(static_cast<int>(sink.lastMode), 7);
    ResetUpgradeModule();
}

TEST(SimBuildingUpgrade, PerformUpgradeOtherKindNotHandled) {
    ResetStockHooks();
    ResetUpgradeModule();
    RecordingSink sink; SetUpgradeSink(&sink);
    int rc = Interaction_PerformBuildingUpgrade(MakeInteraction(2, 0, 5));
    CHECK_EQ(rc, 0);
    CHECK_EQ(sink.charges, 0);
    ResetUpgradeModule();
}

TEST(SimBuildingUpgrade, PerformUpgradeOnObjectBuyBranch) {
    ResetStockHooks();
    ResetUpgradeModule();
    RecordingSink sink; SetUpgradeSink(&sink);
    // handlerKind 4 && objKind 8 => BUY; handlerFound true => fires.
    int rc = Interaction_PerformBuildingUpgradeOnObject(MakeInteraction(4, 8, 5), true);
    CHECK_EQ(rc, 19);
    CHECK_EQ(sink.buys, 1);
    CHECK_EQ(sink.charges, 0);
    // handler not found => 0.
    sink = RecordingSink{};
    rc = Interaction_PerformBuildingUpgradeOnObject(MakeInteraction(4, 8, 5), false);
    CHECK_EQ(rc, 0);
    CHECK_EQ(sink.buys, 0);
    ResetUpgradeModule();
}

TEST(SimBuildingUpgrade, PerformUpgradeOnObjectUpgradeBranchPaysMinusOne) {
    ResetStockHooks();
    ResetUpgradeModule();
    SeedTypeForCost(5, 50, 0, 9);     // cost 57600
    RecordingSink sink; SetUpgradeSink(&sink);
    // objKind 4 (not buy) => upgrade branch; payer is always -1 here.
    int rc = Interaction_PerformBuildingUpgradeOnObject(MakeInteraction(0, 4, 5), false);
    CHECK_EQ(rc, 19);
    CHECK_EQ(sink.charges, 1);
    CHECK_EQ(sink.lastPayer, -1);
    CHECK_EQ(sink.lastTarget, 0x3333);   // secondaryId (*(a3+4))
    CHECK_EQ(sink.lastCost, 57600);
    ResetUpgradeModule();
}

TEST(SimBuildingUpgrade, PerformUpgradeOnObjectFallbackNotHandled) {
    ResetStockHooks();
    ResetUpgradeModule();
    RecordingSink sink; SetUpgradeSink(&sink);
    int rc = Interaction_PerformBuildingUpgradeOnObject(MakeInteraction(0, 0, 5), false);
    CHECK_EQ(rc, 0);
    CHECK_EQ(sink.charges, 0);
    CHECK_EQ(sink.buys, 0);
    ResetUpgradeModule();
}

// --- AI upgrade gate --------------------------------------------------------
TEST(SimBuildingUpgrade, NpcUpgradeTownHallGateAndCode) {
    ResetStockHooks();
    ResetUpgradeModule();
    SeedTypeForCost(5, 50, 0, 9);
    RecordingSink sink; SetUpgradeSink(&sink);

    UpgradeInteraction in = MakeInteraction(4, 0, 5);
    // Gate passes: office storage present, state 15, handlerKind==4.
    CHECK_EQ(NpcAction_UpgradeTownHall(in, true, 15, false), 49);
    CHECK_EQ(sink.charges, 1);
    CHECK_EQ(sink.lastCost, 57600);

    // No office storage => 0.
    sink = RecordingSink{};
    CHECK_EQ(NpcAction_UpgradeTownHall(in, false, 15, false), 0);
    CHECK_EQ(sink.charges, 0);

    // Wrong state byte => 0.
    CHECK_EQ(NpcAction_UpgradeTownHall(in, true, 14, false), 0);

    // handlerKind != 4 AND secondary not empty => 0.
    UpgradeInteraction in2 = MakeInteraction(2, 0, 5);
    CHECK_EQ(NpcAction_UpgradeTownHall(in2, true, 15, false), 0);
    // handlerKind != 4 BUT secondary empty => passes.
    sink = RecordingSink{};
    CHECK_EQ(NpcAction_UpgradeTownHall(in2, true, 15, true), 49);
    CHECK_EQ(sink.charges, 1);
    ResetUpgradeModule();
}

TEST(SimBuildingUpgrade, NpcUpgradeDungeonReturnsSixty) {
    ResetStockHooks();
    ResetUpgradeModule();
    SeedTypeForCost(5, 50, 0, 9);
    RecordingSink sink; SetUpgradeSink(&sink);
    UpgradeInteraction in = MakeInteraction(4, 0, 5);
    CHECK_EQ(NpcAction_UpgradeDungeon(in, true, 15, false), 60);
    CHECK_EQ(sink.charges, 1);
    ResetUpgradeModule();
}
