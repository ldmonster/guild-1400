#pragma once
// Statistics — the full economy-report decision logic (gilde.exe
// VIBE_Statistics_BuildEconomyReport 0x579ad0), building on the float accumulation
// already in world/statistics.h.
//
// Once every fourth round (round >= 8 && round % 4 == 0) the report:
//   1. reduces the per-good accumulator block into four category totals
//      (goods / services / law / luxury), each scaled by 0.25 — see
//      world/statistics.h StatisticsAccumulateCategoryTotals.
//   2. picks an up/down "trend" text id per category from a 0.15 threshold (the law
//      category instead keys on < 0 since its score is signed).
//   3. finds the WEAKEST category (the running argmin over the four totals) and,
//      when its value clears the broadcast gates, renders a "the city's <weak
//      sector> is struggling" line plus a positive/negative summary depending on
//      the luxury total.
//
// The GUI render + He broadcast are the engine's; the recoverable rules are the
// trend-id selection, the argmin (which category is weakest), and the broadcast
// gate thresholds. All four totals + the law score are inputs.
#include "guild/common/types.h"
#include "world/statistics.h"   // StatisticsAccumulateCategoryTotals, kStatScale

namespace guild::world {

// ===========================================================================
// Recovered double thresholds (get_bytes; see comments).
// ===========================================================================
constexpr double kReportTrendThreshold = 0.15;  // dbl_625714 (up/down per category)
constexpr double kReportWeakGate       = 0.20;  // dbl_62571C (weak-sector line gate)
constexpr double kReportTier2Gate      = 0.30;  // dbl_625724 (mid severity tier)
constexpr double kReportTier3Gate      = 0.50;  // dbl_62572C (high severity tier)
constexpr double kReportLuxuryHigh     = 0.55;  // dbl_625734 (luxury "booming" gate)
constexpr double kReportLuxuryLow      = 0.45;  // dbl_62573C (luxury "slump" gate)

// ===========================================================================
// The four category totals + the law score (the report's working set).
// ===========================================================================
// Order matches the original locals: total0 = goods (v33, column 0), total1 =
// services (v34, column 1), total2 = production (v35, column 2), lawScore (v32,
// the signed VIBE_Economy law-score), luxury (v26, column 3).
struct EconomyReportInput {
    float total0 = 0.0f;   // goods    (column 0; v33)
    float total1 = 0.0f;   // services (column 1; v34)
    float total2 = 0.0f;   // trade/production (column 2; v35)
    float lawScore = 0.0f; // signed law score (v32)
    float luxury = 0.0f;   // luxury   (column 3; v26)
};

// Builds the four totals + luxury from a raw 20-float accumulator block via
// StatisticsCategoryTotal (k=0..3). The law score is supplied by the caller (the
// original calls VIBE_Economy_ComputeInterpolatedLawScore which is law-owned).
EconomyReportInput EconomyReportFromAccum(const float* accum, float lawScore);

// gilde.exe 0x579af5 — the report cadence gate: round >= 8 && (round % 4) == 0.
bool EconomyReportShouldRun(i32 round);

// ===========================================================================
// Trend ids (per category up/down).
// ===========================================================================
// gilde.exe 0x579bac.. — each non-law category compares its total against the 0.15
// trend threshold (> -> "up" id, else "down" id); the law category keys on < 0.
//   total2 (trade):    > 0.15 ? 6178 : 6174
//   total0 (goods):    > 0.15 ? 6179 : 6175
//   lawScore:          < 0    ? 6180 : 6176
//   total1 (services): > 0.15 ? 6181 : 6177
struct EconomyReportTrends {
    int trade;     // v1
    int goods;     // v36
    int law;       // v4
    int services;  // v5
};
EconomyReportTrends EconomyReportTrendIds(const EconomyReportInput& in);

// ===========================================================================
// Weakest category (argmin) + its value.
// ===========================================================================
// gilde.exe 0x579c1a.. — the running min/select over (total2, total0, lawScore,
// total1) in that comparison order. `value` is the true minimum of the four totals
// (v24); `index` (v12, 0..3) is the original's category code that selects which
// "<sector> struggling" string is rendered. NOTE: the index is NOT a strict argmin
// — it is computed by the binary's exact short-circuit comparison chain (e.g. a tie
// or a late-chain comparison can leave it at 3), reproduced 1:1 in the .cpp.
struct EconomyReportWeak {
    int   index;   // v12 (0..3) — the original's category code (see note above)
    float value;   // v24 (the minimum total)
};
EconomyReportWeak EconomyReportWeakest(const EconomyReportInput& in);

// gilde.exe 0x579d03 — the weak-sector line fires only when the weakest value
// exceeds the 0.20 gate (dbl_62571C). Returns true when the "<sector> struggling"
// line is emitted.
bool EconomyReportEmitsWeakLine(const EconomyReportWeak& weak);

// ===========================================================================
// Severity tier of the weak-sector line  (gilde.exe 0x579d28..).
// ===========================================================================
// Within the weak-line branch the string set is chosen by the weak value's tier:
//   value <  0.30          -> tier 0 (mild)
//   0.30 <= value < 0.50   -> tier 1 (moderate)
//   value >= 0.50          -> tier 2 (severe)
enum class EconomyReportSeverity { kMild = 0, kModerate = 1, kSevere = 2 };
EconomyReportSeverity EconomyReportWeakSeverity(float weakValue);

// ===========================================================================
// Luxury summary branch  (gilde.exe 0x579d5b..).
// ===========================================================================
// The trailing summary keys on the luxury total:
//   luxury >  0.55  -> "booming" summary (ids 6170/6171)
//   luxury <  0.45  -> "slump"   summary (ids 6172/6173)
//   else            -> no summary (only the weak line, if any)
enum class EconomyReportLuxury { kBooming, kSlump, kNeutral };
EconomyReportLuxury EconomyReportLuxuryBranch(float luxury);

} // namespace guild::world
