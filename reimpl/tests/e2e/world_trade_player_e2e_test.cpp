// End-to-end: a player visits a market, BUYS goods at a stall, the stall stock
// is updated, then the player takes a BANK LOAN — verifying money, stock, and
// debt state plus the emitted command sequence against a hand-computed reference.
#include "tests/framework/test.h"

#include "world/trade_player.h"
#include "world/exchange_loop.h"
#include "world/market_stall.h"
#include "world/bank_loan.h"
#include "crt/rand.h"

#include <vector>

using namespace guild;
using namespace guild::world;

namespace {

// A tiny world: a player account, a stall account, a bank/lender, and a stall
// stock book. Commands route through the hooks into this state.
struct MiniWorld {
    i32 playerCash = 0;
    i32 stallCash = 0;
    i32 familyDebt = 0;
    std::vector<TradeEntry> stallStock;
    std::vector<TradeMoneyCommand> moneyCmds;
    std::vector<LoanCommand> loanCmds;
};

MiniWorld* g_world = nullptr;

void MoneyHook(const TradeMoneyCommand& c, void*) {
    g_world->moneyCmds.push_back(c);
    // The wine-cellar buy encodes the player's post-trade cash delta. Apply the
    // net effect: player pays `cost` to the stall regardless of leg direction.
    // (Here we reconcile from the recorded legs: afford leg moves player->stall,
    // overdraft leg stall->player of the magnitude difference; the net is the
    // same item cost.)
}

void LoanHook(const LoanCommand& c, void*) {
    g_world->loanCmds.push_back(c);
    g_world->playerCash += c.amount; // principal paid out to the borrower
}

} // namespace

TEST(WorldTradePlayerE2E, BuyThenStockThenLoan) {
    MiniWorld w;
    g_world = &w;
    w.playerCash = 1000;
    w.stallCash = 0;

    // --- Currency setup: city 0 uses currency 0 at rate 2 -------------------
    TradeSetCurrencyRateTable({2});
    TradeSetCityCurrencyTable({0});

    // === Step 1: player BUYS a good worth 50 at the market stall ============
    TradeSetMoneyHook(MoneyHook, nullptr);
    BuyInput buy;
    buy.itemValue = 50; buy.city = 0; buy.playerCash = w.playerCash;
    buy.playerAccount = 1; buy.sellerAccount = 2;
    BuyResult br = WineCellarBuy(buy, true);

    // Reference: cost = 50*rate(2) = 100; player can afford (1000 >= 100).
    CHECK_EQ(br.cost, 100);
    CHECK(br.afford);
    CHECK_EQ((int)w.moneyCmds.size(), 1);
    CHECK_EQ(w.moneyCmds[0].payer, 1);       // player pays
    CHECK_EQ(w.moneyCmds[0].recipient, 2);   // stall receives
    CHECK_EQ(w.moneyCmds[0].amount, 900);    // playerCash - cost (the delta leg)

    // Apply the economic net to the mini-world (player -100, stall +100).
    w.playerCash -= br.cost;
    w.stallCash += br.cost;
    CHECK_EQ(w.playerCash, 900);
    CHECK_EQ(w.stallCash, 100);

    // === Step 2: the stall's stock book is updated for the sold good ========
    // The lockstep applies an ExUpsertTradeEntry that records the order onto the
    // stall (good key 7, +40 units of stock this pass).
    TradeOrder order;
    order.key = 7; order.stockDelta = 40; order.fieldA = 50; /* value */
    int applyState = 0;
    int rc = MarketStallUpsertTradeEntry(order, /*dst*/true, /*src*/true,
                                         w.stallStock, &applyState);
    CHECK_EQ(rc, 0);
    CHECK_EQ(applyState, 1);
    CHECK_EQ((int)w.stallStock.size(), 1);
    CHECK_EQ((int)w.stallStock[0].stock, 40);

    // A restock pass adds another 70 -> accumulates and clamps to 100.
    order.stockDelta = 70;
    MarketStallUpsertTradeEntry(order, true, true, w.stallStock, &applyState);
    CHECK_EQ((int)w.stallStock.size(), 1);       // upsert
    CHECK_EQ((int)w.stallStock[0].stock, 100);   // 40+70 -> clamp

    // === Step 3: the player takes a BANK LOAN ===============================
    // The bank router resolves the "Kredite" contact (foreign city -> take-loan).
    BankContactInput bc;
    bc.inHomeCity = false; bc.clicked = BankContactInput::Clicked::Credit;
    CHECK(BankRouteContact(bc) == BankContact::TakeLoanDialog);

    // The dialog generates offers (deterministic for seed 1) and the player
    // accepts the FIRST offer.
    crt::Srand(1);
    LoanOfferInput li;
    li.lenderWealth = 100000; li.prevBase = 80000;
    li.lenderRateField = 1000; li.tier = LoanRelationTier::Default;
    li.lenderIsBankType = false; li.favorability = 0.0f; li.count = 3;
    auto offers = LoanGenerateOffers(li);
    CHECK_EQ((int)offers.size(), 3);
    CHECK_EQ(offers[0].amount, 6900);   // golden (matches unit test / python)
    CHECK_EQ(offers[0].term, 6);
    CHECK_EQ(offers[0].interest, 1500);

    // Grant the chosen offer: principal paid out, family debt grows.
    LoanSetCmdHook(LoanHook, nullptr);
    i32 chosen = offers[0].amount;
    bool granted = CreditConfirmLoanRequest(/*accept*/true, /*resourceOk*/true,
                                            /*confirmed*/true, /*lender*/5,
                                            /*borrower*/1, chosen, /*currency*/0,
                                            &w.familyDebt, true);
    CHECK(granted);
    CHECK_EQ((int)w.loanCmds.size(), 1);
    CHECK_EQ(w.loanCmds[0].lender, 5);
    CHECK_EQ(w.loanCmds[0].borrower, 1);
    CHECK_EQ(w.loanCmds[0].amount, 6900);

    // Final state: cash grew by the principal, debt recorded.
    CHECK_EQ(w.playerCash, 900 + 6900); // LoanHook credited the principal
    CHECK_EQ(w.familyDebt, 6900);

    TradeSetMoneyHook(nullptr, nullptr);
    LoanSetCmdHook(nullptr, nullptr);
    g_world = nullptr;
}
