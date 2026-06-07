#pragma once
// ===========================================================================
// bank_loan.{h,cpp} — the bank LOAN OFFER generator + the loan-grant commit
// (gilde.exe). MODULE: the remaining Bank/Credit loan body (offer
// amount/term/interest generation + the confirm transfer). The per-turn interest
// pass (VIBE_Amt_ProcessLoanRepayments) already lives in world/bank.{h,cpp}; this
// file adds the OFFER side (the dialog that proposes loans).
// ===========================================================================
//
// Recovered byte-for-byte:
//
//   * VIBE_Command_EvaluatePurchaseAction 0x591990 (mode 3 == loan) — generates
//     up to N loan offers, each an (amount, termMonths, interest) triple. The
//     amounts/terms/rates are RNG-driven (VIBE_Util_RandNext == crt::RandNext) and
//     scaled by the lender's wealth, the borrower's relation tier, and a
//     per-lender rate field. Also computes the lender's grant CAPACITY
//     (wealth * 0.2, clamped to 64000) used to gate which existing loans stay.
//   * VIBE_Credit_ConfirmLoanRequest 0x51a100 — on accept, transfers the loan
//     principal from lender to borrower (EnqueueCmd15) and ADDS it to the
//     borrower family's debt total (+72). The message/handler-free leaves are
//     routed through the command hook.
//
// All FP constants are recovered from get_bytes (offsets noted). RNG is the
// shared crt LCG so the offer sequence is reproducible for golden tests.
#include <vector>

#include "guild/common/types.h"

namespace guild::world {

// ===========================================================================
// Recovered tuning constants (get_bytes @ the noted addresses).
// ===========================================================================
namespace loan {
constexpr double kWealthCapFactor = 0.2;        // dbl_626A1C (0x3FC999999999999A)
constexpr float  kWealthCapCeil   = 64000.0f;   // flt_626A24 (0x477A0000)
constexpr double kBaseScaleHalf   = 0.5;        // dbl_626A2C (0x3FE0000000000000)
constexpr double kJitterStep      = 0.01;       // dbl_626A34 (0x3F847AE147AE147B)
constexpr double kBaseScaleQuarter= 0.25;       // dbl_626A3C (0x3FD0000000000000)
constexpr float  kTermSixth       = 0.16666667f;// flt_626A44 (0x3E2AAAAB == 1/6)
constexpr double kRateLow         = 0.7;        // dbl_626A4C (0x3FE6666666666666)
constexpr double kRateHigh        = 1.5;        // dbl_626A54 (0x3FF8000000000000)
constexpr float  kFavScale        = 5.0f;       // flt_626A5C (0x40A00000)
constexpr float  kFavBias         = -50.0f;     // flt_626A60 (0xC2480000)

// Relation-tier factor applied to the offer base (the v48 selection):
constexpr float kFactorDefault = 0.3f;          // no special relation
constexpr float kFactorMid     = 0.45f;         // DispatchByType(11) & 2
constexpr float kFactorHigh    = 0.6f;          // DispatchByType(15) & 2
} // namespace loan

// Borrower relation tier that selects the offer-base factor v48.
enum class LoanRelationTier { Default, Mid, High };
float LoanRelationFactor(LoanRelationTier tier);

// ===========================================================================
// gilde.exe 0x591990 — the lender GRANT CAPACITY:
//   cap = wealth * 0.2;  if (cap >= 64000) cap = 64000;   (trunc to int)
// (Only granted when gameDay < 6 and the lender is not a guard/criminal class;
// the caller supplies those gates.) Returns the int capacity.
// ===========================================================================
i32 LoanGrantCapacity(i32 lenderWealth);

// ===========================================================================
// One generated loan offer.
// ===========================================================================
struct LoanOffer {
    i32 amount = 0;   // principal offered  (var_14)
    int term = 0;     // term in months     (var_10 = rand%6 + 2)
    i32 interest = 0; // per-term interest  (var_18)
};

// Inputs to the offer generator (the values the original reads from the lender /
// borrower / relation tables).
struct LoanOfferInput {
    i32  lenderWealth = 0;   // VIBE_Person_ComputeTotalWealth(lender)
    i32  prevBase = 0;       // the running base the offer scales from (the count
                             // term: max(prevBase*0.5, wealth*factor) feeds var_4C)
    i32  lenderRateField = 0;// *(lender+101) — the per-lender rate basis (field101)
    LoanRelationTier tier = LoanRelationTier::Default;
    bool lenderIsBankType = false; // *(lender+2) in {6,7} -> apply the fav adjust
    float favorability = 0.0f;     // VIBE_Ai_ComputePersonFavorability(borrower)
    int  count = 3;          // number of offers to generate (a4)
};

// gilde.exe 0x591990 offer loop. Generates `in.count` offers using crt::RandNext
// for the per-offer jitter (rand%30) and term (rand%6). Deterministic given the
// RNG state — seed crt before calling for a golden sequence. The first offer
// (index 0) scales the base by 0.25, the second (index 1) by 0.5, the rest by 1.
std::vector<LoanOffer> LoanGenerateOffers(const LoanOfferInput& in);

// The single-offer interest formula (exposed for direct golden testing):
//   interest = trunc( field*0.7 + term*(1/6)*(field*1.5 - field*0.7) )
// then, if the lender is a bank type, minus (fav - 50)*0.01*5.0.
i32 LoanComputeInterest(i32 lenderRateField, int term, bool bankType, float fav);

// The offer base var_4C (exposed): trunc( min(wealth*factor, prevBase*0.5) ).
i32 LoanOfferBase(i32 lenderWealth, i32 prevBase, LoanRelationTier tier);

// ===========================================================================
// Command hook (lockstep). The loan grant commits one transfer + a family-debt
// bump; the dialog plumbing (message box, handler free) is deferred.
// ===========================================================================
struct LoanCommand {
    i32 lender = 0;     // a1+1   (lender account)
    i32 borrower = 0;   // a2+176 (borrower person)
    i32 amount = 0;     // a2+180 (loan principal)
    int currency = 0;   // byte_6477A1
};
using LoanCmdHook = void (*)(const LoanCommand& cmd, void* ctx);
void LoanSetCmdHook(LoanCmdHook hook, void* ctx);
void LoanEmit(const LoanCommand& cmd);

// ===========================================================================
// gilde.exe 0x51a100 — VIBE_Credit_ConfirmLoanRequest (the grant commit).
//   if (accept && CheckResourceAmount(amount) && ShowMessageBox(1)):
//     EnqueueCmd15(lender, borrower, amount, currency);   // pay out principal
//     familyDebt += amount;                                // borrower family +72
// `borrowerFamilyDebt` is updated in place (the +72 dword). Returns true if the
// loan was granted (the gates passed). `accept`/`resourceOk`/`confirmed` mirror
// the original's three guards (the player-clicked-yes, funds-available, and
// message-box-confirmed conditions).
// ===========================================================================
bool CreditConfirmLoanRequest(bool accept, bool resourceOk, bool confirmed,
                              i32 lenderAccount, i32 borrowerPerson, i32 amount,
                              int currency, i32* borrowerFamilyDebt, bool commit);

} // namespace guild::world
