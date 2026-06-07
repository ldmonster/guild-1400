#include "world/player_finance.h"

#include "crt/rand.h"  // crt::RandNext (VIBE_Util_RandNext)

namespace guild::world {

// ===========================================================================
// gilde.exe 0x591600 — VIBE_Person_SumStoredMoney.
// The original walks the person's container with QueryFind(type=1,cat=4,good=9)
// and sums the +14 amount of each yielded money stack. We take the yielded
// amounts as a view; `recordFound` reproduces the Person_FindRecordById null
// check (-1 sentinel). Note: the original leaves the accumulator uninitialized
// before the first iteration (v4 is only written inside the loop's `v4=v5+v6`
// update), but the QueryFind always yields the canonical money stack first when
// the record exists, so an empty list cannot occur on the found path; we sum
// from 0 which matches every observable call.
// ===========================================================================
int PersonSumStoredMoney(bool recordFound, const std::vector<int>& moneyStacks) {
    if (!recordFound)
        return -1;
    int sum = 0;
    for (int amount : moneyStacks)
        sum += amount;
    return sum;
}

// ===========================================================================
// gilde.exe 0x592b50 — VIBE_Person_ComputeTopWealthList.
// Insertion into a 5-slot descending board. Index 0 holds the richest; the new
// candidate is dropped into slot 4 when it beats the current 5th place, then
// bubbled up while it exceeds the entry above it. The class-byte (<10) gate is
// re-checked inside the bubble loop exactly as the original (a redundant guard
// the candidate already passed, preserved for fidelity).
// ===========================================================================
TopWealthBoard PersonComputeTopWealthList(const std::vector<WealthSlot>& slots) {
    TopWealthBoard board;
    for (int k = 0; k < finance::kTopWealthCount; ++k) {
        board.ids[k] = 0xFFFF;
        board.wealth[k] = 0;
    }

    for (const WealthSlot& s : slots) {
        if (!s.active || s.classByte >= 10)
            continue;
        i32 wealth = s.wealth;
        if (wealth <= board.wealth[finance::kTopWealthCount - 1])
            continue;

        // Drop into the last slot.
        board.wealth[finance::kTopWealthCount - 1] = wealth;
        board.ids[finance::kTopWealthCount - 1] = s.id;

        // Bubble up from index 3 toward 0.
        int j = finance::kTopWealthCount - 2; // == 3
        while (j >= 0) {
            if (wealth <= board.wealth[j])
                break;
            if (s.classByte >= 10) // original re-tests byte_12CE912[i] < 10
                break;
            board.wealth[j + 1] = board.wealth[j];
            board.ids[j + 1] = board.ids[j];
            board.wealth[j] = wealth;
            board.ids[j] = s.id;
            --j;
        }
    }
    return board;
}

// ===========================================================================
// gilde.exe 0x592c18 — VIBE_Amt_CheckExamFeeAffordable.
//   fee = fav * 0.01f * examLevel / requiredLevel;            (single-precision)
//   threshold = RandNext() * (1/32768) + 0.15;                 (double tail)
//   return fee > threshold;
// The fee chain is computed in the x87 stack: fav (st0 double from the AI call)
// * flt_626A8C, * (float)examLevel, / (float)requiredLevel, stored as a float
// (var_C). The threshold mixes a float multiply with a double add. We reproduce
// the float store of the fee and the double comparison.
// ===========================================================================
bool AmtCheckExamFeeAffordable(double favorability, int examLevel,
                               int requiredLevel) {
    float fee = static_cast<float>(
        favorability * static_cast<double>(finance::kExamFeeFavScale) *
        static_cast<double>(examLevel) / static_cast<double>(requiredLevel));
    double threshold = static_cast<double>(crt::RandNext()) *
                           static_cast<double>(finance::kExamFeeRandNorm) +
                       finance::kExamFeeRollBias;
    return static_cast<double>(fee) > threshold;
}

// ===========================================================================
// gilde.exe 0x4c4b60 — VIBE_He_ShowFineAmount (fine amount + dispatch).
// ===========================================================================
i32 HeComputeFineAmount(int totalWealth) {
    // fine = trunc( (double)totalWealth * 0.03f )
    // The original loads totalWealth as an int, multiplies by the float rate in
    // the x87 stack (so the rate is promoted to double), and truncates via
    // VIBE_Coord_ConvertX (round-toward-zero) before fistp.
    double v = static_cast<double>(totalWealth) *
               static_cast<double>(finance::kCourtFineRate);
    return static_cast<i32>(static_cast<long long>(v));
}

namespace {
FineRenderHook g_fineHook = nullptr;
void* g_fineHookCtx = nullptr;
} // namespace

void HeSetFineRenderHook(FineRenderHook hook, void* ctx) {
    g_fineHook = hook;
    g_fineHookCtx = ctx;
}

i32 HeShowFineAmount(int totalWealth, int variant) {
    i32 fine = HeComputeFineAmount(totalWealth);

    FineMessage msg;
    msg.amount = fine;
    // The original: edx == 0 -> no-amount; the body uses (unsigned)edx, so the
    // `cmp edx,1 / jb` treats 0 as the no-amount case. Variant 1 renders the
    // amount, variant 3 pushes a literal 4 then the amount, everything else
    // renders the bare message id.
    if (variant >= 1) {
        msg.messageId = finance::kFineMessageStride * variant +
                        finance::kFineMessageBase;
        if (variant == 1) {
            msg.hasAmount = true;
        } else if (variant == 3) {
            msg.hasAmount = true;
            msg.hasExtra4 = true;
        } else {
            // variant == 2 or > 3: bare message.
            msg.messageId = finance::kFineMessageStride * variant +
                            finance::kFineMessageBase;
        }
    } else {
        // variant == 0 (and any negative, which the unsigned compare folds in):
        msg.messageId = finance::kFineMessageStride * variant +
                        finance::kFineMessageBase;
    }

    if (g_fineHook)
        g_fineHook(msg, g_fineHookCtx);
    return fine;
}

} // namespace guild::world
