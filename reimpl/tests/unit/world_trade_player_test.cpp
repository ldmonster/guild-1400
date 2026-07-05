// Unit tests for the player Trade + Exchange + market-stall + bank-loan bodies.
//   world/trade_player.{h,cpp}, world/exchange_loop.{h,cpp},
//   world/market_stall.{h,cpp}, world/bank_loan.{h,cpp}
// Golden vectors computed with python3 replicating the binary's arithmetic.
#include "tests/framework/test.h"

#include "world/trade_player.h"
#include "world/exchange_loop.h"
#include "world/market_stall.h"
#include "world/bank_loan.h"
#include "crt/rand.h"

using namespace guild;
using namespace guild::world;

// ---------------------------------------------------------------------------
// trade_player: Money helpers + slot scan + buy rule + Einkauf router
// ---------------------------------------------------------------------------
TEST(WorldTradePlayer, MoneyConvertToDisplayCoord) {
    // rate table: currency 0 -> 1, currency 1 -> 3.  city 0->cur0, city 1->cur1.
    TradeSetCurrencyRateTable({1, 3});
    TradeSetCityCurrencyTable({0, 1});
    CHECK_EQ(TradeCityRate(0), 1);
    CHECK_EQ(TradeCityRate(1), 3);
    CHECK_EQ(MoneyConvertToDisplayCoord(100, 0), 100);  // 100/1 + .5
    CHECK_EQ(MoneyConvertToDisplayCoord(100, 1), 33);   // 100/3 + .5 -> 33
    CHECK_EQ(MoneyConvertToDisplayCoord(101, 1), 34);   // 101/3 + .5 -> 34
    CHECK_EQ(MoneyConvertToDisplayCoord(-5, 0), -4);    // -5/1 + .5 = -4.5 -> -4
}

TEST(WorldTradePlayer, MoneyDivideByRate) {
    TradeSetCurrencyRateTable({1, 3, 7});
    CHECK_EQ(MoneyDivideByRate(100, 1), 33);  // 100/3 truncated
    CHECK_EQ(MoneyDivideByRate(100, 2), 14);  // 100/7
    CHECK_EQ(MoneyDivideByRate(100, 0), 100); // rate 1
    CHECK_EQ(MoneyDivideByRate(100, 9), 100); // out of range -> fallback rate 1
}

TEST(WorldTradePlayer, FindSelectedSlotIndex) {
    std::vector<i32> slots = {10, -1, 30, 40};
    CHECK_EQ(TradeFindSelectedSlotIndex(false, 30, slots), -1); // no click
    CHECK_EQ(TradeFindSelectedSlotIndex(true, 30, slots), 2);
    CHECK_EQ(TradeFindSelectedSlotIndex(true, 40, slots), 3);
    CHECK_EQ(TradeFindSelectedSlotIndex(true, 99, slots), -1); // not found
    CHECK_EQ(TradeFindSelectedSlotIndex(true, -1, slots), -1); // empty id skipped
}

namespace {
struct BuyCapture { TradeMoneyCommand last; int n = 0; };
void BuyHook(const TradeMoneyCommand& c, void* ctx) {
    auto* cap = static_cast<BuyCapture*>(ctx);
    cap->last = c;
    cap->n++;
}
} // namespace

TEST(WorldTradePlayer, WineCellarBuyAfford) {
    TradeSetCurrencyRateTable({2});      // currency 0 -> rate 2
    TradeSetCityCurrencyTable({0});      // city 0 -> currency 0
    BuyCapture cap;
    TradeSetMoneyHook(BuyHook, &cap);
    BuyInput in;
    in.itemValue = 50; in.city = 0; in.playerCash = 500;
    in.playerAccount = 7; in.sellerAccount = 9;
    BuyResult r = WineCellarBuy(in, true);
    CHECK_EQ(r.cost, 100);          // 50 * rate 2
    CHECK(r.afford);
    CHECK_EQ(cap.n, 1);
    CHECK_EQ(cap.last.payer, 7);
    CHECK_EQ(cap.last.recipient, 9);
    CHECK_EQ(cap.last.amount, 400); // playerCash - cost
    TradeSetMoneyHook(nullptr, nullptr);
}

