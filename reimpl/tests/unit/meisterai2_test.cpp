// Unit tests for the MeisterAi RuleEval continuation (src/ai/meisterai2.cpp).
// Golden vectors were computed with python3 mirroring the exact float32 / trunc
// arithmetic of the gilde.exe bodies (see the agent report). The four leaf reads
// (group, law record, city counter, RNG) are injected via RuleEnv2 so each rule's
// scoring/decision core is exercised deterministically.
#include "ai/meisterai2.h"

#include "tests/framework/test.h"

using namespace guild::ai;

namespace {

// A deterministic RNG that returns a fixed value (the rule masks &0xFFFF).
int g_rngValue = 0;
int FixedRng(int /*n*/) { return g_rngValue; }

int g_group = 0;
int GroupOf(int /*typeCode*/) { return g_group; }

int g_city = 100;
int CityCount() { return g_city; }

Rule2Law g_law{};
Rule2Law LawOf(int /*id*/) { return g_law; }

RuleEnv2 MakeEnv() {
    RuleEnv2 e;
    e.group_from_code = &GroupOf;
    e.law_record = &LawOf;
    e.city_count = &CityCount;
    e.rng_mod = &FixedRng;
    return e;
}

} // namespace

TEST(MeisterAi2, IncreaseSetting) {
    RuleEnv2 e = MakeEnv();
    Rule2Building b;
    int out = -99;

    // (2.5-2)*5 = 2.5 -> trunc 2; current 0 -> change to 2.
    b.f43 = 2.5f; g_law.current = 0;
    CHECK_EQ(RuleEvalIncreaseSetting(b, e, &out), 1);
    CHECK_EQ(out, 2);

    // (3.0-2)*5 = 5 -> clamp to 4; current 5 -> change to 4.
    b.f43 = 3.0f; g_law.current = 5; out = -99;
    CHECK_EQ(RuleEvalIncreaseSetting(b, e, &out), 1);
    CHECK_EQ(out, 4);

    // (2.4-2)*5 = 2 == current -> no change.
    b.f43 = 2.4f; g_law.current = 2; out = -99;
    CHECK_EQ(RuleEvalIncreaseSetting(b, e, &out), 0);
    CHECK_EQ(out, -99);
}

TEST(MeisterAi2, AdjustSetting) {
    RuleEnv2 e = MakeEnv();
    Rule2Building b;
    int out = -99;

    // (2.0-2)*5 = 0 -> clamp up to 1; current 0 -> change.
    b.f43 = 2.0f; g_law.current = 0;
    CHECK_EQ(RuleEvalAdjustSetting(b, e, &out), 1);
    CHECK_EQ(out, 1);

    // (2.05-2)*5 = 0.25 -> trunc 0 -> clamp up to 1; current 1 -> no change.
    b.f43 = 2.05f; g_law.current = 1; out = -99;
    CHECK_EQ(RuleEvalAdjustSetting(b, e, &out), 0);
    CHECK_EQ(out, -99);
}

TEST(MeisterAi2, RangeLow) {
    RuleEnv2 e = MakeEnv();
    Rule2Building b;
    int out = -99;
    g_law.low = 10; g_law.high = 20;

    // group 8: target = trunc(10*0.33 + 10) = 13; current 15 > 13 -> low + rng(=1).
    g_group = 8; g_law.current = 15; g_rngValue = 1; out = -99;
    CHECK_EQ(RuleEvalRangeLow(b, e, &out), 1);
    CHECK_EQ(out, 11);

    // group 8: current 12 <= 13 -> no change.
    g_law.current = 12; out = -99;
    CHECK_EQ(RuleEvalRangeLow(b, e, &out), 0);

    // default: target = trunc(10*0.66 + 10) = 16; current 15 < 16 -> high - rng(=2).
    g_group = 0; g_law.current = 15; g_rngValue = 2; out = -99;
    CHECK_EQ(RuleEvalRangeLow(b, e, &out), 1);
    CHECK_EQ(out, 18);

    // city gate: count < 4 -> never fires.
    g_city = 3; out = -99;
    CHECK_EQ(RuleEvalRangeLow(b, e, &out), 0);
    g_city = 100;
}

