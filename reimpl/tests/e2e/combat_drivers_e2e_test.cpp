#include "test.h"

#include "sim/combat_drivers.h"

using namespace guild;
using namespace guild::sim;

namespace {
// Scripted RNG shared across the flow so the whole battle is reproducible.
int g_seq[32];
int g_len = 0, g_pos = 0;
int FlowRoll(u16 n) {
    if (g_pos >= g_len) return 0;
    int v = g_seq[g_pos++];
    return n ? (v % n) : 0;
}
void Script(std::initializer_list<int> vals) {
    g_len = g_pos = 0;
    for (int v : vals) g_seq[g_len++] = v;
}
} // namespace

// End-to-end: drive a single auto-resolved battle through the whole driver chain —
// roster validation -> deployment AI decision -> pursuit-order loop -> auto-resolve
// score + winner, then classify the loser's result-screen rank.
TEST(CombatDriversE2E, AutoResolvedBattleFlow) {
    CombatDriversHooks h;
    h.randomModulo = &FlowRoll;
    SetCombatDriversHooks(&h);

    // 1) Validate the defender roster: two real, one stale.
    std::vector<SetupSlot> def = {{40, true}, {41, false}, {42, true}};
    bool dirty = false;
    int liveDef = ValidateRoster(def, &dirty);
    CHECK_EQ(liveDef, 2);
    CHECK(dirty);
    CHECK_EQ(def[1].id, -1);

    // 2) Deployment AI: defender has units, modest player force.
    //    force roll RandomModulo(5)=4 (>2 engage), mode RandomModulo(4)=2 -> thr 50,
    //    draw RandomModulo(90)=30 + 10 = 40 <= 50 -> ACCEPT (defends but yields).
    Script({4, 2, 30});
    DeployDecision dec = DeploymentAiDecision(true, 2);
    CHECK(dec == DeployDecision::kAccept);

    // 3) Pursuit loop for three attacker units: high ratio attacks without rolling,
    //    low ratio rolls (35 -> flee, 12 -> attack).
    Script({35, 12});
    std::vector<PursuitUnit> pursuers = {{1, 0.9}, {2, 0.05}, {3, 0.05}};
    auto acts = DrivePursuitTargets(pursuers, 100.0);
    CHECK(acts[0] == PursuitAction::kAttack);  // 90 >= 20, no roll
    CHECK(acts[1] == PursuitAction::kFlee);    // roll 35 > 30
    CHECK(acts[2] == PursuitAction::kAttack);  // roll 12 <= 30

    // 4) Auto-resolve scores (units alive contribute their strength, dead skipped).
    std::vector<AutoResolveUnit> atk = {{1, 30.0}, {1, 20.0}, {0, 99.0}};
    std::vector<AutoResolveUnit> dfn = {{1, 15.0}, {4, 99.0}, {3, 10.0}};
    int atkScore = SumSideScore(atk);   // (int)(30) -> (int)(20+30) = 50
    int dfnScore = SumSideScore(dfn);   // (int)(15) -> escaped skip -> (int)(10+15)=25
    CHECK_EQ(atkScore, 50);
    CHECK_EQ(dfnScore, 25);

    // bonus rolls RandomModulo(30): attacker 7, defender 28.
    Script({7, 28});
    int atkBonus = static_cast<int>(DriverRoll(kSetupBonusMod));
    int dfnBonus = static_cast<int>(DriverRoll(kSetupBonusMod));
    CHECK_EQ(atkBonus, 7);
    CHECK_EQ(dfnBonus, 28);
    // attackerTotal 57 vs defenderTotal 53 -> defenderTotal <= attackerTotal -> DEFENDER.
    DriverBattleWinner w = AutoResolveWinner(atkScore, dfnScore, atkBonus, dfnBonus);
    CHECK(w == DriverBattleWinner::kDefender);

    // 5) Result screen: the survivor counts agree with the winner, and the loser
    //    (defender) gets a rank index from their leader's class byte (16 -> 1).
    std::vector<ResultUnit> atkTeam = {{1, 1, 0}, {2, 1, 0}};   // 2 survivors
    std::vector<ResultUnit> dfnTeam = {{3, 4, 0}, {4, 0, 0}};   // 0 survivors (escaped/dead)
    int atkSurv = CountSurvivors(atkTeam);
    int dfnSurv = CountSurvivors(dfnTeam);
    CHECK_EQ(atkSurv, 2);
    CHECK_EQ(dfnSurv, 0);
    CHECK(PickWinner(atkSurv, dfnSurv) == DriverBattleWinner::kDefender); // tie/atk-maj quirk
    CHECK_EQ(ClassifyRankIndex(16, 0), 1);

    SetCombatDriversHooks(nullptr);
}

// A second flow: a fully manned defender that the AI decides to hold (fight) — the
// roster is clean, no slot cleared, and the human-offer path uses fixed thresholds.
TEST(CombatDriversE2E, DefenderHoldsAndManualOffer) {
    CombatDriversHooks h;
    h.randomModulo = &FlowRoll;
    SetCombatDriversHooks(&h);

    std::vector<SetupSlot> def = {{1, true}, {2, true}};
    bool dirty = true;
    CHECK_EQ(ValidateRoster(def, &dirty), 2);
    CHECK(!dirty);

    // force roll RandomModulo(5)=1 <= playerForce 3 -> HOLD (no further rolls).
    Script({1});
    CHECK(DeploymentAiDecision(true, 3) == DeployDecision::kHold);

    // Manual high-offer widget (threshold 90): draw 79+10=89 <= 90 -> accept.
    Script({79});
    CHECK(DeploymentOfferAccepted(90));
    // Manual low-offer (threshold 10): draw 5+10=15 > 10 -> refuse.
    Script({5});
    CHECK(!DeploymentOfferAccepted(10));

    SetCombatDriversHooks(nullptr);
}
