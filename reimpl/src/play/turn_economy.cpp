#include "play/turn_economy.h"

#include "crt/rand.h"
#include "world/amt.h"               // AmtRunTurnCycle, AmtEvaluateLoan, AmtComputeOfficeWages,
                                     //   TaxCollectOfficeAllTaxes, AmtSetTransferHook
#include "world/city.h"              // CityInitParameterTable, g_capDivisor, g_cityTotalMoney
#include "world/economy.h"           // PersonEcoView, g_goods, kGoodCategoryCount
#include "world/economy_tick.h"      // EconomyTickPriceLevel, CityTickStatsAndBroadcast,
                                     //   GetSmoothedPriceLevel, SetSmoothedPriceLevel
#include "world/office_prosperity.h" // ProsperityUpdateBuilding, ProsperitySetCommitHook
#include "world/production.h"        // ProductionComputeOutputOverTime / DailyHourOutput

namespace guild::play {

namespace {

// ---------------------------------------------------------------------------
// Inert command-commit hooks. The real Amt passes commit every money move and
// every prosperity field write through the cross-module command queue
// (VIBE_Command_QueueRequest16 etc.). For a standalone economy turn we install
// recording-but-inert defaults so the deterministic arithmetic runs to completion
// without needing the live command path. They count commits (proof the pass
// reached the commit point) but mutate nothing outside `state`.
// ---------------------------------------------------------------------------
int g_transferCommits = 0;
int g_prosperityCommits = 0;

bool InertTransfer(i32 /*payer*/, i32 /*recipient*/, i32 /*amount*/,
                   int /*currency*/) {
    ++g_transferCommits;
    return true;
}
void InertProsperityCommit(world::ProsperityCommit /*which*/, i32 /*objectId*/,
                           float /*value*/, void* /*ctx*/) {
    ++g_prosperityCommits;
}

// Sum the price deltas the economy core wrote this turn (g_goods[3..27]).
double SumPriceDeltas() {
    double s = 0.0;
    for (int g = 3; g < world::kGoodCategoryCount; ++g)
        s += world::g_goods[g].priceDelta;
    return s;
}

// The per-pass context the AmtRunTurnCycle callback drives. It carries the live
// EconomyTurnState plus the wired-in price-tick output for the final stat tick.
struct TurnCtx {
    EconomyTurnState* st = nullptr;
};

// Invoked once per Amt pass, in the order AmtRunTurnCycle replays (which is the
// recovered VIBE_GameTick_BeginPlayerRound order). Each slot calls the REAL
// reconstructed sibling implementing that pass's rules core.
void RunPass(world::AmtPass pass, void* ctxv) {
    auto* ctx = static_cast<TurnCtx*>(ctxv);
    EconomyTurnState& s = *ctx->st;
    ++s.passesRun;

    switch (pass) {
    case world::AmtPass::Production: {
        // VIBE_Amt_RunProductionPass (0x57d448): recompute production over the
        // day's work window (the real production integral). Credit the treasury
        // the work-minutes (the production yield the office collects).
        world::ProdTime ps{s.day, s.hour, 0};
        world::ProdTime pe{s.day, 22, 0};
        int mins = world::ProductionComputeOutputOverTime(ps, pe, /*pause=*/false);
        s.workMinutes = mins;
        s.treasury += mins;
        break;
    }
    case world::AmtPass::Prosperity: {
        // VIBE_Amt_UpdateOfficeProsperity (0x57b718): recompute the building's
        // prosperity score + the *0.95 AI decay, committing through the inert hook.
        world::ProsperityInput in{};
        in.ownerWealth = s.ownerWealth;
        in.room[0] = s.room[0]; in.room[1] = s.room[1]; in.room[2] = s.room[2];
        in.cityMax = s.cityMaxWealth > 0 ? s.cityMaxWealth : 1;
        world::ProsperityResult r =
            world::ProsperityUpdateBuilding(in, /*objectId=*/1, s.prosperity,
                                            s.aiMethod);
        s.prosperity += r.prosperityDelta;   // field +480 trends toward score
        s.aiMethod   += r.aiDecayDelta;       // field +180 decays *0.95
        break;
    }
    case world::AmtPass::BuildingTax: {
        // VIBE_Amt_RunBuildingTaxPass(3) (0x57b9ac): collect the office taxes
        // (flags=3 -> commit + accumulate). Credit the treasury the total.
        i32 collected =
            world::TaxCollectOfficeAllTaxes(s.tax, /*payer=*/-1, /*recipient=*/1,
                                            /*flags=*/3);
        s.taxCollected += collected;
        s.treasury     += collected;
        break;
    }
    case world::AmtPass::LoanRepayments: {
        // VIBE_Amt_ProcessLoanRepayments (0x57b304): charge per-turn interest on
        // a building in debt (or foreclose past the overdraft limit).
        world::LoanDecision d =
            world::AmtEvaluateLoan(s.loanLawSlot, /*currency=*/0, s.heldCurrency,
                                   /*hasLender=*/false);
        if (d.charge) {
            s.interestPaid += d.perTurnInterest;
            s.treasury     -= d.perTurnInterest;
            s.heldCurrency -= d.perTurnInterest; // the debt grows each turn
        }
        break;
    }
    case world::AmtPass::OfficeWages: {
        // VIBE_Amt_ProcessAllOfficeWages (0x57b6bc): pay the two office seats
        // their rank*100*32*lawRate wage, committing through the inert hook.
        world::WagePair w =
            world::AmtComputeOfficeWages(s.officeRankA, s.officeRankB,
                                         s.wageLawRate, /*officeAccount=*/1,
                                         /*commit=*/true);
        i64 paid = static_cast<i64>(w.wageA) + static_cast<i64>(w.wageB);
        s.wagesPaid += paid;
        s.treasury  -= paid;
        break;
    }
    case world::AmtPass::UpdateOffices:
        // VIBE_Amt_UpdateOffices: the office-commit leaf (GUI/office-panel +
        // net-sync owned); no standalone rules core. Inert here.
        break;
    case world::AmtPass::Count:
        break;
    }
}

} // namespace

EconomyTurnState SeedEconomyTurnState() {
    // Seed the 28-good economy parameter table (drift/contrib/cap defaults). The
    // capDivisor=0 forces the first price tick onto the "seed" branch (flt_641DAC
    // := target) so the EMA has a defined start.
    world::CityInitParameterTable(100.0f);
    world::g_capDivisor = 0.0f;
    world::SetSmoothedPriceLevel(0.0f);

    EconomyTurnState s;
    s.demand.assign(world::kGoodCategoryCount, {});
    for (int g = 1; g < world::kGoodCategoryCount; ++g) {
        int n = 2 + (crt::RandNext() % 5);   // 2..6 persons per category
        for (int p = 0; p < n; ++p) {
            world::PersonEcoView v{};
            v.need       = static_cast<u8>(crt::RandNext() % 5);  // 0..4
            v.unemployed = (crt::RandNext() & 1) != 0;
            s.demand[g].push_back(v);
        }
    }

    // Default office tax rates / account (the city's finance-law rate fields).
    s.tax.isGuildOffice     = false;
    s.tax.tradeRate         = 50;     // Gesetz(8)
    s.tax.guildRate         = 40;     // Gesetz(9)
    s.tax.buildingRate      = 30;     // Gesetz(10)
    s.tax.propertyRate      = 20;     // Gesetz(12)
    s.tax.staffRate         = 25;     // Gesetz(11)
    s.tax.activeFinanceLaws = 0;      // book slot open
    s.tax.account           = 100000; // dword_12CEAAC[player]
    s.tax.propertyValue     = 50000;  // dword_12CEAB8[player]
    s.tax.staffValue        = 20000;
    return s;
}

EconomyTurnDeltas RunEconomyTurn(EconomyTurnState& state) {
    EconomyTurnDeltas d{};
    d.priceBefore     = world::GetSmoothedPriceLevel();
    d.cityMoneyBefore = world::g_cityTotalMoney;
    d.treasuryBefore  = state.treasury;
    const i64 taxBefore      = state.taxCollected;
    const i64 wagesBefore    = state.wagesPaid;
    const i64 interestBefore = state.interestPaid;

    // Install the inert command-commit hooks for the duration of the turn so the
    // tax/wage/prosperity passes commit to a sink (not the live command path).
    g_transferCommits = 0;
    g_prosperityCommits = 0;
    world::AmtSetTransferHook(&InertTransfer);
    world::ProsperitySetCommitHook(&InertProsperityCommit, nullptr);

    // Per-day demand drift: re-roll each good's needs from the seeded RNG so the
    // economy genuinely EVOLVES across turns (not a fixed point the EMA converges
    // to), while staying fully deterministic (RNG rooted at crt::Srand).
    for (int g = 1; g < world::kGoodCategoryCount; ++g)
        for (auto& v : state.demand[g])
            v.need = static_cast<u8>(crt::RandNext() % 5);

    state.passesRun = 0;

    // Replay the recovered BeginPlayerRound Amt pass order (RunProductionPass ->
    // UpdateOfficeProsperity -> RunBuildingTaxPass -> ProcessLoanRepayments ->
    // ProcessAllOfficeWages -> UpdateOffices). AmtRunTurnCycle IS the recovered
    // order table from VIBE_GameTick_BeginPlayerRound @0x533188.
    TurnCtx ctx{&state};
    world::AmtRunTurnCycle(&RunPass, &ctx, /*outOrder=*/nullptr);

    // Final stat tick: VIBE_City_TickStatsAndBroadcast (0x57919c) is the last
    // economy call of the round. Its sibling VIBE_Economy_TickPriceLevel
    // (0x579098) carries the price-level EMA + g_goods[].priceDelta recompute the
    // live game runs each day from ExAdvanceGameTick; we run it here so the price
    // level + price deltas move (and the seeded demand is consumed).
    state.priceLevel = world::EconomyTickPriceLevel(state.demand.data());

    // Restore inert defaults (un-installing leaves the module hooks null again).
    world::AmtSetTransferHook(nullptr);
    world::ProsperitySetCommitHook(nullptr, nullptr);

    d.priceAfter      = world::GetSmoothedPriceLevel();
    d.cityMoneyAfter  = world::g_cityTotalMoney;
    d.priceDeltaSum   = SumPriceDeltas();
    d.treasuryAfter   = state.treasury;
    d.workMinutes     = state.workMinutes;
    d.taxThisTurn     = state.taxCollected  - taxBefore;
    d.wagesThisTurn   = state.wagesPaid     - wagesBefore;
    d.interestThisTurn= state.interestPaid  - interestBefore;
    d.passesRun       = state.passesRun;
    d.priceLevel      = state.priceLevel;
    return d;
}

int EconomyTurnPassOrder(world::AmtPass* out, int cap) {
    world::AmtPass order[static_cast<int>(world::AmtPass::Count)];
    world::AmtRunTurnCycle(nullptr, nullptr, order);
    int n = static_cast<int>(world::AmtPass::Count);
    if (n > cap) n = cap;
    for (int i = 0; i < n; ++i) out[i] = order[i];
    return n;
}

} // namespace guild::play
