#pragma once
// guild::gui — statistics-chart DATA-POINT / LAYOUT computation.
//
// The statistics panel (panel\statistik_stadt) plots up to five overlaid series
// into a chart sub-window.  The five plotter routines live at:
//   VIBE_Stats_DrawBarChart          @0x55e408  (bar chart, integer series)
//   VIBE_StatPanel_DrawGraphLine     @0x55e600  (line series, flt_1234F98..)
//   VIBE_StatPanel_DrawGraphSeriesC  @0x55ebf0  (line series, flt_1234FA4)
//   VIBE_StatPanel_DrawGraphSeriesA  @0x55e778  (line series, flt_1234FA0)
//   VIBE_StatPanel_DrawGraphSeriesB  @0x55e9b4  (line series, flt_1234FA8)
// and the chart frame / axis ticks / legend are laid out by
//   VIBE_StatPanel_RenderChart       @0x55ee70.
//
// Every plotter does the SAME thing structurally: it walks a 16-sample series and,
// for each sample, computes a screen (x,y) and draws a line segment connecting the
// previous column to the current.  This module recovers that COORDINATE math (the
// LAYOUT, not the pixel blit) byte-for-byte; the actual VIBE_Paintbox_DrawLine blit
// and VIBE_Coord_ConvertX FPU round are forward-declared / reproduced as documented
// leaf helpers.
//
// Recovered axis layout (RenderChart, all reads off the chart window geometry):
//   The chart window inner width  W = window.w - 105 (originally `w - 105`, then the
//   column step is `(W) / 16` via the sign-correct shift idiom `(W - frac) >> 4`).
//   The first data column sits at x = 45; column k sits at x = 45 + k*step.
//   The plot baseline is at y = (windowH - base); a sample value v scales to
//   y = (windowH - base) - height * v   where height = (windowH - 30).
//
// VIBE_Coord_ConvertX @0x5c6b08 is an FPU `frndint` (round-to-nearest-even) applied
// to st(0) before the `(int)` truncation in the originals.  We reproduce it as
// RoundToNearestEven() so the integer pixel coordinates match bit-for-bit.

#include "guild/common/types.h"
#include <array>
#include <cstdint>

