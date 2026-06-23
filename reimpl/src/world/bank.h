#pragma once
// Bank (Geldleihe) loan operations — rules core (gilde.exe).
//
// VIBE_Bank_RunContactDispatchLoop (0x51da04) is a UI contact loop that fans out
// to the loan/exchange/courier dialogs; it carries no standalone arithmetic. The
// recoverable bank rules core is the per-turn loan-interest model the Amt loan
// pass (VIBE_Amt_ProcessLoanRepayments 0x57b304) drives:
//   interestBase = VIBE_Money_MultiplyByRate(4 - lawSlot + 10, currency)
//   overdraftLimit = 2 * interestBase
// A debtor whose |debt| exceeds the overdraft limit with no lender is
// foreclosed; otherwise interestBase is charged each turn. This is the same
// computation modeled by AmtEvaluateLoan; this header provides a bank-flavoured
// facade plus the loan-account abstraction (the UI dialog is deferred).
#include "guild/common/types.h"

#include "world/amt.h"  // LoanDecision, AmtEvaluateLoan, AmtMoneyMultiplyByRate

namespace guild::world {

// A loan account as the bank pass reads it.
struct LoanAccount {
    i32  ownerAccount = -1; // building/office account id
    i32  heldCurrency = 0;  // VIBE_Person_SumCurrencyHeld (negative == in debt)
    bool hasLender = false; // a private lender already covers this debt (dword_12CEA7C)
    bool noCreditor = true; // dword_12CE96C[i]==-1 — no creditor on record (foreclosure gate)
    u8   currency = 0;      // currency context for the rate lookup
};

// gilde.exe 0x57b304 (per-account decision) — VIBE_Amt_ProcessLoanRepayments.
// Computes the per-turn interest, overdraft limit, and foreclosure decision for
// `acct` under finance-law slot `lawSlot`. Thin facade over AmtEvaluateLoan.
LoanDecision BankEvaluateLoan(const LoanAccount& acct, int lawSlot);

// Per-turn interest base only (4 - lawSlot + 10 scaled by the city currency
// rate). Exposed for callers/tests that need just the magnitude.
i32 BankInterestBase(int lawSlot, u8 currency);

// Apply the per-turn loan step: if the account owes, either forecloses (returns
// the decision with foreclose=true and enqueues nothing here — foreclosure is a
// higher-level command) or charges interestBase via the Amt transfer hook
// (payer = ownerAccount, recipient = the bank/office storage = -1). When
// `commit`, the charge is enqueued. Returns the decision.
LoanDecision BankApplyLoanStep(const LoanAccount& acct, int lawSlot, bool commit);

} // namespace guild::world
