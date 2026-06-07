#include "world/economy_quality.h"

#include "world/city.h"
#include "world/law.h"

namespace guild::world {

// Recovered FP-constant bit patterns (get_bytes):
//   dbl_62577C / dbl_625784 = 0xBFF0000000000000          == -1.0  (ratio gates)
//   flt_62578C              = 0x3E800000                  ==  0.25 (law-sat scale)
//   dbl_625794              = 0x3FE8000000000000          ==  0.75 (law-sat hi)
//   dbl_62579C              = 0x3FD0000000000000          ==  0.25 (law-sat lo)
//   dword_577A24[10]        = 0.06 0.05 0.08 0.04 0.13 0.10 0.13 0.12 0.14 0.15
//   dword_577A4C[7]         = 0.30 0.25 0.12 0.10 0.05 0.08 0.10
// (See economy_quality.h ecoq:: for the constexpr forms.)

// ---------------------------------------------------------------------------
// 0x579a38 — VIBE_Economy_ComputeAverageQuality
//
//   v1 = 0; v2 = 0;                       // numerator / denominator
//   for (v0 = 169; v0 != 4732; v0 += 169) // object indices 1..27
//       v3 = object[v0];                  // type byte (+0)
//       if (v3) {
//           v4 = 589*v3 + typeTableBase;
//           v1 += *(u8*)(v4 + 583);
//           v2 += *(u8*)(v4 + 584);
//       }
//   if (!v2) return 0.0;
//   return (float)((double)v1 / (double)v2);
//
// The vector carries exactly the occupied objects (type byte != 0); each entry
// is the (typeDef+583, typeDef+584) pair the original reads. The float cast on
// the result mirrors the original's COERCE to a 32-bit float before return.
// ---------------------------------------------------------------------------
double EconomyComputeAverageQuality(const std::vector<ObjectQualityView>& objects) {
    int numerator = 0;    // v1
    int denominator = 0;  // v2
    for (const ObjectQualityView& o : objects) {
        numerator   += o.qualityNeed;
        denominator += o.qualityCapacity;
    }
    if (denominator == 0)
        return 0.0;
    return static_cast<float>(static_cast<double>(numerator) /
                              static_cast<double>(denominator));
}

// ---------------------------------------------------------------------------
// Shared clamp shape for the industry / residential ratios.
//   ratio = (total - divisor) / divisor
//   if (ratio >= floor && ratio > 1.0) return 1.0;   // upper clamp
//   if (ratio <  floor)               return floor;  // lower clamp (== -1.0)
//   return (float)ratio;
// The original recomputes `(total - divisor) * (1/divisor)` twice; both reads
// see the same globals, so a single computation is behavior-identical.
// ---------------------------------------------------------------------------
static double ClampRatio(float total, float divisor) {
    using namespace ecoq;
    double ratio = static_cast<double>(total - divisor) *
                   static_cast<double>(1.0f / divisor);
    if (ratio >= kRatioFloor && ratio > 1.0)
        return 1.0;
    if (ratio < kRatioFloor)
        return -1.0;
    return static_cast<float>(ratio);
}

// 0x57a3c8 — VIBE_Economy_ComputeIndustryRatio (city GOODS total over divisor).
double EconomyComputeIndustryRatio() {
    return ClampRatio(g_cityTotalGoods, g_capDivisor);  // flt_641FD8 / flt_641DA8
}

// 0x57a474 — VIBE_Economy_ComputeResidentialRatio (city MONEY total over divisor).
double EconomyComputeResidentialRatio() {
    return ClampRatio(g_cityTotalMoney, g_capDivisor);  // flt_641FD4 / flt_641DA8
}

// ---------------------------------------------------------------------------
// 0x57a5dc — VIBE_Economy_LoadDemandSnapshot
//
//   if (!a1) return 0.0;
//   qmemcpy(a1, &dword_1234910, 0x28u);   // 10 floats
//   a1[6] = flt_641DA8;                    // patch slot 6 with the cap divisor
//   return a1[9];
// The snapshot block is runtime sim state; modeled as a settable 10-float block.
// ---------------------------------------------------------------------------
namespace {
std::array<float, 10> g_demandSnapshot = {};  // dword_1234910 (40 bytes)
}  // namespace

void EconomySetDemandSnapshot(const std::array<float, 10>& block) {
    g_demandSnapshot = block;
}

double EconomyLoadDemandSnapshot(float* out) {
    if (!out)
        return 0.0;
    for (int i = 0; i < 10; ++i)
        out[i] = g_demandSnapshot[i];
    out[6] = g_capDivisor;  // flt_641DA8
    return out[9];
}

// ---------------------------------------------------------------------------
// 0x57a520 — VIBE_Economy_ComputeLawSatisfaction
//
//   VIBE_Gesetz_GetRecord(0, rec0);   // rec0.threshold == +24
//   VIBE_Gesetz_GetRecord(1, rec1);
//   return (double)(4 - rec0.threshold) * flt_62578C * dbl_625794
//        + (double)(1 - rec1.threshold) * dbl_62579C;
// flt_62578C == 0.25, dbl_625794 == 0.75, dbl_62579C == 0.25.
// ---------------------------------------------------------------------------
double EconomyComputeLawSatisfaction() {
    using namespace ecoq;
    LawRecord r0;
    LawRecord r1;
    GesetzGetRecord(0, &r0);
    GesetzGetRecord(1, &r1);
    return static_cast<double>(4 - r0.threshold) * kLawSatScale * kLawSatHiFactor +
           static_cast<double>(1 - r1.threshold) * kLawSatLoFactor;
}

// ---------------------------------------------------------------------------
// 0x57a580 — VIBE_Economy_ComputeWeightedLawScore
//
//   sum = 0.0;
//   for (v2 = 16, i = 0; v2 < 26; ++v2, ++i)
//       VIBE_Gesetz_GetRecord(v2, rec);          // rec.threshold == +24
//       sum += (double)rec.threshold * weight[i]; // weight == dword_577A24[i]
//   return sum;
// ---------------------------------------------------------------------------
double EconomyComputeWeightedLawScore() {
    using namespace ecoq;
    // v10 (the accumulator) is a float in the original; each step computes the
    // product in double (st7) and narrows the running sum back to float.
    float sum = 0.0f;
    for (int i = 0; i < 10; ++i) {
        LawRecord rec;
        GesetzGetRecord(static_cast<u8>(16 + i), &rec);
        double prod = static_cast<double>(rec.threshold) * kWeightedLawWeights[i];
        sum = static_cast<float>(prod + sum);
    }
    return sum;
}

// ---------------------------------------------------------------------------
// 0x57a990 — VIBE_Economy_ComputeInterpolatedLawScore
//
//   sum = 0.0; term = 0.0;
//   for (id = 8, i = 0; id <= 14; ++id, ++i)
//       VIBE_Gesetz_GetRecord(id, rec);
//       lo = rec+4; hi = rec+8; value = rec+24;
//       t    = (double)(value - lo) / (double)(hi - lo);
//       term = weight[i] * (1.0 - t);       // weight == dword_577A4C[i]
//       sum += term;
//   return sum + term;                      // last term double-counted (quirk)
// ---------------------------------------------------------------------------
double EconomyComputeInterpolatedLawScore(const std::array<LawRangeRecord, 7>& laws8to14) {
    using namespace ecoq;
    // v11 (sum) and v12 (term) are both floats in the original; the final return
    // promotes to double.
    float sum = 0.0f;
    float term = 0.0f;
    for (int i = 0; i < 7; ++i) {
        const LawRangeRecord& rec = laws8to14[i];
        // v12 (a float) = (value - lo) / (hi - lo), computed in double then
        // narrowed to float.
        float t = static_cast<float>(static_cast<double>(rec.value - rec.lo) /
                                     static_cast<double>(rec.hi - rec.lo));
        // v3 (a float) = weight * (1.0 - t): the (1.0 - t) is double, the
        // product is narrowed back to a float term.
        term = static_cast<float>(static_cast<double>(kInterpolatedLawWeights[i]) *
                                  (1.0 - static_cast<double>(t)));
        sum = sum + term;
    }
    return static_cast<double>(sum) +
           static_cast<double>(term);  // last term double-counted (unrolled tail)
}

}  // namespace guild::world
