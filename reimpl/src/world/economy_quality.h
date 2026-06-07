#pragma once
// Economy quality / supply-pressure / law-satisfaction scalars (gilde.exe).
//
// This module recovers the next cluster of VIBE_Economy_* functions after the
// supply/demand/price core (world/economy.cpp). They turn the per-city
// aggregate state (the economy good table, the cap/equilibrium divisor, the
// city money/goods totals, and the law table) into the normalized scalars the
// price-tick and the AI use:
//
//   VIBE_Economy_ComputeAverageQuality        0x579a38 — mean good "quality"
//       across the active object array (numerator/denominator type bytes).
//   VIBE_Economy_ComputeIndustryRatio         0x57a3c8 — clamped pressure of the
//       city GOODS total over the equilibrium divisor.
//   VIBE_Economy_ComputeResidentialRatio      0x57a474 — clamped pressure of the
//       city MONEY total over the equilibrium divisor.
//   VIBE_Economy_LoadDemandSnapshot           0x57a5dc — copy the 40-byte demand
//       snapshot block, patch slot 6 with the cap divisor, return slot 9.
//   VIBE_Economy_ComputeLawSatisfaction       0x57a520 — two-term law score from
//       law records 0 and 1.
//   VIBE_Economy_ComputeWeightedLawScore      0x57a580 — weighted sum of law
//       thresholds for laws 16..25 (10 weights, dword_577A24).
//   VIBE_Economy_ComputeInterpolatedLawScore  0x57a990 — interpolated law score
//       for laws 8..14 (7 weights, dword_577A4C).
//
// The aggregate float globals (g_capDivisor == flt_641DA8, g_cityTotalMoney ==
// flt_641FD4, g_cityTotalGoods == flt_641FD8) live in world/city.cpp and are
// reused via extern; the law table (g_lawTable) lives in world/law.cpp.
//
// The object-array quality scan and the 40-byte snapshot block are runtime sim
// state; to keep the math byte-faithful AND unit-testable without the whole sim
// they are surfaced as small POD views / settable blocks (mirroring the
// PersonEcoView pattern in world/economy.h).
#include <array>
#include <vector>

#include "guild/common/types.h"

