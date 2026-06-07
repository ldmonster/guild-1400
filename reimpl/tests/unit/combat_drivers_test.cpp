#include "test.h"

#include "sim/combat_drivers.h"

using namespace guild;
using namespace guild::sim;

namespace {
// A scripted RNG so the roll-driven cores are deterministic without crt state.
int g_seq[16];
int g_seqLen = 0;
int g_seqPos = 0;
int ScriptRoll(u16 n) {
    if (g_seqPos >= g_seqLen)
        return 0;
    int v = g_seq[g_seqPos++];
    return n ? (v % n) : 0;
}
void SetScript(std::initializer_list<int> vals) {
    g_seqLen = 0;
    g_seqPos = 0;
    for (int v : vals)
        g_seq[g_seqLen++] = v;
}
CombatDriversHooks MakeRollHooks() {
    CombatDriversHooks h;
    h.randomModulo = &ScriptRoll;
    return h;
}
} // namespace

// ---------------------------------------------------------------------------
// CountSurvivors / PickWinner (0x48e4e4)
// ---------------------------------------------------------------------------
TEST(CombatDrivers, CountSurvivorsExcludesDeadEscapedCaptured) {
    std::vector<ResultUnit> team = {
        {10, 1, 0},   // alive, counts
        {-1, 1, 0},   // empty slot -> skip
        {11, 0, 0},   // dead (alive==0) -> skip
        {12, 4, 0},   // escaped (alive==4) -> skip
        {13, 2, 1},   // captured building (+533==1) -> skip
        {14, 3, 0},   // alive (state 3) -> counts
    };
    CHECK_EQ(CountSurvivors(team), 2);
    CHECK_EQ(CountSurvivors(std::vector<ResultUnit>{}), 0);
}

TEST(CombatDrivers, PickWinnerTieGoesToDefender) {
    // v15 <= v11 -> defender. atk==def is a tie -> defender.
    CHECK(PickWinner(3, 3) == DriverBattleWinner::kDefender);
    CHECK(PickWinner(5, 2) == DriverBattleWinner::kDefender);  // attacker majority -> defender
    CHECK(PickWinner(2, 5) == DriverBattleWinner::kAttacker);  // strict defender majority
    CHECK(PickWinner(0, 0) == DriverBattleWinner::kDefender);
}

TEST(CombatDrivers, PickWinnerForced) {
    CHECK(PickWinnerForced(true) == DriverBattleWinner::kDefender);
    CHECK(PickWinnerForced(false) == DriverBattleWinner::kAttacker);
}

TEST(CombatDrivers, ClassifyRankIndex) {
    CHECK_EQ(ClassifyRankIndex(19, 7), 0);
    CHECK_EQ(ClassifyRankIndex(16, 7), 1);
    CHECK_EQ(ClassifyRankIndex(4, 7), 2);
    CHECK_EQ(ClassifyRankIndex(5, 7), 7);   // unmapped -> keep previous
    CHECK_EQ(ClassifyRankIndex(0, 3), 3);
}

// ---------------------------------------------------------------------------
// RunBattleSetup auto-resolve (0x490014)
// ---------------------------------------------------------------------------
TEST(CombatDrivers, ValidateRosterClearsUnresolved) {
    std::vector<SetupSlot> def = {
        {5, true},    // resolves -> counts
        {6, false},   // set but unresolved -> cleared + dirty
        {-1, true},   // empty -> ignored
        {7, true},    // resolves -> counts
    };
    bool dirty = false;
    int live = ValidateRoster(def, &dirty);
    CHECK_EQ(live, 2);
    CHECK(dirty);
    CHECK_EQ(def[1].id, -1);   // cleared
    CHECK_EQ(def[0].id, 5);    // untouched
}

TEST(CombatDrivers, ValidateRosterCleanNoDirty) {
    std::vector<SetupSlot> def = {{1, true}, {-1, true}};
    bool dirty = true;
    CHECK_EQ(ValidateRoster(def, &dirty), 1);
    CHECK(!dirty);
}

TEST(CombatDrivers, SumSideScoreFloatSpill) {
    // acc = (int)(strength + acc), iterated. dead/escaped excluded.
    std::vector<AutoResolveUnit> side = {
        {1, 10.7},   // (int)(10.7 + 0)   = 10
        {0, 99.0},   // dead -> skip
        {4, 99.0},   // escaped -> skip
        {3, 5.9},    // (int)(5.9 + 10)   = 15
        {1, 2.2},    // (int)(2.2 + 15)   = 17
    };
    CHECK_EQ(SumSideScore(side), 17);
}