TEST(WorldTradePlayer, WineCellarBuyOverdraft) {
    TradeSetCurrencyRateTable({2});
    TradeSetCityCurrencyTable({0});
    BuyCapture cap;
    TradeSetMoneyHook(BuyHook, &cap);
    BuyInput in;
    in.itemValue = 50; in.city = 0; in.playerCash = 30;
    in.playerAccount = 7; in.sellerAccount = 9;
    BuyResult r = WineCellarBuy(in, true);
    CHECK_EQ(r.cost, 100);
    CHECK(!r.afford);
    CHECK_EQ(cap.last.payer, 9);      // overdraft leg reverses payer/recipient
    CHECK_EQ(cap.last.recipient, 7);
    CHECK_EQ(cap.last.amount, 70);    // cost - playerCash
    TradeSetMoneyHook(nullptr, nullptr);
}

TEST(WorldTradePlayer, EinkaufRouter) {
    EinkaufContext c;
    c.homeCity = 1; c.currentCity = 1;     // same city -> no routing
    EinkaufResult r0 = TradeRegisterEinkaufContact(c, nullptr, nullptr);
    CHECK(r0.matched == EinkaufContact::None);
    CHECK(!r0.openPanel);

    c.currentCity = 2;                     // foreign
    c.present[0] = false; c.present[1] = true; c.present[3] = true; // Metal first present
    c.clickedIsContact = false;
    EinkaufResult r1 = TradeRegisterEinkaufContact(c, nullptr, nullptr);
    CHECK(r1.matched == EinkaufContact::Metal);
    CHECK(!r1.openPanel);                   // matched but not clicked

    c.clickedIsContact = true;
    EinkaufResult r2 = TradeRegisterEinkaufContact(c, nullptr, nullptr);
    CHECK(r2.matched == EinkaufContact::Metal);
    CHECK(r2.openPanel);
}

// ---------------------------------------------------------------------------
// exchange_loop
// ---------------------------------------------------------------------------
TEST(WorldExchangeLoop, InitAndSeed) {
    std::vector<ExchangeSlot> give, take;
    ExchangeInitSlotTables(give, take);
    CHECK_EQ((int)give.size(), 16);
    CHECK_EQ((int)take.size(), 16);
    CHECK_EQ(give[0].id, -1);
    CHECK_EQ(take[5].id, -1);

    std::vector<ExchangeCityRow> cities(4);
    cities[1] = {true, 11};
    cities[2] = {false, 22};
    cities[3] = {true, 33};
    int seeded = ExchangeSeedCityCurrencies(cities, take);
    CHECK_EQ(seeded, 2);
    CHECK_EQ(take[0].currency, 11);   // city 1 -> slot 0
    CHECK_EQ((int)take[0].flag, 1);
    CHECK_EQ(take[1].currency, 0);    // city 2 inactive -> unseeded
    CHECK_EQ(take[2].currency, 33);   // city 3 -> slot 2
    CHECK_EQ((int)take[2].flag, 3);
}

TEST(WorldExchangeLoop, CountAndOverflow) {
    std::vector<ExchangeSlot> s(16);
    for (auto& x : s) x.id = -1;
    for (int i = 0; i < 5; ++i) s[i].id = i + 100;
    CHECK_EQ(ExchangeCountActiveSlots(s), 5);
    CHECK(ExchangeListOverflows(5));
    CHECK(!ExchangeListOverflows(4));
}

TEST(WorldExchangeLoop, CourierGate) {
    CHECK(ExchangeShouldShowCourier(false, 2, 3));
    CHECK(!ExchangeShouldShowCourier(true, 2, 3));  // shipment pending
    CHECK(!ExchangeShouldShowCourier(false, -1, 3)); // nothing selected
    CHECK(!ExchangeShouldShowCourier(false, 2, -1)); // nothing populated
}

TEST(WorldExchangeLoop, ButtonDispatch) {
    ExchangeWidgets w;
    w.feesButton = 40; w.exchangeButton = 44;
    w.giveUp = 51; w.giveDn = 47; w.takeUp = 50; w.takeDn = 49;
    CHECK(ExchangeDispatchButton(1210, 40, w) == ExchangeAction::ShowFees);
    CHECK(ExchangeDispatchButton(1210, 44, w) == ExchangeAction::ShowExchange);
    CHECK(ExchangeDispatchButton(1210, 99, w) == ExchangeAction::None);
    CHECK(ExchangeDispatchButton(1211, 51, w) == ExchangeAction::ScrollGiveUp);
    CHECK(ExchangeDispatchButton(1212, 50, w) == ExchangeAction::ScrollTakeUp);
    CHECK(ExchangeDispatchButton(1, 51, w) == ExchangeAction::None);
}