TEST(MeisterAi2, RangeHigh) {
    RuleEnv2 e = MakeEnv();
    Rule2Building b;
    int out = -99;
    g_law.low = 10; g_law.high = 20;

    // group 9: target = trunc(10*0.55 + 10) = 15; current 14 < 15 -> high - rng(=1).
    g_group = 9; g_law.current = 14; g_rngValue = 1; out = -99;
    CHECK_EQ(RuleEvalRangeHigh(b, e, &out), 1);
    CHECK_EQ(out, 19);

    // default: target = trunc(10*0.45 + 10) = 14; current 15 > 14 -> low + rng(=1).
    g_group = 0; g_law.current = 15; g_rngValue = 1; out = -99;
    CHECK_EQ(RuleEvalRangeHigh(b, e, &out), 1);
    CHECK_EQ(out, 11);
}

TEST(MeisterAi2, InterpolatedTarget) {
    RuleEnv2 e = MakeEnv();
    Rule2Building b;
    int out = -99;
    g_law.low = 0; g_law.high = 100;

    // a = 3-1 = 2; c = 700*(1/700) = 1; prod = 2 -> t clamps to 1; target = 100.
    b.f43 = 1.0f; b.f7 = 700.0f; g_law.current = 0; out = -99;
    CHECK_EQ(RuleEvalInterpolatedTarget(b, e, &out), 1);
    CHECK_EQ(out, 100);

    // a = 3-2.5 = 0.5; c = 350/700 = 0.5; prod = 0.25; target = trunc(100*0.25) = 25.
    b.f43 = 2.5f; b.f7 = 350.0f; g_law.current = 0; out = -99;
    CHECK_EQ(RuleEvalInterpolatedTarget(b, e, &out), 1);
    CHECK_EQ(out, 25);

    // within +-3 of current -> no change (target 25, current 27).
    g_law.current = 27; out = -99;
    CHECK_EQ(RuleEvalInterpolatedTarget(b, e, &out), 0);
}

TEST(MeisterAi2, SeasonalStock) {
    RuleEnv2 e = MakeEnv();
    Rule2Building b;
    int out = -99;
    g_law.low = 0; g_law.high = 100;

    // group 7 (tavern): target = trunc((2.5-2)*100) = 50; 50 > current 0 -> 50.
    g_group = 7; b.f43 = 2.5f; g_law.current = 0; out = -99;
    CHECK_EQ(RuleEvalSeasonalStock(b, e, &out), 1);
    CHECK_EQ(out, 50);

    // default: target = trunc((3-2.5)*100) = 50; 50 < current 100 -> 50.
    g_group = 0; b.f43 = 2.5f; g_law.current = 100; out = -99;
    CHECK_EQ(RuleEvalSeasonalStock(b, e, &out), 1);
    CHECK_EQ(out, 50);

    // group 7: target 50 not > current 80 -> no change.
    g_group = 7; g_law.current = 80; out = -99;
    CHECK_EQ(RuleEvalSeasonalStock(b, e, &out), 0);
}

TEST(MeisterAi2, CostBenefit) {
    RuleEnv2 e = MakeEnv();
    Rule2Building b;
    int out = -99;
    g_group = 0;

    // score = max(2.2-2, 2.0-2)*1.1 = 0.22 < 0.5; enable 0; base 1 > high 0 -> out 1.
    b.f52 = 2.2f; b.f55 = 2.0f;
    b.f61 = 3.0f; b.f63 = 1.0f;   // base = (3-2)*1 = 1
    b.f67 = 2.0f; b.f69 = 1.0f;   // high = (2-2)*1 = 0
    g_law.enable = 0; out = -99;
    CHECK_EQ(RuleEvalCostBenefit(b, e, &out), 1);
    CHECK_EQ(out, 1);

    // score = max(3-2,0)*1.1 = 1.1 > 0.5; enable 1; high 1 > base 0 -> out 0.
    b.f52 = 3.0f; b.f55 = 2.0f;
    b.f61 = 2.0f; b.f63 = 1.0f;   // base = 0
    b.f67 = 3.0f; b.f69 = 1.0f;   // high = 1
    g_law.enable = 1; out = -99;
    CHECK_EQ(RuleEvalCostBenefit(b, e, &out), 1);
    CHECK_EQ(out, 0);

    // score high but enable 0 (mismatch) and base !> high -> no change.
    b.f52 = 3.0f; b.f55 = 2.0f; g_law.enable = 0; out = -99;
    b.f61 = 2.0f; b.f67 = 3.0f;   // base 0, high 1 -> base !> high
    CHECK_EQ(RuleEvalCostBenefit(b, e, &out), 0);
}
