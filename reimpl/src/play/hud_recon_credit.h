#pragma once
// ===========================================================================
// hud_recon_credit — gilde.exe credit / loan (Geldleihe) dialog logic
// ===========================================================================
// The bank "Geldleihe" slice has four sibling dialogs. This unit reconstructs the
// PURE value/formula/table logic of each 1:1; the retained-mode form shell, the live
// frame loop, and the command queue are coupled leaves (inert hooks). The on-disk
// .form shells are already loaded by gui::Form_ParseResourceFile; src/play/dialog_bank
// already drives the *take-loan* sibling (0x51a244). These are the other four:
//
//   0x519dbc VIBE_Credit_ShowNewLoanDialog      — new-loan offer list ("Neuerkredit")
//   0x51a868 VIBE_Credit_ShowLoanListDialog     — outstanding-loan list  ("Kreditaufnehmen")
//   0x51ab48 VIBE_Credit_ShowLoanReleaseDialog  — repay / release a loan ("KreditFreigabe")
//   0x51d64c VIBE_Credit_ShowAccountInfoDialog  — account overview tabs  ("info")
//
// Additive: new file, namespace guild::play.

#include "guild/common/types.h"

namespace guild {
namespace play {

using namespace guild;

// gilde.exe 0x58f19c — VIBE_Money_MultiplyByRate(amount, ratePct) = amount*ratePct/100
// (self-contained copy; matches src/app/session_init.cpp).
inline i32 Credit_MultiplyByRate(i32 amount, u8 ratePct) {
    return static_cast<i32>(static_cast<i64>(amount) * ratePct / 100);
}

// ===========================================================================
// 0x519dbc — VIBE_Credit_ShowNewLoanDialog: lender-offer compaction.
// VIBE_Command_EvaluatePurchaseAction fills a 9-dword scratch (3 candidate lenders ×
// 3 dwords: {amount, b, c}). The dialog packs the candidates whose amount>0 into a
// row table of stride 5: row = {amount, b, c, childObjectId(0), index}. The packing
// loop (note v5 += 5 BEFORE the first store, and the writes land at v36[v5],
// v36[v5+1], v36[v5+2], v36[v5+4]) means rows begin at base offset 5.
//   for (i=0; i<3; ++i) {
//     amount = src[3*i];
//     if (amount > 0) {
//       v5 += 5;
//       row.amount = amount;          // v36[v5]
//       row.b      = src[3*i+1];       // v36[v5+1]
//       row.c      = src[3*i+2];       // v36[v5+2]
//       row.index  = i;                // v36[v5+4]  (v3 == i)
//     }
//   }
// Later the click loop scans rows v36[5+5*k] (k=0..2) for amount!=0 && clicked==childId.
// ===========================================================================
struct LoanOffer {
    i32 amount; // src[3*i] (>0 to be included)
    i32 b;      // src[3*i+1]
    i32 c;      // src[3*i+2]
};
struct LoanOfferRow {
    i32 amount;        // v36[base]
    i32 b;             // v36[base+1]
    i32 c;             // v36[base+2]
    i32 childObjectId; // v36[base+3]  (filled when the row's widget is created; -1/0)
    i32 index;         // v36[base+4]  (original candidate index 0..2)
};
inline constexpr int kLoanOfferCandidates = 3;

// Compact `src[3]` candidates into `outRows` (capacity >= 3). Returns the row count.
int NewLoan_CompactOffers(const LoanOffer src[kLoanOfferCandidates],
                          LoanOfferRow outRows[kLoanOfferCandidates]);

// ===========================================================================
// 0x51ab48 — VIBE_Credit_ShowLoanReleaseDialog: repayment money math.
// held       = VIBE_GameObject_CountAtLocation(*(a1+93))  (the player's cash, v5)
// repayAmt   = MultiplyByRate(GetDataPtr(amountWidget), byte_6477A1)  (slider value)
// remaining/transfer selection:
//   if (held >= repayAmt) { transfer = held - repayAmt; payer=player;  recip=lender }
//   else                  { transfer = repayAmt - held; payer=lender;  recip=player }
// (the original swaps the (v24=src,v26=dst) accounts; the magnitude is |held-repay|)
// dword_63170C (the "after repay" remaining) = held - repayAmt (signed), live each frame.
// ===========================================================================
struct LoanRepayResult {
    i32  transfer;       // |held - repayAmt|  (the opcode-15 amount)
    bool playerPays;     // true when held>=repayAmt (player is the payer)
    i32  remainingAfter; // held - repayAmt (signed; mirrors dword_63170C)
};
// gilde.exe 0x51acf1.. — repay decision from player cash + repay amount.
LoanRepayResult LoanRelease_ComputeRepay(i32 held, i32 repayAmt);

// gilde.exe 0x51acb6.. — the per-frame "remaining after repay" preview value.
//   dword_63170C = held - MultiplyByRate(GetDataPtr(widget), byte_6477A1)
inline i32 LoanRelease_RemainingPreview(i32 held, i32 sliderValue, u8 ratePct) {
    return held - Credit_MultiplyByRate(sliderValue, ratePct);
}

// ===========================================================================
// 0x51d64c — VIBE_Credit_ShowAccountInfoDialog: total wealth + tab state machine.
// wealth = VIBE_Person_ComputeTotalWealth(*result, 1).  The dialog has two tabs:
//   tab 1 (default, v2==1): building price rows  (Hud_BuildBuildingPriceRows)
//   tab 2 (v2==2)         : building choice list (Hud_BuildBuildingChoiceList)
// Clicking the "prices" child (ChildObjectId) switches to tab 1; clicking the "choice"
// child (v34) switches to tab 2. v35 ("dirty") forces a rebuild on the next frame.
// ===========================================================================
enum class AccountTab { kPriceRows = 1, kChoiceList = 2 };
struct AccountTabState {
    AccountTab tab   = AccountTab::kPriceRows; // v2
    bool       dirty = true;                   // v35
};
// gilde.exe 0x51d81e.. — apply a click event; returns true if the tab/dirty changed.
//   priceChildClicked  -> if tab!=1: tab=1, dirty=true
//   choiceChildClicked -> if tab!=2: tab=2, dirty=true
bool AccountInfo_HandleClick(AccountTabState& st,
                             bool priceChildClicked,
                             bool choiceChildClicked);

// ===========================================================================
// 0x51a868 — VIBE_Credit_ShowLoanListDialog: outstanding-loan row table init.
// The dialog pre-fills a 27-dword row table (9 rows × stride 3) with sentinels:
//   for (i=0; i<27; i+=3) { tbl[i]=-1; tbl[i+1]=-1; tbl[i+2]=0; }
// then, per frame when dirty, tears down stale rows and re-collects loans via
// VIBE_Building_CollectByCityHandle. We expose the sentinel-init + the row stride.
// ===========================================================================
inline constexpr int kLoanListRows       = 9;
inline constexpr int kLoanListRowStride  = 3;
inline constexpr int kLoanListTableDwords = kLoanListRows * kLoanListRowStride; // 27
// The original backing array v24 is 30 dwords because the init loop pre-increments
// the index by 3 and writes tbl[i], tbl[i+1] (=-1) and tbl[i+2] (=0) with i running
// 3,6,..,27 — so it touches tbl[3..29] and never tbl[0..2]. Buffer must be >=30.
inline constexpr int kLoanListBufDwords = 30;
// Initialise the row table to sentinels with the EXACT index pattern of the original
// (writes tbl[3..29]; tbl[0..2] are left untouched). `tbl` must hold >=30 dwords.
void LoanList_InitRowTable(i32* tbl /*[30]*/);

} // namespace play
} // namespace guild
