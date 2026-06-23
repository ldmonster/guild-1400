#include "gui/statchart.h"

#include "util/coord.h"

namespace guild::gui {

// gilde.exe 0x5c6b08 — VIBE_Coord_ConvertX.  VERIFIED 1:1 (wave-17): it
//   fstcw  ; save the x87 control word
//   mov [.+1], 1Fh ; force the CW high byte to 0x1F -> RC bits (10..11) = 0b11
//   fldcw  ; load it -> rounding mode = ROUND TOWARD ZERO (truncate)
//   frndint; round st(0) to integer using that mode  => truncation toward zero
//   fldcw  ; restore the original control word
// so st(0) is TRUNCATED toward zero (NOT round-to-nearest-even).  Every float->int
// site in the city-statistics window routes through this:
//   ShowCityStatistics @0x55f4ac : floorMark/growth/supply/demand/price  (5 sites)
//   RenderChart        @0x55ee70 : grid-X and grid-label-X               (2 sites)
//   DrawBarChart       @0x55e408 : axis-max + bar y                      (5 sites)
//   DrawGraphLine/A/B/C@0x55e6.. : line y                                (per series)
// There is NO bare fistp anywhere in this window — all rounding truncates.  After
// frndint st(0) is already integral, so the subsequent C `(int)` cast is exact; the
// composite is `(int)trunc(x)`, which `static_cast<int>` of a `std::trunc` reproduces.
// Reuse the canonical reconstruction in util/coord (CLAUDE.md: no duplicate defs).
double ConvertX(double x) {
    return guild::util::ConvertX(x);
}

// gilde.exe — the column-step idiom shared by every plotter:
//   step = (W - 105 - (CF(((W-105)>>31),4) + 16*((W-105)>>31))) >> 4
// `__CFSHL__(s, 4)` is the carry out of `s << 4`; combined with `16*s` this is just
// the C "round toward zero" correction for an arithmetic divide by 16.  For the usual
// non-negative widths it reduces to `(W-105) >> 4`; we reproduce the exact integer
// division-by-16-truncating-toward-zero so negative widths match too.
int ChartColumnStep(int windowW) {
    int w = windowW - kChartXReserve;  // W - 105
    // Truncating division toward zero by 16 (matches the shift-with-carry idiom).
    return w / 16;
}

i32 BarAxisMaximum(i32 peak) {
    // DrawBarChart @0x55e408 selects the axis maximum by scanning ONLY the first
    // EIGHT ladder entries (loop `cmp esi,20h` -> idx 0..7 == 500..20000; disasm
    // 0x55e4a2 / 0x55e4ae / 0x55e5e9).  It returns the first entry strictly greater
    // than `peak` (idx 0: `jl`/P<v35[0]; idx 1..7: `jge` advance else pick).  If
    // `peak` is >= every checked entry (>= 20000) the loop falls through at
    // loc_55E4BA with var_2C unchanged, so the axis max is `peak` ITSELF — the 9th
    // ladder entry (50000) is dead code and is never selected.
    for (int i = 0; i < kBarTickLadderChecked; ++i)
        if (peak < kBarTickLadder[i])
            return kBarTickLadder[i];
    return peak;
}

std::array<ChartPoint, kChartSamples> ComputeLineSeriesPoints(
    const float* series, int windowW, int windowH) {
    std::array<ChartPoint, kChartSamples> pts{};
    const int step = ChartColumnStep(windowW);
    // height = (float)(a12 - 30)  [v26]; baseline = (double)(a12 - 32) + 10.0 == a12 - 22
    // [v25, a double].  The original forms v17 = v25 - series[k]*height entirely on the
    // x87 stack, so the product series[k]*height is evaluated in EXTENDED precision; we
    // reproduce that by promoting both factors to double (a plain float*float product
    // would round to float32 first and diverge on values like 320*0.1f -> 32.0 vs
    // 31.99999... — the x87-80bit-vs-SSE boundary).  ConvertX then TRUNCATES.
    const float height = static_cast<float>(windowH - kChartHeightOffset);  // a12 - 30
    const int baseline = windowH - kLineTopOffset;                          // a12 - 22
    for (int k = 0; k < kChartSamples; ++k) {
        // floor at flt_641DA8 == 0.0
        float v = series[k] > kChartFloor ? series[k] : kChartFloor;
        pts[k].x = kFirstColumnX + k * step;
        double y = static_cast<double>(baseline) -
                   static_cast<double>(v) * static_cast<double>(height);
        pts[k].y = static_cast<int>(ConvertX(y));
    }
    return pts;
}

std::array<ChartPoint, kChartSamples> ComputeBarSeriesPoints(
    const i32* series, int windowW, int windowH, i32* axisMax) {
    std::array<ChartPoint, kChartSamples> pts{};
    const int step = ChartColumnStep(windowW);
    const int baseline = windowH - kBarTopOffset;  // a12 - 22

    // peak = max sample, floored at flt_641DA8 (== 0).  The original seeds the running
    // max at -1 and walks samples 0..15; values below the floor are raised to it.
    i32 peak = -1;
    for (int k = 0; k < kChartSamples; ++k)
        if (series[k] > peak)
            peak = series[k];
    float peakF = static_cast<float>(peak);
    if (peakF < kChartFloor)
        peakF = kChartFloor;
    i32 top = BarAxisMaximum(static_cast<i32>(peakF));
    if (axisMax)
        *axisMax = top;

    // scale = (double)(a12-30) / (double)top, stored as FLOAT [v43]; baseline term
    // v44 = (float)(a12-22).  The per-bar y = (float)baseline - (double)sample*scale is
    // built on the x87 stack, then ConvertX TRUNCATES.  Match the binary's float scale
    // and float baseline; the product/subtraction widen to double (extended on x87).
    const float scale = static_cast<float>(
        static_cast<double>(windowH - kChartHeightOffset) / static_cast<double>(top));
    const float baselineF = static_cast<float>(baseline);
    for (int k = 0; k < kChartSamples; ++k) {
        pts[k].x = kFirstColumnX + k * step;
        double y = static_cast<double>(baselineF) -
                   static_cast<double>(series[k]) * static_cast<double>(scale);
        pts[k].y = static_cast<int>(ConvertX(y));
    }
    return pts;
}

std::array<int, kGridCount> ComputeGridlineX(int windowW) {
    std::array<int, kGridCount> g{};
    for (int k = 0; k < kGridCount; ++k) {
        // x_k = trunc( k * 0.25 * (windowW - 30) + 8.0 )  [RenderChart @0x55efec,
        // ConvertX @0x55eff7].  All flt_* constants are float32 promoted to double.
        double v = static_cast<double>(k) * static_cast<double>(kGridStep) *
                       (static_cast<double>(windowW) + static_cast<double>(kGridWidthBias)) +
                   static_cast<double>(kGridLineBias);
        g[k] = static_cast<int>(ConvertX(v));
    }
    return g;
}

std::array<int, kGridCount> ComputeGridLabelX(int windowW) {
    std::array<int, kGridCount> g{};
    for (int k = 0; k < kGridCount; ++k) {
        // label x_k = trunc( k * 0.25 * (windowW - 30) + 5.0 )  [RenderChart @0x55f04e,
        // ConvertX @0x55f054].
        double v = static_cast<double>(k) * static_cast<double>(kGridStep) *
                       (static_cast<double>(windowW) + static_cast<double>(kGridWidthBias)) +
                   static_cast<double>(kGridLabelBias);
        g[k] = static_cast<int>(ConvertX(v));
    }
    return g;
}

} // namespace guild::gui
