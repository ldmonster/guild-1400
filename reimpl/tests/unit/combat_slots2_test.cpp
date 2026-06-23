// Unit tests for combat batch-2 leaves (gilde.exe VIBE_Combat_*):
//   AccumulateThreatStats (0x57eb64), FindNearestWareObject (0x48b4d4),
//   FindNearestConquerTarget (0x48b5d8), retreat morale gate (0x49080c).
// Golden vectors computed with python3 (round-half-to-even tiers, weighted scores).
#include "sim/combat_slots2.h"
#include "test.h"

#include <cmath>

using namespace guild::sim;

namespace {
bool Near(double a, double b, double eps = 1e-6) { return std::fabs(a - b) <= eps; }
}

// --- RoundTier: TRUNCATE toward zero of clamp(cash * 0.1f, 0..7) ----------
// The conversion is VIBE_Coord_ConvertX @0x5c6b08 (decompiled wave-15: sets x87
// RC=11 = round-toward-ZERO, frndint) at the 0x57eb64 call site `ConvertX(); v13 =
// (int)v12`, so the tier TRUNCATES — NOT round-to-nearest (the earlier golden was
// wrong; corrected to the binary). scale flt_625994 == 0.1f.
TEST(CombatSlots2, RoundTierClampAndRound) {
    CHECK_EQ(RoundTier(0), 0);
    CHECK_EQ(RoundTier(-5), 0);      // negative clamps to 0
    CHECK_EQ(RoundTier(5), 0);       // 5*0.1f ~= 0.5000000074 -> trunc 0
    CHECK_EQ(RoundTier(14), 1);      // 1.40... -> 1
    CHECK_EQ(RoundTier(15), 1);      // 1.50... -> trunc 1
    CHECK_EQ(RoundTier(24), 2);      // 2.40... -> 2
    CHECK_EQ(RoundTier(25), 2);      // 2.50... -> trunc 2
    CHECK_EQ(RoundTier(35), 3);      // 3.50... -> trunc 3
    CHECK_EQ(RoundTier(45), 4);      // 4.50... -> trunc 4
    CHECK_EQ(RoundTier(55), 5);      // 5.50... -> trunc 5
    CHECK_EQ(RoundTier(65), 6);      // 6.50... -> trunc 6
    CHECK_EQ(RoundTier(70), 7);      // 7.0 -> 7 (ceiling)
    CHECK_EQ(RoundTier(1000), 7);    // clamps to ceiling 7
}

// --- AccumulateThreatStats golden vector ----------------------------------
TEST(CombatSlots2, AccumulateThreatStatsGolden) {
    std::vector<ThreatSlot> slots = {
        // eligible, cash, fillCount, requiredCap, currentOutput, maxOutput
        {true,  50.0,  3, 5.0f, 2.0, 4.0},   // tier 5, underfull (3 < 5)
        {true,  50.0,  6, 5.0f, 1.0, 2.0},   // tier 5, NOT underfull (6 >= 5)
        {true,  140.0, 1, 3.0f, 3.0, 6.0},   // tier 7, underfull (1 < 3)
        {false, 999.0, 0, 0.0f, 9.0, 9.0},   // ineligible -> skipped entirely
        {true,  0.0,   0, 1.0f, 1.0, 5.0},   // tier 0, underfull (0 < 1)
    };
    ThreatStats st = AccumulateThreatStats(slots);

    CHECK_EQ(st.totalActive, 4);
    CHECK_EQ(st.tierCount[0], 1);
    CHECK_EQ(st.tierCount[5], 2);
    CHECK_EQ(st.tierCount[7], 1);
    CHECK_EQ(st.tierCount[1], 0);

    // underfull: 3 of 4 -> 75.0
    CHECK(Near(st.underfullCount, 3.0));
    CHECK(Near(st.underfullPct, 75.0));

    // Per-tier sums.
    CHECK(Near(st.curOutSum[5], 3.0));   // 2.0 + 1.0
    CHECK(Near(st.maxOutSum[5], 6.0));   // 4.0 + 2.0
    CHECK(Near(st.curOutSum[7], 3.0));
    CHECK(Near(st.maxOutSum[7], 6.0));
    CHECK(Near(st.curOutSum[0], 1.0));
    CHECK(Near(st.maxOutSum[0], 5.0));

    // Normalized output percentages and averages (finite tiers only).
    CHECK(Near(st.outputPct[0], 20.0));  // 1/5 * 100
    CHECK(Near(st.avgMaxOut[0], 5.0));   // 5 / 1
    CHECK(Near(st.outputPct[5], 50.0));  // 3/6 * 100
    CHECK(Near(st.avgMaxOut[5], 3.0));   // 6 / 2
    CHECK(Near(st.outputPct[7], 50.0));  // 3/6 * 100
    CHECK(Near(st.avgMaxOut[7], 6.0));   // 6 / 1

    // Empty tiers divide 0/0 -> NaN (preserving the original's unconditional div).
    CHECK(std::isnan(st.outputPct[1]));
    CHECK(std::isnan(st.avgMaxOut[1]));
}

