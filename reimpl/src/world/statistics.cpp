#include "world/statistics.h"

// Faithful port of the economy-report accumulation (gilde.exe 0x579ad0).
//
// The report reads four interleaved float columns out of the per-good
// accumulator block at flt_1235090. Indexing the block as a flat float array `a`
// where a[0] is the float at byte offset 0x90:
//
//   a[ 0]=0x90 a[ 1]=0x94 a[ 2]=0x98 a[ 3]=0x9C a[ 4]=0xA0
//   a[ 5]=0xA4 a[ 6]=0xA8 a[ 7]=0xAC a[ 8]=0xB0 a[ 9]=0xB4
//   a[10]=0xB8 a[11]=0xBC a[12]=0xC0 a[13]=0xC4 a[14]=0xC8
//   a[15]=0xCC a[16]=0xD0 a[17]=0xD4 a[18]=0xD8 a[19]=0xDC
//
// The four reported totals (original locals v33/v34/v35/v26) are:
//   v33 = (a[0]+a[5]+a[10]+a[15]) * scale   bytes 0x90,0xA4,0xB8,0xCC
//   v34 = (a[1]+a[6]+a[11]+a[16]) * scale   bytes 0x94,0xA8,0xBC,0xD0
//   v35 = (a[2]+a[7]+a[12]+a[17]) * scale   bytes 0x98,0xAC,0xC0,0xD4
//   v26 = (a[3]+a[8]+a[13]+a[18]) * scale   bytes 0x9C,0xB0,0xC4,0xD8
//
// i.e. total[k] = (a[k] + a[k+5] + a[k+10] + a[k+15]) * scale, k in 0..3.

namespace guild::world {

float StatisticsCategoryTotal(const float* accum, int k) {
    // 0x579b04..0x579b89: fld a[k+15]; fadd a[k+10]; fadd a[k+5]; fadd a[k];
    // fmul flt_62570C; fstp — the sum runs HIGH offset first entirely on the
    // x87 stack (80-bit intermediates, modeled with double); only the final
    // store rounds to float.
    return static_cast<float>(
        (static_cast<double>(accum[k + 15]) + accum[k + 10] + accum[k + 5] +
         accum[k]) *
        static_cast<double>(kStatScale));
}

void StatisticsAccumulateCategoryTotals(const float* accum, float* out) {
    for (int k = 0; k < kStatCategoryCnt; ++k)
        out[k] = StatisticsCategoryTotal(accum, k);
}

// gilde.exe 0x57a900 — tax-window row -> absolute round number.
//   the loop renders round = (currentRound - 16) + row for row 0..16.
i32 StatisticsTaxRowRound(i32 currentRound, int row) {
    return currentRound - (kStatTaxRounds - 1) + row;   // -16 + row
}

} // namespace guild::world
