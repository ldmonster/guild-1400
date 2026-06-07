// End-to-end flow for the building-upgrade module (guild::sim) — gilde.exe.
//
// Drives a whole "player upgrades a building" cycle across the real sibling
// modules:
//   1. price the upgrade through the real worth model (Building_SumFlaggedSlots-
//      Worth -> ComputeMarketPrice) and the module's 0.3 cost factor,
//   2. dispatch the interaction (handler kind 4) -> capture the money-deduction
//      command,
//   3. apply the level-up math (type byte ++, condition recompute) and confirm
//      the building advances and stops at its max level on the next attempt,
//   4. drop the construction scaffold (HideUpgradeScaffold predicate).
// GUARDED on GUILD_GAME_DIR so the real-asset leg stays a clean skip; the math
// runs regardless of asset presence (it uses the in-binary tables we seed).
#include "test.h"

#include <cstdlib>

#include "sim/building.h"
#include "sim/building_stock.h"
#include "sim/building_types.h"
#include "sim/building_upgrade.h"

using namespace guild;
using namespace guild::sim;

namespace {

// Captures the upgrade money-deduction the command path enqueues.
struct E2ESink : IUpgradeSink {
    int charges = 0; int cost = 0; i32 payer = 0, target = 0; u8 mode = 0;
    void EnqueueUpgradeCharge(i32 p, i32 t, int c, u8 m) override {
        ++charges; cost = c; payer = p; target = t; mode = m;
    }
};

}  // namespace

TEST(SimBuildingUpgradeE2E, FullUpgradeCycle) {
    const bool haveAssets = std::getenv("GUILD_GAME_DIR") != nullptr;

    ResetBuildings();
    ResetStockHooks();
    ResetUpgradeModule();

    // --- seed a building type that has two upgrade levels available ----------
    g_buildingTypesLoaded = true;
    BuildingTypeDef& lvl0 = g_buildingTypes[20];
    lvl0.kind = 4;
    lvl0.security = 0;            // current level 0
    lvl0.maxUpgradeLevel = 2;    // can upgrade to level 1 then 2
    lvl0.roomWorthMul = 25;      // worth base 3840*25 = 96000

    BuildingTypeDef& lvl1 = g_buildingTypes[21];   // the next type record
    lvl1.kind = 4;
    lvl1.security = 1;
    lvl1.maxUpgradeLevel = 2;
    lvl1.roomWorthMul = 25;

    BuildingTypeDef& lvl2 = g_buildingTypes[22];   // top level
    lvl2.kind = 4;
    lvl2.security = 2;
    lvl2.maxUpgradeLevel = 2;
    lvl2.roomWorthMul = 25;

    // --- 1. price the upgrade through the real worth model -------------------
    int worth = Building_SumFlaggedSlotsWorth(20);
    CHECK_EQ(worth, 96000);
    int cost = Building_ComputeUpgradeCost(20);
    // trunc(96000 * 0.30000001192092896) = trunc(28800.0011..) = 28800.
    CHECK_EQ(cost, 28800);

    // --- 2. dispatch the interaction and capture the deduction --------------
    E2ESink sink; SetUpgradeSink(&sink);
    SetUpgradePriceMode(3);
    UpgradeInteraction in{};
    in.handlerKind = 4;
    in.buildingTypeId = 20;
    in.payerId = 7000;
    in.targetId = 8000;
    int rc = Interaction_PerformBuildingUpgrade(in);
    CHECK_EQ(rc, 17);
    CHECK_EQ(sink.charges, 1);
    CHECK_EQ(sink.cost, 28800);
    CHECK_EQ(sink.payer, 7000);
    CHECK_EQ(sink.target, 8000);
    CHECK_EQ(static_cast<int>(sink.mode), 3);

    // --- 3. apply the level-up math: type 20 -> 21, recompute condition ------
    // packed high byte 40 (0x28): cond = 40 + (100-40)/2 = 40 + 30 = 70.
    UpgradeApplyResult step1 = Building_ApplyUpgradeLevel(20, 0x28000000);
    CHECK(!step1.atMaxLevel);
    CHECK_EQ(static_cast<int>(step1.newTypeByte), 21);
    CHECK_EQ(static_cast<int>(step1.newCondition), 70);

    // From type 21 (security 1 < max 2) we can advance once more to type 22.
    // packed high byte 70 (0x46): cond = 70 + (100-70)/2 = 70 + 15 = 85.
    UpgradeApplyResult step2 = Building_ApplyUpgradeLevel(21, 0x46000000);
    CHECK(!step2.atMaxLevel);
    CHECK_EQ(static_cast<int>(step2.newTypeByte), 22);
    CHECK_EQ(static_cast<int>(step2.newCondition), 85);

    // At type 22 (security 2 == max 2) a further attempt hits the guard.
    UpgradeApplyResult step3 = Building_ApplyUpgradeLevel(22, 0x46000000);
    CHECK(step3.atMaxLevel);
    CHECK_EQ(static_cast<int>(step3.newTypeByte), 22);   // unchanged

    // --- 4. drop the scaffold once the upgrade is built ---------------------
    // A mid-construction state byte (1) needs the texture re-selection; the final
    // states (2/4) are no-ops.
    CHECK(Object_HideUpgradeScaffoldNeedsRetexture(1));
    CHECK(!Object_HideUpgradeScaffoldNeedsRetexture(2));
    CHECK(!Object_HideUpgradeScaffoldNeedsRetexture(4));

    if (!haveAssets) {
        std::printf("    [e2e] GUILD_GAME_DIR unset: real-asset reload leg skipped\n");
    }

    ResetUpgradeModule();
    ResetStockHooks();
    ResetBuildings();
}
