// Golden tests for reconstructed AI decision logic (ai_recon5_decisions).
// Mirrors gilde.exe 0x46670c / 0x4675b8 / 0x47a524 / 0x47d568.
#include "tests/framework/test.h"
#include "sim/ai_recon5_decisions.h"

#include <cmath>
#include <cstring>
#include <initializer_list>

using namespace guild::sim;

namespace {

// Deterministic RNG helpers shared by tests.
int g_rngSeq[8];
int g_rngIdx;
int g_rngN;
int RngScripted() {
    if (g_rngIdx >= g_rngN) return 0;
    return g_rngSeq[g_rngIdx++];
}
void RngReset(std::initializer_list<int> seq) {
    g_rngN = 0;
    for (int v : seq) { if (g_rngN < 8) g_rngSeq[g_rngN++] = v; }
    g_rngIdx = 0;
}

double RandScaleHost() {
    unsigned bits = 0x38000100u; float f; std::memcpy(&f, &bits, sizeof(f));
    return (double)f;
}

} // namespace

// ---- RNG primitives ---------------------------------------------------------
TEST(AiRecon5, RandomFloatScaled_inert_zero) {
    CHECK_EQ(RandomFloatScaled(kInertAiRecon5Hooks), 0.0);
}
TEST(AiRecon5, RandomFloatScaled_scales) {
    AiRecon5Hooks hk; RngReset({32768}); hk.randNext = RngScripted;
    double got = RandomFloatScaled(hk);
    double want = 32768.0 * RandScaleHost();
    CHECK(std::fabs(got - want) < 1e-9);
}
TEST(AiRecon5, RandomModulo_zero_n) {
    CHECK_EQ(RandomModulo(kInertAiRecon5Hooks, 0), 0);
}
TEST(AiRecon5, RandomModulo_mods) {
    AiRecon5Hooks hk; RngReset({17}); hk.randNext = RngScripted;
    CHECK_EQ(RandomModulo(hk, 5), 2);
}

// ---- PlaceBet ---------------------------------------------------------------
TEST(AiRecon5, PlaceBet_guard_rejects_bad_type) {
    CardBet b = AiCardGame_PlaceBet(kInertAiRecon5Hooks, 1234, 5 /*!=6*/, 2, 0.5f);
    CHECK(!b.ok);
}
TEST(AiRecon5, PlaceBet_guard_rejects_zero_person) {
    CardBet b = AiCardGame_PlaceBet(kInertAiRecon5Hooks, 0, 6, 2, 0.5f);
    CHECK(!b.ok);
}
TEST(AiRecon5, PlaceBet_guard_rejects_bad_mult) {
    CardBet b = AiCardGame_PlaceBet(kInertAiRecon5Hooks, 7, 6, 3 /*not 1/2/4*/, 0.5f);
    CHECK(!b.ok);
}

TEST(AiRecon5, PlaceBet_curve_and_seatA) {
    // disp below 10000 -> w clamps to 10000.0.
    AiRecon5Hooks hk;
    hk.personTotalWealth = [](int){ return 500; };
    hk.moneyToDisplay = [](int w, unsigned char){ return w; }; // returns 500 < 10000
    hk.moneyMulByRate = [](int v, unsigned char){ return v; }; // identity
    hk.selectMoodColor = [](){ return (short)7; };
    // First RNG draw is the seat pick; agg=0.5 so pick must be > 0.5*... to take
    // else-branch (seat A). Use a big rand for the pick, small for the nudge.
    RngReset({1 << 20, 0});
    hk.randNext = RngScripted;

    double w = 10000.0;
    double v12 = w * 0.1 / (std::log10(w) + 1.0) * (2.0 * 0.25);
    int stakeA = (int)v12;
    int stakeB = (int)((double)stakeA * 0.1f);

    CardBet b = AiCardGame_PlaceBet(hk, 42, 6, 2, 0.5f);
    CHECK(b.ok);
    CHECK_EQ(b.stakeA, stakeA);
    CHECK_EQ(b.stakeB, stakeB);
    CHECK_EQ(b.moodColor, (short)7);
    CHECK_EQ(b.humanSeat, 0); // big pick > agg -> else branch -> seat A
    // agg nudge (seat A): agg + 0.2 + rand2*0.11 ; rand2==0 -> 0.7
    CHECK(std::fabs(b.familyAgg - 0.7f) < 1e-5f);
}

