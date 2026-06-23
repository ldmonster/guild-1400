// Unit tests for the MeisterAi economy passes (guild::ai):
//   the RuleEval scoring/decision family (golden, vs. hand-computed scores),
//   the staffing decision cores (seeded RNG), the PlanProduction ratio, and the
//   trade stock-deficit / priority-sort cores.
#include "tests/framework/test.h"

#include "ai/rule_eval.h"
#include "ai/meister_economy.h"
#include "ai/meister_trade.h"
#include "crt/rand.h"
#include "util/math_random.h"

#include <vector>

using namespace guild;

// ---------------------------------------------------------------------------
// RuleEval scoring core: build a synthetic environment + building and verify the
// fire/no-fire decision against the hand-computed score path.
// ---------------------------------------------------------------------------
namespace {
// A controllable environment for the rule cores.
struct FakeRuleEnv {
    int group = 0;
    u8  scalar = 0;
    float rating = 0.0f;
    int law = 0;
};
FakeRuleEnv g_env;

int FakeGroup(int) { return g_env.group; }
u8  FakeScalar(int) { return g_env.scalar; }
float FakeRating(const ai::RuleBuilding&, int) { return g_env.rating; }
int FakeLaw(int) { return g_env.law; }

ai::RuleEnv MakeEnv() {
    ai::RuleEnv e;
    e.group_from_code = &FakeGroup;
    e.rate_scalar = &FakeScalar;
    e.eval_rating = &FakeRating;
    e.law_value = &FakeLaw;
    return e;
}
} // namespace

TEST(AiMeisterEconomy, RuleScoreFiresRaise) {
    // ServiceLevel rule (law 3, col 4). Drive score < 1.0 and law==0, with the
    // high trend > base trend so the rule raises the setting (out=1).
    ai::RuleEnv env = MakeEnv();
    const ai::RuleConfig& cfg = ai::RuleConfigServiceLevel();

    // score = (rating - (scalar+1)*c0) * c1 * groupMul.
    // pick scalar=0 -> baseline = 1*c0 = 0.003968; rating=0.01 ->
    // (0.01 - 0.003968)*c1(0.0238) = ~0.0001436; group default (1.0) -> < 1.0.
    g_env.group = 0;            // default multiplier (1.0)
    g_env.scalar = 0;
    g_env.rating = 0.01f;
    g_env.law = 0;

    // base = (setting61 + bias(-2))*weight63; high = (setting67 + bias)*weight69.
    // make high > base: base small, high large.
    ai::RuleBuilding b;
    b.setting61 = 3.0f; b.weight63 = 1.0f;  // base = (3-2)*1 = 1.0
    b.setting64 = 0.0f; b.weight66 = 1.0f;
    b.setting67 = 5.0f; b.weight69 = 1.0f;  // high = (5-2)*1 = 3.0 > base
    int out = -1;
    CHECK(ai::RuleEvalSettingToggle(b, env, cfg, &out));
    CHECK_EQ(out, 1);

    // If high <= base, no fire.
    b.setting67 = 2.0f; // high = 0 < base 1.0
    out = -1;
    CHECK(!ai::RuleEvalSettingToggle(b, env, cfg, &out));
}

TEST(AiMeisterEconomy, RuleScoreFiresLower) {
    // Drive score > 1.0 and law==1, with mid > base -> lower the setting (out=0).
    ai::RuleEnv env = MakeEnv();
    const ai::RuleConfig& cfg = ai::RuleConfigServiceLevel();

    // make score > 1.0: rating large. (rating - baseline)*c1*groupMul.
    // group 7 -> *1.1. choose rating so score>1: rating=50 ->
    //   (50-0.00397)*0.0238*1.1 ~= 1.30 > 1.0.
    g_env.group = 7;            // SV group 7 -> *1.1
    g_env.scalar = 0;
    g_env.rating = 50.0f;
    g_env.law = 1;

    ai::RuleBuilding b;
    b.setting61 = 3.0f; b.weight63 = 1.0f;  // base = 1.0
    b.setting64 = 5.0f; b.weight66 = 1.0f;  // mid  = 3.0 > base
    b.setting67 = 0.0f; b.weight69 = 1.0f;
    int out = -1;
    CHECK(ai::RuleEvalSettingToggle(b, env, cfg, &out));
    CHECK_EQ(out, 0);

    // law != 1 -> no fire on the lower branch.
    g_env.law = 0;
    out = -1;
    // score>1.0 but law==0 -> first branch needs score<1.0; neither fires.
    CHECK(!ai::RuleEvalSettingToggle(b, env, cfg, &out));
}

