// Unit tests for the AiMethod scoring/decision leaf family (src/ai/aimethod2.*).
// Golden vectors computed with python3 (see the implementer report); RNG-bearing
// paths are driven with deterministic env hooks.
#include <cmath>

#include "ai/aimethod2.h"
#include "tests/framework/test.h"

using namespace guild;
using namespace guild::ai;

// --- ClassifyWealthTier -----------------------------------------------------
TEST(AiMethod2, ClassifyWealthTier) {
    CHECK_EQ(ClassifyWealthTier(0.0f), 0);
    CHECK_EQ(ClassifyWealthTier(4.4f), 0);
    CHECK_EQ(ClassifyWealthTier(4.6f), 1);
    CHECK_EQ(ClassifyWealthTier(5.0f), 1);
    CHECK_EQ(ClassifyWealthTier(8.2f), 2);
    CHECK_EQ(ClassifyWealthTier(12.9f), 5);
    CHECK_EQ(ClassifyWealthTier(15.0f), 6);
    CHECK_EQ(ClassifyWealthTier(17.5f), 7);
    CHECK_EQ(ClassifyWealthTier(-3.0f), 0);
    CHECK_EQ(ClassifyWealthTier(2.5f), 0);
}

// --- table lookup -----------------------------------------------------------
TEST(AiMethod2, DrinkTableParams) {
    struct { u8 cid; int w; float base; float rs; } expect[] = {
        {0, 10, 20.0f, 40.0f},   {1, 13, 40.0f, 60.0f},
        {2, 16, 60.0f, 80.0f},   {3, 19, 80.0f, 100.0f},
        {4, 23, 100.0f, 120.0f}, {5, 27, 120.0f, 140.0f},
        {6, 32, 140.0f, 160.0f},
    };
    for (auto& e : expect) {
        int w = -1; float b = -1, r = -1;
        CHECK(LookupDrinkParams(e.cid, &w, &b, &r));
        CHECK_EQ(w, e.w);
        CHECK(b == e.base);
        CHECK(r == e.rs);
    }
    // class 7 is not present.
    int w; CHECK(!LookupDrinkParams(7, &w, nullptr, nullptr));
    // LookupClassEntry returns a pointer for valid ids, null otherwise.
    CHECK(LookupClassEntry(3) != nullptr);
    CHECK(LookupClassEntry(9) == nullptr);
}

// --- WealthScore A / B ------------------------------------------------------
namespace {
struct WealthFixture {
    static int valA, valB;
    static int total(u16 id) { return id == 1 ? valA : (id == 2 ? valB : 0); }
    static int identity(int x) { return x; }
};
int WealthFixture::valA = 0;
int WealthFixture::valB = 0;
} // namespace

TEST(AiMethod2, WealthScoreGolden) {
    MethodEnv env = DefaultMethodEnv();
    env.total_wealth = &WealthFixture::total;
    env.money_to_display = &WealthFixture::identity;

    struct { int a, b, expA, expB; } cases[] = {
        {0, 0, 6400, 3200},
        {5000, 5000, 6400, 3200},
        {20000, 30000, 28075, 14037},
        {100000, 0, 53333, 26666},
    };
    for (auto& c : cases) {
        WealthFixture::valA = c.a;
        WealthFixture::valB = c.b;
        CHECK_EQ(ComputeWealthScoreA(1, 2, env), c.expA);
        CHECK_EQ(ComputeWealthScoreB(1, 2, env), c.expB);
    }
}

// --- ComputeClassWeight -----------------------------------------------------
TEST(AiMethod2, ClassWeightGolden) {
    MethodEnv env = DefaultMethodEnv();
    env.total_wealth = &WealthFixture::total;

    struct { int a, b; u8 cid; int exp; } cases[] = {
        {10000, 10000, 0, 100},
        {50000, 50000, 3, 950},
        {1000, 1000, 6, 32},
        {0, 0, 2, 0},
    };
    for (auto& c : cases) {
        WealthFixture::valA = c.a;
        WealthFixture::valB = c.b;
        CHECK_EQ(ComputeClassWeight(1, 2, c.cid, env), c.exp);
    }
}

// --- ComputeDistancePenalty -------------------------------------------------
namespace {
struct RankFixture {
    static int rA, rB;
    static int rank(u16 id) { return id == 1 ? rA : (id == 2 ? rB : 0); }
};
int RankFixture::rA = 0;
int RankFixture::rB = 0;
} // namespace

TEST(AiMethod2, DistancePenaltyGolden) {
    MethodEnv env = DefaultMethodEnv();
    env.office_rank = &RankFixture::rank;
    struct { int scale, ra, rb, exp; } cases[] = {
        {1, 0, 0, 10}, {2, 5, 1, 28}, {3, 1, 5, 18}, {10, 0, 0, 100},
    };
    for (auto& c : cases) {
        RankFixture::rA = c.ra;
        RankFixture::rB = c.rb;
        CHECK_EQ(ComputeDistancePenalty(c.scale, 1, 2, env), c.exp);
    }
}