TEST(AiRecon5, PlaceBet_seatB_branch) {
    AiRecon5Hooks hk;
    hk.personTotalWealth = [](int){ return 0; };
    hk.moneyToDisplay = [](int, unsigned char){ return 0; };
    hk.moneyMulByRate = [](int v, unsigned char){ return v; };
    hk.selectMoodColor = [](){ return (short)3; };
    RngReset({0, 0}); // pick==0 <= agg -> if-branch (seat B); rand2==0
    hk.randNext = RngScripted;
    CardBet b = AiCardGame_PlaceBet(hk, 9, 6, 4, 0.9f);
    CHECK(b.ok);
    CHECK_EQ(b.humanSeat, 1);
    // agg nudge (seat B): agg - 0.2 - rand2*0.11 ; rand2==0 -> 0.7
    CHECK(std::fabs(b.familyAgg - 0.7f) < 1e-5f);
    // disp 0 < 10000 -> w=10000; mult=4
    double w = 10000.0;
    double v12 = w * 0.1 / (std::log10(w) + 1.0) * (4.0 * 0.25);
    CHECK_EQ(b.stakeA, (int)v12);
}

// ---- ExecThreaten -----------------------------------------------------------
TEST(AiRecon5, Threaten_reject_bad_cmd) {
    ThreatenDecision d = AiPlayer_ExecThreaten(kInertAiRecon5Hooks, 6, true);
    CHECK_EQ(d.result, 0);
    CHECK(!d.doSlotReset);
}
TEST(AiRecon5, Threaten_reject_no_target) {
    ThreatenDecision d = AiPlayer_ExecThreaten(kInertAiRecon5Hooks, 7, false);
    CHECK_EQ(d.result, 0);
}
TEST(AiRecon5, Threaten_executes_no_reset) {
    AiRecon5Hooks hk;
    hk.ratingCurveA = [](){ return 0.2f; };
    RngReset({0}); hk.randNext = RngScripted; // rand 0 -> 0.2 + 0 < 1.0
    ThreatenDecision d = AiPlayer_ExecThreaten(hk, 7, true);
    CHECK_EQ(d.result, 1);
    CHECK(!d.doSlotReset);
}
TEST(AiRecon5, Threaten_executes_with_reset) {
    AiRecon5Hooks hk;
    hk.ratingCurveA = [](){ return 0.6f; };
    // need rating + rand >= 1.0 ; rand = big * scale
    RngReset({1 << 16}); hk.randNext = RngScripted; // 65536*~3.05e-5 ~= 2.0
    ThreatenDecision d = AiPlayer_ExecThreaten(hk, 7, true);
    CHECK_EQ(d.result, 1);
    CHECK(d.doSlotReset);
}