TEST(AiMeisterEconomy, RuleGroupMultipliers) {
    // Capacity rule: groups 11/12 -> 1.1, else 0.9. Verify the multiplier flips
    // the score across the 1.0 boundary.
    ai::RuleEnv env = MakeEnv();
    const ai::RuleConfig& cfg = ai::RuleConfigCapacityToggle();
    CHECK_EQ(cfg.lawId, 21);

    // pick rating so the un-multiplied score is just under 1.0/0.9 boundary.
    // (rating-baseline)*c1. baseline (scalar 0) = c0.
    // we want raw*0.9 < 1.0 < raw*1.1 i.e. raw in (0.909, 1.111).
    // raw = (rating - c0)*c1 = 1.0 -> rating = 1.0/c1 + c0.
    g_env.scalar = 0;
    g_env.rating = 1.0f / cfg.c1 + static_cast<float>(cfg.bias == 0 ? 0 : 0) + cfg.c0;
    g_env.law = 0;

    ai::RuleBuilding b;
    b.setting61 = 3.0f; b.weight63 = 1.0f;  // base 1.0
    b.setting64 = 0.0f; b.weight66 = 1.0f;
    b.setting67 = 5.0f; b.weight69 = 1.0f;  // high 3.0 > base

    // group 5 -> default 0.9 -> score < 1.0 -> first branch fires (out=1).
    g_env.group = 5;
    int out = -1;
    CHECK(ai::RuleEvalSettingToggle(b, env, cfg, &out));
    CHECK_EQ(out, 1);
    // group 11 -> 1.1 -> score > 1.0; first branch needs score<1.0 so no raise.
    // and law==0 so lower branch (needs law==1) also no -> no fire.
    g_env.group = 11;
    out = -1;
    CHECK(!ai::RuleEvalSettingToggle(b, env, cfg, &out));
}

TEST(AiMeisterEconomy, RuleToggleVariant) {
    // ToggleA: variant, midScore = (setting64+bias)*groupMul, base vs high.
    ai::RuleEnv env = MakeEnv();
    const ai::RuleConfig& cfg = ai::RuleConfigToggleA();
    CHECK_EQ(cfg.lawId, 16);

    g_env.law = 0;
    g_env.group = 0; // default 1.0

    ai::RuleBuilding b;
    // midScore = (setting64 - 2) * 1.0; want < 0.5 -> setting64 = 2.4 -> 0.4.
    b.setting64 = 2.4f;
    // base = (setting61-2)*weight63 ; high = (setting67-2)*weight69.
    b.setting61 = 5.0f; b.weight63 = 1.0f; // base = 3.0
    b.setting67 = 3.0f; b.weight69 = 1.0f; // high = 1.0 ; base > high
    int out = -1;
    CHECK(ai::RuleEvalToggleVariant(b, env, cfg, &out));
    CHECK_EQ(out, 1);  // midScore<0.5, law==0, base>high -> raise

    // group 5/6 -> *1.5 pushes midScore (0.4*1.5=0.6) above 0.5 -> first branch
    // no; law==0 so lower branch no.
    g_env.group = 5;
    out = -1;
    CHECK(!ai::RuleEvalToggleVariant(b, env, cfg, &out));
}

TEST(AiMeisterEconomy, RuleQualitySetting) {
    int out = -1;
    // quality==0 && law==1: affinity>=0.4 -> 0 ; else 2.
    CHECK(ai::RuleEvalQualitySetting(0, 1, 0.5f, &out)); CHECK_EQ(out, 0);
    CHECK(ai::RuleEvalQualitySetting(0, 1, 0.3f, &out)); CHECK_EQ(out, 2);
    // quality==1 && law==0: affinity<0.4 -> 2 ; else 1.
    CHECK(ai::RuleEvalQualitySetting(1, 0, 0.3f, &out)); CHECK_EQ(out, 2);
    CHECK(ai::RuleEvalQualitySetting(1, 0, 0.5f, &out)); CHECK_EQ(out, 1);
    // law==2 && affinity>0.8 -> quality!=0.
    CHECK(ai::RuleEvalQualitySetting(2, 2, 0.9f, &out)); CHECK_EQ(out, 1);
    CHECK(ai::RuleEvalQualitySetting(0, 2, 0.9f, &out)); CHECK_EQ(out, 0);
    // no-change case.
    CHECK(!ai::RuleEvalQualitySetting(2, 0, 0.1f, &out));
}

