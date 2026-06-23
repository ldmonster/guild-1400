// Golden-vector tests for the MeisterAi rule evaluators (ai_recon2).
// Values are derived directly from the gilde.exe decision math; see header.
#include "tests/framework/test.h"

#include "src/sim/meisterai_ruleeval_ai_recon2.h"

using namespace guild::sim;

// ProductionRatingScore: pre = (rate+1)*0.00396825 ; score = (rating-pre)*0.02380952
TEST(Ai2ReconRuleEval, ProductionScoreBasic) {
    // rate=0 → pre = 1*0.00396825 ; rating=42 → score=(42-0.00396825)/42 ≈ 0.99990...
    float s = ProductionRatingScore(0, 42.0f);
    CHECK(s > 0.999f && s < 1.0f);
    // rate=251 (max), rating=0 → pre=(252)*0.00396825=1.0 → score=(0-1.0)*1/42 < 0
    float s2 = ProductionRatingScore(251, 0.0f);
    CHECK(s2 < 0.0f);
}

TEST(Ai2ReconRuleEval, PolicyTerm) {
    // (f + (-2.0)) * w
    CHECK(PolicyTerm(5.0f, 2.0f) == 6.0f);   // (5-2)*2
    CHECK(PolicyTerm(2.0f, 10.0f) == 0.0f);  // (2-2)*10
    CHECK(PolicyTerm(0.0f, 1.0f) == -2.0f);  // (0-2)*1
}

TEST(Ai2ReconRuleEval, ToggleDecisionTurnOn) {
    // score<1.0 && cur==0 && offTerm>refTerm → *out=1, return 1.
    int out = -99;
    int r = ToggleDecision(/*score*/0.5f, /*on*/0.0f, /*off*/5.0f, /*ref*/1.0f,
                           /*cur*/0, &out);
    CHECK_EQ(r, 1);
    CHECK_EQ(out, 1);
}

TEST(Ai2ReconRuleEval, ToggleDecisionTurnOnRefuse) {
    // score<1.0 && cur==0 but offTerm<=refTerm → return 0, out untouched.
    int out = -99;
    int r = ToggleDecision(0.5f, 0.0f, 1.0f, 5.0f, 0, &out);
    CHECK_EQ(r, 0);
    CHECK_EQ(out, -99);
}

TEST(Ai2ReconRuleEval, ToggleDecisionTurnOff) {
    // score>1.0 && cur==1 && onTerm>refTerm → *out=0, return 1.
    int out = -99;
    int r = ToggleDecision(2.0f, /*on*/5.0f, /*off*/0.0f, /*ref*/1.0f, /*cur*/1, &out);
    CHECK_EQ(r, 1);
    CHECK_EQ(out, 0);
}

TEST(Ai2ReconRuleEval, ToggleDecisionNoChange) {
    // score>1.0, cur==1, onTerm<=refTerm → 0.
    int out = -99;
    int r = ToggleDecision(2.0f, 1.0f, 0.0f, 5.0f, 1, &out);
    CHECK_EQ(r, 0);
    CHECK_EQ(out, -99);
    // score>1.0 but cur==0 → 0 (no on-gate match, off-gate skipped).
    out = -99;
    r = ToggleDecision(2.0f, 5.0f, 0.0f, 1.0f, 0, &out);
    CHECK_EQ(r, 0);
}

TEST(Ai2ReconRuleEval, EvalWageLevelTurnOn) {
    // Pick a low score → first branch. rate=255, rating=0 → pre≈1.016 → score<0,
    // *1.1 still <0. cur=0, off=(f67-2)*f69 large, ref=(f61-2)*f63 small.
    int out = -1;
    GesetzRecord law; law.cur = 0;
    int r = EvalWageLevel(255, 0.0f, /*f61*/3, /*f63*/1, /*f64*/0, /*f66*/0,
                          /*f67*/10, /*f69*/2, law, &out);
    // off=(10-2)*2=16, ref=(3-2)*1=1 → 16>1 → turn on.
    CHECK_EQ(r, 1);
    CHECK_EQ(out, 1);
}

TEST(Ai2ReconRuleEval, EvalServiceLevelGroupScale) {
    // group 7 scales score by 1.1; group 11/12 by 0.9. Use a score crossing 1.0
    // boundary depending on scale to detect the multiply.
    int out = -1;
    GesetzRecord law; law.cur = 1;
    // Construct rating so base score ~1.05; *0.9 → ~0.94 (<1 path), *1.1 → ~1.15 (>1).
    // rate=0 → pre=0.00396825. score=(rating-pre)/42. For score≈1.05, rating≈44.1.
    float rating = 44.1f;
    // group 11 (×0.9): score ≈ 0.945 < 1.0 → on-branch skipped, off branch:
    //   cur==1 so first if (score<1 && cur==0) false; second: score<=1.0? 0.945<=1 true → return 0
    int r11 = EvalServiceLevel(11, 0, rating, 3, 1, 0, 0, 10, 2, law, &out);
    CHECK_EQ(r11, 0);
    // group 7 (×1.1): score ≈ 1.155 > 1.0, cur==1, on=(f64-2)*f66. Set f64=20 → on big.
    out = -1;
    int r7 = EvalServiceLevel(7, 0, rating, /*f61*/3, /*f63*/1, /*f64*/20, /*f66*/2,
                              /*f67*/0, /*f69*/0, law, &out);
    // on=(20-2)*2=36 > ref=(3-2)*1=1 → turn off → out=0,return1.
    CHECK_EQ(r7, 1);
    CHECK_EQ(out, 0);
}

