// hud_recon_credit.cpp — gilde.exe Geldleihe (credit/loan) dialog pure logic.
//   0x519dbc NewLoan offer compaction · 0x51ab48 LoanRelease repay math ·
//   0x51d64c AccountInfo tab state · 0x51a868 LoanList row-table init.
#include "hud_recon_credit.h"

namespace guild {
namespace play {

// ---------------------------------------------------------------------------
// 0x519dbc — new-loan offer compaction.
// The original packs into a flat v36[20] dword buffer with row stride 5, where the
// store index v5 is PRE-incremented by 5 before the first write (v5 starts at 0), so
// row k (0-based) occupies v36[5*(k+1) .. 5*(k+1)+4]; the click loop later reads
// v36[5 + 5*k] for the amount. We materialise the rows in order; the +3 (childId)
// slot is created later by the widget builder, so it inits to 0 here (the buffer is
// not zeroed by the original for that slot until the row widget is made — we present
// the logical row, childObjectId left 0).
// ---------------------------------------------------------------------------
int NewLoan_CompactOffers(const LoanOffer src[kLoanOfferCandidates],
                          LoanOfferRow outRows[kLoanOfferCandidates]) {
    int rowCount = 0;
    for (int i = 0; i < kLoanOfferCandidates; ++i) { // v3 = i, v4 = 3*i
        i32 amount = src[i].amount; // v37[v4]
        if (amount > 0) {           // v6 > 0
            LoanOfferRow& row = outRows[rowCount];
            row.amount        = amount;       // v36[v5]
            row.b             = src[i].b;     // v36[v5+1]
            row.c             = src[i].c;     // v36[v5+2]
            row.childObjectId = 0;            // v36[v5+3] (set when widget created)
            row.index         = i;            // v36[v5+4] = v3
            ++rowCount;
        }
    }
    return rowCount;
}

// ---------------------------------------------------------------------------
// 0x51ab48 — loan repayment money math.
// ---------------------------------------------------------------------------
LoanRepayResult LoanRelease_ComputeRepay(i32 held, i32 repayAmt) {
    LoanRepayResult r{};
    r.remainingAfter = held - repayAmt; // dword_63170C preview
    if (held >= repayAmt) {
        // v25 = &v5[-v21] = held - repayAmt ; player is payer (v24=player acct)
        r.transfer   = held - repayAmt;
        r.playerPays = true;
    } else {
        // v25 = v22 - (DWORD)v5 = repayAmt - held ; lender is payer (v24=lender)
        r.transfer   = repayAmt - held;
        r.playerPays = false;
    }
    return r;
}

// ---------------------------------------------------------------------------
// 0x51d64c — account-info tab state machine.
// ---------------------------------------------------------------------------
bool AccountInfo_HandleClick(AccountTabState& st,
                             bool priceChildClicked,
                             bool choiceChildClicked) {
    if (priceChildClicked) {
        // if ( dword_62D22C == ChildObjectId ) { if (v2 != 1) { v2=1; v35=1; } }
        if (st.tab != AccountTab::kPriceRows) {
            st.tab   = AccountTab::kPriceRows;
            st.dirty = true;
            return true;
        }
    } else if (choiceChildClicked) {
        // else if ( dword_62D22C == v34 && v2 != 2 ) { v2=2; v35=1; }
        if (st.tab != AccountTab::kChoiceList) {
            st.tab   = AccountTab::kChoiceList;
            st.dirty = true;
            return true;
        }
    }
    return false;
}

// ---------------------------------------------------------------------------
// 0x51a868 — loan-list row-table sentinel init.
//   for ( i = 0; i != 27; v24[i+2] = 0 ) { i += 3; v24[i] = -1; v24[i+1] = -1; }
// Note the decompiler's odd loop form: i is pre-incremented by 3, then v24[i] and
// v24[i+1] are set to -1, and the loop-tail sets v24[i+2]=0 (with i already advanced).
// The net effect over the run is that every 3-dword row {-1,-1,0} is written for the
// 9 rows; the first row lands at indices 3..5 due to the pre-increment, and the last
// row's trailing 0 lands at index 27+? — but the table is sized 30 dwords (v24[30])
// to absorb it. We reproduce the exact net writes the original makes.
// ---------------------------------------------------------------------------
void LoanList_InitRowTable(i32* tbl /*[30]*/) {
    // Byte-exact to the decompiled loop:
    //   for ( i = 0; i != 27; v24[i+2] = 0 ) { i += 3; v24[i] = -1; v24[i+1] = -1; }
    // i runs 3,6,..,27 for the body's -1 writes; the loop-tail writes v24[i+2]=0 for
    // each i value the loop body produced (3..27 -> tbl[5..29]). The condition `i!=27`
    // is checked AFTER the body, so the body runs for i=3..27 (9 iterations).
    int i = 0;
    do {
        i += kLoanListRowStride;     // i = 3,6,...,27
        tbl[i] = -1;                 // v24[i]
        tbl[i + 1] = -1;             // v24[i+1]
        tbl[i + 2] = 0;              // v24[i+2]  (loop-tail; folded into the body)
    } while (i != kLoanListTableDwords);
}

} // namespace play
} // namespace guild
