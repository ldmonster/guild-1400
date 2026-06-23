#include "world/bank.h"

namespace guild::world {

LoanDecision BankEvaluateLoan(const LoanAccount& acct, int lawSlot) {
    return AmtEvaluateLoan(lawSlot, acct.currency, acct.heldCurrency,
                           acct.hasLender, acct.noCreditor);
}

i32 BankInterestBase(int lawSlot, u8 currency) {
    // gilde.exe 0x57b346: VIBE_Money_MultiplyByRate(4 - lawSlot + 10, currency).
    return AmtMoneyMultiplyByRate(4 - lawSlot + 10, currency);
}

// gilde.exe 0x57b304 per-account branch.
//   if (held < 0) {
//     if (!lender && held==-1-marker && |held| > 2*base) -> foreclose;
//     else if (lender) -> dun the lender;
//   }
//   if (officeOwner==1) -> charge base interest to ownerAccount;
// We model the charge of `base` to the owner when in debt and not foreclosed.
LoanDecision BankApplyLoanStep(const LoanAccount& acct, int lawSlot, bool commit) {
    LoanDecision d = AmtEvaluateLoan(lawSlot, acct.currency, acct.heldCurrency,
                                     acct.hasLender, acct.noCreditor);
    if (d.charge && commit) {
        // Original: QueueRequest16(officeStorage, ownerAccount, base, 0).
        // The bank/office storage is the -1 sink endpoint in our hook model.
        AmtCommitTransfer(acct.ownerAccount, -1, d.perTurnInterest, 0);
    }
    return d;
}

} // namespace guild::world