TEST(WorldExchangeLoop, BankRouter) {
    BankContactInput in;
    using Clicked = BankContactInput::Clicked;

    in.inHomeCity = true; in.clicked = Clicked::Exchange;
    CHECK(BankRouteContact(in) == BankContact::GoodsExchangeLoop);
    in.inHomeCity = false;
    CHECK(BankRouteContact(in) == BankContact::GoodsExchangeDialog);

    in.clicked = Clicked::Credit; in.inHomeCity = true;
    CHECK(BankRouteContact(in) == BankContact::LenderDialog);
    in.inHomeCity = false;
    CHECK(BankRouteContact(in) == BankContact::TakeLoanDialog);

    in.clicked = Clicked::Meister; in.isGuildMaster = false;
    CHECK(BankRouteContact(in) == BankContact::None);  // not master
    in.isGuildMaster = true;
    CHECK(BankRouteContact(in) == BankContact::MasterCertificate);
    in.clicked = Clicked::Vermoegen;
    CHECK(BankRouteContact(in) == BankContact::AssetOverview);
}

// ---------------------------------------------------------------------------
// market_stall
// ---------------------------------------------------------------------------
TEST(WorldMarketStall, RouteContact) {
    CHECK(MarketStallRouteContact("ob_MARKTSTAND_ROHSTOFFE") == StallType::Rohstoffe);
    CHECK(MarketStallRouteContact("ob_MARKTSTAND_KIRCHE") == StallType::Kirche);
    CHECK(MarketStallRouteContact("ob_SCHWARZES_BRETT") == StallType::PamphletBoard);
    CHECK(MarketStallRouteContact("contact_ob_SCHWARZES_BRETT") == StallType::PamphletBoard);
    CHECK(MarketStallRouteContact("ob_TRIBUENE") == StallType::Tribune);
    CHECK(MarketStallRouteContact("") == StallType::None);
    CHECK(MarketStallRouteContact("unknown") == StallType::None);
}

TEST(WorldMarketStall, UpsertStockAccumulateAndClamp) {
    std::vector<TradeEntry> entries;
    TradeOrder o;
    o.key = 7; o.stockDelta = 30; o.fieldA = 11;
    int state = 0;
    int rc = MarketStallUpsertTradeEntry(o, true, true, entries, &state);
    CHECK_EQ(rc, 0);
    CHECK_EQ(state, 1);
    CHECK_EQ((int)entries.size(), 1);
    CHECK_EQ((int)entries[0].stock, 30);
    CHECK_EQ(entries[0].fieldA, 11);

    // Second order with same key accumulates onto the existing entry.
    o.stockDelta = 50;
    rc = MarketStallUpsertTradeEntry(o, true, true, entries, &state);
    CHECK_EQ(rc, 0);
    CHECK_EQ((int)entries.size(), 1);     // upsert, not insert
    CHECK_EQ((int)entries[0].stock, 80);

    // Clamp at 100.
    o.stockDelta = 90;
    MarketStallUpsertTradeEntry(o, true, true, entries, &state);
    CHECK_EQ((int)entries[0].stock, 100); // 80+90=170 -> clamp 100
}

TEST(WorldMarketStall, UpsertResolveFail) {
    std::vector<TradeEntry> entries;
    TradeOrder o; o.key = 1;
    int state = 0;
    CHECK_EQ(MarketStallUpsertTradeEntry(o, false, true, entries, &state), 1);
    CHECK_EQ((int)entries.size(), 0);
    CHECK_EQ(MarketStallUpsertTradeEntry(o, true, false, entries, &state), 1);
}

namespace {
std::string NameForCurrency(i32 cur, void*) {
    if (cur == 5) return "Gold";
    if (cur == 6) return "Silber";
    return "Mark";
}
} // namespace

