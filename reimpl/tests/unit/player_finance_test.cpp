#include "test.h"

#include "crt/rand.h"
#include "world/player_finance.h"

using namespace guild;
using namespace guild::world;

// ---------------------------------------------------------------------------
// VIBE_Person_SumStoredMoney 0x591600
// ---------------------------------------------------------------------------
TEST(PlayerFinance, SumStoredMoneyNotFound) {
    // Person_FindRecordById returned null -> -1 sentinel.
    CHECK_EQ(PersonSumStoredMoney(false, {}), -1);
}

TEST(PlayerFinance, SumStoredMoneyAggregates) {
    CHECK_EQ(PersonSumStoredMoney(true, {1200, 50, 9999, 1}), 1200 + 50 + 9999 + 1);
    CHECK_EQ(PersonSumStoredMoney(true, {}), 0);
    CHECK_EQ(PersonSumStoredMoney(true, {42}), 42);
}

// ---------------------------------------------------------------------------
// VIBE_Person_ComputeTopWealthList 0x592b50 — golden vector
// ---------------------------------------------------------------------------
TEST(PlayerFinance, TopWealthBoardGolden) {
    std::vector<WealthSlot> slots = {
        {10, 0, 500, true},
        {11, 0, 9000, true},
        {12, 5, 3000, true},
        {13, 10, 99999, true}, // class >= 10 -> excluded
        {14, 0, 7000, true},
        {15, 0, 100, false},   // inactive -> excluded
        {16, 2, 8000, true},
        {17, 0, 50, true},
        {18, 0, 6000, true},
    };
    TopWealthBoard b = PersonComputeTopWealthList(slots);
    const u16 expIds[5] = {11, 16, 14, 18, 12};
    const i32 expW[5] = {9000, 8000, 7000, 6000, 3000};
    for (int k = 0; k < 5; ++k) {
        CHECK_EQ(b.ids[k], expIds[k]);
        CHECK_EQ(b.wealth[k], expW[k]);
    }
}

TEST(PlayerFinance, TopWealthBoardEmpty) {
    TopWealthBoard b = PersonComputeTopWealthList({});
    for (int k = 0; k < 5; ++k) {
        CHECK_EQ(b.ids[k], static_cast<u16>(0xFFFF));
        CHECK_EQ(b.wealth[k], 0);
    }
}

TEST(PlayerFinance, TopWealthBoardFewerThanFive) {
    std::vector<WealthSlot> slots = {
        {1, 0, 100, true},
        {2, 0, 300, true},
        {3, 0, 200, true},
    };
    TopWealthBoard b = PersonComputeTopWealthList(slots);
    CHECK_EQ(b.ids[0], static_cast<u16>(2));
    CHECK_EQ(b.wealth[0], 300);
    CHECK_EQ(b.ids[1], static_cast<u16>(3));
    CHECK_EQ(b.wealth[1], 200);
    CHECK_EQ(b.ids[2], static_cast<u16>(1));
    CHECK_EQ(b.wealth[2], 100);
    // The two unfilled tail slots keep their sentinels.
    CHECK_EQ(b.ids[3], static_cast<u16>(0xFFFF));
    CHECK_EQ(b.wealth[3], 0);
    CHECK_EQ(b.ids[4], static_cast<u16>(0xFFFF));
    CHECK_EQ(b.wealth[4], 0);
}

// ---------------------------------------------------------------------------
// VIBE_Amt_CheckExamFeeAffordable 0x592c18 — golden vector (seed 1 LCG)
// ---------------------------------------------------------------------------
TEST(PlayerFinance, ExamFeeAffordableSeedSequence) {
    // Seed the shared LCG; the threshold consumes one RandNext per call, so the
    // outcomes follow the fixed rand sequence 16838, 5758, 10113, 17515, ...
    crt::Srand(1);
    // fee = fav*0.01*lvl/req
    CHECK(AmtCheckExamFeeAffordable(80.0, 5, 2) == true);   // fee 2.0   > 0.6639
    CHECK(AmtCheckExamFeeAffordable(10.0, 1, 4) == false);  // fee 0.025 < 0.3257
    CHECK(AmtCheckExamFeeAffordable(100.0, 3, 3) == true);  // fee 1.0   > 0.4586
    CHECK(AmtCheckExamFeeAffordable(0.0, 5, 1) == false);   // fee 0     < 0.6845
}

// ---------------------------------------------------------------------------
// VIBE_He_ShowFineAmount 0x4c4b60 — fine math golden vector
// ---------------------------------------------------------------------------
TEST(PlayerFinance, FineAmountTruncatesFloatRate) {
    // 0.03f is < 0.03, and the product is truncated toward zero.
    CHECK_EQ(HeComputeFineAmount(100000), 2999);
    CHECK_EQ(HeComputeFineAmount(12345), 370);
    CHECK_EQ(HeComputeFineAmount(33333), 999);
    CHECK_EQ(HeComputeFineAmount(-5000), -149); // trunc toward zero
    CHECK_EQ(HeComputeFineAmount(0), 0);
}

namespace {
struct FineCapture {
    FineMessage last;
    int count = 0;
};
void FineSink(const FineMessage& m, void* ctx) {
    auto* c = static_cast<FineCapture*>(ctx);
    c->last = m;
    c->count++;
}
} // namespace

TEST(PlayerFinance, FineDispatchVariants) {
    FineCapture cap;
    HeSetFineRenderHook(FineSink, &cap);

    // variant 0 -> bare message 4442, no amount.
    i32 f = HeShowFineAmount(100000, 0);
    CHECK_EQ(f, 2999);
    CHECK_EQ(cap.last.messageId, 4442);
    CHECK(cap.last.hasAmount == false);
    CHECK(cap.last.hasExtra4 == false);

    // variant 1 -> 4445 with amount.
    HeShowFineAmount(100000, 1);
    CHECK_EQ(cap.last.messageId, 4445);
    CHECK(cap.last.hasAmount == true);
    CHECK(cap.last.hasExtra4 == false);
    CHECK_EQ(cap.last.amount, 2999);

    // variant 3 -> 4451 with literal 4 + amount.
    HeShowFineAmount(100000, 3);
    CHECK_EQ(cap.last.messageId, 4451);
    CHECK(cap.last.hasAmount == true);
    CHECK(cap.last.hasExtra4 == true);

    // variant 2 -> bare message 4448, no amount.
    HeShowFineAmount(100000, 2);
    CHECK_EQ(cap.last.messageId, 4448);
    CHECK(cap.last.hasAmount == false);

    // variant 5 -> bare message 3*5+4442 = 4457.
    HeShowFineAmount(100000, 5);
    CHECK_EQ(cap.last.messageId, 4457);
    CHECK(cap.last.hasAmount == false);

    CHECK_EQ(cap.count, 5);
    HeSetFineRenderHook(nullptr, nullptr);
}