TEST(Ai2ReconRuleEval, EvalToggleBShopScale) {
    // groups 5/6 scale `on` by 1.5. on = f64-2. f64=3 → on=1.0 ; *1.5 → 1.5.
    // on<0.5? no. on<=0.5(double)? no. cur must be 1 and off>ref to turn off.
    int out = -1;
    GesetzRecord law; law.cur = 1;
    int r = EvalToggleB(5, /*f61*/3, /*f63*/1, /*f64*/3, /*f67*/10, /*f69*/2, law, &out);
    // off=(10-2)*2=16, ref=(3-2)*1=1 → off>ref, on(1.5)>0.5 → turn off.
    CHECK_EQ(r, 1);
    CHECK_EQ(out, 0);
    // turn-on path: on small (<0.5), cur=0, ref>off.
    out = -1; law.cur = 0;
    // f64=2 → on=0 ; group default (no scale) → on stays 0 (<0.5). ref=(10-2)*2=16, off=(3-2)*1=1.
    int r2 = EvalToggleB(1, /*f61*/10, /*f63*/2, /*f64*/2, /*f67*/3, /*f69*/1, law, &out);
    CHECK_EQ(r2, 1);
    CHECK_EQ(out, 1);
}

TEST(Ai2ReconRuleEval, EvalDecreaseSetting) {
    // computed = trunc((3.0 - f43) * 5.0).
    int out = -1;
    GesetzRecord law; law.cur = 10;
    // f43=1.0 → (3-1)*5 = 10 == cur → no change.
    CHECK_EQ(EvalDecreaseSetting(1.0f, law, &out), 0);
    // f43=0.0 → 15 != 10 → out=15.
    out = -1;
    CHECK_EQ(EvalDecreaseSetting(0.0f, law, &out), 1);
    CHECK_EQ(out, 15);
    // f43=2.5 → (0.5)*5 = 2.5 → trunc → 2.
    out = -1;
    CHECK_EQ(EvalDecreaseSetting(2.5f, law, &out), 1);
    CHECK_EQ(out, 2);
}

TEST(Ai2ReconRuleEval, ClassifyQualityTier) {
    int ref = 10;
    CHECK_EQ(ClassifyQualityTier(20, ref), 4);   // < 3*ref=30
    CHECK_EQ(ClassifyQualityTier(29, ref), 4);
    CHECK_EQ(ClassifyQualityTier(30, ref), 3);   // not < 30, not > others
    CHECK_EQ(ClassifyQualityTier(65, ref), 2);   // > 6*ref=60, not > 8*ref=80
    CHECK_EQ(ClassifyQualityTier(90, ref), 1);   // > 8*ref=80, not > 12*ref=120
    CHECK_EQ(ClassifyQualityTier(130, ref), 0);  // > 120
}

TEST(Ai2ReconRuleEval, EvalQualityTierStep) {
    int out = -1;
    GesetzRecord law; law.cur = 2; law.loBound = 0; law.hiBound = 4;
    // target=2 → |2-2|=0 < 2 → no change.
    CHECK_EQ(EvalQualityTierStep(2, law, &out), 0);
    // target=3 → |2-3|=1 < 2 → no change.
    CHECK_EQ(EvalQualityTierStep(3, law, &out), 0);
    // target=4 → |2-4|=2 not <2 → step toward cur: 4>2 → v8=cur+1=3 ; clamp [0,4] → 3.
    out = -1;
    CHECK_EQ(EvalQualityTierStep(4, law, &out), 1);
    CHECK_EQ(out, 3);
    // target=0 → step: 0<2 → v8=cur-1=1 ; clamp → 1.
    out = -1;
    CHECK_EQ(EvalQualityTierStep(0, law, &out), 1);
    CHECK_EQ(out, 1);
}

TEST(Ai2ReconRuleEval, EvalQualityTierStepClampLo) {
    // cur low, target far below → step to cur-1 then clamp up to lo.
    int out = -1;
    GesetzRecord law; law.cur = 0; law.loBound = 0; law.hiBound = 4;
    // target=3 → |0-3|=3 not<2 → 3>0 → v8=cur+1=1 ; v9=min(1,hi=4)=1 ; 1<=lo(0)? no → out=1.
    CHECK_EQ(EvalQualityTierStep(3, law, &out), 1);
    CHECK_EQ(out, 1);
}

TEST(Ai2ReconRuleEval, StockWeightedTarget) {
    // span = hi-lo. avgQ>=Q2(0.66) → w=span*0.66 ; Q1<=avgQ<Q2 → span*0.44 ; else span*0.22.
    // lo=10, hi=110 → span=100. float weights truncate just below the round value:
    //   0.66f≈0.66000003 → 100*w=66.0000026 → 10+ → trunc 76
    //   0.44f≈0.43999999 → 100*w=43.99999976 → 10+ → trunc 53
    //   0.22f≈0.21999999 → 100*w=21.99999988 → 10+ → trunc 31
    CHECK_EQ(StockWeightedTarget(10, 110, 0.7f), 76);   // 0.7>=0.66 → hi weight
    CHECK_EQ(StockWeightedTarget(10, 110, 0.5f), 53);   // 0.33<=0.5<0.66 → mid weight
    CHECK_EQ(StockWeightedTarget(10, 110, 0.1f), 31);   // <0.33 → lo weight
}
