#pragma once
// ===========================================================================
// player_finance.{h,cpp} — player / person money + civic-fee math (gilde.exe).
//
// MODULE: the remaining player-finance rules core not covered by the existing
// world finance files (tax.cpp / amt.cpp office taxes, treasury.cpp balances,
// bank_loan.cpp loan offers, money_format.cpp display). This file adds:
//
//   * the per-person *stored-money* aggregation read by the bribe/cash dialogs
//   * the city-wide TOP-5 WEALTH ranking (the church "richest citizens" board)
//   * the master-exam FEE affordability gate (a favorability-scaled fee roll)
//   * the court-trial FINE amount (wealth * 0.03) + its message dispatch
//
// Mutations and the heavy cross-cluster leaves (favorability, total-wealth,
// formatted-text render) are routed through forward-declared hooks so the
// deterministic arithmetic stays standalone-testable. RNG is the shared crt
// ANSI LCG (crt::RandNext), so fee/fine rolls reproduce for golden vectors.
//
// Translated 1:1:
//   VIBE_Person_SumStoredMoney        0x591600
//   VIBE_Person_ComputeTopWealthList  0x592b50
//   VIBE_Amt_CheckExamFeeAffordable   0x592c18
//   VIBE_He_ShowFineAmount            0x4c4b60
//
// Recovered FP constants (get_bytes):
//   flt_626A8C = 0.01f   (exam-fee favorability scale)
//   flt_626A90 = 1/32768 (RandNext [0,32767] -> [0,1) normalize, 0x38000100)
//   dbl_626A94 = 0.15    (exam-fee roll bias)
//   flt_61E638 = 0.03f   (court-fine rate)
// ===========================================================================
#include <array>
#include <vector>

#include "guild/common/types.h"

namespace guild::world {

// ===========================================================================
// Recovered tuning constants.
// ===========================================================================
namespace finance {
constexpr float  kExamFeeFavScale = 0.0099999998f; // flt_626A8C (0x3C23D70A)
constexpr float  kExamFeeRandNorm = 3.0517578e-05f;// flt_626A90 (0x38000100 == 1/32768)
constexpr double kExamFeeRollBias = 0.15;          // dbl_626A94 (0x3FC3333333333333)
constexpr float  kCourtFineRate   = 0.029999999f;  // flt_61E638 (0x3CF5C28F)
// Top-wealth board: the original tracks the 5 highest (the loop seeds 5 slots).
constexpr int kTopWealthCount = 5;
// Fine message base id (0x115A) + the per-variant stride (3).
constexpr int kFineMessageBase = 4442; // 0x115A
constexpr int kFineMessageStride = 3;
} // namespace finance

// ===========================================================================
// gilde.exe 0x591600 — VIBE_Person_SumStoredMoney(playerSlot@<eax>).
//   rec = Person_FindRecordById(dword_12CE914[134*playerSlot]);
//   if (!rec) return -1;
//   sum = 0;
//   for (i = QueryFind(rec.container@+94, type=1, cat=4, good=9); i; i = IterNext())
//       sum += *(i+14);          // the +14 amount field of each money stack
//   return sum;
//
// We model the money stacks the iterator yields as a view: a list of the +14
// amount fields. `recordFound` is false when Person_FindRecordById returned
// null (the -1 sentinel path). Returns the summed amount, or -1 if not found.
// ===========================================================================
int PersonSumStoredMoney(bool recordFound, const std::vector<int>& moneyStacks);

// ===========================================================================
// gilde.exe 0x592b50 — VIBE_Person_ComputeTopWealthList.
//   Scans all 768 person slots (411648/536 == 768, stride 536 bytes). A slot
//   qualifies when its id (dword_12CE914) != -1 AND its class byte
//   (byte_12CE912) < 10 (excludes guards/criminals/special classes). Its wealth
//   is dword_12CEABC[slot]. The five highest are kept in two parallel arrays:
//   `ids` (word_12CE910 person id, seeded 0xFFFF) and `wealth` (seeded 0),
//   sorted descending by insertion. Returns the two filled arrays.
//
// `slots` supplies, per person slot, (id, classByte, wealth). The arithmetic is
// the byte-faithful insertion sort the original performs into 5 slots.
// ===========================================================================
struct WealthSlot {
    u16 id = 0xFFFF;     // word_12CE910 person id
    u8  classByte = 0;   // byte_12CE912 (must be < 10 to qualify)
    i32 wealth = 0;      // dword_12CEABC[slot]
    bool active = true;  // dword_12CE914 != -1
};
struct TopWealthBoard {
    std::array<u16, finance::kTopWealthCount> ids{};    // 0xFFFF == empty
    std::array<i32, finance::kTopWealthCount> wealth{}; // 0 == empty
};
TopWealthBoard PersonComputeTopWealthList(const std::vector<WealthSlot>& slots);

// ===========================================================================
// gilde.exe 0x592c18 — VIBE_Amt_CheckExamFeeAffordable.
//   fav = Ai_ComputePersonFavorability(examinee, examiner, 1);
//   fee = fav * 0.01f * (float)examLevel / (float)requiredLevel;
//   return fee > (float)RandNext() * (1/32768) + 0.15;
//
// `favorability` is the examiner->examinee favorability (the heavy AI leaf, an
// injected value here). `examLevel` (ebx) and `requiredLevel` (the inherited
// ecx divisor) come from the exam handler. The roll consumes one crt::RandNext.
// Returns true when the favorability-scaled fee clears the random threshold
// (the master accepts the exam fee), matching the original's `>` test.
// ===========================================================================
bool AmtCheckExamFeeAffordable(double favorability, int examLevel,
                               int requiredLevel);

// ===========================================================================
// gilde.exe 0x4c4b60 — VIBE_He_ShowFineAmount (court-trial fine).
//   fine = trunc( (double)Person_ComputeTotalWealth(person) * 0.03f );
//   switch (variant) {                         // variant in edx
//     0:      RenderFormattedMessage(buf, 4442);                 // no amount
//     1:      RenderFormattedMessage(buf, 4445, fine);           // 3*1+4442
//     3:      RenderFormattedMessage(buf, 4451, 4, fine);        // 3*3+4442
//     else:   RenderFormattedMessage(buf, 3*variant+4442);       // no amount
//   }
//
// The pure-math core (fine amount) is exposed; the message dispatch is recorded
// through a hook so a test observes which message id / args would render.
// ===========================================================================
i32 HeComputeFineAmount(int totalWealth);

// One formatted-message render the fine dispatcher would emit.
struct FineMessage {
    int  messageId = 0;   // 3*variant + 4442
    bool hasAmount = false;
    bool hasExtra4 = false; // the variant==3 path pushes a literal 4 before the amount
    i32  amount = 0;        // the fine (only when hasAmount)
};
using FineRenderHook = void (*)(const FineMessage& msg, void* ctx);
void HeSetFineRenderHook(FineRenderHook hook, void* ctx);

// gilde.exe 0x4c4b60 dispatch. `totalWealth` is Person_ComputeTotalWealth(person)
// (the heavy building/currency aggregation, an injected value). `variant` selects
// the message form (edx). Computes the fine and emits the matching FineMessage
// through the render hook. Returns the computed fine amount.
i32 HeShowFineAmount(int totalWealth, int variant);

} // namespace guild::world
