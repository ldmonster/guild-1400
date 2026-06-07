#include "world/economy_tick.h"

#include <cstring>

#include "util/coord.h"
#include "world/city.h"
#include "world/economy.h"
#include "world/economy_quality.h"
#include "world/law.h"
#include "world/law_types.h"

namespace guild::world {

// Recovered FP-constant bit patterns (get_bytes):
//   flt_6256C8 = 0x3F000000                 == 0.5    (price EMA alpha)
//   dbl_6256CC = 0x3FE8000000000000         == 0.75   (price first-tick seed)
//   flt_6256E4 = 0x3F000000                 == 0.5    (city EMA alpha)
//   dbl_6256EC = 0x3FEE666666666666         == 0.95   (city first-tick seed)
//   dbl_6256D4 / dbl_6256F4 = 0x3FF07AE147AE147B == 1.03
//   dbl_6256DC / dbl_6256FC = 0x3F4756CAC201756D == 0.0007122507122507123
//   dbl_62574C = 0xBFF0000000000000         == -1.0   (trend floor)
//   flt_625754 = 0x3E99999A                 == 0.30   (weighted-law weight)
//   flt_625758 = 0x3F266666                 == 0.65   (population weight)
//   dbl_625774 = 0x3FA999999999999A         == 0.05   (law-satisfaction weight)
// (See economy_tick.h ecotick:: for the constexpr forms.)

// ===========================================================================
// Module-local model of the mutated broadcast / display globals.
// ===========================================================================
namespace {
float g_smoothedPrice = 0.0f;     // flt_641DAC
float g_broadcastSpread = 0.0f;   // flt_1235234
u8    g_clock[22] = {};           // qword_13CE852 source (injected)
u8    g_clockSnapshot[22] = {};   // qword_1235262 (broadcast copy)
u8    g_stateStruct[36] = {};     // dword_1235238
PopulationStats g_pop = {};       // word/flt_1234790..1234888

// Copy the 22-byte clock block the originals move with the movsd/movsd/movsd/movsw
// sequence (qword_13CE852 + unk_13CE85A + unk_13CE85E == 8+8+6 == 22 bytes).
void SnapshotBroadcastClock() {
    std::memcpy(g_clockSnapshot, g_clock, sizeof(g_clockSnapshot));
}
}  // namespace

void SetPopulationStats(const PopulationStats& s) { g_pop = s; }

float GetSmoothedPriceLevel()           { return g_smoothedPrice; }
void  SetSmoothedPriceLevel(float v)    { g_smoothedPrice = v; }
float GetBroadcastSpread()              { return g_broadcastSpread; }

void SetBroadcastClock(const u8 clock[22]) { std::memcpy(g_clock, clock, 22); }
const u8* GetBroadcastClockSnapshot()      { return g_clockSnapshot; }
void SetBroadcastStateStruct(const u8 state[36]) { std::memcpy(g_stateStruct, state, 36); }

// ---------------------------------------------------------------------------
// 0x579098 — VIBE_Economy_TickPriceLevel
//
// (esp guard `mov eax,esp; test eax,eax; jz` is never taken — esp != 0 — so the
//  snapshot load always runs; modeled unconditionally.)
//
//   ComputeGoodsDemand(profession);            // refreshes flt_641FD4
//   snapshot = dword_1234910; snapshot[6] = flt_641DA8; s9 = snapshot[9];
//   target = ((float)s9 + 1.0) * flt_641FD4;   // var_10 (v11)
//   if (flt_641DA8 & 0x7FFFFFFF == 0) {         // first tick (==0.0 / -0.0)
//       flt_641DAC = target;
//       dword_1234910 = snapshot;
//       flt_641DA8 = target * 0.75;
//   } else {                                    // loc_57916D
//       flt_641DAC = (target - flt_641DAC) * 0.5 + flt_641DAC;
//       dword_1234910 = snapshot;
//   }
//   ComputePriceDeltas();
//   broadcast clock; flt_1235234 = (flt_641DAC - flt_641DA8)*1.03*0.000712...;
//   return (int)ConvertX(flt_641DAC);
// ---------------------------------------------------------------------------
int EconomyTickPriceLevel(const void* professionDemand) {
    using namespace ecotick;
    const auto* persons =
        static_cast<const std::vector<PersonEcoView>*>(professionDemand);
    EconomyComputeGoodsDemand(persons);  // 0x578438 (refreshes g_cityTotalMoney)

    // Load the demand snapshot exactly as the original (slot[6]=divisor, take [9]).
    float snap[10];
    double s9 = EconomyLoadDemandSnapshot(snap);  // 0x57a5dc semantics

    // var_28 (v7) = (double)s9; var_30 (v6) = 1.0 + s9; var_10 (v11) = v6 * money.
    float target = static_cast<float>((1.0 + s9) *
                                      static_cast<double>(g_cityTotalMoney));

    // `test ecx, 7FFFFFFFh` — true (jnz) when any non-sign bit is set, i.e. the
    // value is not +0.0 / -0.0.
    if ((reinterpret_cast<const u32&>(g_capDivisor) & 0x7FFFFFFFu) != 0) {
        // loc_57916D — EMA blend.
        g_smoothedPrice = (target - g_smoothedPrice) * kPriceEmaAlpha + g_smoothedPrice;
        EconomySetDemandSnapshot(
            std::array<float, 10>{snap[0], snap[1], snap[2], snap[3], snap[4],
                                  snap[5], snap[6], snap[7], snap[8], snap[9]});
    } else {
        // First tick.
        g_smoothedPrice = target;
        EconomySetDemandSnapshot(
            std::array<float, 10>{snap[0], snap[1], snap[2], snap[3], snap[4],
                                  snap[5], snap[6], snap[7], snap[8], snap[9]});
        g_capDivisor = static_cast<float>(static_cast<double>(target) * kPriceInitFactor);
    }

    EconomyComputePriceDeltas();  // 0x5787d4

    SnapshotBroadcastClock();
    g_broadcastSpread = static_cast<float>(
        static_cast<double>(g_smoothedPrice - g_capDivisor) * kBroadcastScaleA *
        kBroadcastScaleB);

    return static_cast<int>(guild::util::ConvertX(static_cast<double>(g_smoothedPrice)));
}

// ---------------------------------------------------------------------------
// 0x57919c — VIBE_City_TickStatsAndBroadcast
//
//   AggregateDistrictStats(v5);                // 13 floats -> passed in here
//   dword_1234910 = v5 (first 10 floats);      // qmemcpy 0x28
//   if (flt_641DA8 & 0x7FFFFFFF != 0) {         // already seeded
//       flt_641DAC = (snapshot[5] - flt_641DAC) * 0.5 + flt_641DAC;
//       snapshot[5] = flt_641DAC;
//   } else {
//       flt_641DAC = snapshot[5];               // raw bits
//       flt_641DA8 = snapshot[5] * 0.95;
//   }
//   flt_1234928 = flt_641DA8;
//   build broadcast body v1[0..10]:
//     v1[0..4] = snapshot[0..4]; v1[5] = flt_641DAC; v1[6] = flt_641DA8;
//     snapshot[5] = flt_641DAC; broadcast clock;
//     v1[7]=snapshot[7]; v1[8]=snapshot[8];
//     flt_1235234 = (flt_641DAC - flt_641DA8)*1.03*0.000712...;
//     v1[9]=snapshot[9]; v1[10]=flt_1235234;
//   return RequestBuildOp65(v1, 0);
// The 44-byte body (11 dwords) is emitted to `out`; the network send is the
// caller's responsibility (the shim boundary).
// ---------------------------------------------------------------------------
int CityTickStatsAndBroadcast(const float stats[13], u8 out[44]) {
    using namespace ecotick;
    if (out == nullptr)
        return 0;

    // dword_1234910 = first 10 floats of the aggregate (qmemcpy 0x28).
    std::array<float, 10> snap;
    for (int i = 0; i < 10; ++i)
        snap[i] = stats[i];

    if ((reinterpret_cast<const u32&>(g_capDivisor) & 0x7FFFFFFFu) != 0) {
        // EMA blend toward slot[5] (dword_1234924).
        g_smoothedPrice = (snap[5] - g_smoothedPrice) * kCityEmaAlpha + g_smoothedPrice;
        snap[5] = g_smoothedPrice;
    } else {
        // First tick — copy raw bits, seed the divisor at *0.95.
        g_smoothedPrice = snap[5];
        g_capDivisor = static_cast<float>(static_cast<double>(snap[5]) * kCityInitFactor);
    }
    // flt_1234928 = flt_641DA8 (slot[6] of the persisted snapshot region).

    // Assemble the 11-dword broadcast body.
    float body[11];
    body[0] = snap[0];
    body[1] = snap[1];
    body[2] = snap[2];
    body[3] = snap[3];
    body[4] = snap[4];
    body[5] = g_smoothedPrice;   // flt_641DAC
    body[6] = g_capDivisor;      // flt_641DA8
    snap[5] = g_smoothedPrice;   // *(float*)&dword_1234924 = flt_641DAC

    SnapshotBroadcastClock();

    body[7] = snap[7];
    body[8] = snap[8];
    g_broadcastSpread = static_cast<float>(
        static_cast<double>(g_smoothedPrice - g_capDivisor) * kBroadcastScaleA *
        kBroadcastScaleB);
    body[9] = snap[9];
    body[10] = g_broadcastSpread;  // flt_1235234

    // Persist the snapshot back (the original keeps dword_1234910.. updated).
    EconomySetDemandSnapshot(snap);

    std::memcpy(out, body, sizeof(body));  // 11 dwords == 44 bytes
    return static_cast<int>(sizeof(body));
}

// ---------------------------------------------------------------------------
// 0x57a008 — VIBE_Economy_ComputePopulationTrend
//
//   div = flt_641DA8;
//   growth = (word_1234880*flt_1234888 + word_1234884 - div) / div;   // v15
//   t1 = max(growth, -1.0);                                           // v11 / v21
//   if (word_1234884 > div && t1 < 0.0) t1 = 0.0;
//   births = (word_1234794 >= div) ? 0.0                              // v19
//          : max(-(word_1234790*flt_1234798)/div, -1.0);
//   deaths = (word_1234854 >= div) ? 0.0                              // v20
//          : max(-(word_1234850*flt_1234858)/div, -1.0);
//   sum = t1 + births + deaths;                                       // v2 / v22
//   trend = (sum < 1.0 && sum <= -1.0) ? -1.0 : min(sum, 1.0);        // v10 / v23
//   score = WeightedLawScore()*0.30 + trend*0.65;                     // v16
//   return LawSatisfaction()*0.05 + score;
// ---------------------------------------------------------------------------
double EconomyComputePopulationTrend() {
    using namespace ecotick;
    const double div = static_cast<double>(g_capDivisor);  // flt_641DA8

    // Growth term (v15) and its lower clamp (v11/v21).
    double growth = (static_cast<double>(g_pop.prevCount) *
                         static_cast<double>(g_pop.prevScale) +
                     static_cast<double>(g_pop.curCount) - div) /
                    div;
    double t1 = (growth > kTrendFloor) ? growth : -1.0;
    if (static_cast<double>(g_pop.curCount) > div && t1 < 0.0)
        t1 = 0.0;

    // Births term (v19): negative pressure, gated by birthsCmp.
    double births;
    if (static_cast<double>(g_pop.birthsCmp) >= div) {
        births = 0.0;
    } else {
        double v8 = -(static_cast<double>(g_pop.birthsCount) *
                      static_cast<double>(g_pop.birthsScale)) /
                    div;
        births = (v8 <= kTrendFloor) ? -1.0 : v8;
    }

    // Deaths term (v20): negative pressure, gated by deathsCmp.
    double deaths;
    if (static_cast<double>(g_pop.deathsCmp) >= div) {
        deaths = 0.0;
    } else {
        double v9 = -(static_cast<double>(g_pop.deathsCount) *
                      static_cast<double>(g_pop.deathsScale)) /
                    div;
        deaths = (v9 <= kTrendFloor) ? -1.0 : v9;
    }

    // sum (v2/v22) is held as a float in the original; the upper clamp keeps the
    // narrowed value.
    float sum = static_cast<float>(t1 + births + deaths);
    double trend;
    if (sum < 1.0f && static_cast<double>(sum) <= kTrendFloor) {
        trend = -1.0;
    } else {
        trend = (sum >= 1.0f) ? 1.0 : static_cast<double>(sum);
    }
    // v23 (trend) is stored as a float before the weighted sum.
    float trendF = static_cast<float>(trend);

    double score = EconomyComputeWeightedLawScore() * static_cast<double>(kLawWeight) +
                   static_cast<double>(trendF) * static_cast<double>(kPopWeight);

    // The tail re-reads law records 0 & 1 exactly as EconomyComputeLawSatisfaction.
    return EconomyComputeLawSatisfaction() * kLawSatWeight + score;
}

// ---------------------------------------------------------------------------
// 0x57aa30 — VIBE_Economy_FillLawRangeRatios
//
//   ecx = 8; out = eax + 32;                    // out[8]
//   do {
//       GetRecord(ecx, rec);                    // 36 bytes
//       lo = rec[+0]; hi = rec[+4]; value = rec[+20];
//       *out++ = (float)((value - lo) / (hi - lo));   // FP divide in double
//   } while (++ecx <= 14);
// (Note the +0/+4/+20 triple — distinct from InterpolatedLawScore's +4/+8/+24.)
// ---------------------------------------------------------------------------
void EconomyFillLawRangeRatios(const std::array<LawRatioTriple, 7>& laws8to14,
                               float* out) {
    for (int i = 0; i < 7; ++i) {  // ecx 8..14 -> out[8..14]
        const LawRatioTriple& r = laws8to14[i];
        out[8 + i] = static_cast<float>(static_cast<double>(r.value - r.lo) /
                                        static_cast<double>(r.hi - r.lo));
    }
}

void EconomyFillLawRangeRatiosFromTable(float* out) {
    std::array<LawRatioTriple, 7> rows{};
    for (int i = 0; i < 7; ++i) {
        u8 raw[kLawStride];
        GesetzGetRecord(static_cast<u8>(8 + i), reinterpret_cast<LawRecord*>(raw));
        i32 lo, hi, value;
        std::memcpy(&lo, raw + 0, 4);
        std::memcpy(&hi, raw + 4, 4);
        std::memcpy(&value, raw + 20, 4);
        rows[i] = LawRatioTriple{lo, hi, value};
    }
    EconomyFillLawRangeRatios(rows, out);
}

// ---------------------------------------------------------------------------
// 0x579448 — VIBE_City_CopyStateStruct
//   qmemcpy(result, &dword_1235238, 0x24u); return result;
// ---------------------------------------------------------------------------
u8* CityCopyStateStruct(u8 result[36]) {
    std::memcpy(result, g_stateStruct, 36);  // 0x24
    return result;
}

}  // namespace guild::world
