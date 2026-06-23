#include "gui/statpanel.h"

namespace guild::gui {

// Scaling constants recovered from ShowCityStatistics (see statchart.h for the chart
// ones).  dbl_624AD4 = 2.5, flt_624ADC = 5.0, flt_624AE0 = 100.0, dbl_624AE4 = 100.0.
namespace {
inline constexpr double kGrowthScale = 2.5;   // dbl_624AD4
inline constexpr float  kSupplyScale = 5.0f;  // flt_624ADC
inline constexpr float  kDemandScale = 100.0f;// flt_624AE0
inline constexpr double kPriceScale  = 100.0; // dbl_624AE4
inline constexpr int kClampMax = 4;           // phrase-id clamp ceiling
inline constexpr int kGrowthPhraseBase = 6897;
inline constexpr int kSupplyPhraseBase = 6903;
} // namespace

CityStatReadouts ComputeCityStatReadouts(const DemandSnapshot& snap) {
    CityStatReadouts r{};

    // All five readouts route through VIBE_Coord_ConvertX @0x5c6b08 (TRUNCATE toward
    // zero — VERIFIED wave-17, ShowCityStatistics @0x55f4ac sites 0x55f58a/5bc/5fd/67a/
    // 6e4), then `(int)`.  float*float products are widened to double to match the x87
    // extended intermediate the originals form on the FPU stack.

    // floorMark = trunc(flt_641DA8) == trunc(0.0) == 0.    [0x55f584/0x55f58a]
    r.floorMark = static_cast<int>(ConvertX(kChartFloor));

    // growth = trunc((v[9]+1.0)*2.5); phrase = min(growth,4)+6897.   [0x55f5b6]
    int growth = static_cast<int>(
        ConvertX((static_cast<double>(snap.v[9]) + 1.0) * kGrowthScale));
    r.growthPhraseId = (growth <= kClampMax ? growth : kClampMax) + kGrowthPhraseBase;

    // supply = trunc(v[2]*5.0); phrase = min(supply,4)+6903.   [0x55f5f7]
    int supply = static_cast<int>(ConvertX(static_cast<double>(snap.v[2]) *
                                           static_cast<double>(kSupplyScale)));
    r.supplyPhraseId = (supply <= kClampMax ? supply : kClampMax) + kSupplyPhraseBase;

    // demand = trunc(max(v[1]*100.0, 0)).   [0x55f63e clamp, 0x55f67a ConvertX]
    // `v40 = v33[1] * flt_624AE0` is a DOUBLE (the x87-extended product of two floats);
    // the clamp `if (v40 <= 0.0)` and ConvertX both operate on that double.
    double demand =
        static_cast<double>(snap.v[1]) * static_cast<double>(kDemandScale);
    if (demand <= 0.0)
        demand = 0.0;
    r.demandValue = static_cast<int>(ConvertX(demand));

    // price = trunc(max(v[3],0) * 100.0).   [0x55f6b5 clamp on v33[3], 0x55f6e4]
    float price = snap.v[3];
    if (price <= 0.0f)
        price = 0.0f;
    r.priceValue =
        static_cast<int>(ConvertX(static_cast<double>(price) * kPriceScale));

    return r;
}

std::array<LegendRow, kLegendRows> ComputeLegendLayout() {
    std::array<LegendRow, kLegendRows> rows{};
    static constexpr int kSeriesBits[kLegendRows] = {
        kSeriesBar, kSeriesLine, kSeriesC, kSeriesA, kSeriesB};
    for (int i = 0; i < kLegendRows; ++i) {
        LegendRow& row = rows[i];
        // checkbox: AddToWindow(window, y=30*i+5, x=5, 1210)
        row.checkboxX = kLegendCheckboxX;
        row.checkboxY = kLegendRowPitch * i + 5;
        // label: AddTextLabel(x=25, y=30*i+4)
        row.labelX = kLegendLabelX;
        row.labelY = kLegendRowPitch * i + 4;
        // swatch scanline span: 30*i+9 .. 30*i+19 (the inner `do { } while (v22 != v21)`
        // where v22 starts at 30*i+9 and v21 == 30*i+9+ (19-? ) ; original v43 starts 19,
        // increments by 30 -> row i top == 30*i+9, bottom == 30*i+19).
        row.swatchYBegin = kLegendRowPitch * i + 9;
        row.swatchYEnd = kLegendRowPitch * i + 19;
        row.messageId = kLegendBaseMessageId + i;
        row.color = kLegendColors[i];
        row.seriesBit = kSeriesBits[i];
    }
    return rows;
}

i32 LegendStateToMask(const std::array<bool, kLegendRows>& checked) {
    // word_1233500 |= (checked ? (1<<i) : 0) — bit i per row, in declaration order.
    i32 mask = 0;
    for (int i = 0; i < kLegendRows; ++i)
        if (checked[i])
            mask |= (1 << i);
    return mask;
}

std::array<bool, kLegendRows> MaskToLegendState(i32 mask) {
    std::array<bool, kLegendRows> s{};
    for (int i = 0; i < kLegendRows; ++i)
        s[i] = (mask & (1 << i)) != 0;
    return s;
}

// gilde.exe 0x55e778 / 0x55e9b4 / 0x55ebf0 — VIBE_StatPanel_DrawGraphSeriesA/B/C.
// All three share this body (verified against disasm 0x55e7e1.. / 0x55ea1d.. /
// 0x55ecc4..); only the series array, draw colour, and closing scalar differ — and
// the closing scalar is just element[15] of the same series, so a single point loop
// reproduces every observable.  The original walks the loop with esi = byte index
// (0,20,..,280) reading the current sample and the next (var_20 = esi+20), drawing a
// segment between the previous and current plotted point; we materialise the 16
// per-column points instead (the load-bearing output) so the segment endpoints match.
//
// Per sample:  s = max(series[k], 0.0)            (flt_641DA8 == 0; fldz/fcomp clamp)
//              height = (float)(a12 - 30)          [var_24/var_29/var_27]
//              y = trunc( (double)(a12 - 22) - (double)height * (double)s )
// The product height*s is x87-extended; both factors widened to double match it.
// (a12-22 in the loop comes from fild(ebx-16h); the closing point uses fild(var_40+10)
//  == fild((a12-32)+10) — the same integer a12-22.)  ConvertX TRUNCATES.
std::array<ChartPoint, kChartSamples> ComputeGraphSeriesPoints(
    const float* series, int windowW, int windowH) {
    std::array<ChartPoint, kChartSamples> pts{};
    const int step = ChartColumnStep(windowW);            // (a11-105)/16
    const float height = static_cast<float>(windowH - kChartHeightOffset);  // a12 - 30
    const int baseline = windowH - kLineTopOffset;        // a12 - 22
    for (int k = 0; k < kChartSamples; ++k) {
        float s = series[k] > kChartFloor ? series[k] : kChartFloor;  // max(s,0)
        pts[k].x = kFirstColumnX + k * step;
        double y = static_cast<double>(baseline) -
                   static_cast<double>(height) * static_cast<double>(s);
        pts[k].y = static_cast<int>(ConvertX(y));
    }
    return pts;
}

// gilde.exe 0x55e600 — VIBE_StatPanel_DrawGraphLine.  Overlays two series: each loop
// iteration (disasm 0x55e69b) loads flt_1234FAC[esi] AND flt_1234F98[esi], scales both
// by height, subtracts from the FLOAT baseline, truncates each via ConvertX, and draws
// a segment from the FAC point (x=ebp) to the F98 point (x=edi=ebp+step).  There is NO
// per-sample clamp here (no fldz/fcomp in the body).  The closing segment uses
// flt_12350D8 (== flt_1234F98[15]); the running-max loop over flt_1234FAC at the top
// (v27) computes a peak that is never used — preserved here only as a comment.
//
//   height   = (float)(a12 - 30)                         [var_2C]
//   baseline = (float)( (double)(a12 - 32) + 10.0 )      [var_30, flt_624A9C]
//   y(s)     = trunc( (double)baseline - (double)s * (double)height )
GraphLinePoints ComputeGraphLinePoints(
    const float* seriesFAC, const float* seriesF98, int windowW, int windowH) {
    GraphLinePoints out{};
    const int step = ChartColumnStep(windowW);
    const float height = static_cast<float>(windowH - kChartHeightOffset);  // a12 - 30
    // baseline is stored as a FLOAT in the original (fild(a12-32) + 10.0 -> fstp float).
    const float baseline = static_cast<float>(
        static_cast<double>(windowH - 32) + static_cast<double>(kGraphLineBaselineBias));
    // Dead running-max over flt_1234FAC (v27, seeded -1.0): unused by the binary.
    for (int k = 0; k < kChartSamples; ++k) {
        const int x = kFirstColumnX + k * step;
        out.seriesFAC[k].x = x;
        out.seriesF98[k].x = x;
        double yFac = static_cast<double>(baseline) -
                      static_cast<double>(seriesFAC[k]) * static_cast<double>(height);
        double yF98 = static_cast<double>(baseline) -
                      static_cast<double>(seriesF98[k]) * static_cast<double>(height);
        out.seriesFAC[k].y = static_cast<int>(ConvertX(yFac));
        out.seriesF98[k].y = static_cast<int>(ConvertX(yF98));
    }
    return out;
}

namespace {
StatPanelCommandSink g_defaultSink;
StatPanelCommandSink* g_sink = &g_defaultSink;
} // namespace

void StatPanel_SetCommandSink(StatPanelCommandSink* sink) {
    g_sink = sink ? sink : &g_defaultSink;
}

void StatPanel_ShowCityStatistics(const DemandSnapshot& snap, i32 seriesMask) {
    CityStatReadouts readouts = ComputeCityStatReadouts(snap);
    auto legend = ComputeLegendLayout();
    g_sink->EmitReadouts(readouts);
    g_sink->EmitLegend(legend);
    g_sink->RenderChart(seriesMask, snap);
}

} // namespace guild::gui
