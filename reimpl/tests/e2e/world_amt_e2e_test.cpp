// End-to-end: drive one full per-turn Amt cycle on a synthetic city and verify
// the resulting treasury / goods / wage state against a hand-computed reference.
//
// The heartbeat order (VIBE_GameTick_BeginPlayerRound 0x533188) is:
//   Production -> Prosperity -> BuildingTax -> LoanRepayments
//                                    -> OfficeWages -> UpdateOffices
// We wire mock passes that perform the recovered arithmetic and feed/observe a
// shared treasury, then assert the final state.
#include <vector>

#include "tests/framework/test.h"
#include "world/amt.h"
#include "world/tradetransport.h"
#include "world/treasury.h"
#include "world/bank.h"

using namespace guild;
using namespace guild::world;

namespace {

// A synthetic city: one office holding two seats, a small production base, a
// trade account, and a treasury that money flows into/out of.
struct SynthCity {
    Treasury treasury;        // city/office cash sink (accountId = -1 sink mirror)
    i32      officeAccount = 200;

    // Office tax inputs (one office building).
    OfficeTaxInput tax;

    // Wage inputs.
    int   rankA = 3, rankB = 2;
    float wageLawRate = 1.0f;

    // Goods distribution.
    int  activeBuildings = 10;
    bool belowThreshold = true;
    int  spawnRand2 = 0;

    // Loan.
    LoanAccount loan;
    int loanLawSlot = 0;

    // Observed cumulative collections.
    i32 taxesCollected = 0;
    i32 wagesPaid = 0;
    int spawnsRequested = 0;
    bool loanCharged = false;

    std::vector<AmtPass> order;
};

SynthCity g_city;

// The transfer hook routes every money move into the city treasury mirror.
// payer == -1 means "from treasury sink" (wages paid out); recipient == -1 means
// "to treasury sink" (loan interest). Tax transfers (payer/recipient both >=0)
// also add into the city accumulator we track separately.
bool TurnTransfer(i32 payer, i32 /*recipient*/, i32 amount, int /*currency*/) {
    if (payer == -1)
        g_city.treasury.balance -= amount; // paid out of treasury
    else
        g_city.treasury.balance += amount; // collected into treasury
    return true;
}

void RunPass(AmtPass pass, void* ctx) {
    SynthCity& c = *static_cast<SynthCity*>(ctx);
    switch (pass) {
        case AmtPass::Production: {
            // Production recompute is a no-op on cash; the nested goods
            // distribution determines how many businesses to spawn.
            c.spawnsRequested =
                AmtGoodsDistributionSpawnCount(c.activeBuildings, c.belowThreshold,
                                               c.spawnRand2);
            break;
        }
        case AmtPass::Prosperity:
            break; // prosperity recompute: no cash effect in this synthetic model
        case AmtPass::BuildingTax: {
            i32 total = TaxCollectOfficeAllTaxes(c.tax, /*payer*/ 5, /*recip*/ 6,
                                                 /*flags*/ 1 /*commit*/);
            c.taxesCollected += total;
            break;
        }
        case AmtPass::LoanRepayments: {
            LoanDecision d = BankApplyLoanStep(c.loan, c.loanLawSlot, true);
            c.loanCharged = d.charge;
            break;
        }
        case AmtPass::OfficeWages: {
            WagePair wp = AmtComputeOfficeWages(c.rankA, c.rankB, c.wageLawRate,
                                                c.officeAccount, true /*commit*/);
            c.wagesPaid += wp.wageA + wp.wageB;
            break;
        }
        case AmtPass::UpdateOffices:
            break; // office bookkeeping: no cash effect here
        default:
            break;
    }
}

} // namespace

