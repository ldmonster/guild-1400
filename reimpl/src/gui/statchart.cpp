#include "gui/statchart.h"

#include <cmath>

namespace guild::gui {

// gilde.exe 0x5c6b08 — VIBE_Coord_ConvertX  (FPU frndint, round-to-nearest-even).
// The original sets the x87 rounding mode to round-to-nearest, executes `frndint`
// on st(0), and restores the previous control word.  std::nearbyint with the default
// FE_TONEAREST mode is the exact equivalent (round half to even).
double RoundToNearestEven(double x) {
    return std::nearbyint(x);
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
    // First ladder entry >= peak; if none, the largest ladder entry.
    for (i32 t : kBarTickLadder)
        if (peak < t)
            return t;
    return kBarTickLadder.back();
}

std::array<ChartPoint, kChartSamples> ComputeLineSeriesPoints(
    const float* series, int windowW, int windowH) {
    std::array<ChartPoint, kChartSamples> pts{};
    const int step = ChartColumnStep(windowW);
    const float height = static_cast<float>(windowH - kChartHeightOffset);  // a12 - 30
    const int baseline = windowH - kLineTopOffset;                          // a12 - 22
    for (int k = 0; k < kChartSamples; ++k) {
        // floor at flt_641DA8 == 0.0
        float v = series[k] > kChartFloor ? series[k] : kChartFloor;
        pts[k].x = kFirstColumnX + k * step;
        // y is a *truncating* (int) cast in the original (no ConvertX on the y term).
        pts[k].y = static_cast<int>(static_cast<double>(baseline) - height * v);
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

    // scale = (windowH - 30) / topVal
    const double scale = static_cast<double>(windowH - kChartHeightOffset) /
                         static_cast<double>(top);
    for (int k = 0; k < kChartSamples; ++k) {
        pts[k].x = kFirstColumnX + k * step;
        pts[k].y = static_cast<int>(static_cast<double>(baseline) -
                                    static_cast<double>(series[k]) * scale);
    }
    return pts;
}

std::array<int, kGridCount> ComputeGridlineX(int windowW) {
    std::array<int, kGridCount> g{};
    for (int k = 0; k < kGridCount; ++k) {
        // x_k = round( k * 0.25 * (windowW - 30) + 8.0 )
        double v = static_cast<double>(k) * kGridStep *
                       (static_cast<double>(windowW) + kGridWidthBias) +
                   kGridLineBias;
        g[k] = static_cast<int>(RoundToNearestEven(v));
    }
    return g;
}

std::array<int, kGridCount> ComputeGridLabelX(int windowW) {
    std::array<int, kGridCount> g{};
    for (int k = 0; k < kGridCount; ++k) {
        double v = static_cast<double>(k) * kGridStep *
                       (static_cast<double>(windowW) + kGridWidthBias) +
                   kGridLabelBias;
        g[k] = static_cast<int>(RoundToNearestEven(v));
    }
    return g;
}

} // namespace guild::gui
