#pragma once
// Per-round / per-day economy + city simulation tick (gilde.exe).
//
// This module recovers the cluster of VIBE_ functions that advance the city
// economy ONE step: the price-level EMA tick, the city stats day-step (its
// sibling that aggregates the districts and broadcasts the result), the
// population-trend score the AI consumes, the law-range-ratio fill, and the
// broadcast state-struct copy.
//
// Translated functions:
//   VIBE_Economy_TickPriceLevel        0x579098 — per-round price-level EMA
//   VIBE_City_TickStatsAndBroadcast    0x57919c — city day-step (sibling EMA)
//   VIBE_Economy_ComputePopulationTrend 0x57a008 — population growth/trend score
//   VIBE_Economy_FillLawRangeRatios    0x57aa30 — laws 8..14 range-ratio fill
//   VIBE_City_CopyStateStruct          0x579448 — copy the 36-byte broadcast state
//
// REUSE (extern / no redefinition):
//   * world/economy.cpp        — EconomyComputeGoodsDemand, EconomyComputePriceDeltas
//   * world/economy_quality.cpp— EconomyComputeWeightedLawScore, EconomyComputeLawSatisfaction
//   * world/city.cpp           — g_capDivisor (flt_641DA8), g_cityTotalMoney
//                                (flt_641FD4 used here as the demand multiplier)
//   * world/law.cpp            — GesetzGetRecord, g_lawTable
//   * util/coord.cpp           — ConvertX (VIBE_Coord_ConvertX, x87 round-toward-zero)
//
// DISPLAY / BROADCAST GLOBALS. The originals mutate a band of runtime state used
// by the network-broadcast command and the on-screen report:
//   flt_641DAC          — the smoothed price-level (EMA accumulator)
//   dword_1234910[10]   — the demand snapshot block (== world/economy_quality's
//                         g_demandSnapshot; reused via the accessors below)
//   flt_1235234         — broadcast: (price - divisor) * 1.03 * 0.0007122507...
//   qword_1235262 (+22B)— broadcast: copy of the game clock (qword_13CE852)
//   dword_1235238[9]    — the 36-byte broadcast state struct CopyStateStruct reads
// These are sim/UI state, so the state-mutation is translated faithfully and the
// blocks are surfaced as settable module-local storage (mirroring the
// PersonEcoView / g_demandSnapshot pattern already used in this layer). The
// game clock is injected via SetBroadcastClock (the original reads qword_13CE852,
// owned by the sim time module).
#include <array>
#include <cstddef>
#include <vector>

#include "guild/common/types.h"