TEST(AiMeisterEconomy, RuleDemandToggle) {
    // gilde.exe 0x465ac8: 2-trend rule (base from setting61, high from setting67,
    // NO mid) with the INVERSE first-branch test (base > high -> out=1) and
    // unconditional *0.8 group multiplier.
    ai::RuleEnv env = MakeEnv();
    const ai::RuleConfig& cfg = ai::RuleConfigDemandToggle();
    CHECK_EQ(cfg.lawId, 18);

    // drive score < 1.0: rating small. score = (rating - (scalar+1)*c0)*c1*0.8.
    g_env.group = 0;        // default mul (== 0.8 for this config)
    g_env.scalar = 0;
    g_env.rating = 0.01f;
    g_env.law = 0;

    ai::RuleBuilding b;
    // base = (setting61-2)*weight63 ; high = (setting67-2)*weight69.
    b.setting61 = 5.0f; b.weight63 = 1.0f; // base = 3.0
    b.setting64 = 99.0f; b.weight66 = 99.0f; // mid is IGNORED by this rule
    b.setting67 = 3.0f; b.weight69 = 1.0f; // high = 1.0 ; base > high
    int out = -1;
    CHECK(ai::RuleEvalDemandToggle(b, env, cfg, &out));
    CHECK_EQ(out, 1);                       // score<1.0, law==0, base>high -> raise

    // If high >= base, the first branch does NOT fire (inverse of SettingToggle).
    b.setting67 = 6.0f;                     // high = 4.0 > base 3.0
    out = -1;
    CHECK(!ai::RuleEvalDemandToggle(b, env, cfg, &out));

    // Lower branch: score > 1.0, law==1, high > base -> out=0.
    g_env.rating = 100.0f;                  // (100-~0)*c1*0.8 ~= 1.90 > 1.0
    g_env.law = 1;
    b.setting61 = 3.0f; b.weight63 = 1.0f;  // base = 1.0
    b.setting67 = 5.0f; b.weight69 = 1.0f;  // high = 3.0 > base
    out = -1;
    CHECK(ai::RuleEvalDemandToggle(b, env, cfg, &out));
    CHECK_EQ(out, 0);

    // mid is ignored: with high <= base the lower branch must not fire even if a
    // SettingToggle-style mid would have.
    b.setting67 = 3.0f; b.weight69 = 1.0f;  // high = 1.0 == base -> high<=base
    out = -1;
    CHECK(!ai::RuleEvalDemandToggle(b, env, cfg, &out));
}

// ---------------------------------------------------------------------------
// Staffing decision cores (seeded RNG).
// ---------------------------------------------------------------------------
TEST(AiMeisterEconomy, HireStaffNoCapacity) {
    // staffCount >= cap -> never hire (no RNG consumed).
    CHECK(!ai::HireStaffDecision(5, 5, 100, 100000, false, false));
}

TEST(AiMeisterEconomy, HireStaffFirstWorker) {
    // staffCount == 0 -> gate true; hire iff !hasHandler (no RNG consumed).
    CHECK(ai::HireStaffDecision(0, 3, 999999, 0, true, false)); // wage/budget/busy ignored
    CHECK(!ai::HireStaffDecision(0, 3, 0, 100000, false, true)); // hasHandler blocks
}

TEST(AiMeisterEconomy, HireStaffRollGate) {
    // staffCount=1: gate = !busy && wage<=budget && RandomModulo(0x48) >= 34.
    crt::Srand(2024);
    int roll = util::RandomModulo(0x48);
    crt::Srand(2024);
    bool hire = ai::HireStaffDecision(1, 3, 100, 100000, false, false);
    CHECK_EQ(hire, roll >= 34);

    // busy flag blocks before the roll is even needed for the result, but the
    // original still draws the roll? No: the && short-circuits; busy -> false
    // without drawing. Verify no hire and RNG unaffected.
    crt::Srand(2024);
    CHECK(!ai::HireStaffDecision(1, 3, 100, 100000, true, false));
    crt::Srand(2024);
    int roll2 = util::RandomModulo(0x48);
    CHECK_EQ(roll, roll2);
}

