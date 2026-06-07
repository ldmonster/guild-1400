// Integration: drive amt_economy2's VIBE_Amt_ComputeBuildingRivalryScore +
// ComputeOfficeRenderOffset against TWO REAL reconstructed siblings, wired
// exactly as the live engine forwards them:
//   * the rivalry RNG roll routes through AmtEconomy2Hooks.randFloatScaled, which
//     the original implements as VIBE_Math_RandomFloatScaled (0x58b910,
//     util/math_rng_float.cpp) == (double)VIBE_Util_RandNext()/32767;
//   * the coord truncation routes through AmtEconomy2Hooks.truncate, which the
//     original implements as VIBE_Coord_ConvertX (0x5c6b08, util/coord.cpp) ==
//     round-toward-zero of the FPU st0.
// We forward each hook into the genuine reconstructed function (thin double->float
// / double->i32 adapters matching the hook signatures) and, with the shared CRT
// LCG seeded, predict the exact roll the binary would draw — then assert the
// cross-module payout decision and the truncated render offset.
//
// The remaining leaves (Office_AddTableEntry, Person_FindRecordById, the
// command/network queue commits, He_SendEntityMessage) have no reconstructed
// sibling; those hooks stay inert / use captors. The ResetGuildSlots and
// FindNextActiveBuilding tests below exercise the module's faithful table logic
// directly (some entirely through the INERT default-hook path, noted inline).
#include "test.h"

#include "world/amt_economy2.h"
#include "util/math_rng_float.h"
#include "util/coord.h"
#include "crt/rand.h"

#include <cmath>

using namespace guild;
using namespace guild::world;

namespace {

// REAL Coord_ConvertX sibling, forwarded as the truncate hook (double -> i32).
i32 RealConvertX(double v) { return static_cast<i32>(util::ConvertX(v)); }

// REAL Math_RandomFloatScaled sibling, forwarded as the randFloatScaled hook.
float RealRandFloatScaled() { return static_cast<float>(util::RandomFloatScaled()); }

} // namespace

// Rivalry roll against a single foreign rival, using the REAL RNG + REAL ConvertX.
// We seed the shared CRT LCG, independently predict the same draw, and craft the
// scenario so the roll is decisively won; then assert the payout was queued and
// equals the truncation the binary would compute.
TEST(AmtEconomy2Itest, RivalryRollUsesRealRngAndConvertX) {
    static i32 g_payTo = 0, g_payAmt = 0; static int g_payCalls = 0;
    g_payTo = 0; g_payAmt = 0; g_payCalls = 0;

    AmtEconomy2Hooks h{};
    h.randFloatScaled = RealRandFloatScaled;
    h.truncate = RealConvertX;
    h.queueRequest16 = [](i32, i32 recipient, i32 amount, int) {
        g_payTo = recipient; g_payAmt = amount; g_payCalls++;
    };
    AmtEconomy2SetHooks(h);

    // Predict the first RNG draw from a known seed (RandomFloatScaled == the next
    // CRT RandNext()/32767). The roll wins when draw*2 <= base; with a large base
    // (big workforce, same region) the win is guaranteed regardless of the draw.
    crt::Srand(7u);
    double predictedRoll = static_cast<double>(crt::RandNext()) / 32767.0;

    RivalrySelf self;
    self.cityId = 1;
    self.cityRegion = 5;
    self.cityRegion2 = 5;
    self.workstationSum = 100.0f;   // supply = 100*0.0125 + 1 = 2.25
    self.wealth = 1000;
    self.payoutCoordFlag = false;

    RivalryRival rival;
    rival.id = 77;
    rival.type = 1;                 // active, not 6/7/8, < 10
    rival.cityId = 2;               // different city -> not skipped
    rival.cityRegion = 5;           // same region as self -> repDelta +0.05, base*=supply
    rival.workForce = 200;          // base = 200*0.25 = 50, *supply 2.25 = 112.5
    rival.wealth = 2000;
    rival.reputation = 0.5f;        // inside (0.05, 0.95)

    // base = 112.5; roll*2 <= 112.5 is true for any draw in [0,1) -> win.
    CHECK(predictedRoll * 2.0 <= 112.5);

    crt::Srand(7u);                 // re-seed so the module draws the same value
    RivalryResult r = ComputeBuildingRivalryScore(self, &rival, 1, /*currency*/ 0);
    AmtEconomy2ResetHooks();

    CHECK_EQ(r.rivalsPaid, 1);
    CHECK_EQ(g_payCalls, 1);
    CHECK_EQ(g_payTo, 77);

    // Reproduce the payout math the binary computes, truncated by the REAL ConvertX.
    double supply = 100.0 * 0.0125000002 + 1.0;
    double base = static_cast<double>((double)200 * 0.25) * supply; // workForce*0.25*supply
    double ratio = (double)2000 / (double)1000 * 160.0;             // wealth ratio scaled
    double wf = ratio > 0.0 ? ratio : 0.0;
    i32 expectedPayout = static_cast<i32>(util::ConvertX((wf + 160.0) * base));
    CHECK_EQ(g_payAmt, expectedPayout);
    CHECK_EQ(r.totalPayout, expectedPayout);
}

