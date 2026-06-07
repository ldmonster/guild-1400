// End-to-end flow for the combat batch-2 leaves: a small squad evaluates its
// situation, decides whether to press the attack or retreat, then a fighting unit
// picks its conquer target / nearest ware via the weighted scorers — exercising
// AccumulateThreatStats, CountFightingUnits, ShouldPressAttack,
// FindNearestConquerTarget and FindNearestWareObject together.
#include "sim/combat_slots2.h"
#include "test.h"

#include <cmath>

using namespace guild::sim;

namespace {
bool Near(double a, double b, double eps = 1e-6) { return std::fabs(a - b) <= eps; }
}

// A platoon resolves: count fighters, decide morale, then route the strongest unit
// to its objective. The whole chain must be deterministic.
TEST(CombatSlots2E2E, ThreatThenTargetFlow) {
    // 1) Threat/strength report over the squad's person slots.
    std::vector<ThreatSlot> slots = {
        {true,  60.0, 4, 3.0f, 8.0, 10.0},   // tier 6, healthy
        {true,  60.0, 2, 3.0f, 2.0, 10.0},   // tier 6, underfull, low output
        {true,  30.0, 5, 4.0f, 5.0, 5.0},    // tier 3, full
        {false, 0.0,  0, 0.0f, 0.0, 0.0},    // empty
    };
    ThreatStats st = AccumulateThreatStats(slots);
    CHECK_EQ(st.totalActive, 3);
    CHECK_EQ(st.tierCount[6], 2);
    CHECK_EQ(st.tierCount[3], 1);
    CHECK(Near(st.underfullCount, 1.0));               // only the 2<3 slot
    CHECK(Near(st.underfullPct, 100.0 / 3.0));
    CHECK(Near(st.outputPct[6], (8.0 + 2.0) / (10.0 + 10.0) * 100.0));  // 50%
    CHECK(Near(st.avgMaxOut[6], 10.0));

    // 2) Morale: our 3 fighters vs. the enemy's 2 -> foe/friend = 2/3 > 0.5.
    std::vector<guild::u8> myStates  = {1, 2, 6};   // 3 fighting
    std::vector<guild::u8> foeStates = {2, 0, 3, 4}; // 2 fighting (0 dead, 4 retreating)
    int friendCount = CountFightingUnits(myStates);
    int foeCount    = CountFightingUnits(foeStates);
    CHECK_EQ(friendCount, 3);
    CHECK_EQ(foeCount, 2);
    bool press = ShouldPressAttack(friendCount, foeCount, /*rng*/99);
    CHECK(press);   // 2/3 ~ 0.667 > 0.5 -> press the attack

    // 3) Pressing the attack: the lead unit picks a conquer target. Two candidate
    //    objects in the world: an enemy-held one (close) and a friendly one (far).
    int enemyObj = 0xE, friendObj = 0xF;
    float origin[3] = {0, 0, 0};
    int myTeam = 7;

    GatheredObject eo;  // enemy-owned, dist 5 -> score 50
    eo.pos[0] = 5; eo.hasOwner = true; eo.ownerTeam = 9; eo.identity = &enemyObj;
    GatheredObject fo;  // ally-owned, dist 2 -> score 30
    fo.pos[0] = 2; fo.hasOwner = true; fo.ownerTeam = myTeam; fo.identity = &friendObj;
    std::vector<GatheredObject> targets = {eo, fo};
    // ally score 2*15=30 < enemy 5*10=50 -> friendly objective is "nearest".
    CHECK_EQ(FindNearestConquerTarget(targets, origin, myTeam), (const void*)&friendObj);

    // 4) Same unit also scans for the nearest ware drop (raw distance, no weights).
    int wareA = 0xA, wareB = 0xB;
    GatheredObject wa; wa.pos[0] = 12; wa.identity = &wareA;
    GatheredObject wb; wb.pos[2] = 4;  wb.identity = &wareB;   // dist 4 (closest)
    std::vector<GatheredObject> wares = {wa, wb};
    CHECK_EQ(FindNearestWareObject(wares, origin), (const void*)&wareB);
}

// The mirror situation: heavily outnumbering the enemy (tiny foe/friend) with an
// unlucky roll -> the side falls back instead of pressing.
TEST(CombatSlots2E2E, OutnumberingEnemyMayRetreat) {
    std::vector<guild::u8> myStates  = {1, 2, 3, 5, 6, 1, 2, 3, 5, 6};  // 10 fighting
    std::vector<guild::u8> foeStates = {1, 0, 4, 4};                    // 1 fighting
    int friendCount = CountFightingUnits(myStates);
    int foeCount    = CountFightingUnits(foeStates);
    CHECK_EQ(friendCount, 10);
    CHECK_EQ(foeCount, 1);
    // foe/friend = 0.1, not > 0.5; rng 50 > 20 -> fall back.
    CHECK(!ShouldPressAttack(friendCount, foeCount, 50));
    // but a low roll (<= 20) still forces the attack.
    CHECK(ShouldPressAttack(friendCount, foeCount, 15));
}