// ---- SelectConversationTarget ----------------------------------------------
TEST(AiRecon5, Conv_default_returns_self_when_talkable) {
    ConvSelfRecord self; self.present = true; self.aliveByte = 1;
    self.typeByte = 5; self.flag229 = 0; self.recordId = 100;
    ConvCandidate slots[1]{};
    ConvResult r = AiMethod_SelectConversationTarget(
        kInertAiRecon5Hooks, slots, 0, /*personType*/3, self);
    CHECK(r.haveTarget);
    CHECK_EQ(r.targetId, 100);
}
TEST(AiRecon5, Conv_default_self_blocked_by_flag) {
    ConvSelfRecord self; self.present = true; self.aliveByte = 1;
    self.typeByte = 5; self.flag229 = 4 /*blocks*/; self.recordId = 100;
    ConvCandidate slots[1]{};
    ConvResult r = AiMethod_SelectConversationTarget(
        kInertAiRecon5Hooks, slots, 0, 3, self);
    CHECK(!r.haveTarget);
}
TEST(AiRecon5, Conv_default_picks_eligible_candidate) {
    AiRecon5Hooks hk; RngReset({0}); hk.randNext = RngScripted; // mod -> idx 0
    ConvSelfRecord self; self.present = false; // self not talkable
    ConvCandidate slots[2]{};
    slots[0].present = true; slots[0].aliveByte = 1; slots[0].relation = 20;
    slots[0].typeByte = 5; slots[0].recordId = 55;
    slots[1].present = false;
    ConvResult r = AiMethod_SelectConversationTarget(hk, slots, 2, 3, self);
    CHECK(r.haveTarget);
    CHECK_EQ(r.targetId, 55);
}
TEST(AiRecon5, Conv_relation_threshold_excludes) {
    AiRecon5Hooks hk; RngReset({0}); hk.randNext = RngScripted;
    ConvSelfRecord self; self.present = false;
    ConvCandidate slots[1]{};
    slots[0].present = true; slots[0].aliveByte = 1;
    slots[0].relation = 0x0B; // not > 0x0B -> not eligible
    slots[0].typeByte = 5; slots[0].recordId = 7;
    ConvResult r = AiMethod_SelectConversationTarget(hk, slots, 1, 3, self);
    CHECK(!r.haveTarget);
}
TEST(AiRecon5, Conv_leave_path_returns_pick) {
    AiRecon5Hooks hk; RngReset({0}); hk.randNext = RngScripted;
    ConvSelfRecord self; self.present = false;
    ConvCandidate slots[1]{};
    slots[0].present = true; slots[0].aliveByte = 1; slots[0].relation = 30;
    slots[0].typeByte = 4; slots[0].recordId = 88;
    ConvResult r = AiMethod_SelectConversationTarget(
        hk, slots, 1, /*personType*/5, self);
    CHECK(r.haveTarget);
    CHECK_EQ(r.targetId, 88);
}
TEST(AiRecon5, Conv_pick_type_gate_excludes) {
    // pick exists but its type byte not in (1,10) -> no target in default path.
    AiRecon5Hooks hk; RngReset({0}); hk.randNext = RngScripted;
    ConvSelfRecord self; self.present = false;
    ConvCandidate slots[1]{};
    slots[0].present = true; slots[0].aliveByte = 1; slots[0].relation = 30;
    slots[0].typeByte = 10; /*not <10*/ slots[0].recordId = 12;
    ConvResult r = AiMethod_SelectConversationTarget(hk, slots, 1, 3, self);
    CHECK(!r.haveTarget);
}

