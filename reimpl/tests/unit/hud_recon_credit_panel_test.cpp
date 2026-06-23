// Golden-vector unit tests for the HUD / info-panel / credit reconstruction:
//   gilde.exe 0x54e7b4 0x55d03c 0x519dbc 0x51a868 0x51ab48 0x51d64c.
#include "tests/framework/test.h"
#include "src/play/hud_recon_selaction.h"
#include "src/play/hud_recon_giftpanel.h"
#include "src/play/hud_recon_credit.h"

using namespace guild;
using namespace guild::play;

// ===========================================================================
// 0x54e7b4 — VIBE_Hud_BuildSelectedObjectAction
// ===========================================================================
TEST(HudReconSelAction, NoSelectionIsNothing) {
    SelActionInput in{};
    in.selectionActive = false;
    auto r = Hud_BuildSelectedObjectAction(in);
    CHECK(r.kind == SelActionKind::kNothing);
}

TEST(HudReconSelAction, ResourceNameIndexHighWord) {
    // *(int*)(slot+8) >> 16
    CHECK_EQ(SelAction_ResourceNameIndex(0x00050000), 5);
    CHECK_EQ(SelAction_ResourceNameIndex(0x0007ABCD), 7);
    CHECK_EQ(SelAction_ResourceNameIndex(0), 0);
}

TEST(HudReconSelAction, RankLowGivesGenericTooltip) {
    SelActionInput in{};
    in.selectionActive = true;
    in.buildModeActive = false; // not free-act
    in.slot.valid = true;
    in.slot.rank = 1;           // <=1 -> generic
    auto r = Hud_BuildSelectedObjectAction(in);
    CHECK(r.kind == SelActionKind::kTooltipGeneric);
    CHECK_EQ(r.resourceNameIndex, -1);
}

TEST(HudReconSelAction, RankHighGivesNamedTooltip) {
    SelActionInput in{};
    in.selectionActive = true;
    in.buildModeActive = false;
    in.slot.valid = true;
    in.slot.rank = 4;           // >1 -> named
    in.slot.field8 = 0x000A0000; // index 10
    auto r = Hud_BuildSelectedObjectAction(in);
    CHECK(r.kind == SelActionKind::kTooltipNamed);
    CHECK_EQ(r.resourceNameIndex, 10);
}

static int g_visits = 0;
TEST(HudReconSelAction, FreeActGateAndEntityIteration) {
    // Gate: buildMode && slot.valid && (rank>1 || override) && entityReady
    SelActionInput in{};
    in.selectionActive  = true;
    in.buildModeActive  = true;
    in.entityArrayReady = true;
    in.slot.valid = true;
    in.slot.rank  = 1;          // rank<=1 but override forces free-act
    in.buildOverride = true;
    in.slot.field0 = 0x1234;

    g_visits = 0;
    Hud_SetSelActionFreeActHook(
        [](int, i32 owner, void*) { (void)owner; ++g_visits; }, nullptr);

    // entity i is active iff i is even (deterministic golden pattern)
    auto r = Hud_BuildSelectedObjectAction(
        in, [](int i) { return (i % 2) == 0; });
    CHECK(r.kind == SelActionKind::kFreeAct);
    // 768 entities, even indices -> 384 active
    CHECK_EQ(r.freeActEntityVisits, 384);
    CHECK_EQ(g_visits, 384);
    Hud_SetSelActionFreeActHook(nullptr, nullptr);
}

TEST(HudReconSelAction, EntityCountIs768) {
    CHECK_EQ(kSelActionEntityCount, 768);
    CHECK_EQ(411648 / 536, 768);
}

// ===========================================================================
// 0x55d03c — VIBE_InfoPanel_RunGiftDialog (slider range + confirm)
// ===========================================================================
TEST(HudReconGift, SmallWealthHitsFloors) {
    // wealth small: hi = wealth*0.005 < 1600 -> sliderMax = 1600.
    //               cap = min(held, wealth*0.05); v36 likely <3200 -> sliderMin=3200.
    auto r = Gift_ComputeSliderRange(/*wealth*/ 10000, /*held*/ 100000);
    CHECK_EQ(r.sliderMax, 1600);  // 10000*0.005=50 -> floor 1600
    CHECK_EQ(r.sliderMin, 3200);  // min(100000, 500)=500 -> floor 3200
    CHECK(!r.aborted);
}

TEST(HudReconGift, LargeWealthExceedsMaxFloor) {
    // wealth=1_000_000: hi = 1e6 * flt_624A3C(0.0049999998...) = 4999.99988 ->
    // (int) truncates to 4999 (>=1600) -> sliderMax=4999. This exercises the EXACT
    // single-precision constant (the value is NOT 5000 — flt_624A3C is below 1/200).
    // cap = min(held, 1e6 * flt_624A40(0.0500000007)) = min(80000, 50000.00074) ->
    // 50000.00074 -> (int) -> 50000 (>=3200) -> sliderMin=50000.
    auto r = Gift_ComputeSliderRange(1000000, 80000);
    CHECK_EQ(r.sliderMax, 4999);
    CHECK_EQ(r.sliderMin, 50000);
    CHECK(!r.aborted);
}

TEST(HudReconGift, HeldCapsTheLowerEndpoint) {
    // wealth=1e6 -> cap candidate 50000, but held=4000 -> min=4000 (>=3200) -> 4000.
    auto r = Gift_ComputeSliderRange(1000000, 4000);
    CHECK_EQ(r.sliderMin, 4000);
}

