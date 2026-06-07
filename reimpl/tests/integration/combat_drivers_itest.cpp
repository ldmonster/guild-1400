#include "test.h"

// Integration: drive combat_drivers' AI rolls against the REAL sibling RNG chain,
// not a mock. The CombatDriversHooks.randomModulo slot is the live
// VIBE_Math_RandomModulo (sim/combat.cpp, gilde.exe 0x58b89c) which itself calls the
// real VIBE_Util_RandNext LCG (crt/rand.cpp, 0x5cb8bc). We Srand a known seed, wire
// the hook straight into Math_RandomModulo exactly as the game does, and assert the
// deployment / pursuit / auto-resolve decisions match the values the real LCG yields.
#include "sim/combat_drivers.h"
#include "sim/combat.h"     // REAL sibling: guild::sim::Math_RandomModulo
#include "crt/rand.h"       // REAL LCG: guild::crt::Srand / RandNext

using namespace guild;
using namespace guild::sim;

namespace {
// The live wiring: forward the driver's roll hook into the real Math_RandomModulo.
int RealRandomModulo(u16 n) {
    return Math_RandomModulo(n);
}
} // namespace

// Reproduce the exact LCG draw sequence in the test so the asserted decisions are
// pinned to recovered numbers, then prove the driver consumes them identically.
TEST(CombatDriversIntegration, DeploymentDecisionMatchesRealLcg) {
    CombatDriversHooks h;
    h.randomModulo = &RealRandomModulo;
    SetCombatDriversHooks(&h);

    // seed 12345: raw RandNext draws = 21468, 9988, 22117, ...
    //   force  = 21468 % 5 = 3  (> playerForce 2 -> engage)
    //   mode   = 9988  % 4 = 0  -> threshold 10
    //   draw   = 22117 % 90 = 67; +10 = 77 > 10 -> REFUSE.
    crt::Srand(12345);
    DeployDecision dec = DeploymentAiDecision(true, 2);
    CHECK(dec == DeployDecision::kRefuse);

    // Re-seed and confirm the individual draws so the chain is unambiguous.
    crt::Srand(12345);
    CHECK_EQ(Math_RandomModulo(5), 3);
    CHECK_EQ(Math_RandomModulo(4), 0);
    CHECK_EQ(Math_RandomModulo(90), 67);

    SetCombatDriversHooks(nullptr);
}

TEST(CombatDriversIntegration, PursuitLoopMatchesRealLcg) {
    CombatDriversHooks h;
    h.randomModulo = &RealRandomModulo;
    SetCombatDriversHooks(&h);

    // seed 999: first two RandNext draws %100 = 84, 52 (both > 30 -> flee). High-ratio
    // unit attacks WITHOUT drawing (short-circuit), so it must NOT consume an LCG draw
    // — the next two low-ratio units then see exactly 84 and 52.
    crt::Srand(999);
    std::vector<PursuitUnit> units = {
        {1, 0.9},    // 90 >= 20 -> attack, no roll
        {-1, 0.0},   // empty -> skip, no roll
        {2, 0.0},    // roll 84 > 30 -> flee
        {3, 0.0},    // roll 52 > 30 -> flee
    };
    auto acts = DrivePursuitTargets(units, 100.0);
    CHECK_EQ((int)acts.size(), 4);
    CHECK(acts[0] == PursuitAction::kAttack);
    CHECK(acts[1] == PursuitAction::kSkip);
    CHECK(acts[2] == PursuitAction::kFlee);
    CHECK(acts[3] == PursuitAction::kFlee);

    // Independently confirm the raw modulo draws for seed 999.
    crt::Srand(999);
    CHECK_EQ(Math_RandomModulo(100), 84);
    CHECK_EQ(Math_RandomModulo(100), 52);

    SetCombatDriversHooks(nullptr);
}

// Auto-resolve winner over the real RNG bonus rolls: scores are deterministic data,
// the +/-30 bonuses come from the live LCG and decide a close match.
TEST(CombatDriversIntegration, AutoResolveBonusFromRealLcg) {
    CombatDriversHooks h;
    h.randomModulo = &RealRandomModulo;
    SetCombatDriversHooks(&h);

    std::vector<AutoResolveUnit> atk = {{1, 40.0}};   // score 40
    std::vector<AutoResolveUnit> dfn = {{1, 41.0}};   // score 41
    int atkScore = SumSideScore(atk);
    int dfnScore = SumSideScore(dfn);
    CHECK_EQ(atkScore, 40);
    CHECK_EQ(dfnScore, 41);

    // seed 12345: bonus draws RandomModulo(30) = 21468%30 = 18, 9988%30 = 28.
    crt::Srand(12345);
    int atkBonus = Math_RandomModulo(kSetupBonusMod);
    int dfnBonus = Math_RandomModulo(kSetupBonusMod);
    CHECK_EQ(atkBonus, 18);
    CHECK_EQ(dfnBonus, 28);
    // attackerTotal 58 vs defenderTotal 69 -> defenderTotal > attackerTotal -> ATTACKER.
    CHECK(AutoResolveWinner(atkScore, dfnScore, atkBonus, dfnBonus)
          == DriverBattleWinner::kAttacker);

    SetCombatDriversHooks(nullptr);
}