TEST(WorldAmtE2E, FullTurnCycle) {
    g_city = SynthCity{};
    g_city.treasury.accountId = -1;
    g_city.treasury.balance = 100000; // starting city cash

    // One non-guild office: trade 50% of 200000, building 10% of 200000,
    // property 20% of 50000, staff 25% of 8000.
    g_city.tax.isGuildOffice = false;
    g_city.tax.tradeRate = 50;
    g_city.tax.buildingRate = 10;
    g_city.tax.propertyRate = 20;
    g_city.tax.staffRate = 25;
    g_city.tax.activeFinanceLaws = 0;
    g_city.tax.account = 200000;
    g_city.tax.propertyValue = 50000;
    g_city.tax.staffValue = 8000;

    // Office seats rank 3 + 2 at law-rate 1.0.
    g_city.rankA = 3; g_city.rankB = 2; g_city.wageLawRate = 1.0f;

    // 10 active buildings, below threshold => spawn (40-10)/2 = 15.
    g_city.activeBuildings = 10; g_city.belowThreshold = true; g_city.spawnRand2 = 0;

    // Loan: office account in debt with a lender => charged interest base 14.
    g_city.loan.ownerAccount = 200; g_city.loan.heldCurrency = -10;
    g_city.loan.hasLender = true; g_city.loanLawSlot = 0;
    g_city.loan.currency = 0;

    AmtSetRateHook(nullptr);            // identity rate
    AmtSetTransferHook(&TurnTransfer);

    AmtPass order[static_cast<int>(AmtPass::Count)];
    AmtRunTurnCycle(&RunPass, &g_city, order);

    // ----- Reference values (hand / python computed) -----
    // Single-precision float rounding makes building 10%x200000 = 19999 and
    // property 20%x50000 = 9999 (not the naive 20000 / 10000); the faithful
    // float chain is bit-for-bit what the original computes.
    const i32 kTaxes = 100000 + 19999 + 9999 + 2000; // 131998
    const i32 kWages = 9600 + 6400;                    // 16000
    const int kSpawns = 15;
    const i32 kLoanInterest = 14;
    const i32 kStart = 100000;
    // Treasury: +taxes (collected in) - wages (paid out) - loan interest (paid
    // out: payer == ownerAccount which is >= 0 => collected IN per hook rule;
    // loan interest endpoint is recipient==-1, payer==200 => +14). So the loan
    // interest is a collection from the debtor into the treasury.
    const i32 kExpectedBalance = kStart + kTaxes - kWages + kLoanInterest;

    // Order assertion: heartbeat sequence exactly.
    CHECK_EQ(order[0], AmtPass::Production);
    CHECK_EQ(order[1], AmtPass::Prosperity);
    CHECK_EQ(order[2], AmtPass::BuildingTax);
    CHECK_EQ(order[3], AmtPass::LoanRepayments);
    CHECK_EQ(order[4], AmtPass::OfficeWages);
    CHECK_EQ(order[5], AmtPass::UpdateOffices);

    CHECK_EQ(g_city.taxesCollected, kTaxes);
    CHECK_EQ(g_city.wagesPaid, kWages);
    CHECK_EQ(g_city.spawnsRequested, kSpawns);
    CHECK(g_city.loanCharged);
    CHECK_EQ(g_city.treasury.balance, kExpectedBalance);

    AmtSetTransferHook(nullptr);
}

TEST(WorldAmtE2E, TradeRouteThenTreasury) {
    // A caravan sells cargo at the remote contor, then the proceeds land in the
    // guild treasury; verify cost+value+balance end-to-end.
    TradeTransportSetMarketPriceHook([](guild::i32 gid, guild::u8 ctx) -> double {
        return static_cast<double>(gid * 10 + ctx);
    });
    std::vector<CargoSlot> cargo = {{5, 3}, {7, 2}, {11, 4}};
    // Value at home market (no factor, priceMul 1.0, ctx 1): 5*51? compute:
    //   gid5: price=5*10+1=51 *3 = 153
    //   gid7: 71 *2 = 142
    //   gid11:111 *4 = 444   => 739
    double value = TradeTransportComputeCargoValue(cargo, false, 1.0f, 1, 2,
                                                   TransportMode::Slow);
    CHECK(value > 738.99 && value < 739.01);

    // Ship it Fast: cart cost on a 100000-value load = 15000.
    static i32 lastCharge = 0;
    AmtSetTransferHook([](i32, i32, i32 amt, int) -> bool { lastCharge = amt; return true; });
    lastCharge = 0;
    RouteResult r = TradeTransportAssignRoute(100000, TransportMode::Fast, 9, true);
    CHECK_EQ(r.cost, 15000);
    CHECK_EQ(lastCharge, 15000);

    // Treasury collects the sale proceeds (use the integer cargo value).
    Treasury guild; guild.accountId = 2; guild.balance = 0;
    AmtSetTransferHook(nullptr);
    i32 proceeds = static_cast<i32>(value); // 739
    CHECK_EQ(TreasuryDeposit(guild, 3, proceeds), 739);
    CHECK_EQ(guild.balance, 739);

    TradeTransportSetMarketPriceHook(nullptr);
}