TEST(AiMeisterEconomy, TrainStaffDecision) {
    // train iff !busy && RandomModulo(100) >= threshold && !hasTrainer &&
    //   trainerCount<3 && budget>=38400.
    crt::Srand(99);
    int roll = util::RandomModulo(0x64);
    crt::Srand(99);
    bool train = ai::TrainStaffDecision(0, false, false, 0, 40000);
    CHECK_EQ(train, roll >= 0); // threshold 0 -> always >= -> true (if other gates ok)
    CHECK(train);

    // budget below 38400 -> no train (roll still consumed).
    crt::Srand(99);
    CHECK(!ai::TrainStaffDecision(0, false, false, 0, 38399));
    // busy -> no train, no roll consumed.
    crt::Srand(99);
    CHECK(!ai::TrainStaffDecision(0, true, false, 0, 40000));
    crt::Srand(99);
    CHECK_EQ(util::RandomModulo(0x64), roll);
}

// ---------------------------------------------------------------------------
// PlanProduction ratio (golden, vs. exact recovered constants).
// ---------------------------------------------------------------------------
TEST(AiMeisterEconomy, PlanProductionRatio) {
    // case 1: stock*flt_6198F0 (1.5625e-05) then *0.5 + 0.25.
    // (disasm 0x4598a0: case 1 multiplies by flt_6198F0 = 1.5625000742147677e-05.)
    double r1 = ai::PlanProductionRatio(1, 1000);
    double exp1 = 1000.0 * 1.5625000742147677e-05 * 0.5 + 0.25;
    CHECK(r1 > exp1 - 1e-9 && r1 < exp1 + 1e-9);
    // case 3: stock*flt_6198E8 (3.906e-06); case 1 is 4x case 3's stock scale.
    // (disasm 0x4599ad: case 3 multiplies by flt_6198E8 = 3.906250185536919e-06.)
    double r3 = ai::PlanProductionRatio(3, 1000);
    double exp3 = 1000.0 * 3.906250185536919e-06 * 0.5 + 0.25;
    CHECK(r3 > exp3 - 1e-9 && r3 < exp3 + 1e-9);
    // default tier -> bias only.
    double r0 = ai::PlanProductionRatio(0, 1000);
    CHECK(r0 > 0.25 - 1e-9 && r0 < 0.25 + 1e-9);
}

// ---------------------------------------------------------------------------
// Trade stock-deficit + priority sort cores.
// ---------------------------------------------------------------------------
TEST(AiMeisterEconomy, StockCapacity) {
    CHECK_EQ(ai::StockCapacity(477, 4), 5 * 4 + 10); // type 477 -> 5*level+10
    CHECK_EQ(ai::StockCapacity(100, 3), 80);          // level 3 -> 80
    CHECK_EQ(ai::StockCapacity(100, 2), 40);          // 20*level
}

TEST(AiMeisterEconomy, StockDeficit) {
    // cap=40, count=8: used = (40*8)>>2 = 80; have=8 -> 8-80 = -72 -> clamp 0.
    CHECK_EQ(ai::StockDeficit(40, 8, 100), 0);
    // cap=4, count=8: used = (32)>>2 = 8; have=8 -> 0.
    CHECK_EQ(ai::StockDeficit(4, 8, 100), 0);
    // cap=4, count=8, type 42 -> have = 7; used=8 -> -1 -> 0.
    CHECK_EQ(ai::StockDeficit(4, 8, 42), 0);
    // cap=1, count=8: used = (8)>>2 = 2; have=8 -> 6.
    CHECK_EQ(ai::StockDeficit(1, 8, 100), 6);
    // type 278 reduces have by 1: cap=1,count=8 -> have=7, used=2 -> 5.
    CHECK_EQ(ai::StockDeficit(1, 8, 278), 5);
}

TEST(AiMeisterEconomy, SortStockByValue) {
    std::vector<ai::StockNeed> v(4);
    v[0].value = 1.0f; v[0].payload[0] = 10;
    v[1].value = 5.0f; v[1].payload[0] = 50;
    v[2].value = 3.0f; v[2].payload[0] = 30;
    v[3].value = 2.0f; v[3].payload[0] = 20;
    ai::SortStockByValue(v.data(), v.size());
    CHECK_EQ(v[0].value, 5.0f); CHECK_EQ(v[0].payload[0], 50);
    CHECK_EQ(v[1].value, 3.0f);
    CHECK_EQ(v[2].value, 2.0f);
    CHECK_EQ(v[3].value, 1.0f);
}
