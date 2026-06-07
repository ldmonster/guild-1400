// Unit tests for the Amt per-turn passes + trade transport + treasury/bank.
// Golden values computed with python3 against the recovered float chains.
#include <vector>

#include "tests/framework/test.h"
#include "world/amt.h"
#include "world/tradetransport.h"
#include "world/treasury.h"
#include "world/bank.h"

using namespace guild;
using namespace guild::world;

// ---------------------------------------------------------------------------
// Tax formulas.
// ---------------------------------------------------------------------------
TEST(WorldAmtTax, ComputeIncomeGolden) {
    CHECK_EQ(TaxComputeIncome(50, 200000), 100000);
    CHECK_EQ(TaxComputeIncome(10, 1000000), 99999);   // float rounding loses 1
    CHECK_EQ(TaxComputeIncome(100, 55000), 55000);
    CHECK_EQ(TaxComputeIncome(0, 100000), 0);
    CHECK_EQ(TaxComputeIncome(50, -100), 0);          // clamped at 0
    CHECK_EQ(TaxComputeIncome(7, 999999), 69999);
}

TEST(WorldAmtTax, CollectIncomeLawSlotFull) {
    TaxLine line;
    // 6 active finance laws => slot full => returns 0, no line.
    int r = TaxCollectIncome(TaxKind::Guild, 50, 6, 200000, 1, 2, 1, &line, nullptr);
    CHECK_EQ(r, 0);
    CHECK_EQ(line.amount, 0);
    CHECK(!line.collected);
}

TEST(WorldAmtTax, CollectIncomeCommitAndAccumulate) {
    static i32 lastAmount = -1;
    static int callCount = 0;
    AmtSetTransferHook([](i32, i32, i32 amt, int) -> bool {
        lastAmount = amt; ++callCount; return true;
    });
    lastAmount = -1; callCount = 0;
    i32 cityAcc = 1000;
    TaxLine line;
    // flags bit0 (commit) | bit1 (accumulate).
    int r = TaxCollectIncome(TaxKind::Building, 100, 0, 55000, 11, 22, 3, &line, &cityAcc);
    CHECK_EQ(r, 1);
    CHECK_EQ(line.amount, 55000);
    CHECK(line.collected);
    CHECK(line.committed);
    CHECK_EQ(lastAmount, 55000);
    CHECK_EQ(cityAcc, 1000 + 55000);
    AmtSetTransferHook(nullptr);
}

TEST(WorldAmtTax, CollectOfficeAllTaxesDispatch) {
    AmtSetTransferHook(nullptr); // no commit; just sum amounts
    OfficeTaxInput in;
    in.isGuildOffice = false;     // -> trade branch
    in.tradeRate = 50;            // trade: 50% of 200000 = 100000
    in.buildingRate = 10;         // building: 10% of 200000 = 20000
    in.propertyRate = 20;         // property: 20% of 50000 = 10000
    in.staffRate = 25;            // staff: 25% of 8000 = 2000
    in.activeFinanceLaws = 0;
    in.account = 200000;
    in.propertyValue = 50000;
    in.staffValue = 8000;
    i32 total = TaxCollectOfficeAllTaxes(in, 1, 2, 0);
    // float chain: building 10%x200000 = 19999, property 20%x50000 = 9999.
    CHECK_EQ(total, 100000 + 19999 + 9999 + 2000);
}

TEST(WorldAmtTax, OfficePropertySkippedWhenZero) {
    OfficeTaxInput in;
    in.isGuildOffice = true;      // -> guild branch (same 0.01 scale)
    in.guildRate = 50;
    in.buildingRate = 0;
    in.propertyRate = 20;
    in.staffRate = 0;
    in.account = 100000;          // guild: 50000
    in.propertyValue = 0;         // property pass skipped entirely
    in.staffValue = 0;
    i32 total = TaxCollectOfficeAllTaxes(in, 1, 2, 0);
    CHECK_EQ(total, 50000);
}

// ---------------------------------------------------------------------------
// Wages.
// ---------------------------------------------------------------------------
TEST(WorldAmtWage, ComputeWageGolden) {
    CHECK_EQ(AmtComputeWage(3, 1.0f), 9600);
    CHECK_EQ(AmtComputeWage(2, 0.5f), 3200);
    CHECK_EQ(AmtComputeWage(4, 1.25f), 16000);
    CHECK_EQ(AmtComputeWage(0, 2.0f), 0);
    CHECK_EQ(AmtComputeWage(1, 0.0f), 0);
}

