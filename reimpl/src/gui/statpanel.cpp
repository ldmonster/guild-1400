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

    // floorMark = round(flt_641DA8) == round(0.0) == 0.
    r.floorMark = static_cast<int>(RoundToNearestEven(kChartFloor));

    // growth = round((v[9]+1.0)*2.5); phrase = min(growth,4)+6897.
    int growth = static_cast<int>(
        RoundToNearestEven((static_cast<double>(snap.v[9]) + 1.0) * kGrowthScale));
    r.growthPhraseId = (growth <= kClampMax ? growth : kClampMax) + kGrowthPhraseBase;

    // supply = round(v[2]*5.0); phrase = min(supply,4)+6903.
    int supply = static_cast<int>(
        RoundToNearestEven(static_cast<double>(snap.v[2] * kSupplyScale)));
    r.supplyPhraseId = (supply <= kClampMax ? supply : kClampMax) + kSupplyPhraseBase;

    // demand = round(max(v[1]*100.0, 0)).
    float demand = snap.v[1] * kDemandScale;
    if (demand <= 0.0f)
        demand = 0.0f;
    r.demandValue = static_cast<int>(RoundToNearestEven(static_cast<double>(demand)));

    // price = round(max(v[3],0) * 100.0).
    float price = snap.v[3];
    if (price <= 0.0f)
        price = 0.0f;
    r.priceValue =
        static_cast<int>(RoundToNearestEven(static_cast<double>(price) * kPriceScale));

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