namespace guild::gui {

using guild::i32;

// ---------------------------------------------------------------------------
// Layout constants recovered byte-for-byte from the chart routines.
// ---------------------------------------------------------------------------
// Number of plotted samples per series (the loops run idx 0..75 step 5 == 16 cols,
// then one extra closing segment to the 16th column at 16*step+45).
inline constexpr int kChartSamples = 16;
inline constexpr int kFirstColumnX = 45;   // x of column 0
inline constexpr int kChartXReserve = 105;  // window.w - 105 == usable width

// Per-series baseline offsets (the `a12 - N` terms; a12 == window height in px).
// Bar chart baseline:           windowH - 30   (DrawBarChart: a12 - 30, a12 - 22)
inline constexpr int kBarBaselineOffset = 30;   // height divisor base
inline constexpr int kBarTopOffset      = 22;   // baseline y = a12 - 22
// Line series A/B/C share:      y = (a12 - 22) - height*v   ; close at a12 - 22
inline constexpr int kLineTopOffset   = 22;     // a12 - 22
inline constexpr int kLineCloseOffset = 22;     // a12 - 32 + 10 == a12 - 22
// "DrawGraphLine" (series 0x02) is the odd one out: it draws against a precomputed
// max and uses baseline (a12 - 32) + 10.0 (flt_624A9C) == a12 - 22.
inline constexpr float kGraphLineBaselineBias = 10.0f;  // flt_624A9C

// Chart height scale: height = windowH - 30 (the `a12 - 30` factor every series uses).
inline constexpr int kChartHeightOffset = 30;

// Gridline (vertical scale marks) layout (RenderChart):
//   x_k = round( k * 0.25 * (windowW - 30) + 8.0 )         (the value-mark lines)
//   label x_k = round( k * 0.25 * (windowW - 30) + 5.0 )   (the % text labels)
inline constexpr float kGridStep   = 0.25f;   // flt_624AA8
inline constexpr float kGridWidthBias = -30.0f; // flt_624AAC
inline constexpr float kGridLineBias  = 8.0f;   // flt_624AB0
inline constexpr float kGridLabelBias = 5.0f;   // flt_624AB4
inline constexpr int   kGridCount = 4;          // four scale marks; labels 100,75,50,25

// Bar-chart "nice maximum" tick ladder (dword_5526D0): the bar chart rounds its peak
// up to the first entry >= peak so the y-axis tops out at a round number.
inline constexpr std::array<i32, 9> kBarTickLadder = {
    500, 1000, 2000, 4000, 8000, 10000, 15000, 20000, 50000};

// Minimum bar/line floor (flt_641DA8 == 0.0): values below this are clamped to it.
inline constexpr float kChartFloor = 0.0f;

// Legend swatch RGB triples (dword_5526F4): one per series, drawn next to the
// series checkbox in ShowCityStatistics.  0x33='3' grey, 0x10/0xEE the palette ramp.
inline constexpr std::array<std::array<std::uint8_t, 3>, 5> kLegendColors = {{
    {0x33, 0x33, 0x33},   // bar (series bit 0)
    {0x10, 0xEE, 0x10},   // line (bit 1)
    {0xEE, 0x10, 0x10},   // series C (bit 2)
    {0xEE, 0xEE, 0x00},   // series A (bit 3)
    {0xEE, 0x10, 0xEE},   // series B (bit 4)
}};

// ---------------------------------------------------------------------------
// Series flavours (RenderChart dispatches on the bitmask `word_1233500`).
// ---------------------------------------------------------------------------
enum SeriesBit : i32 {
    kSeriesBar   = 0x01,  // VIBE_Stats_DrawBarChart
    kSeriesLine  = 0x02,  // VIBE_StatPanel_DrawGraphLine
    kSeriesC     = 0x04,  // VIBE_StatPanel_DrawGraphSeriesC
    kSeriesA     = 0x08,  // VIBE_StatPanel_DrawGraphSeriesA
    kSeriesB     = 0x10,  // VIBE_StatPanel_DrawGraphSeriesB
};

// ---------------------------------------------------------------------------
// VIBE_Coord_ConvertX @0x5c6b08 — round st(0) to nearest integer (round-half-even),
// the FPU `frndint` the originals apply before the `(int)` truncation.
// ---------------------------------------------------------------------------
double RoundToNearestEven(double x);

// A computed plotted point: the integer screen (x,y) of one series sample.
struct ChartPoint {
    int x;
    int y;
};

// Column step `(windowW - 105) / 16`, computed via the original sign-correct shift
// idiom `(W - (CF<<4 + 16*(W>>31))) >> 4`.  For W >= 0 this is plain `W >> 4`.
int ChartColumnStep(int windowW);

// ---------------------------------------------------------------------------
// Line-series point computation (DrawGraphSeriesA/B/C / DrawGraphLine).
//
//   step   = ChartColumnStep(windowW)
//   x_k    = 45 + k*step                              (k = 0..15)
//   height = (float)(windowH - 30)
//   v_k    = max(series[k], 0.0f)                     (floor at flt_641DA8 == 0)
//   y_k    = (int) round( (windowH - 22) - height * v_k )
// The 16th (closing) sample uses series[15] (== flt_12350xx) and baseline windowH-22.
//
// `series` must hold kChartSamples floats.  Returns kChartSamples points.
// ---------------------------------------------------------------------------
std::array<ChartPoint, kChartSamples> ComputeLineSeriesPoints(
    const float* series, int windowW, int windowH);

// ---------------------------------------------------------------------------
// Bar-series point computation (DrawBarChart).
//
//   peak   = max over series of the integer samples, floored at flt_641DA8
//   topVal = first kBarTickLadder entry >= peak (the rounded y-axis maximum)
//   scale  = (float)(windowH - 30) / (float)topVal
//   x_k    = 45 + k*step
//   y_k    = (int) round( (windowH - 22) - series[k] * scale )
//
// Returns the rounded axis maximum in *axisMax (if non-null) and the points.
// ---------------------------------------------------------------------------
std::array<ChartPoint, kChartSamples> ComputeBarSeriesPoints(
    const i32* series, int windowW, int windowH, i32* axisMax);

// Round a bar-chart peak up to the tick ladder (returns the ladder maximum if the
// peak exceeds every entry).
i32 BarAxisMaximum(i32 peak);

// ---------------------------------------------------------------------------
// Gridline X positions (RenderChart): the four vertical scale marks and their label
// anchors.  windowW is the chart window pixel width.
// ---------------------------------------------------------------------------
std::array<int, kGridCount> ComputeGridlineX(int windowW);
std::array<int, kGridCount> ComputeGridLabelX(int windowW);

} // namespace guild::gui