namespace guild::world {

// ===========================================================================
// Recovered FP constants (get_bytes; raw bit patterns in the .cpp).
// ===========================================================================
namespace ecotick {
// --- VIBE_Economy_TickPriceLevel (0x579098) ---
constexpr float  kPriceEmaAlpha   = 0.5f;  // flt_6256C8 — EMA blend factor
constexpr double kPriceInitFactor = 0.75;  // dbl_6256CC — first-tick divisor seed
// --- VIBE_City_TickStatsAndBroadcast (0x57919c) ---
constexpr float  kCityEmaAlpha    = 0.5f;  // flt_6256E4 — EMA blend factor
constexpr double kCityInitFactor  = 0.95;  // dbl_6256EC — first-tick divisor seed
// --- shared broadcast scale for flt_1235234 ---
constexpr double kBroadcastScaleA = 1.03;                  // dbl_6256D4 / dbl_6256F4
constexpr double kBroadcastScaleB = 0.0007122507122507123; // dbl_6256DC / dbl_6256FC
// --- VIBE_Economy_ComputePopulationTrend (0x57a008) ---
constexpr double kTrendFloor      = -1.0;  // dbl_62574C — lower clamp gate
constexpr float  kLawWeight       = 0.30f; // flt_625754 — weighted-law-score weight
constexpr float  kPopWeight       = 0.65f; // flt_625758 — population-trend weight
constexpr double kLawSatWeight    = 0.05;  // dbl_625774 — law-satisfaction weight
}  // namespace ecotick

// ===========================================================================
// Runtime city-stat inputs for ComputePopulationTrend (gilde.exe word/flt_1234xxx).
// ===========================================================================
// The original reads three (count16, scaled16, scale-float) demographic triples
// that VIBE_City_AggregateDistrictStats fills. All are zero in the static image,
// so they are surfaced as a settable POD (mirroring g_demandSnapshot). Field
// names map one-to-one to the source globals.
struct PopulationStats {
    u16   prevCount;   // word_1234880 — prior population count
    float prevScale;   // flt_1234888  — prior-count scale factor
    u16   curCount;    // word_1234884 — current population count
    u16   birthsCount; // word_1234790 — births / influx count
    float birthsScale; // flt_1234798  — births scale factor
    u16   birthsCmp;   // word_1234794 — births gate count
    u16   deathsCount; // word_1234850 — deaths / outflux count
    float deathsScale; // flt_1234858  — deaths scale factor
    u16   deathsCmp;   // word_1234854 — deaths gate count
};
void SetPopulationStats(const PopulationStats& s);

// ===========================================================================
// Broadcast / display state (module-local model of the mutated globals).
// ===========================================================================
// flt_641DAC — the smoothed price level. Settable so a tick can resume from a
// prior state; readable so tests can assert the EMA result.
float GetSmoothedPriceLevel();                 // flt_641DAC
void  SetSmoothedPriceLevel(float v);

// flt_1235234 — the broadcast price-spread word written by the two ticks.
float GetBroadcastSpread();                    // flt_1235234

// qword_13CE852 — the game clock the ticks snapshot into the broadcast block
// (qword_1235262, 22 bytes). Injected (owned by the sim time module).
void SetBroadcastClock(const u8 clock[22]);
const u8* GetBroadcastClockSnapshot();         // qword_1235262 (22 bytes)

// dword_1235238 — the 36-byte broadcast state struct CopyStateStruct reads.
void SetBroadcastStateStruct(const u8 state[36]);

// ===========================================================================
// 0x579098 — VIBE_Economy_TickPriceLevel  (__usercall, eax=(profession@esi))
// ===========================================================================
// Advances the price level for one round:
//   1. EconomyComputeGoodsDemand(profession)  -> refreshes g_cityTotalMoney.
//   2. snapshot = g_demandSnapshot; snapshot[6] = g_capDivisor; s9 = snapshot[9].
//   3. target = (1.0 + (float)s9) * g_cityTotalMoney.
//   4. if (g_capDivisor == 0):                        // first tick
//         flt_641DAC = target; g_demandSnapshot = snapshot;
//         g_capDivisor = target * 0.75;
//      else:                                          // EMA blend
//         flt_641DAC = (target - flt_641DAC) * 0.5 + flt_641DAC;
//         g_demandSnapshot = snapshot;
//   5. EconomyComputePriceDeltas();
//   6. broadcast clock; flt_1235234 = (flt_641DAC - g_capDivisor)*1.03*0.000712...
//   7. return (int)trunc(flt_641DAC).
// `professionDemand` carries the per-good PersonEcoView lists for the demand
// recompute (same parameter EconomyComputeGoodsDemand takes); may be null.
int EconomyTickPriceLevel(const void* professionDemand /* const std::vector<PersonEcoView>* */);

// ===========================================================================
// 0x57919c — VIBE_City_TickStatsAndBroadcast
// ===========================================================================
// The city day-step sibling of the price tick. The original first calls
// VIBE_City_AggregateDistrictStats (the 13-float district reduction, owned by the
// city-stats agent) to fill the demand snapshot; here that aggregate is passed in
// as the freshly-computed 13-float block (`stats`). The remainder — the EMA over
// flt_641DAC with alpha 0.5 / seed 0.95, the broadcast-block assembly, and the
// VIBE_Command_RequestBuildOp65 broadcast — is translated faithfully; the network
// command is surfaced as an out-parameter the caller forwards to the shim.
//   stats : the 13 floats VIBE_City_AggregateDistrictStats produced.
//   out   : the 44-byte (11-dword) broadcast command body the original builds.
// Returns the byte length written to `out` (44), or 0 if out is null.
int CityTickStatsAndBroadcast(const float stats[13], u8 out[44]);

// ===========================================================================
// 0x57a008 — VIBE_Economy_ComputePopulationTrend  (__fastcall(unused, unused))
// ===========================================================================
// Combines the weighted-law score, the clamped population-growth trend, and the
// law-satisfaction term into one scalar the AI consumes:
//   trend  = clamp01_floor( growth + births + deaths )
//   score  = EconomyComputeWeightedLawScore()*0.30 + trend*0.65
//   return EconomyComputeLawSatisfaction()*0.05 + score
// where the three demographic terms come from PopulationStats / g_capDivisor.
// The two unused __fastcall args (dead stores in the original) are dropped.
double EconomyComputePopulationTrend();

// ===========================================================================
// 0x57aa30 — VIBE_Economy_FillLawRangeRatios  (__usercall, eax=(out@eax), ecx=?)
// ===========================================================================
// For laws 8..14 (7 records) writes out[8 + i] = (value - lo) / (hi - lo) where
// the original reads three ints from each 36-byte law record:
//   lo    = record + 0   (the id/low dword)
//   hi    = record + 4
//   value = record + 20
// The output base in the original is (eax + 32), i.e. out[8..14] of a float
// array. This is a DIFFERENT triple than ComputeInterpolatedLawScore (+4/+8/+24);
// preserved exactly. The records are surfaced as a settable triple list so tests
// can feed chosen ranges; the live path reads them from g_lawTable via the raw
// 36-byte record (the descriptor dwords at +0/+4/+20).
struct LawRatioTriple {
    i32 lo;     // record + 0
    i32 hi;     // record + 4
    i32 value;  // record + 20
};
// Fills out[8..14] (indices 8..14) of a >=15-element float array.
void EconomyFillLawRangeRatios(const std::array<LawRatioTriple, 7>& laws8to14,
                               float* out /* float[15], writes [8..14] */);
// Live-table convenience: read laws 8..14 from g_lawTable's raw record bytes.
void EconomyFillLawRangeRatiosFromTable(float* out /* float[15] */);

// ===========================================================================
// 0x579448 — VIBE_City_CopyStateStruct  (__usercall, eax=(result@eax))
// ===========================================================================
// qmemcpy(result, &dword_1235238, 0x24u); return result.  Copies the 36-byte
// broadcast state struct into `result` (set via SetBroadcastStateStruct).
u8* CityCopyStateStruct(u8 result[36]);

}  // namespace guild::world