TEST(WorldMarketStall, SortedItemList) {
    // home currency = 5 -> "AAAA"; 0 -> "ZZZZ"; others by name.
    std::vector<SortSlot> slots(4);
    slots[0] = {100, 0, 0, 1};   // none -> ZZZZ (last)
    slots[1] = {101, 0, 6, 2};   // Silber
    slots[2] = {102, 0, 5, 3};   // home -> AAAA (first)
    slots[3] = {103, 0, 7, 4};   // Mark
    CHECK_EQ(TradeSortableCount(slots), 4);
    TradeBuildSortedItemList(slots, 5, NameForCurrency, nullptr);
    // ascending name order (gilde.exe 0x51b3ca swaps when name(i) > name(j)):
    // AAAA(home) < Mark < Silber < ZZZZ(none).
    // [old pin (descending, ZZZZ first) PROVEN wrong: VIBE_Util_StrCmp @0x5d3f10
    //  returns 1 when its eax operand (key(i)) is greater — see 0x51b3bc/0x51b3ca.]
    CHECK_EQ(slots[0].currency, 5);   // AAAA home first
    CHECK_EQ(slots[1].currency, 7);   // Mark
    CHECK_EQ(slots[2].currency, 6);   // Silber
    CHECK_EQ(slots[3].currency, 0);   // ZZZZ last
}

// ---------------------------------------------------------------------------
// bank_loan
// ---------------------------------------------------------------------------
TEST(WorldBankLoan, GrantCapacity) {
    CHECK_EQ(LoanGrantCapacity(100000), 20000);  // 0.2*100000
    CHECK_EQ(LoanGrantCapacity(500000), 64000);  // clamp at 64000
}

TEST(WorldBankLoan, OfferBaseMin) {
    // min(wealth*0.3, prev*0.5): 100000*0.3=30000 vs 80000*0.5=40000 -> 30000
    CHECK_EQ(LoanOfferBase(100000, 80000, LoanRelationTier::Default), 30000);
    // 100000*0.6=60000 vs 80000*0.5=40000 -> 40000
    CHECK_EQ(LoanOfferBase(100000, 80000, LoanRelationTier::High), 40000);
}

TEST(WorldBankLoan, InterestFormula) {
    // field*0.7 + term*(1/6)*(field*1.5 - field*0.7)
    CHECK_EQ(LoanComputeInterest(1000, 5, false, 0.0f), 1366);
    CHECK_EQ(LoanComputeInterest(1000, 2, true, 80.0f), 964);
}

TEST(WorldBankLoan, GenerateOffersGolden) {
    crt::Srand(1);
    LoanOfferInput in;
    in.lenderWealth = 100000; in.prevBase = 80000;
    in.lenderRateField = 1000; in.tier = LoanRelationTier::Default;
    in.lenderIsBankType = false; in.favorability = 0.0f; in.count = 3;
    auto offers = LoanGenerateOffers(in);
    CHECK_EQ((int)offers.size(), 3);
    // Golden (python, seed 1): offer0 amt 6900 term 6 int 1500; offer1 14550/3/1100;
    //                          offer2 29700/7/1633.
    CHECK_EQ(offers[0].amount, 6900);
    CHECK_EQ(offers[0].term, 6);
    CHECK_EQ(offers[0].interest, 1500);
    CHECK_EQ(offers[1].amount, 14550);
    CHECK_EQ(offers[1].term, 3);
    CHECK_EQ(offers[1].interest, 1100);
    CHECK_EQ(offers[2].amount, 29700);
    CHECK_EQ(offers[2].term, 7);
    CHECK_EQ(offers[2].interest, 1633);
}

namespace {
struct LoanCapture { LoanCommand last; int n = 0; };
void LoanHook(const LoanCommand& c, void* ctx) {
    auto* cap = static_cast<LoanCapture*>(ctx);
    cap->last = c; cap->n++;
}
} // namespace

TEST(WorldBankLoan, ConfirmGrant) {
    LoanCapture cap;
    LoanSetCmdHook(LoanHook, &cap);
    i32 debt = 200;
    bool ok = CreditConfirmLoanRequest(true, true, true, 3, 9, 5000, 1, &debt, true);
    CHECK(ok);
    CHECK_EQ(cap.n, 1);
    CHECK_EQ(cap.last.lender, 3);
    CHECK_EQ(cap.last.borrower, 9);
    CHECK_EQ(cap.last.amount, 5000);
    CHECK_EQ(debt, 5200);   // 200 + 5000

    // A failed gate grants nothing.
    cap.n = 0; debt = 0;
    CHECK(!CreditConfirmLoanRequest(true, false, true, 3, 9, 5000, 1, &debt, true));
    CHECK_EQ(cap.n, 0);
    CHECK_EQ(debt, 0);
    LoanSetCmdHook(nullptr, nullptr);
}
