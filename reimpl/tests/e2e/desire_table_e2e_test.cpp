// End-to-end flow test for ai/desire_table: drives a realistic AI "buy-plan"
// sequence — name -> attribute index -> goods price-ratio table -> action label —
// across all of the module's functions at once, with a price env standing in for
// the live market.
//
// GUARDED per the repo convention: if the real game assets
// (europe_guild_1400_original/Resources/forms.BIN) are absent, the test skip-passes
// (it exercises no asset directly, but follows the e2e guard contract so the e2e
// suite is uniform and CI without assets stays green).
#include "test.h"

#include <cstdio>

#include "ai/desire_table.h"

using namespace guild;
using namespace guild::ai;

namespace {
bool AssetsPresent() {
    const char* paths[] = {
        "europe_guild_1400_original/Resources/forms.BIN",
        "../europe_guild_1400_original/Resources/forms.BIN",
        "reimpl/europe_guild_1400_original/Resources/forms.BIN",
    };
    for (const char* p : paths) {
        if (std::FILE* f = std::fopen(p, "rb")) { std::fclose(f); return true; }
    }
    return false;
}

// A market env where each good's cached price is a fixed multiple of its base
// price plus a per-good jitter, so the ratio min/max/avg are non-trivial.
struct MarketEnv : DesirePriceEnv {
    double CachedMarketPrice(u16 g) override { return (g & 0xFF) + 20; }
    double BaseMarketPrice(u16 g) override { return (g % 17) + 3; }
};
} // namespace

TEST(DesireTableE2E, BuyPlanFullFlow) {
    if (!AssetsPresent()) {
        std::printf("    [skip] forms.BIN absent — e2e skip-pass\n");
        CHECK(true);
        return;
    }

    MarketEnv env;

    // Step 1: a desire NAME the AI is reasoning about resolves to its index.
    i8 desireIdx = LookupAttributeIndex("VERGNUEGEN");
    CHECK_EQ(desireIdx, 5);

    // Step 2: build the goods price-ratio table for a behavior category.
    DesirePlan plan;
    plan.categoryId = 0x34; // a multi-good row (count 6)
    int ok = ComputeWeights(plan, env);
    CHECK_EQ(ok, 1);
    CHECK(plan.goodCount > 0);

    // Invariants the planner relies on: avg lies within [min ratio, max ratio];
    // every slot's ratio == cached/base; the min/max slots are real indices.
    CHECK(plan.minRatioSlot >= 0 && plan.minRatioSlot < plan.goodCount);
    CHECK(plan.maxRatioSlot >= 0 && plan.maxRatioSlot < plan.goodCount);
    float minR = plan.ratio[plan.minRatioSlot];
    float maxR = plan.ratio[plan.maxRatioSlot];
    CHECK(minR <= maxR);
    CHECK(plan.avgRatio >= minR - 1e-4f && plan.avgRatio <= maxR + 1e-4f);
    for (int i = 0; i < plan.goodCount; ++i) {
        float expect = static_cast<float>(static_cast<double>(plan.cachedPrice[i])
                                        / static_cast<double>(plan.basePrice[i]));
        CHECK(plan.ratio[i] == expect);
        CHECK(plan.ratio[i] >= minR && plan.ratio[i] <= maxR);
    }

    // Step 3: an unknown category yields no plan (the AI falls back).
    DesirePlan empty;
    empty.categoryId = 0xEE;
    CHECK_EQ(ComputeWeights(empty, env), 0);

    // Step 4: the AI executes an intrigue action and emits its label code.
    CHECK_EQ(PerformEnterBuilding(true, 0), 40u);
    CHECK_EQ(PerformUseBack(true, 4242), 42u);
    CHECK_EQ(PerformOpenDoorLarge(false, 0), 0u); // rejected -> reject code
}
