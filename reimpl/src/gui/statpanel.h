#pragma once
// guild::gui — city-statistics panel CONTENT build.
//
// VIBE_StatPanel_ShowCityStatistics @0x55f4ac loads panel\statistik_stadt, reads an
// economy "demand snapshot" of 15 floats (VIBE_Economy_LoadDemandSnapshot writes a
// float[15] at +0x100), scales selected entries into the displayed scalar readouts,
// drives the overlaid chart (statchart.{h,cpp}), and lays out a five-row legend with
// a series-toggle checkbox + colour swatch per row.
//
// This module recovers the CONTENT/LAYOUT math byte-for-byte:
//   - the snapshot -> displayed-value scaling (the dbl_624AD4.. constants),
//   - the legend row geometry (checkbox / swatch / label positions),
//   - the series-selection bitmask <-> checkbox state mapping.
// The form load, widget creation and modal frame loop are routed through a command
// hook so the data math is testable in isolation.

#include "gui/statchart.h"
#include "guild/common/types.h"

#include <array>

namespace guild::gui {

// ---------------------------------------------------------------------------
// Economy demand snapshot — the float[15] VIBE_Economy_LoadDemandSnapshot @0x57a5dc
// fills.  ShowCityStatistics only reads indices {1,2,3,9} for the scalar readouts;
// the whole array is forwarded into RenderChart as the per-series data.
// ---------------------------------------------------------------------------
struct DemandSnapshot {
    std::array<float, 15> v{};
};

// The five scalar readouts the panel prints (RenderRichString message ids in comments).
struct CityStatReadouts {
    // 6895: minimum/floor mark — flt_641DA8 (== 0).
    int floorMark;
    // 6896 + clamp: growth index  (v[9]+1)*2.5, clamped to [.,4], +6897 selects a
    // localized phrase id.
    int growthPhraseId;
    // 6902 + clamp: supply index  v[2]*5.0, clamped to [.,4], +6903 selects a phrase.
    int supplyPhraseId;
    // 6908: demand value          max(v[1]*100.0, 0).
    int demandValue;
    // 6909: price value           max(v[3],0)*100.0.
    int priceValue;
};

// gilde.exe 0x55f4ac — compute the five scalar readouts from the snapshot.
//   floorMark      = (int) round( flt_641DA8 )                       == 0
//   growth         = (int) round( (v[9]+1.0) * 2.5 )  ; phrase = min(growth,4)+6897
//   supply         = (int) round( v[2] * 5.0 )        ; phrase = min(supply,4)+6903
//   demand         = (int) round( max(v[1]*100.0, 0) )
//   price          = (int) round( max(v[3],0) * 100.0 )
// The `(int)` casts are preceded by VIBE_Coord_ConvertX (round-to-nearest-even).
CityStatReadouts ComputeCityStatReadouts(const DemandSnapshot& snap);

// ---------------------------------------------------------------------------
// Legend layout (the `do … while (v42 < 5)` block in ShowCityStatistics).
// Five rows; row i holds a toggle checkbox (object id 1210) and a colour swatch.
//   checkbox object  : AddToWindow(window, y=30*i+5, x=5, gfx=1210)
//   label            : AddTextLabel(x=25, y=30*i+4, …)   message id 6912+i
//   swatch fill rows : y from (30*i+9) to (30*i+19) inclusive-exclusive, drawn at the
//                      right edge (windowRight-1 .. windowRight-11) in kLegendColors[i]
// ---------------------------------------------------------------------------
struct LegendRow {
    int checkboxX, checkboxY;     // series-toggle checkbox
    int labelX, labelY;           // text label
    int swatchYBegin, swatchYEnd; // swatch scanline span [begin, end)
    int messageId;                // localized label id (6912 + i)
    std::array<std::uint8_t, 3> color;  // swatch RGB (kLegendColors[i])
    int seriesBit;                // bitmask bit toggled by this row's checkbox
};

inline constexpr int kLegendRows = 5;
inline constexpr int kLegendRowPitch = 30;       // 30*i
inline constexpr int kLegendCheckboxX = 5;
inline constexpr int kLegendLabelX = 25;
inline constexpr int kLegendBaseMessageId = 6912; // 6912 + i

// gilde.exe 0x55f4ac — build the five legend rows.
std::array<LegendRow, kLegendRows> ComputeLegendLayout();

// Map the five checkbox states (true == checked) to the series bitmask word_1233500.
// Bit i is set when row i's checkbox is checked.
i32 LegendStateToMask(const std::array<bool, kLegendRows>& checked);

// Inverse: which rows are checked for a given mask.
std::array<bool, kLegendRows> MaskToLegendState(i32 mask);

// ---------------------------------------------------------------------------
// Statistics-panel GRAPH-SERIES plotters (wave-20).
//
// RenderChart @0x55ee70 dispatches into four line-series renderers, each walking a
// 16-sample series stored in the interleaved economy-history table (5 floats per
// row, stride 20 bytes; one column per series).  Per column they draw a segment
// connecting the previous column's plotted point to the current column's.  All
// recover the same observable: the integer screen (x,y) of each plotted sample.
//
// Shared layout (from ChartColumnStep / kFirstColumnX in statchart.h):
//   step = ChartColumnStep(windowW) = (windowW-105)/16  (trunc toward zero)
//   x_k  = 45 + k*step                                  (k = 0..15)
//   height = (float)(windowH - 30)                      [a12 - 30]
// Every float->int site routes through VIBE_Coord_ConvertX (TRUNCATE toward zero,
// wave-17).  The series*height product is formed in x87 extended precision; we widen
// both factors to double to reproduce it bit-exactly (float32 product diverges).
//
//   VIBE_StatPanel_DrawGraphSeriesA @0x55e778  series flt_1234FA0 (col 1)
//   VIBE_StatPanel_DrawGraphSeriesB @0x55e9b4  series flt_1234FA8 (col 4)
//   VIBE_StatPanel_DrawGraphSeriesC @0x55ebf0  series flt_1234FA4 (col 2)
// A/B/C are identical structurally; they differ only in series source, draw colour,
// and the closing scalar (== element[15] of the series).  Each sample is clamped to
// >= 0.0 (flt_641DA8) and plotted at:
//   y_k = (int) trunc( (double)(windowH-22) - (double)height * (double)max(s_k,0) )
// ---------------------------------------------------------------------------

// Per-series plotted points (16 samples).  `series` must hold kChartSamples floats.
std::array<ChartPoint, kChartSamples> ComputeGraphSeriesPoints(
    const float* series, int windowW, int windowH);

// ---------------------------------------------------------------------------
// VIBE_StatPanel_DrawGraphLine @0x55e600 — the odd plotter.  It overlays TWO series
// (flt_1234FAC col 4 and flt_1234F98 col 0): each segment joins the FAC point at the
// previous column to the F98 point at the current column.  Unlike A/B/C there is NO
// per-sample clamp, and the baseline is a FLOAT:
//   baseline = (float)( (double)(windowH-32) + 10.0(flt_624A9C) )   == windowH-22
//   y(series) = (int) trunc( (double)baseline - (double)series * (double)height )
// Returns the two plotted-point columns: `.first` = FAC series, `.second` = F98.
// (The dead running-max loop over flt_1234FAC is preserved as a comment; its result
// v27 is never used by the original.)
// ---------------------------------------------------------------------------
struct GraphLinePoints {
    std::array<ChartPoint, kChartSamples> seriesFAC;  // flt_1234FAC column
    std::array<ChartPoint, kChartSamples> seriesF98;  // flt_1234F98 column
};
GraphLinePoints ComputeGraphLinePoints(
    const float* seriesFAC, const float* seriesF98, int windowW, int windowH);

// ---------------------------------------------------------------------------
// Command hook (mockable) — the form/chart/legend mutations.
// ---------------------------------------------------------------------------
struct StatPanelCommandSink {
    virtual ~StatPanelCommandSink() = default;
    virtual void RenderChart(i32 /*seriesMask*/, const DemandSnapshot&) {}
    virtual void EmitReadouts(const CityStatReadouts&) {}
    virtual void EmitLegend(const std::array<LegendRow, kLegendRows>&) {}
};
void StatPanel_SetCommandSink(StatPanelCommandSink* sink);

// gilde.exe 0x55f4ac — one full build pass: compute readouts + legend, push them and
// the chart through the sink.  `seriesMask` defaults to all-bar (word_1233500 init==1).
void StatPanel_ShowCityStatistics(const DemandSnapshot& snap, i32 seriesMask);

} // namespace guild::gui