// ---- CountInventoryMatch ----------------------------------------------------
TEST(AiRecon5, Inv_empty_recipe_returns_zero) {
    InvIngredient ings[1]{};
    InvMatch m = AiObject_CountInventoryMatch(ings, 0, 1);
    CHECK_EQ(m.matched, 0);
}
TEST(AiRecon5, Inv_cat33_always_satisfiable) {
    InvIngredient ings[1]{};
    ings[0].classWord = 0x0042; ings[0].itemCategory = 33;
    InvMatch m = AiObject_CountInventoryMatch(ings, 1, 1);
    CHECK_EQ(m.matched, 0); // cat 33 skipped, recipe ends -> 0
}
TEST(AiRecon5, Inv_direct_need_missing) {
    // cat not 33/2/6 -> LABEL_36 direct: needCovered false, no extra gates -> miss.
    InvIngredient ings[1]{};
    ings[0].classWord = 0x8055; // top bit set, should mask to 0x0055
    ings[0].itemCategory = 5;   // "other"
    ings[0].needCovered = false;
    InvMatch m = AiObject_CountInventoryMatch(ings, 1, 1);
    CHECK_EQ(m.matched, 1);
    CHECK_EQ(m.classWord, (unsigned short)0x0055);
    CHECK_EQ(m.objId, -1);
}
TEST(AiRecon5, Inv_direct_need_covered_passes) {
    InvIngredient ings[1]{};
    ings[0].classWord = 0x0010; ings[0].itemCategory = 5;
    ings[0].needCovered = true; // already have it -> not missing
    InvMatch m = AiObject_CountInventoryMatch(ings, 1, 1);
    CHECK_EQ(m.matched, 0);
}
TEST(AiRecon5, Inv_stock_match_satisfies) {
    InvIngredient ings[1]{};
    ings[0].classWord = 0x0021; ings[0].itemCategory = 2;
    ings[0].foundInStock = true; ings[0].supplyObjId = 999;
    // found -> not missing; recipe ends -> 0
    InvMatch m = AiObject_CountInventoryMatch(ings, 1, 1);
    CHECK_EQ(m.matched, 0);
}
TEST(AiRecon5, Inv_buy_fallback_then_miss) {
    // cat 2: not in stock, owned<threshold, no for-sale, need not covered -> miss.
    InvIngredient ings[1]{};
    ings[0].classWord = 0x0021; ings[0].itemCategory = 6;
    ings[0].ownedCount = 0; ings[0].foundInStock = false;
    ings[0].foundForSale = false; ings[0].needCovered = false;
    InvMatch m = AiObject_CountInventoryMatch(ings, 1, 1);
    CHECK_EQ(m.matched, 1);
    CHECK_EQ(m.classWord, (unsigned short)0x0021);
    CHECK_EQ(m.objId, -1);
}
TEST(AiRecon5, Inv_owned_above_threshold_no_miss) {
    // cat 2: owned >= threshold -> the LABEL_10 buy/need block skipped -> no miss.
    InvIngredient ings[1]{};
    ings[0].classWord = 0x0021; ings[0].itemCategory = 2;
    ings[0].ownedCount = 5; ings[0].foundInStock = false;
    ings[0].needCovered = false; // would miss if reached, but owned>=threshold
    InvMatch m = AiObject_CountInventoryMatch(ings, 1, 2);
    CHECK_EQ(m.matched, 0);
}
TEST(AiRecon5, Inv_extra_gate_blocks_miss) {
    // direct need missing, but an extra-gate class is required and IS coverable;
    // gate satisfied -> miss still fires (gate is an AND of "need && gates").
    InvIngredient ings[1]{};
    ings[0].classWord = 0x0030; ings[0].itemCategory = 5;
    ings[0].needCovered = false;
    ings[0].extraGateA = 0x0099; ings[0].extraGateAOk = true;
    InvMatch m = AiObject_CountInventoryMatch(ings, 1, 1);
    CHECK_EQ(m.matched, 1);
}
TEST(AiRecon5, Inv_extra_gate_unmet_suppresses_miss) {
    // extra gate present but NOT coverable -> gate false -> no miss this slot,
    // and since it's the only slot the recipe ends -> matched 0.
    InvIngredient ings[1]{};
    ings[0].classWord = 0x0030; ings[0].itemCategory = 5;
    ings[0].needCovered = false;
    ings[0].extraGateA = 0x0099; ings[0].extraGateAOk = false;
    InvMatch m = AiObject_CountInventoryMatch(ings, 1, 1);
    CHECK_EQ(m.matched, 0);
}
TEST(AiRecon5, Inv_first_missing_wins) {
    // two slots; first satisfiable (cat33), second missing -> reports second.
    InvIngredient ings[2]{};
    ings[0].classWord = 0x0001; ings[0].itemCategory = 33;
    ings[1].classWord = 0x0002; ings[1].itemCategory = 5; ings[1].needCovered = false;
    InvMatch m = AiObject_CountInventoryMatch(ings, 2, 1);
    CHECK_EQ(m.matched, 1);
    CHECK_EQ(m.classWord, (unsigned short)0x0002);
}
