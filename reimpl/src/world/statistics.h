#pragma once
// Statistics — economy-report accumulation. Faithful 1:1 port of the float
// accumulation that drives VIBE_Statistics_BuildEconomyReport (gilde.exe
// 0x579ad0): the per-good accumulator array (flt_1235090.., the float column the
// economy supply/demand pass fills) is reduced into four category totals, each
// scaled by flt_62570C.
//
// The report function itself is GUI/network-bound (it formats trend strings and
// broadcasts them to player persons); the recoverable, deterministic core is the
// accumulation below.
#include "guild/common/types.h"

namespace guild::world {

// The economy float accumulators the report reads. The originals are four
// interleaved floats per good slot starting at flt_1235090, stride 0x14 bytes
// (== 5 floats). The report sums one column across four good-slots:
//   total[k] = ( acc[k] + acc[k+5] + acc[k+10] + acc[k+15] ) * scale
// for k in {0,1,2,3} (the 0x90/0x94/0x98/0x9C byte offsets), where scale is
// flt_62570C == 0.25f. We model the relevant 20-float window as a flat array.
constexpr int   kStatAccumCount  = 20;     // 4 good-slots x 5 floats
constexpr int   kStatCategoryCnt = 4;      // four reported category totals
constexpr float kStatScale       = 0.25f;  // flt_62570C @0x62570C (0x3E800000)

// gilde.exe 0x579ad0 — accumulation core. Reads `accum[kStatAccumCount]` and
// writes the four scaled category totals into `out[kStatCategoryCnt]`.
//   out[0] <- (a[0]+a[5]+a[10]+a[15]) * scale   (bytes 0x90,0xA4,0xB8,0xCC)
//   out[1] <- (a[1]+a[6]+a[11]+a[16]) * scale   (bytes 0x94,0xA8,0xBC,0xD0)
//   out[2] <- (a[2]+a[7]+a[12]+a[17]) * scale   (bytes 0x98,0xAC,0xC0,0xD4)
//   out[3] <- (a[3]+a[8]+a[13]+a[18]) * scale   (bytes 0x9C,0xB0,0xC4,0xD8)
// The full index->byte-offset mapping is documented in statistics.cpp.
void StatisticsAccumulateCategoryTotals(const float* accum, float* out);

// Convenience: returns the single scaled total of one category column k (0..3)
// over the four good-slots, matching the original's per-category expression.
float StatisticsCategoryTotal(const float* accum, int k);

// ===========================================================================
// Tax-history window query  (gilde.exe VIBE_Statistics_ShowTaxWindow 0x57a900)
// ===========================================================================
// The tax window renders a rolling window of the last kStatTaxRounds (17) rounds
// from the per-round tax-amount ring dword_12350E0, labelling each row with its
// absolute round number. For row i (0..16) the original computes:
//   round  = currentRound - 16 + i     ( = qword_13CE852 + i - 16 )
//   amount = dword_12350E0[i]          (the i-th stored round amount)
// The GUI render is deferred; the deterministic query — the round number for a
// given row — is recovered here.
constexpr int kStatTaxRounds = 17;   // 17-row window (v5 < 17)

// Returns the absolute round number shown on tax-window row `row` (0..16) for the
// given current round. (currentRound - 16 + row.)
i32 StatisticsTaxRowRound(i32 currentRound, int row);

} // namespace guild::world