// --- CompareFavorability ----------------------------------------------------
TEST(AiMethod2, CompareFavorability) {
    MethodEnv env = DefaultMethodEnv();
    // not found -> 1 (regardless of fav)
    CHECK_EQ(CompareFavorability(false, 0.0f, 0.0f, env), 1);
    // d - e > 20 -> 1
    CHECK_EQ(CompareFavorability(true, 100.0f, 50.0f, env), 1);
    // d - e == 21 > 20 -> 1
    CHECK_EQ(CompareFavorability(true, 21.0f, 0.0f, env), 1);
    // e - d > 20 (favCA much higher) -> neither branch -> 0
    CHECK_EQ(CompareFavorability(true, 0.0f, 100.0f, env), 0);
    // |d-e| <= 20 -> RandomModulo(2). Force a deterministic rng to test both.
    env.rng_mod = [](u16) -> int { return 1; };
    CHECK_EQ(CompareFavorability(true, 5.0f, 5.0f, env), 1);
    env.rng_mod = [](u16) -> int { return 0; };
    CHECK_EQ(CompareFavorability(true, 5.0f, 5.0f, env), 0);
}

// --- ShouldInitiateConflict -------------------------------------------------
TEST(AiMethod2, ShouldInitiateConflict) {
    MethodEnv env = DefaultMethodEnv();
    // gate blocked -> 0
    CHECK_EQ(ShouldInitiateConflict(true, 1000.0f, 0.0f, 0.0f, false, env), 0);
    // ratingCurve default 0: v9=v10=0. v13 = favA*0.01, v12 = favB*0.01.
    // v11 = field4^2 * 0.0025. return v12+v11 < v13.
    //   favA=1000 -> v13=10 ; favB=0 -> v12=0 ; field4=0 -> v11=0 -> 0 < 10 -> 1
    CHECK_EQ(ShouldInitiateConflict(false, 1000.0f, 0.0f, 0.0f, false, env), 1);
    //   favA=0, favB=1000 -> v13=0, v12=10 -> 10 < 0 -> 0
    CHECK_EQ(ShouldInitiateConflict(false, 0.0f, 1000.0f, 0.0f, false, env), 0);
    //   field4 large defense -> v11 grows, suppresses attack
    CHECK_EQ(ShouldInitiateConflict(false, 1000.0f, 0.0f, 100.0f, false, env), 0);
}

// --- ShouldFollowTarget -----------------------------------------------------
TEST(AiMethod2, ShouldFollowTarget) {
    MethodEnv env = DefaultMethodEnv();
    env.office_rank = &RankFixture::rank;
    // equal -> 0
    CHECK_EQ(ShouldFollowTarget(true, 1, 2, env), 0);
    // score = fav(default 0) + (rank(target)-rank(self))*2.5. rng_float forced.
    RankFixture::rA = 0;   // self
    RankFixture::rB = 10;  // target
    env.rng_float = []() -> double { return 0.0; }; // 0*75 <= 25 -> 1
    CHECK_EQ(ShouldFollowTarget(false, 1, 2, env), 1);
    env.rng_float = []() -> double { return 1.0; }; // 75 <= 25 -> 0
    CHECK_EQ(ShouldFollowTarget(false, 1, 2, env), 0);
}

// --- terminal stubs / EvalReturnN -------------------------------------------
TEST(AiMethod2, TerminalStubs) {
    CHECK_EQ(StubReturnZero(), 0);
    CHECK_EQ(StubReturn1(), 1);
    CHECK_EQ(StubReturn2(), 2);
    CHECK_EQ(StubReturn3(), 3);
    CHECK_EQ(StubReturn6(), 6);
    CHECK_EQ(StubReturn7(), 7);
    CHECK_EQ(StubReturn8(), 8);

    // EvalReturnSix: result!=0 -> 0 ; result==0 && armed==0 -> 6 ; else result(0)
    CHECK_EQ(EvalReturnSix(5, 0), 0);
    CHECK_EQ(EvalReturnSix(0, 0), 6);
    CHECK_EQ(EvalReturnSix(0, 1), 0);
    // EvalReturnSeven mirrors with 7
    CHECK_EQ(EvalReturnSeven(5, 0), 0);
    CHECK_EQ(EvalReturnSeven(0, 0), 7);
    CHECK_EQ(EvalReturnSeven(0, 1), 0);
}

// --- ComputeDrinkConsumption (deterministic env: RandFloat=0.5, RandInt=0) ---
TEST(AiMethod2, DrinkConsumptionGolden) {
    MethodEnv env = DefaultMethodEnv(); // defaults already RandFloat=0.5, RandInt=0
    struct { u8 cid; float budget; int rounds; bool half; int flag; float spend; } cases[] = {
        {0, 100.0f, 5, false, 1, -33.0f},
        {2, 500.0f, 10, false, 1, -384.48f},
        {6, 50.0f, 3, true, 1, -180.0f},
        {1, 1000.0f, 2, false, 0, -140.0f},
    };
    for (auto& c : cases) {
        int flag = -1; float spend = 0;
        int r = ComputeDrinkConsumption(c.cid, c.budget, c.rounds, c.half,
                                        &flag, &spend, env);
        CHECK_EQ(r, c.flag);
        CHECK_EQ(flag, c.flag);
        CHECK(std::fabs(spend - c.spend) < 0.05f);
    }
}
