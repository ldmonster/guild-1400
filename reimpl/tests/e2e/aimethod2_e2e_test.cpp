// End-to-end flow for the AiMethod scoring/decision family: classify a person's
// wealth tier, run the candidate scorers over a small method set, pick the best,
// and run the terminal result mapper — the planner's evaluate -> score -> pick ->
// advance cycle for a single person, using deterministic env hooks.
#include "ai/aimethod2.h"
#include "tests/framework/test.h"

using namespace guild;
using namespace guild::ai;

namespace {
// A tiny "world" the env reads from.
struct World {
    static int wealthA, wealthB;
    static int rankA, rankB;
    static float favSelfTarget;

    static int wealth(u16 id) { return id == 1 ? wealthA : (id == 2 ? wealthB : 0); }
    static int rank(u16 id) { return id == 1 ? rankA : (id == 2 ? rankB : 0); }
    static float fav(u16, u16) { return favSelfTarget; }
};
int   World::wealthA = 0;
int   World::wealthB = 0;
int   World::rankA = 0;
int   World::rankB = 0;
float World::favSelfTarget = 0.0f;

MethodEnv MakeEnv() {
    MethodEnv e = DefaultMethodEnv();
    e.total_wealth = &World::wealth;
    e.office_rank  = &World::rank;
    e.favorability = &World::fav;
    return e;
}
} // namespace

// Push a set of candidate "methods" (here just scorers), evaluate each, keep the
// best-scoring action, then map it through the EvalReturnN terminal.
TEST(AiMethod2E2E, EvaluateScorePickAdvance) {
    MethodEnv env = MakeEnv();

    // A wealthy pair of merchants.
    World::wealthA = 80000;
    World::wealthB = 120000;
    World::rankA = 6;   // self
    World::rankB = 2;   // target
    World::favSelfTarget = 40.0f;

    // 1) classify the actor's wealth tier from a gauge value.
    char tier = ClassifyWealthTier(9.5f); // trunc(9.5-1)=8, frac(9.5)=.5>=.5 -> 9 -> tier 3
    CHECK_EQ(tier, 3);

    // 2) score the candidate "methods": two wealth scorers + a class weight.
    int scoreA = ComputeWealthScoreA(1, 2, env);
    int scoreB = ComputeWealthScoreB(1, 2, env);
    int classW = ComputeClassWeight(1, 2, static_cast<u8>(tier), env);
    CHECK(scoreA > 0);
    CHECK(scoreB > 0);
    CHECK(scoreA > scoreB);    // A's coef (0.1) is double B's (0.05)
    CHECK(classW > 0);

    // 3) pick the best-scoring candidate (criterion-A wins ties as in the planner).
    int best = scoreA;
    char chosenAction = 1;
    if (scoreB > best) { best = scoreB; chosenAction = 2; }
    if (classW > best) { best = classW; chosenAction = 3; }
    CHECK_EQ(chosenAction, 1);   // wealth-A dominates here

    // 4) a follow / distance refinement on the chosen target.
    int penalty = ComputeDistancePenalty(2, 1, 2, env);
    CHECK_EQ(penalty, 28);       // (3*(6-2)+30)*2/3 = 28

    // self ranks above target so following is unlikely; force the RNG low so the
    // score (fav 40 + (rank2-rank6)*2.5 = 40 - 10 = 30) clears 0*75.
    env.rng_float = []() -> double { return 0.0; };
    int follow = ShouldFollowTarget(false, 1, 2, env);
    CHECK_EQ(follow, 1);

    // 5) advance: map the result through the terminal mapper. With result==0 and
    // armed==0 the executor commits state 6.
    char advanced = EvalReturnSix(0, 0);
    CHECK_EQ(advanced, 6);
    // If a higher-priority action already fired (result!=0), it short-circuits to 0.
    CHECK_EQ(EvalReturnSix(chosenAction, 0), 0);
}

// A conflict-initiation gate flow: a defended target suppresses the attack.
TEST(AiMethod2E2E, ConflictGateFlow) {
    MethodEnv env = MakeEnv();

    // Aggressor strongly favored, no defense -> initiate.
    CHECK_EQ(ShouldInitiateConflict(false, 2000.0f, 100.0f, 0.0f, false, env), 1);
    // Same favorabilities but heavy defensive field -> suppressed.
    CHECK_EQ(ShouldInitiateConflict(false, 2000.0f, 100.0f, 200.0f, false, env), 0);
    // Gate (busy/dueling) blocks outright.
    CHECK_EQ(ShouldInitiateConflict(true, 2000.0f, 0.0f, 0.0f, false, env), 0);

    // A drink decision rounds out the social loop (deterministic env).
    int flag = 0; float spend = 0;
    ComputeDrinkConsumption(2, 500.0f, 10, false, &flag, &spend, env);
    CHECK_EQ(flag, 1);
    CHECK(spend < 0.0f);   // money is spent (stored negative)
}