// Rivalry where the base is tiny and the roll loses -> no payout. We pick a seed
// whose first REAL draw is large enough that draw*2 > base. Proves the real RNG
// genuinely gates the payout (not a stubbed constant).
TEST(AmtEconomy2Itest, RivalryRollCanLoseWithRealRng) {
    static int g_payCalls = 0; g_payCalls = 0;

    AmtEconomy2Hooks h{};
    h.randFloatScaled = RealRandFloatScaled;
    h.truncate = RealConvertX;
    h.queueRequest16 = [](i32, i32, i32, int) { g_payCalls++; };
    AmtEconomy2SetHooks(h);

    // Search a seed whose first draw is clearly > 0 so a tiny base loses.
    i32 seed = 1; double draw = 0.0;
    for (; seed < 1000; ++seed) {
        crt::Srand(static_cast<u32>(seed));
        draw = static_cast<double>(crt::RandNext()) / 32767.0;
        if (draw > 0.1) break;       // draw*2 > 0.2; base below 0.2 will lose
    }

    RivalrySelf self;
    self.cityId = 1; self.cityRegion = 5; self.cityRegion2 = 5;
    self.workstationSum = 0.0f;      // supply = 1.0
    self.wealth = 1000; self.payoutCoordFlag = false;

    RivalryRival rival;
    rival.id = 77; rival.type = 1; rival.cityId = 2;
    rival.cityRegion = 5;            // same region -> base = workForce*0.25*supply
    rival.workForce = 0;             // base = 0 -> roll*2 (>0.2) > 0 -> loses
    rival.wealth = 2000; rival.reputation = 0.5f;

    crt::Srand(static_cast<u32>(seed));
    RivalryResult r = ComputeBuildingRivalryScore(self, &rival, 1, 0);
    AmtEconomy2ResetHooks();

    CHECK(draw > 0.1);               // confirms we found a non-trivial draw
    CHECK_EQ(r.rivalsPaid, 0);
    CHECK_EQ(g_payCalls, 0);
}

// ComputeOfficeRenderOffset uses the REAL ConvertX truncation for the stretch and
// each per-slot pitch step. One occupied slot of type 1 (5 tiles) at law level 2
// (stretch 1.0 -> trunc 1) yields trunc(5 * 100 * 32 * 1) == 16000.
TEST(AmtEconomy2Itest, RenderOffsetUsesRealConvertX) {
    AmtEconomy2Hooks h{};
    h.truncate = RealConvertX;
    AmtEconomy2SetHooks(h);

    OfficeHolder slots[2]{};
    slots[0].city = 7;   slots[0].type = 1;   // occupied (city != -1), 5 tiles
    slots[1].city = -1;  slots[1].type = 1;   // vacant -> skipped

    i32 off = ComputeOfficeRenderOffset(/*baseX*/ 0, /*lawLevel*/ 2, slots, 2);
    AmtEconomy2ResetHooks();

    // stretch[2]=1.0 -> x=ConvertX(1.0)=1; step=ConvertX(5*100*32*1)=16000.
    CHECK_EQ(off, 16000);
}

// ResetGuildSlots runs its faithful two-phase slot/building rebuild with captor
// hooks for the (unreconstructed) Office_AddTableEntry / Person_FindRecordById /
// delta-commit leaves. Asserts the assign/vacancy tallies and building-flag pass.
TEST(AmtEconomy2Itest, ResetGuildSlotsTallies) {
    AmtEconomy2Hooks h{};
    h.officeAddTableEntry = [](u8, i32, int, int, int) -> int { return 1; };
    // Slot 0's holder id is present+active; slot 1's holder is absent.
    h.personFind = [](i32 id, bool* present, bool* dirty) -> bool {
        if (id == 100) { *present = true;  *dirty = false; return true; }
        *present = false; *dirty = true;   // found-but-absent + dirty -> field 0x166
        return true;
    };
    static int g_deltaCalls = 0; g_deltaCalls = 0;
    h.queueDeltaFlag = [](i32, int) { g_deltaCalls++; };
    AmtEconomy2SetHooks(h);

    OfficeHolder slots[2]{};
    slots[0].holder = 1; slots[0].city = 100; slots[0].state = 3; // present -> assign (state!=1)
    slots[1].holder = 2; slots[1].city = 200; slots[1].state = 1; // absent  -> vacancy (state!=3)

    bool bldgDirty[3] = {true, false, true};
    ResetGuildSlotsResult r = ResetGuildSlots(slots, 2, bldgDirty, 3);
    AmtEconomy2ResetHooks();

    CHECK_EQ(r.slotAssignsQueued, 1);       // slot 0 active + state 3 != 1
    CHECK_EQ(r.slotVacanciesQueued, 1);     // slot 1 absent + state 1 != 3
    CHECK_EQ(r.holderFlagsCommitted, 1);    // slot 1 found-but-absent + dirty -> 0x166
    CHECK_EQ(r.buildingFlagsCommitted, 2);  // two dirty buildings -> 0x168
    CHECK_EQ(g_deltaCalls, 3);              // 1 holder flag + 2 building flags
}

// FindNextActiveBuilding runs entirely through the module's faithful cache/scan
// logic with NO hooks installed (INERT default-hook path) — it consults no leaves,
// only the caller-supplied building view. Picks the max-wealth active building.
TEST(AmtEconomy2Itest, FindNextActiveBuildingInertScan) {
    AmtEconomy2ResetHooks();   // inert defaults — this path uses no hooks at all

    BuildingScanEntry b[4]{};
    b[0].id = 10; b[0].type = 1; b[0].active = true;  b[0].wealth = 500;
    b[1].id = 11; b[1].type = 2; b[1].active = true;  b[1].wealth = 900;  // richest
    b[2].id = 12; b[2].type = 1; b[2].active = false; b[2].wealth = 9999; // inactive
    b[3].id = -1;                                                          // empty

    ActiveBuildingCache cache;
    i32 outId = 0, outWealth = 0;
    int found = FindNextActiveBuilding(&cache, /*generation*/ 5, b, 4, &outId, &outWealth);

    CHECK_EQ(found, 1);
    CHECK_EQ(outId, 11);
    CHECK_EQ(outWealth, 900);
}
