#include "world/bank_loan.h"

#include <cmath>

#include "crt/rand.h"

namespace guild::world {

namespace {
LoanCmdHook g_loanHook = nullptr;
void*       g_loanCtx  = nullptr;

// VIBE_Coord_ConvertX 0x5c6b08 == round-toward-zero (truncation).
i32 Trunc(double x) { return static_cast<i32>(std::trunc(x)); }
} // namespace

float LoanRelationFactor(LoanRelationTier tier) {
    switch (tier) {
        case LoanRelationTier::High: return loan::kFactorHigh; // 0.6
        case LoanRelationTier::Mid:  return loan::kFactorMid;  // 0.45
        case LoanRelationTier::Default:
        default:                     return loan::kFactorDefault; // 0.3
    }
}

// gilde.exe 0x591990 (asm @591ad8..591b8d): the debt-coverage credit-payout clamp.
//   v47 = (float)((double)wealth * 0.2);   // fstp [var_34] -- stored as 32-bit float
//   if (v47 >= 64000.0f) v46 = 64000.0f;   // fcomp flt_626A24 (float compare)
//   else                 v46 = v47;        // var_38 = float bits of var_34
//   ... v43 = (i64)v46;                    // ConvertX -> fistp [var_54], trunc(v46)
// var_34/var_38 are 4-byte (var_34 typed `float` in the stack frame), so the
// wealth*0.2 product and the 64000 clamp are evaluated in SINGLE precision before
// the truncation -- not double. Reproduce the float intermediate exactly.
i32 LoanGrantCapacity(i32 lenderWealth) {
    float cap = static_cast<float>(static_cast<double>(lenderWealth) * loan::kWealthCapFactor);
    if (cap >= loan::kWealthCapCeil)  // flt_626A24 == 64000.0f (single-precision compare)
        cap = loan::kWealthCapCeil;
    // The original truncates v46 via VIBE_Coord_ConvertX (round-toward-zero) before use.
    return Trunc(static_cast<double>(cap));
}

// gilde.exe 0x591990: v41 = wealth*v48; v40 = prevBase*0.5;
//   v42 = (v41 >= v40) ? v40 : v41;   // == min(v41, v40)
//   v44 = trunc(v42);
i32 LoanOfferBase(i32 lenderWealth, i32 prevBase, LoanRelationTier tier) {
    double v41 = static_cast<double>(lenderWealth) * LoanRelationFactor(tier);
    double v40 = static_cast<double>(prevBase) * loan::kBaseScaleHalf;
    double v42 = (v41 >= v40) ? v40 : v41;
    return Trunc(v42);
}

// gilde.exe 0x591990 (asm @591dc6): the interest formula.
//   f    = field;
//   base = f * 0.7;                                  // dbl_626A4C
//   span = f * 1.5 - base;                           // (dbl_626A54 - low)
//   rate = base + term*(1/6) * span;                 // flt_626A44
//   rate = trunc(rate);
//   if (bankType) rate -= (fav + (-50.0)) * 0.01 * 5.0;  // (-50,0.01,5.0)
i32 LoanComputeInterest(i32 lenderRateField, int term, bool bankType, float fav) {
    double f = static_cast<double>(lenderRateField);
    double base = f * loan::kRateLow;
    double span = f * loan::kRateHigh - base;
    double rate = base + static_cast<double>(term) * static_cast<double>(loan::kTermSixth) * span;
    i32 r = Trunc(rate);
    if (bankType) {
        double adj = (static_cast<double>(fav) + static_cast<double>(loan::kFavBias))
                   * loan::kJitterStep * static_cast<double>(loan::kFavScale);
        r = Trunc(static_cast<double>(r) - adj);
    }
    return r;
}

// gilde.exe 0x591990 offer loop.
std::vector<LoanOffer> LoanGenerateOffers(const LoanOfferInput& in) {
    std::vector<LoanOffer> offers;
    i32 base = LoanOfferBase(in.lenderWealth, in.prevBase, in.tier);

    for (int i = 0; i < in.count; ++i) {
        LoanOffer o;

        // amount = trunc( (1 - (rand%30)*0.01) * base )
        int jitter = crt::RandNext() % 30;
        double mul = 1.0 + static_cast<double>(-jitter) * loan::kJitterStep;
        i32 amount = Trunc(mul * static_cast<double>(base));

        // per-offer index scaling: offer 0 -> *0.25, offer 1 -> *0.5, else *1.
        if (i == 0)
            amount = Trunc(static_cast<double>(amount) * loan::kBaseScaleQuarter);
        else if (i == 1)
            amount = Trunc(static_cast<double>(amount) * loan::kBaseScaleHalf);
        o.amount = amount;

        // term = (rand%6) + 2
        o.term = (crt::RandNext() % 6) + 2;

        // interest
        o.interest = LoanComputeInterest(in.lenderRateField, o.term,
                                         in.lenderIsBankType, in.favorability);
        offers.push_back(o);
    }
    return offers;
}

void LoanSetCmdHook(LoanCmdHook hook, void* ctx) {
    g_loanHook = hook;
    g_loanCtx = ctx;
}
void LoanEmit(const LoanCommand& cmd) {
    if (g_loanHook)
        g_loanHook(cmd, g_loanCtx);
}

// gilde.exe 0x51a100 — VIBE_Credit_ConfirmLoanRequest.
bool CreditConfirmLoanRequest(bool accept, bool resourceOk, bool confirmed,
                              i32 lenderAccount, i32 borrowerPerson, i32 amount,
                              int currency, i32* borrowerFamilyDebt, bool commit) {
    // if (a1 && CheckResourceAmount(..) && ShowMessageBox(1, ..))
    if (!(accept && resourceOk && confirmed))
        return false;

    if (commit) {
        LoanCommand cmd;
        cmd.lender = lenderAccount;
        cmd.borrower = borrowerPerson;
        cmd.amount = amount;
        cmd.currency = currency;
        LoanEmit(cmd);
    }
    // familyRecord+72 (+18 dword) += amount.
    if (borrowerFamilyDebt)
        *borrowerFamilyDebt += amount;
    return true;
}

} // namespace guild::world