TEST(WorldAmtWage, ComputeOfficeWagesPairCommit) {
    static int n = 0; static i32 sum = 0;
    AmtSetTransferHook([](i32, i32, i32 amt, int) -> bool { ++n; sum += amt; return true; });
    n = 0; sum = 0;
    WagePair wp = AmtComputeOfficeWages(3, 2, 1.0f, 42, true);
    CHECK(wp.valid);
    CHECK_EQ(wp.wageA, 9600);
    CHECK_EQ(wp.wageB, 6400);
    CHECK_EQ(n, 2);
    CHECK_EQ(sum, 9600 + 6400);
    AmtSetTransferHook(nullptr);
}

TEST(WorldAmtWage, NoHolderNoWage) {
    WagePair wp = AmtComputeOfficeWages(0, 0, 2.0f, 42, true);
    CHECK(!wp.valid);
    CHECK_EQ(wp.wageA, 0);
    CHECK_EQ(wp.wageB, 0);
}

// ---------------------------------------------------------------------------
// Loan repayments.
// ---------------------------------------------------------------------------
TEST(WorldAmtLoan, EvaluateThresholds) {
    AmtSetRateHook(nullptr); // identity rate => base = 4 - slot + 10
    LoanDecision d = AmtEvaluateLoan(0, 0, -100, false);
    CHECK_EQ(d.perTurnInterest, 14);
    CHECK_EQ(d.overdraftLimit, 28);
    CHECK(d.foreclose);       // |100| > 28, no lender
    CHECK(!d.charge);

    d = AmtEvaluateLoan(0, 0, -100, true);
    CHECK(!d.foreclose);      // has lender => no foreclose
    CHECK(d.charge);

    d = AmtEvaluateLoan(2, 0, -50, false);
    CHECK_EQ(d.perTurnInterest, 12);
    CHECK(d.foreclose);       // 50 > 24

    d = AmtEvaluateLoan(0, 0, 5, false);
    CHECK(!d.foreclose);      // positive balance
    CHECK(!d.charge);
}

TEST(WorldAmtLoan, RateHookScalesBase) {
    AmtSetRateHook([](i32 amt, guild::u8) -> i32 { return amt * 100; });
    i32 base = BankInterestBase(0, 0);
    CHECK_EQ(base, 14 * 100);
    AmtSetRateHook(nullptr);
}

TEST(WorldAmtLoan, BankApplyStepCharges) {
    AmtSetRateHook(nullptr);
    static i32 charged = 0; static i32 fromAcct = 0;
    AmtSetTransferHook([](i32 payer, i32, i32 amt, int) -> bool {
        fromAcct = payer; charged = amt; return true;
    });
    charged = 0; fromAcct = 0;
    LoanAccount acct; acct.ownerAccount = 77; acct.heldCurrency = -10; acct.hasLender = true;
    LoanDecision d = BankApplyLoanStep(acct, 0, true);
    CHECK(d.charge);
    CHECK_EQ(charged, 14);
    CHECK_EQ(fromAcct, 77);
    AmtSetTransferHook(nullptr);
}

// ---------------------------------------------------------------------------
// Goods distribution + event picker.
// ---------------------------------------------------------------------------
TEST(WorldAmtGoods, SpawnCount) {
    CHECK_EQ(AmtGoodsDistributionSpawnCount(10, true, 0), 15);
    CHECK_EQ(AmtGoodsDistributionSpawnCount(35, true, 1), 2);
    CHECK_EQ(AmtGoodsDistributionSpawnCount(45, true, 0), 0);
    CHECK_EQ(AmtGoodsDistributionSpawnCount(20, false, 0), 0);
    CHECK_EQ(AmtGoodsDistributionSpawnCount(38, true, 0), 1);
}

TEST(WorldAmtGoods, EventBuildingQualifies) {
    CHECK(AmtEventBuildingQualifies(3, 50));   // 50 < 96
    CHECK(!AmtEventBuildingQualifies(3, 96));  // 96 < 96 false
    CHECK(AmtEventBuildingQualifies(4, 74));   // 74 < 75
    CHECK(AmtEventBuildingQualifies(7, 5));    // 5 < 9
    CHECK(!AmtEventBuildingQualifies(8, 5));   // clamps to idx 5 -> table 0
}

// ---------------------------------------------------------------------------
// Trade transport.
// ---------------------------------------------------------------------------
TEST(WorldTradeTransport, CartCostGolden) {
    CHECK_EQ(TradeTransportComputeCartCost(100000, TransportMode::Slow), 5000);
    CHECK_EQ(TradeTransportComputeCartCost(100000, TransportMode::Medium), 10000);
    CHECK_EQ(TradeTransportComputeCartCost(100000, TransportMode::Fast), 15000);
    CHECK_EQ(TradeTransportComputeCartCost(10000, TransportMode::Fast), 4800);   // clamped up to 32000
    CHECK_EQ(TradeTransportComputeCartCost(500000, TransportMode::Medium), 25600); // clamped to 256000
    CHECK_EQ(TradeTransportComputeCartCost(0, TransportMode::Fast), 0);
    CHECK_EQ(TradeTransportComputeCartCost(-5, TransportMode::Medium), 0);
    CHECK_EQ(TradeTransportComputeCartCost(32000, TransportMode::Slow), 1600);
    CHECK_EQ(TradeTransportComputeCartCost(256000, TransportMode::Fast), 38400);
}