TEST(HudReconGift, ConfirmValueAndGate) {
    GiftConfirmInput in{};
    in.recipientIsPlayerControlled = false;
    in.sliderValue = 1000;
    in.ratePct = 100;            // MultiplyByRate(1000,100)=1000
    in.resourceAvailable = true;
    auto r = Gift_EvaluateConfirm(in);
    CHECK_EQ(r.amount, 1000);
    CHECK(r.emit);

    in.ratePct = 50;             // 1000*50/100 = 500
    in.resourceAvailable = false;
    auto r2 = Gift_EvaluateConfirm(in);
    CHECK_EQ(r2.amount, 500);
    CHECK(!r2.emit);
}

TEST(HudReconGift, PlayerControlledRecipientNoTransfer) {
    GiftConfirmInput in{};
    in.recipientIsPlayerControlled = true; // v41 -> no amount widget
    in.sliderValue = 9999;
    in.ratePct = 100;
    in.resourceAvailable = true;
    auto r = Gift_EvaluateConfirm(in);
    CHECK(!r.emit);
    CHECK_EQ(r.amount, 0);
}

// ===========================================================================
// 0x519dbc — VIBE_Credit_ShowNewLoanDialog offer compaction
// ===========================================================================
TEST(HudReconCredit, NewLoanCompactSkipsNonPositive) {
    LoanOffer src[3] = {
        {1000, 11, 12},  // included, index 0
        {0,    21, 22},  // skipped (amount==0)
        {3000, 31, 32},  // included, index 2
    };
    LoanOfferRow rows[3];
    int n = NewLoan_CompactOffers(src, rows);
    CHECK_EQ(n, 2);
    CHECK_EQ(rows[0].amount, 1000);
    CHECK_EQ(rows[0].index, 0);
    CHECK_EQ(rows[0].b, 11);
    CHECK_EQ(rows[0].c, 12);
    CHECK_EQ(rows[1].amount, 3000);
    CHECK_EQ(rows[1].index, 2);
    CHECK_EQ(rows[1].b, 31);
    CHECK_EQ(rows[1].childObjectId, 0);
}

TEST(HudReconCredit, NewLoanCompactAllSkipped) {
    LoanOffer src[3] = {{0,0,0},{-5,0,0},{0,0,0}};
    LoanOfferRow rows[3];
    CHECK_EQ(NewLoan_CompactOffers(src, rows), 0);
}

// ===========================================================================
// 0x51ab48 — VIBE_Credit_ShowLoanReleaseDialog repay math
// ===========================================================================
TEST(HudReconCredit, RepayPlayerCanAfford) {
    // held >= repay -> player pays, transfer = held-repay
    auto r = LoanRelease_ComputeRepay(/*held*/ 5000, /*repay*/ 2000);
    CHECK(r.playerPays);
    CHECK_EQ(r.transfer, 3000);
    CHECK_EQ(r.remainingAfter, 3000);
}

TEST(HudReconCredit, RepayPlayerShort) {
    // held < repay -> lender pays, transfer = repay-held; remaining negative
    auto r = LoanRelease_ComputeRepay(/*held*/ 1000, /*repay*/ 2500);
    CHECK(!r.playerPays);
    CHECK_EQ(r.transfer, 1500);
    CHECK_EQ(r.remainingAfter, -1500);
}

TEST(HudReconCredit, RepayRemainingPreview) {
    // held - MultiplyByRate(value, rate)
    CHECK_EQ(LoanRelease_RemainingPreview(10000, 3000, 100), 7000);
    CHECK_EQ(LoanRelease_RemainingPreview(10000, 3000, 50), 8500); // 3000*50/100=1500
}

// ===========================================================================
// 0x51d64c — VIBE_Credit_ShowAccountInfoDialog tab state
// ===========================================================================
TEST(HudReconCredit, AccountTabDefaultsToPriceRows) {
    AccountTabState st;
    CHECK(st.tab == AccountTab::kPriceRows);
    CHECK(st.dirty);
}

TEST(HudReconCredit, AccountTabSwitchToChoice) {
    AccountTabState st;
    st.dirty = false;
    bool changed = AccountInfo_HandleClick(st, false, true);
    CHECK(changed);
    CHECK(st.tab == AccountTab::kChoiceList);
    CHECK(st.dirty);
    // clicking choice again when already on choice -> no change
    st.dirty = false;
    CHECK(!AccountInfo_HandleClick(st, false, true));
    CHECK(!st.dirty);
}

TEST(HudReconCredit, AccountTabSwitchBackToPrices) {
    AccountTabState st;
    st.tab = AccountTab::kChoiceList;
    st.dirty = false;
    bool changed = AccountInfo_HandleClick(st, true, false);
    CHECK(changed);
    CHECK(st.tab == AccountTab::kPriceRows);
    CHECK(st.dirty);
}

// ===========================================================================
// 0x51a868 — VIBE_Credit_ShowLoanListDialog row-table init
// ===========================================================================
TEST(HudReconCredit, LoanListInitWritesSentinels) {
    i32 tbl[kLoanListBufDwords];
    for (int i = 0; i < kLoanListBufDwords; ++i)
        tbl[i] = 0x7777; // poison
    LoanList_InitRowTable(tbl);

    // The original loop touches tbl[3..29] and leaves tbl[0..2] untouched.
    CHECK_EQ(tbl[0], 0x7777);
    CHECK_EQ(tbl[1], 0x7777);
    CHECK_EQ(tbl[2], 0x7777);
    // For i = 3,6,...,27: tbl[i]=-1, tbl[i+1]=-1, tbl[i+2]=0.
    for (int i = 3; i <= 27; i += 3) {
        CHECK_EQ(tbl[i], -1);
        CHECK_EQ(tbl[i + 1], -1);
        CHECK_EQ(tbl[i + 2], 0);
    }
    CHECK_EQ(kLoanListBufDwords, 30);
    CHECK_EQ(kLoanListRows, 9);
}
