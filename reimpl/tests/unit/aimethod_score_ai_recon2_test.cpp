// Golden-vector tests for the AiMethod scoring/selection kernels (ai_recon2).
#include "tests/framework/test.h"

#include "src/sim/aimethod_score_ai_recon2.h"

using namespace guild::sim;

TEST(Ai2ReconMethod, AttackFavorabilityAccept) {
    // accept iff (roll + 40.0) > fav.
    CHECK(AttackFavorabilityAccept(0, 39.0f));    // 40 > 39
    CHECK(!AttackFavorabilityAccept(0, 40.0f));   // 40 > 40 false
    CHECK(AttackFavorabilityAccept(29, 68.0f));   // 69 > 68
    CHECK(!AttackFavorabilityAccept(10, 51.0f));  // 50 > 51 false
}

TEST(Ai2ReconMethod, WealthScoreFloored) {
    // score = trunc(wealth * 0.015f) ; floor at 800. 0.015f≈0.0149999997 so the
    // products truncate just below the round value (a 1:1 fidelity detail):
    CHECK_EQ(WealthScoreFloored(0), 800);          // 0 → floored
    // 50000*0.015f = 749.99998 → trunc 749 → <=800 → floored to 800.
    CHECK_EQ(WealthScoreFloored(50000), 800);
    // 100000*0.015f = 1499.99997 → trunc 1499 > 800.
    CHECK_EQ(WealthScoreFloored(100000), 1499);
    // 53400*0.015f = 800.99998 → trunc 800 → still <=800 → floored to 800.
    CHECK_EQ(WealthScoreFloored(53400), 800);
    // 60000*0.015f = 899.99998 → trunc 899 > 800.
    CHECK_EQ(WealthScoreFloored(60000), 899);
}

TEST(Ai2ReconMethod, MethodEmitWeight) {
    float out = 0.0f;
    // slot < 0 → skipped.
    CHECK(!MethodEmitWeight(-1, 2.0f, &out));
    // slot >= 0 → emit = -raw * 0.5.
    CHECK(MethodEmitWeight(0, 2.0f, &out));
    CHECK(out == -1.0f);
    CHECK(MethodEmitWeight(5, -4.0f, &out));
    CHECK(out == 2.0f);   // -(-4)*0.5 = 2
}

TEST(Ai2ReconMethod, MoveScoreContribution) {
    // sameFaction → *0.02 ; other → *0.01 ; gated on slot>=0 && weight>0.
    // 5 * 10 * 0.02 = 1.0
    CHECK(MoveScoreContribution(0, 5.0f, 10.0f, true) == 1.0f);
    // 5 * 10 * 0.01 = 0.5
    CHECK(MoveScoreContribution(0, 5.0f, 10.0f, false) == 0.5f);
    // slot < 0 → 0
    CHECK(MoveScoreContribution(-1, 5.0f, 10.0f, true) == 0.0f);
    // weight <= 0 → 0
    CHECK(MoveScoreContribution(0, 0.0f, 10.0f, true) == 0.0f);
}

TEST(Ai2ReconMethod, PickMostDislikedRelation) {
    // min below 100.0 ; -1 if none.
    CHECK_EQ(PickMostDislikedRelation({50.0f, 30.0f, 70.0f}), 1);
    CHECK_EQ(PickMostDislikedRelation({100.0f, 100.0f, 100.0f}), -1);
    CHECK_EQ(PickMostDislikedRelation({99.0f, 100.0f, 100.0f}), 0);
    // ties: strict < keeps first.
    CHECK_EQ(PickMostDislikedRelation({40.0f, 40.0f, 90.0f}), 0);
}

TEST(Ai2ReconMethod, SelectBestMethodEmpty) {
    MethodChoice c = SelectBestAiMethod({});
    // bestA stays -1e30 (bits != 0) and bestA >= bestB → choose A winner = -1.
    CHECK_EQ(c.id, -1);
}

TEST(Ai2ReconMethod, SelectBestMethodAWins) {
    std::vector<MethodCandidate> cands = {
        {10, 5.0f, 1.0f},
        {20, 3.0f, 8.0f},
        {30, 6.0f, 2.0f},
    };
    // bestA: id30 (6.0). bestB: id20 (8.0). bestA(6) >= bestB(8)? no → choose B (id20).
    MethodChoice c = SelectBestAiMethod(cands);
    CHECK_EQ(c.id, 20);
    CHECK(!c.choseA);
}

TEST(Ai2ReconMethod, SelectBestMethodChooseA) {
    std::vector<MethodCandidate> cands = {
        {10, 9.0f, 1.0f},
        {20, 3.0f, 5.0f},
    };
    // bestA=9(id10), bestB=5(id20). 9>=5 → choose A (id10).
    MethodChoice c = SelectBestAiMethod(cands);
    CHECK_EQ(c.id, 10);
    CHECK(c.choseA);
    CHECK(c.scoreA == 9.0f);
}

TEST(Ai2ReconMethod, SelectBestMethodTieLastWins) {
    // >= update → last candidate at the max wins each best.
    std::vector<MethodCandidate> cands = {
        {10, 5.0f, 5.0f},
        {20, 5.0f, 5.0f},
    };
    // both update bestA and bestB to id20. bestA(5)>=bestB(5) → A winner id20.
    MethodChoice c = SelectBestAiMethod(cands);
    CHECK_EQ(c.id, 20);
    CHECK(c.choseA);
}