TEST(WorldTradeTransport, CargoValueGolden) {
    // pricefn(gid, ctx) = gid*10 + ctx (deterministic oracle)
    TradeTransportSetMarketPriceHook([](guild::i32 gid, guild::u8 ctx) -> double {
        return static_cast<double>(gid * 10 + ctx);
    });
    std::vector<CargoSlot> slots = {{5,3},{7,2},{-1,9},{2,0},{11,4}};
    double v1 = TradeTransportComputeCargoValue(slots, false, 1.0f, 1, 2, TransportMode::Slow);
    CHECK(v1 > 738.99 && v1 < 739.01);
    double v2 = TradeTransportComputeCargoValue(slots, true, 1.0f, 1, 2, TransportMode::Slow);
    CHECK(v2 > 812.89 && v2 < 812.91);
    double v3 = TradeTransportComputeCargoValue(slots, false, 2.0f, 1, 2, TransportMode::Medium);
    CHECK(v3 > 747.99 && v3 < 748.01); // sell-at-contor uses sellctx, no factor
    TradeTransportSetMarketPriceHook(nullptr);
}

TEST(WorldTradeTransport, AssignRouteCostAndTime) {
    static i32 charged = 0;
    AmtSetTransferHook([](i32, i32, i32 amt, int) -> bool { charged = amt; return true; });
    charged = 0;
    RouteResult r = TradeTransportAssignRoute(100000, TransportMode::Medium, 9, true);
    CHECK_EQ(r.cost, 10000);
    CHECK_EQ(r.travelDays, 1);
    CHECK(r.charged);
    CHECK_EQ(charged, 10000);
    // No mode => no charge.
    RouteResult r2 = TradeTransportAssignRoute(100000, TransportMode::None, 9, true);
    CHECK_EQ(r2.cost, 0);
    CHECK(!r2.charged);
    AmtSetTransferHook(nullptr);
}

// ---------------------------------------------------------------------------
// Treasury / bank / exchange balance ops.
// ---------------------------------------------------------------------------
TEST(WorldTreasury, DepositWithdrawTransfer) {
    AmtSetTransferHook(nullptr); // mirror updates regardless
    Treasury t; t.accountId = 100; t.balance = 0;
    CHECK_EQ(TreasuryDeposit(t, 5, 5000), 5000);
    CHECK_EQ(TreasuryDeposit(t, 5, -10), 5000); // ignored
    i32 moved = -1;
    CHECK_EQ(TreasuryWithdraw(t, 6, 2000, &moved), 3000);
    CHECK_EQ(moved, 2000);
    CHECK_EQ(TreasuryWithdraw(t, 6, 99999, &moved), 0); // clamped to balance
    CHECK_EQ(moved, 3000);

    Treasury city; city.accountId = 1; city.balance = 10000;
    Treasury guild; guild.accountId = 2; guild.balance = 0;
    CHECK_EQ(TreasuryTransfer(city, guild, 4000), 4000);
    CHECK_EQ(city.balance, 6000);
    CHECK_EQ(guild.balance, 4000);
    CHECK_EQ(TreasuryTransfer(city, guild, 999999), 6000); // clamp to from balance
    CHECK_EQ(city.balance, 0);
    CHECK_EQ(guild.balance, 10000);
}

TEST(WorldTreasury, ExchangeFeeFieldWrite) {
    static int writes = 0; static int off105 = -1; static int off109 = -1;
    TreasurySetFieldWriteHook([](guild::i32, int off, guild::i32 val) -> bool {
        ++writes;
        if (off == 105) off105 = val;
        if (off == 109) off109 = val;
        return true;
    });
    writes = 0; off105 = -1; off109 = -1;
    ExchangeFees fees; fees.objectId = 7;
    bool ok = ExchangeSetFees(fees, 3, 8, true);
    CHECK(ok);
    CHECK_EQ(writes, 2);
    CHECK_EQ(off105, 3);
    CHECK_EQ(off109, 8);
    CHECK_EQ(fees.rateFee, 3);
    CHECK_EQ(fees.courierFee, 8);
    TreasurySetFieldWriteHook(nullptr);
}