TEST(CombatDrivers, AutoResolveWinnerDefenderOnTie) {
    // defenderTotal <= attackerTotal -> defender.
    CHECK(AutoResolveWinner(10, 8, 5, 7) == DriverBattleWinner::kDefender); // 15 vs 15 tie
    CHECK(AutoResolveWinner(10, 20, 0, 0) == DriverBattleWinner::kAttacker);// def 20 > atk 10
    CHECK(AutoResolveWinner(20, 10, 0, 0) == DriverBattleWinner::kDefender);
}

// ---------------------------------------------------------------------------
// BuildDeploymentScreen AI decision (0x48dab0)
// ---------------------------------------------------------------------------
TEST(CombatDrivers, DeploymentAiHoldsWhenForceRollLow) {
    CombatDriversHooks h = MakeRollHooks();
    SetCombatDriversHooks(&h);
    // hasUnits, force roll RandomModulo(5)=2 <= playerForce 2 -> hold.
    SetScript({2});
    CHECK(DeploymentAiDecision(true, 2) == DeployDecision::kHold);
    SetCombatDriversHooks(nullptr);
}

TEST(CombatDrivers, DeploymentAiRefuseHighThreshold) {
    CombatDriversHooks h = MakeRollHooks();
    SetCombatDriversHooks(&h);
    // force roll 4 (>2 -> engage); mode RandomModulo(4)=1 -> threshold 90;
    // draw RandomModulo(90)=80 +10 = 90 <= 90 -> accept.
    SetScript({4, 1, 80});
    CHECK(DeploymentAiDecision(true, 2) == DeployDecision::kAccept);
    SetCombatDriversHooks(nullptr);
}

TEST(CombatDrivers, DeploymentAiRefuseLowThreshold) {
    CombatDriversHooks h = MakeRollHooks();
    SetCombatDriversHooks(&h);
    // force roll 4 -> engage; mode 0 -> threshold 10; draw 5+10=15 > 10 -> refuse.
    SetScript({4, 0, 5});
    CHECK(DeploymentAiDecision(true, 2) == DeployDecision::kRefuse);
    SetCombatDriversHooks(nullptr);
}

TEST(CombatDrivers, DeploymentAiNoUnitsAlwaysEngages) {
    CombatDriversHooks h = MakeRollHooks();
    SetCombatDriversHooks(&h);
    // hasUnits == false -> skip the force gate entirely; mode 2 -> threshold 50;
    // draw 39+10 = 49 <= 50 -> accept.
    SetScript({2, 39});
    CHECK(DeploymentAiDecision(false, 99) == DeployDecision::kAccept);
    SetCombatDriversHooks(nullptr);
}

TEST(CombatDrivers, DeploymentOfferAcceptedBoundary) {
    CombatDriversHooks h = MakeRollHooks();
    SetCombatDriversHooks(&h);
    SetScript({40});  // 40 + 10 == 50 <= 50
    CHECK(DeploymentOfferAccepted(50));
    SetScript({41});  // 41 + 10 == 51 > 50
    CHECK(!DeploymentOfferAccepted(50));
    SetCombatDriversHooks(nullptr);
}

// ---------------------------------------------------------------------------
// IssueOrdersForTeam (0x48c15c)
// ---------------------------------------------------------------------------
TEST(CombatDrivers, ResolveTeamRow) {
    std::vector<i32> table = {100, 200, 300, 400};
    CHECK_EQ(ResolveTeamRow(table, 4, 300), 2);
    CHECK_EQ(ResolveTeamRow(table, 4, 100), 0);
    CHECK_EQ(ResolveTeamRow(table, 4, 999), -1);
    // count truncates the scan: id 400 is at index 3, count 3 excludes it.
    CHECK_EQ(ResolveTeamRow(table, 3, 400), -1);
}

// ---------------------------------------------------------------------------
// UpdateOrderSlots (0x4882c0)
// ---------------------------------------------------------------------------
TEST(CombatDrivers, ComputeOrderSlotEmptyAndCaptured) {
    OrderSlotResult e = ComputeOrderSlot({-1, false, true, 5.0}, 100.0);
    CHECK(e.skip);
    OrderSlotResult c = ComputeOrderSlot({3, true, true, 5.0}, 100.0);
    CHECK(c.skip);
}

TEST(CombatDrivers, ComputeOrderSlotActiveValue) {
    // value = (int)(0.375 * 100.0) = 37 ; active -> count 1.
    OrderSlotResult a = ComputeOrderSlot({3, false, true, 0.375}, 100.0);
    CHECK(!a.skip);
    CHECK_EQ(a.count, 1);
    CHECK_EQ(a.outputValue, 37);
    // inactive -> count 0 but value still computed.
    OrderSlotResult b = ComputeOrderSlot({3, false, false, 0.99}, 100.0);
    CHECK_EQ(b.count, 0);
    CHECK_EQ(b.outputValue, 99);
}

