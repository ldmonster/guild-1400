#include "test.h"

#include "crt/rand.h"
#include "world/money_format.h"   // real sibling: VIBE_Money_FormatWithSeparators
#include "world/player_finance.h"
#include "world/tax.h"            // real sibling: VIBE_Tax_CollectTradeIncome
#include "world/treasury.h"       // real sibling: Treasury balance ops

using namespace guild;
using namespace guild::world;

// A computed court fine, when shown, is rendered through the SAME money string
// formatter the panels use. Cross-check that the fine amount round-trips through
// the real MoneyFormatWithSeparators sibling.
TEST(PlayerFinanceItest, FineFormatsThroughMoneyFormatter) {
    i32 fine = HeComputeFineAmount(100000); // 2999
    CHECK_EQ(fine, 2999);
    std::string s = MoneyFormatWithSeparators(fine, 1);
    // The formatter groups every three digits from the right: 2999 -> "2.999".
    CHECK_EQ(s, std::string("2.999") + kCurrencyGlyph);

    i32 bigFine = HeComputeFineAmount(50000000); // 0.03f*5e7 trunc
    std::string sb = MoneyFormatWithSeparators(bigFine, 1);
    // Group-of-three '.' separators must appear for a 7-figure fine.
    CHECK(sb.find('.') != std::string::npos);
}

// The top-wealth board feeds the richest entry into the trade-tax sibling: the
// #1 citizen's wealth acts as the taxable account; collecting trade income from
// it must not exceed the wealth, and the law-slot-full gate still applies.
TEST(PlayerFinanceItest, TopWealthDrivesTradeTax) {
    std::vector<WealthSlot> slots = {
        {1, 0, 40000, true},
        {2, 0, 120000, true},
        {3, 0, 5000, true},
    };
    TopWealthBoard b = PersonComputeTopWealthList(slots);
    CHECK_EQ(b.wealth[0], 120000); // richest

    // 10% trade tax on the richest account via the real tax sibling.
    TradeTaxResult r;
    i32 cityAccum = 0;
    int produced = TaxCollectTradeIncome(/*lawThreshold=*/10,
                                         /*activeFinanceLaws=*/0,
                                         /*accountValue=*/b.wealth[0],
                                         /*payerObjectId=*/0, /*payerId=*/0,
                                         /*flags=*/2, &r, &cityAccum);
    CHECK_EQ(produced, 1);
    // 10 * 0.01f * 120000 truncates to 11999 (0.01f < 0.01, single-precision).
    CHECK_EQ(r.amount, 11999);
    CHECK(r.amount <= b.wealth[0]);
    CHECK_EQ(cityAccum, 11999);
}

// A collected fine flowing into the city treasury sibling: deposit the fine,
// then withdraw it as a payout; balances mirror exactly.
TEST(PlayerFinanceItest, FineDepositedToTreasury) {
    Treasury city;
    city.accountId = 7;
    city.balance = 1000;

    i32 fine = HeComputeFineAmount(33333); // 999
    CHECK_EQ(fine, 999);
    i32 bal = TreasuryDeposit(city, /*payer=*/42, fine);
    CHECK_EQ(bal, 1999);

    i32 moved = 0;
    bal = TreasuryWithdraw(city, /*recipient=*/9, fine, &moved);
    CHECK_EQ(moved, 999);
    CHECK_EQ(bal, 1000);
}

// Exam-fee gate consumes the shared crt LCG; a parallel statistics/economy tick
// that also reseeds must be observable. Verify determinism across reseeds.
TEST(PlayerFinanceItest, ExamFeeDeterministicAcrossReseed) {
    crt::Srand(1);
    bool a0 = AmtCheckExamFeeAffordable(80.0, 5, 2);
    bool a1 = AmtCheckExamFeeAffordable(10.0, 1, 4);

    crt::Srand(1); // same seed -> same outcomes
    bool b0 = AmtCheckExamFeeAffordable(80.0, 5, 2);
    bool b1 = AmtCheckExamFeeAffordable(10.0, 1, 4);
    CHECK(a0 == b0);
    CHECK(a1 == b1);
    CHECK(a0 == true);
    CHECK(a1 == false);
}
