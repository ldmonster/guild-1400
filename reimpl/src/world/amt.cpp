#include "world/amt.h"

namespace guild::world {

// ===========================================================================
// Hooks.
// ===========================================================================
namespace {
AmtTransferHook g_transferHook = nullptr;
AmtRateHook     g_rateHook = nullptr;
} // namespace

void AmtSetTransferHook(AmtTransferHook hook) { g_transferHook = hook; }

bool AmtCommitTransfer(i32 payer, i32 recipient, i32 amount, int currency) {
    if (g_transferHook)
        return g_transferHook(payer, recipient, amount, currency);
    return false;
}

void AmtSetRateHook(AmtRateHook hook) { g_rateHook = hook; }

i32 AmtMoneyMultiplyByRate(i32 amount, u8 currencyId) {
    if (g_rateHook)
        return g_rateHook(amount, currencyId);
    return amount; // identity (multiplier == 1) in isolation
}

// VIBE_Coord_ConvertX 0x5c6b08 is frndint with round-toward-zero, so casting a
// float intermediate to int after it is truncation toward zero.
i32 AmtTrunc(double x) { return static_cast<i32>(static_cast<long long>(x)); }

// ===========================================================================
// Tax formulas.
//   v18 = (float)rate; v20 = v18 * 0.01f; v19 = (float)account;
//   v21 = v20 * v19;   v23 = (v21 <= 0) ? 0 : v21;  result = trunc(v23)
// The multiplies are single-precision in the originals (float locals); we keep
// the float chain so the truncated integer result is bit-faithful.
// ===========================================================================
i32 TaxComputeIncome(i32 rate, i32 account) {
    float fRate = static_cast<float>(rate);
    float scaled = fRate * kTaxRateScale;
    float fAcct = static_cast<float>(account);
    float raw = scaled * fAcct;
    float clamped = (raw <= 0.0f) ? 0.0f : raw;
    return AmtTrunc(static_cast<double>(clamped));
}

// gilde.exe 0x57abd8 / 0x57b0e4 / 0x57ad28 / 0x57af64 — common control flow:
//   count active laws in the kind's finance book slot; if >= 6 return 0;
//   amount = TaxComputeIncome(rate, account);
//   if (account > 0) { if(flags&1) transfer; if(flags&2) cityAcc += amount; }
//   return 1;
int TaxCollectIncome(TaxKind kind, i32 rate, int activeFinanceLaws, i32 account,
                     i32 payer, i32 recipient, int flags, TaxLine* out,
                     i32* cityAccumulator) {
    if (out) { out->kind = kind; out->amount = 0; out->collected = false; out->committed = false; }

    if (activeFinanceLaws >= 6)
        return 0;

    i32 amount = TaxComputeIncome(rate, account);
    if (out) out->amount = amount;

    if (account > 0) {
        if (out) out->collected = true;
        if (flags & 1) {
            // The originals enqueue VIBE_Command_QueueRequest16; recipient/payer
            // order varies per kind but the money magnitude is `amount`.
            bool ok = AmtCommitTransfer(payer, recipient, amount, 0);
            if (out) out->committed = ok;
        }
        if (flags & 2) {
            if (cityAccumulator) *cityAccumulator += amount;
        }
        return 1;
    }
    return 1; // line produced (slot open) even if account <= 0, per originals
}

// gilde.exe 0x57b214 — VIBE_Tax_CollectOfficeAllTaxes dispatch order.
i32 TaxCollectOfficeAllTaxes(const OfficeTaxInput& in, i32 payer, i32 recipient,
                             int flags) {
    i32 total = 0;
    TaxLine line;

    // guild OR trade depending on the office building kind.
    if (in.isGuildOffice) {
        TaxCollectIncome(TaxKind::Guild, in.guildRate, in.activeFinanceLaws,
                         in.account, payer, recipient, flags, &line, nullptr);
    } else {
        TaxCollectIncome(TaxKind::Trade, in.tradeRate, in.activeFinanceLaws,
                         in.account, payer, recipient, flags, &line, nullptr);
    }
    if (line.collected) total += line.amount;

    TaxCollectIncome(TaxKind::Building, in.buildingRate, in.activeFinanceLaws,
                     in.account, payer, recipient, flags, &line, nullptr);
    if (line.collected) total += line.amount;

    // property only when propertyValue > 0 (the original gates on
    // dword_12CEAB8[player] > 0 before calling).
    if (in.propertyValue > 0) {
        TaxCollectIncome(TaxKind::Property, in.propertyRate, in.activeFinanceLaws,
                         in.propertyValue, payer, recipient, flags, &line, nullptr);
        if (line.collected) total += line.amount;
    }

    TaxCollectIncome(TaxKind::Staff, in.staffRate, in.activeFinanceLaws,
                     in.staffValue, payer, recipient, flags, &line, nullptr);
    if (line.collected) total += line.amount;

    return total;
}

// ===========================================================================
// Wages.
//   v9 = (double)officeRank * 100.0f * 32.0f * lawRate;  wage = trunc(v9)
// (The original promotes the two float constants and the float lawRate into the
// double multiply; we match with a double accumulate.)
// ===========================================================================
i32 AmtComputeWage(int officeRank, float lawRate) {
    double v = static_cast<double>(officeRank)
             * static_cast<double>(kWageRankScale)
             * static_cast<double>(kWageBaseScale)
             * static_cast<double>(lawRate);
    return AmtTrunc(v);
}

WagePair AmtComputeOfficeWages(int rankA, int rankB, float lawRate,
                               i32 officeAccount, bool commit) {
    WagePair wp;
    // Seat with no rank yields no wage. The original early-returns when neither
    // seat byte is set; here a zero rank simply produces a zero wage.
    if (rankA <= 0 && rankB <= 0)
        return wp;

    wp.valid = true;
    wp.wageB = AmtComputeWage(rankB, lawRate); // a3+8 (byte_12CEA79 seat)
    wp.wageA = AmtComputeWage(rankA, lawRate); // a3+4 (byte_12CEA76 seat)

    if (commit) {
        if (wp.wageA)
            AmtCommitTransfer(-1, officeAccount, wp.wageA, 0);
        if (wp.wageB)
            AmtCommitTransfer(-1, officeAccount, wp.wageB, 0);
    }
    return wp;
}

// ===========================================================================
// Loan repayments.
//   v3 = VIBE_Money_MultiplyByRate(4 - lawSlot + 10, currency);
//   overdraftLimit = 2 * v3;
//   if held < 0 && no lender && |held| > overdraftLimit -> foreclose
//   else if held < 0 -> charge v3 interest
// ===========================================================================
LoanDecision AmtEvaluateLoan(int lawSlot, u8 currency, i32 heldCurrency,
                             bool hasLender) {
    LoanDecision d;
    i32 base = AmtMoneyMultiplyByRate(4 - lawSlot + 10, currency);
    d.perTurnInterest = base;
    d.overdraftLimit = 2 * base;

    if (heldCurrency < 0) {
        i32 debt = heldCurrency < 0 ? -heldCurrency : heldCurrency; // abs32
        if (!hasLender && debt > d.overdraftLimit) {
            d.foreclose = true;
        } else {
            d.charge = true;
        }
    }
    return d;
}

// ===========================================================================
// Goods-distribution spawn count.
//   if activeCount <= threshold:                       (else: do nothing here)
//     if active < 30:  (40 - active) / 2
//     elif active < 40: rand(2)+1
//     else 0
// (In the original `activeCount` is a float `v0` compared to dbl_62598C and the
// >=30/>=40 branch keys off the int spawn-candidate counter `v32`. We expose the
// active building count for both; the caller passes the int active count.)
// ===========================================================================
int AmtGoodsDistributionSpawnCount(int activeCount, bool belowThreshold,
                                   int rand2) {
    if (!belowThreshold)
        return 0;
    if (activeCount < 30)
        return (40 - activeCount) / 2;
    if (activeCount < 40)
        return rand2 + 1; // rand2 in {0,1} -> {1,2}
    return 0;
}

// ===========================================================================
// Event-building qualification.
//   index = occupiedRooms - 3 (rooms range 3..7 -> 0..4; entry 5 is 0/never)
//   qualifies when (d256 roll + 1) < kEventProbTable[index]
// ===========================================================================
bool AmtEventBuildingQualifies(int occupiedRooms, int d256Plus1Roll) {
    int idx = occupiedRooms - 3;
    if (idx < 0) idx = 0;
    if (idx > 5) idx = 5;
    return d256Plus1Roll < kEventProbTable[idx];
}

// ===========================================================================
// Per-turn cycle order (from VIBE_GameTick_BeginPlayerRound 0x533188).
// The order in the heartbeat is:
//   RunProductionPass, UpdateOfficeProsperity, RunBuildingTaxPass,
//   ProcessLoanRepayments, ProcessAllOfficeWages, UpdateOffices.
// (RunProductionPass internally calls RunGoodsDistributionPass; RunBuildingTaxPass
// internally calls EnforceLawViolations. Those nested passes are part of their
// parent's translation and are not separate top-level slots.)
// ===========================================================================
void AmtRunTurnCycle(AmtPassFn cb, void* ctx, AmtPass* outOrder) {
    static const AmtPass kOrder[] = {
        AmtPass::Production,
        AmtPass::Prosperity,
        AmtPass::BuildingTax,
        AmtPass::LoanRepayments,
        AmtPass::OfficeWages,
        AmtPass::UpdateOffices,
    };
    for (size_t i = 0; i < sizeof(kOrder) / sizeof(kOrder[0]); ++i) {
        if (outOrder) outOrder[i] = kOrder[i];
        if (cb) cb(kOrder[i], ctx);
    }
}

} // namespace guild::world