// Single ineligible slot -> totalActive 0, underfullPct is NaN (0/0).
TEST(CombatSlots2, AccumulateThreatStatsEmpty) {
    std::vector<ThreatSlot> slots = {{false, 100.0, 0, 0.0f, 1.0, 1.0}};
    ThreatStats st = AccumulateThreatStats(slots);
    CHECK_EQ(st.totalActive, 0);
    CHECK(std::isnan(st.underfullPct));
}

// --- FindNearestWareObject: nearest by raw 3D distance --------------------
TEST(CombatSlots2, FindNearestWareObjectPicksClosest) {
    int a = 1, b = 2, c = 3;
    std::vector<GatheredObject> g;
    GatheredObject o1; o1.pos[0] = 10; o1.identity = &a;
    GatheredObject o2; o2.pos[0] = 3;  o2.identity = &b;   // closest
    GatheredObject o3; o3.pos[0] = 7;  o3.identity = &c;
    g = {o1, o2, o3};
    float origin[3] = {0, 0, 0};
    CHECK_EQ(FindNearestWareObject(g, origin), (const void*)&b);
}

TEST(CombatSlots2, FindNearestWareObjectEmpty) {
    std::vector<GatheredObject> g;
    float origin[3] = {0, 0, 0};
    CHECK_EQ(FindNearestWareObject(g, origin), (const void*)nullptr);
}

// --- FindNearestConquerTarget: relation-weighted distance -----------------
TEST(CombatSlots2, ConquerTargetWeights) {
    int enemy = 1, ally = 2, self = 3, unowned = 4;
    float origin[3] = {0, 0, 0};
    int myTeam = 1;

    auto mk = [](float x, bool ho, bool isSelf, int team, const void* id) {
        GatheredObject o;
        o.pos[0] = x; o.hasOwner = ho; o.ownerIsSelf = isSelf;
        o.ownerTeam = team; o.identity = id;
        return o;
    };
    // enemy: dist 10 * 10 = 100; ally: dist 3 * 15 = 45;
    // self: dist 1 * 25 = 25; unowned: dist 4 * 1 = 4 (WINS).
    std::vector<GatheredObject> g = {
        mk(10, true, false, 2, &enemy),
        mk(3,  true, false, 1, &ally),
        mk(1,  true, true,  1, &self),
        mk(4,  false, false, 0, &unowned),
    };
    CHECK_EQ(FindNearestConquerTarget(g, origin, myTeam), (const void*)&unowned);
}

// Self-owned winner -> the original rejects it (returns nullptr).
TEST(CombatSlots2, ConquerTargetSelfOwnedRejected) {
    int self = 3;
    float origin[3] = {0, 0, 0};
    GatheredObject o;
    o.pos[0] = 1; o.hasOwner = true; o.ownerIsSelf = true; o.ownerTeam = 1;
    o.identity = &self;
    std::vector<GatheredObject> g = {o};
    CHECK_EQ(FindNearestConquerTarget(g, origin, 1), (const void*)nullptr);
}

// Enemy-owned wins when nearer than an ally-owned tie-break.
TEST(CombatSlots2, ConquerTargetEnemyVsAlly) {
    int enemy = 1, ally = 2;
    float origin[3] = {0, 0, 0};
    GatheredObject e; e.pos[0] = 1; e.hasOwner = true; e.ownerTeam = 2; e.identity = &enemy; // 1*10=10
    GatheredObject a; a.pos[0] = 1; a.hasOwner = true; a.ownerTeam = 1; a.identity = &ally;  // 1*15=15
    std::vector<GatheredObject> g = {a, e};
    CHECK_EQ(FindNearestConquerTarget(g, origin, 1), (const void*)&enemy);
}

// --- Retreat morale gate ---------------------------------------------------
TEST(CombatSlots2, CountFightingUnits) {
    // state: 0 dead, 4 retreating -> not counted; 1/2/3/5/6 fighting.
    std::vector<guild::u8> states = {0, 1, 4, 2, 3, 0, 5};
    CHECK_EQ(CountFightingUnits(states), 4);   // 1,2,3,5
}

TEST(CombatSlots2, ShouldPressAttackRatio) {
    // foe/friend > 0.5 -> press regardless of rng.
    CHECK(ShouldPressAttack(/*friend*/4, /*foe*/3, /*rng*/99));   // 0.75 > 0.5
    CHECK(ShouldPressAttack(2, 2, 99));                           // 1.0 > 0.5
    // foe/friend <= 0.5 -> falls to rng: <= 20 presses, > 20 retreats.
    CHECK(!ShouldPressAttack(10, 4, 21));                         // 0.4, rng 21 -> retreat
    CHECK(ShouldPressAttack(10, 4, 20));                          // rng 20 -> press
    CHECK(ShouldPressAttack(10, 4, 0));
    CHECK(!ShouldPressAttack(10, 1, 50));                         // 0.1, rng 50 -> retreat
    // exactly 0.5 is NOT > 0.5 -> rng decides.
    CHECK(!ShouldPressAttack(4, 2, 30));                          // 0.5 not > 0.5, rng 30
    CHECK(ShouldPressAttack(4, 2, 10));
}