namespace guild::world {

// ===========================================================================
// Recovered FP constants (get_bytes; see the .cpp for the raw bit patterns).
// ===========================================================================
namespace ecoq {
constexpr double kRatioFloor      = -1.0;  // dbl_62577C / dbl_625784 (ratio gate)
constexpr float  kLawSatScale     = 0.25f; // flt_62578C (law-satisfaction weight)
constexpr double kLawSatHiFactor  = 0.75;  // dbl_625794 (record-0 term factor)
constexpr double kLawSatLoFactor  = 0.25;  // dbl_62579C (record-1 term factor)

// dword_577A24 — 10 per-law weights for ComputeWeightedLawScore (laws 16..25).
constexpr std::array<float, 10> kWeightedLawWeights = {
    0.06f, 0.05f, 0.08f, 0.04f, 0.13f, 0.10f, 0.13f, 0.12f, 0.14f, 0.15f};

// dword_577A4C — 7 per-law weights for ComputeInterpolatedLawScore (laws 8..14).
constexpr std::array<float, 7> kInterpolatedLawWeights = {
    0.30f, 0.25f, 0.12f, 0.10f, 0.05f, 0.08f, 0.10f};
}  // namespace ecoq

// ===========================================================================
// 0x579a38 — VIBE_Economy_ComputeAverageQuality
// ===========================================================================
// The original walks the 169-byte object array (dword_13CE298) for object
// indices 1..27, and for each occupied object reads its type byte (+0). When the
// type is non-zero it indexes the 589-byte type table (dword_13CE294) and
// accumulates two type bytes:
//   numerator   += typeDef[type] + 583   (the need/security byte)
//   denominator += typeDef[type] + 584   (the adjacent capacity byte)
// Returns (float)((double)numerator / (double)denominator), or 0.0 when the
// denominator is 0.
//
// One occupied object's contribution to the average. `qualityNeed` is the
// typeDef+583 byte, `qualityCapacity` the typeDef+584 byte; an empty object
// (type byte == 0) contributes nothing and is simply omitted from the vector.
struct ObjectQualityView {
    u8 qualityNeed;      // typeDef[type] + 583
    u8 qualityCapacity;  // typeDef[type] + 584
};
double EconomyComputeAverageQuality(const std::vector<ObjectQualityView>& objects);

// ===========================================================================
// 0x57a3c8 — VIBE_Economy_ComputeIndustryRatio
// ===========================================================================
// ratio = (g_cityTotalGoods - g_capDivisor) / g_capDivisor   (flt_641FD8 over
//          flt_641DA8). Gate (dbl_62577C == -1.0):
//   if (ratio >= -1.0 && ratio > 1.0)  return 1.0;            // upper clamp
//   if (ratio <  -1.0)                 return -1.0;           // lower clamp
//   return (float)ratio;                                      // pass-through
double EconomyComputeIndustryRatio();

// ===========================================================================
// 0x57a474 — VIBE_Economy_ComputeResidentialRatio
// ===========================================================================
// Identical shape to the industry ratio but over the city MONEY total
// (flt_641FD4 == g_cityTotalMoney); gate dbl_625784 == -1.0.
double EconomyComputeResidentialRatio();

// ===========================================================================
// 0x57a5dc — VIBE_Economy_LoadDemandSnapshot
// ===========================================================================
// Copies the 40-byte demand snapshot block (dword_1234910, 10 floats) into
// `out[10]`, then overwrites out[6] with the cap divisor (flt_641DA8 ==
// g_capDivisor) and returns out[9]. A null `out` returns 0.0 (original guard).
// The snapshot block is runtime sim state; tests seed it via the setter below.
double EconomyLoadDemandSnapshot(float* out /* [10], may be null */);
void   EconomySetDemandSnapshot(const std::array<float, 10>& block);

// ===========================================================================
// 0x57a520 — VIBE_Economy_ComputeLawSatisfaction
// ===========================================================================
// Reads law records 0 and 1 (their +24 threshold field) and returns:
//   (4 - law0.threshold) * 0.25 * 0.75  +  (1 - law1.threshold) * 0.25
// (flt_62578C == 0.25, dbl_625794 == 0.75, dbl_62579C == 0.25.)
double EconomyComputeLawSatisfaction();

// ===========================================================================
// 0x57a580 — VIBE_Economy_ComputeWeightedLawScore
// ===========================================================================
// Sums law thresholds for laws 16..25 (10 records), each weighted by
// kWeightedLawWeights[i] (dword_577A24):
//   sum += (double)law[16+i].threshold * weight[i]
double EconomyComputeWeightedLawScore();

// ===========================================================================
// 0x57a990 — VIBE_Economy_ComputeInterpolatedLawScore
// ===========================================================================
// For laws 8..14 (7 records) computes a normalized interpolation of each
// record's value field within its [lo, hi] range and accumulates the weighted
// (1 - t) contribution (weights dword_577A4C). Per record the original reads
// three ints from the 36-byte record:
//   lo    = record + 4   (the range minimum)
//   hi    = record + 8   (the range maximum)
//   value = record + 24  (the current/threshold value)
//   t     = (value - lo) / (hi - lo)
//   sum  += weight[i] * (1.0 - t)
// The original returns sum + (last term) — i.e. the final weighted term is
// double-counted (a faithful quirk of the unrolled tail); preserved exactly.
// The +4/+8/+24 record fields are surfaced via LawRangeRecord so tests can feed
// hand-chosen ranges (the live g_lawTable carries them in its descriptor bytes).
struct LawRangeRecord {
    i32 lo;     // record + 4
    i32 hi;     // record + 8
    i32 value;  // record + 24 (threshold)
};
double EconomyComputeInterpolatedLawScore(const std::array<LawRangeRecord, 7>& laws8to14);

}  // namespace guild::world
