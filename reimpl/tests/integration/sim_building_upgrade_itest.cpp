// Integration tests for the building-upgrade module against its REAL siblings:
//   - building_stock::Building_SumFlaggedSlotsWorth (worth model)
//   - building_production::Building_ComputeMarketPrice (the flagged-room price)
//   - building.cpp building-type table (BuildingTypeDefAt / g_buildingTypes)
// We seed the real 589-stride type table and a real scene type def, then drive a
// full upgrade flow: price -> charge -> level-up math -> condition, asserting the
// cost equals exactly worth*0.3 computed through the real worth pipeline.
#include "test.h"

#include "sim/building.h"
#include "sim/building_stock.h"
#include "sim/building_production.h"
#include "sim/building_types.h"
#include "sim/building_upgrade.h"

using namespace guild;
using namespace guild::sim;

namespace {

// Sink that records the deduction the upgrade path enqueues.
struct ItestSink : IUpgradeSink {
    int charges = 0; int cost = 0; i32 payer = 0, target = 0;
    void EnqueueUpgradeCharge(i32 p, i32 t, int c, u8) override {
        ++charges; cost = c; payer = p; target = t;
    }
};

}  // namespace

// Drive the real worth pipeline with a FLAGGED room so the integration touches
// the real ComputeMarketPrice sibling, and confirm the upgrade cost == 0.3*worth
// recomputed independently through the real Building_SumFlaggedSlotsWorth.
TEST(SimBuildingUpgradeItest, UpgradeCostMatchesRealWorthPipeline) {
    ResetBuildings();
    ResetStockHooks();
    ResetUpgradeModule();

    g_buildingTypesLoaded = true;
    BuildingTypeDef& td = g_buildingTypes[12];
    td.kind = BuildingTypeKind::kProduction2;   // 12
    td.security = 1;
    td.maxUpgradeLevel = 5;
    td.roomWorthMul = 17;                        // base worth term 3840*17 = 65280
    // One flagged room (high bit set) of a kind whose scene type def is absent ->
    // ComputeMarketPrice returns 0 for it, so the flagged term contributes the
    // running base only.  This still exercises the REAL flagged-room branch.
    td.roomList[0] = static_cast<u16>(0x8000 | 0x0040);  // flagged, kind 64
    td.roomList[1] = 0;                          // terminator

    // The real worth, computed by the real sibling.
    int worth = Building_SumFlaggedSlotsWorth(12);
    CHECK_EQ(worth, 3840 * 17);                  // 65280 (market price 0 for kind 64)

    // The module's cost must be trunc(worth * 0.3) using the SAME worth.
    int expectedCost = static_cast<int>(static_cast<long long>(
        static_cast<double>(worth) * 0.30000001192092896));
    int cost = Building_ComputeUpgradeCost(12);
    CHECK_EQ(cost, expectedCost);

    // Now run the interaction path and confirm it charges that exact cost.
    ItestSink sink; SetUpgradeSink(&sink);
    UpgradeInteraction in{};
    in.handlerKind = 4;
    in.buildingTypeId = 12;
    in.payerId = 4242;
    in.targetId = 9999;
    int rc = Interaction_PerformBuildingUpgrade(in);
    CHECK_EQ(rc, 17);
    CHECK_EQ(sink.charges, 1);
    CHECK_EQ(sink.cost, cost);
    CHECK_EQ(sink.payer, 4242);
    CHECK_EQ(sink.target, 9999);

    ResetUpgradeModule();
    ResetStockHooks();
    ResetBuildings();
}

// Cross-check the level-up guard against the REAL type table: a type whose +583
// security equals its +584 max must report atMaxLevel and leave the record alone,
// while a type below max advances + recomputes condition.
TEST(SimBuildingUpgradeItest, LevelUpGuardAgainstRealTypeTable) {
    ResetBuildings();
    g_buildingTypesLoaded = true;

    // Type 3 at max (security 5 >= max 5).
    g_buildingTypes[3].kind = 4;
    g_buildingTypes[3].security = 5;
    g_buildingTypes[3].maxUpgradeLevel = 5;
    // BuildingTypeDefAt is the real accessor; confirm our reading matches it.
    const BuildingTypeDef* d3 = BuildingTypeDefAt(3);
    CHECK(d3 != nullptr);
    CHECK_EQ(static_cast<int>(d3->security), 5);
    UpgradeApplyResult atMax = Building_ApplyUpgradeLevel(3, 0x40000000);
    CHECK(atMax.atMaxLevel);
    CHECK_EQ(static_cast<int>(atMax.newTypeByte), 3);

    // Type 4 below max: security 2 < max 6 => advance to type 5, recompute cond.
    g_buildingTypes[4].kind = 4;
    g_buildingTypes[4].security = 2;
    g_buildingTypes[4].maxUpgradeLevel = 6;
    // packed high byte 60 (0x3C): cond = 60 + (100-60)/2 = 60 + 20 = 80.
    UpgradeApplyResult adv = Building_ApplyUpgradeLevel(4, 0x3C000000);
    CHECK(!adv.atMaxLevel);
    CHECK_EQ(static_cast<int>(adv.newTypeByte), 5);
    CHECK_EQ(static_cast<int>(adv.newCondition), 80);

    ResetBuildings();
}

// The unloaded-table guard: BuildingTypeDefAt returns null, both bytes read 0, so
// 0 >= 0 -> atMaxLevel (matching the original's null-base behaviour).
TEST(SimBuildingUpgradeItest, UnloadedTableReportsAtMax) {
    ResetBuildings();   // g_buildingTypesLoaded = false
    UpgradeApplyResult r = Building_ApplyUpgradeLevel(7, 0x10000000);
    CHECK(r.atMaxLevel);
    CHECK_EQ(static_cast<int>(r.newTypeByte), 7);
    // Cost over an unloaded table: worth is 0 -> cost 0.
    CHECK_EQ(Building_ComputeUpgradeCost(7), 0);
}