// ---------------------------------------------------------------------------
// AssignSelectedTarget (0x488874)
// ---------------------------------------------------------------------------
TEST(CombatDrivers, FindSelectedTargetOwnerMatch) {
    std::vector<SelectableUnit> u = {
        {true, false, true, true, 1},   // not selectable -> skip
        {false, true, true, true, 1},   // not present -> skip
        {true, true, false, true, 1},   // owner mismatch + no override -> skip
        {true, true, true, true, 1},    // match
    };
    CHECK_EQ(FindSelectedTarget(u, false), 3);
}

TEST(CombatDrivers, FindSelectedTargetGlobalOverride) {
    std::vector<SelectableUnit> u = {
        {true, true, false, true, 1},   // owner mismatch but override on -> match
    };
    CHECK_EQ(FindSelectedTarget(u, true), 0);
    // override on but missing field / dead -> no match
    std::vector<SelectableUnit> u2 = {{true, true, false, false, 1}};
    CHECK_EQ(FindSelectedTarget(u2, true), -1);
    std::vector<SelectableUnit> u3 = {{true, true, false, true, 0}};
    CHECK_EQ(FindSelectedTarget(u3, true), -1);
}

TEST(CombatDrivers, FindSelectedTargetNone) {
    CHECK_EQ(FindSelectedTarget(std::vector<SelectableUnit>{}, true), -1);
}

// ---------------------------------------------------------------------------
// SetUnitFormationMode (0x48980c)
// ---------------------------------------------------------------------------
TEST(CombatDrivers, FirstFreeFormationSlot) {
    CHECK_EQ(FindFirstFreeFormationSlot({-1, 5, 6}), 0);  // slot0 free
    CHECK_EQ(FindFirstFreeFormationSlot({5, 6, -1, 7}), 2);
    CHECK_EQ(FindFirstFreeFormationSlot({1, 2, 3}), -1);  // none free, < 16
    CHECK_EQ(FindFirstFreeFormationSlot(std::vector<i32>{}), -1);
}

TEST(CombatDrivers, FormationModeOpcode) {
    CHECK_EQ(FormationModeOpcode(0), -1);
    CHECK_EQ(FormationModeOpcode(1), kFormOpLine);
    CHECK_EQ(FormationModeOpcode(2), kFormOpA);
    CHECK_EQ(FormationModeOpcode(3), kFormOpA);
    CHECK_EQ(FormationModeOpcode(9), -1);
}

// ---------------------------------------------------------------------------
// UpdatePursuitTargets (0x48c400)
// ---------------------------------------------------------------------------
TEST(CombatDrivers, DrivePursuitHighRatioAlwaysAttacks) {
    CombatDriversHooks h = MakeRollHooks();
    SetCombatDriversHooks(&h);
    SetScript({});  // no rolls needed: ratio 0.5*100 = 50 >= 20 -> attack, no draw.
    std::vector<PursuitUnit> units = {{1, 0.5}, {-1, 9.9}, {2, 0.25}};
    auto acts = DrivePursuitTargets(units, 100.0);
    CHECK_EQ((int)acts.size(), 3);
    CHECK(acts[0] == PursuitAction::kAttack);
    CHECK(acts[1] == PursuitAction::kSkip);
    CHECK(acts[2] == PursuitAction::kAttack);
    SetCombatDriversHooks(nullptr);
}

TEST(CombatDrivers, DrivePursuitLowRatioRolls) {
    CombatDriversHooks h = MakeRollHooks();
    SetCombatDriversHooks(&h);
    // ratio 0.1*100 = 10 < 20 -> roll. roll 25 <= 30 attack; roll 80 > 30 flee.
    SetScript({25, 80});
    std::vector<PursuitUnit> units = {{1, 0.1}, {2, 0.1}};
    auto acts = DrivePursuitTargets(units, 100.0);
    CHECK(acts[0] == PursuitAction::kAttack);
    CHECK(acts[1] == PursuitAction::kFlee);
    SetCombatDriversHooks(nullptr);
}

TEST(CombatDrivers, DrivePursuitBoundaryRoll30Attacks) {
    CombatDriversHooks h = MakeRollHooks();
    SetCombatDriversHooks(&h);
    SetScript({30, 31});
    std::vector<PursuitUnit> units = {{1, 0.0}, {2, 0.0}};
    auto acts = DrivePursuitTargets(units, 100.0);
    CHECK(acts[0] == PursuitAction::kAttack);  // 30 <= 30
    CHECK(acts[1] == PursuitAction::kFlee);    // 31 > 30
    SetCombatDriversHooks(nullptr);
}
