#pragma once
// guild::gui — HUD content-grid / tiled-bar layout builders (the DATA half).
//
// This module recovers the LAYOUT/CONTENT-build math of the HUD widget grids
// byte-for-byte: where each child object is placed (x/y), how many tiles a bar
// gets, and the per-case column/row strides.  The actual sprite/glyph blit and the
// widget-allocation side effects are routed through a mockable "object sink" so the
// pure placement math is testable in isolation.
//
// Recovered functions:
//   * VIBE_Hud_BuildTiledRow        @0x4bd688 — a single horizontal run of tiles.
//   * VIBE_Hud_BuildScaledTiledBar  @0x4bd758 — a value-vs-baseline diff bar (the
//       up/down/equal three-way tiling with the 0.01 tolerance band).
//   * VIBE_Hud_BuildPersonGridLayout   @0x553154 — the 1..8-person grid (the
//       dword_5526A0 stride table + the per-row column counts) → person cards.
//   * VIBE_Hud_BuildPersonColumnLayout @0x55375c — the vertical person column variant
//       (the per-mode x/y placement table a4[4/18/32/46/60/74]).
//   * VIBE_Hud_BuildBuildingChoiceList @0x5529f8 — N child-window rows, 128px pitch.
//   * VIBE_Hud_BuildBuildingPriceRows  @0x552b80 — price/worth rows, 64px pitch.
//   * VIBE_Hud_BuildTrainingRow         @0x550a0c — one 120px training slot row.
//
// Layout constants recovered as exact bytes:
//   flt_61E1F0 / flt_61E200 = 0.047619048 (= 1/21)   tiles-per-pixel scale
//   dbl_61E1F8              = 0.01                     diff-bar tolerance band
//   person card pitch = 101 px (x), row base +35 (y), col-block stride 128 (grid)
//   training row pitch = 120 px ; price row pitch = 64 px ; choice row pitch = 128 px

#include "gui/types.h"
#include <vector>

namespace guild::gui {

// ---------------------------------------------------------------------------
// Object sink (mock seam).  Every builder ultimately calls
// VIBE_Object_AddToWindow(win, y, x, gfx) to place a child; we route those through
// a sink so the layout (x,y,gfx) is observable without the real widget system.
// ---------------------------------------------------------------------------
struct PlacedObject {
    int win;  // dword_62D230 (current window) in the original
    int x;    // a3 / param (left x in map/window coords)
    int y;    // a2 (top y)
    int gfx;  // a4 (sprite/object id)
};

struct ObjectSink {
    virtual ~ObjectSink() = default;
    // Returns the (mock) widget index assigned to the placed object.  The default
    // implementation records the placement and hands back a monotonically-increasing
    // index so callers that store the returned widget id keep working.
    virtual int AddToWindow(int win, int y, int x, int gfx) {
        placed.push_back({win, x, y, gfx});
        return nextIndex++;
    }
    std::vector<PlacedObject> placed;
    int nextIndex = 1;
};

void Hud_SetObjectSink(ObjectSink* sink);
ObjectSink* Hud_GetObjectSink();

// ===========================================================================
// Tiled-row / scaled-tiled-bar tiling math.
// ===========================================================================
// flt_61E1F0 / flt_61E200 = 1/21 px->tile scale; dbl_61E1F8 = 0.01 tolerance.
inline constexpr float  kTileScale     = 0.0476190485060215f; // flt_61E1F0 / flt_61E200
inline constexpr double kTileTolerance = 0.01;                // dbl_61E1F8

// The original derives the tile-step (pitch) from the gfx record width:
//   step = (gfxWidth >> 16) + 2.  We expose this as a parameter so the tiling math
// is decoupled from the renderer's gfx table.
//
// gilde.exe 0x4bd688 — VIBE_Hud_BuildTiledRow.
//   count = (int)(value * (1/21)) / 2 ;  remainder = that quotient's *2-modulus*.
//   places `count` tiles at x = startX + k*step (gfx=baseGfx), then if the value was
//   odd one half-tile (gfx=baseGfx, x continues).  Returns the number of full tiles.
// `value` is a3 (the raw bar length), `step` is the per-tile pitch, `startX`=a1,
// `y`=a2, `baseGfx`=the current window gfx slot.
int Hud_BuildTiledRow(int value, int step, i16 startX, i16 y, int baseGfx);

// gilde.exe 0x4bd758 — VIBE_Hud_BuildScaledTiledBar.
// A three-way diff bar comparing a current production value `cur` against a baseline
// `base` (both in pixels).  When |cur-base|/max(cur,base) <= 0.01 it draws a single
// equal bar of `base` tiles (gfx baseGfx / +1 for the half).  When cur<base it draws
// `base` tiles in the "down" colour (gfx+2/+3) then `cur` tiles in baseGfx (+1 half).
// When cur>base it draws `cur` tiles "up" (gfx+4/+5) then `base` tiles in baseGfx.
// `step` is the per-tile pitch (gfxWidth>>16 + 2 in the original).  Returns the tile
// count of the last run (mirrors the original's `result`).
int Hud_BuildScaledTiledBar(int cur, int base, int step, i16 startX, i16 y, int baseGfx);

// ===========================================================================
// Person grid (1..8) — gilde.exe 0x553154.
// The original picks a row/column shape from the selection count (a2):
//   1..3 -> 1 row of {1,2,3} ;  4 -> 2x2 ; 5 -> 3+2 ; 6 -> 3+3 ;
//   7 -> 3+2+2 ; 8 -> 3+2+3.  Each cell is 101px wide (x) and rows are 128px apart;
//   within a row the columns centre as {202}, {101,202}, {0,101,202} for 1/2/3 cols.
// We recover that placement into a flat list of cell slots (x,y) so the grid shape is
// testable without the renderer.  `count` is the number of people to lay out.
// ===========================================================================
struct GridCell {
    int colX; // a4[14*i + 4]  (per-cell x within the row, 0/101/202)
    int rowY; // a4[14*i + 5]  (row base y: 0,128,256 ...)
};

// dword_5526A0 is a 3-dword scratch (initialised to -1 and overwritten per case); the
// per-case column counts are the recovered switch table.
extern const int kPersonGridColumns[9][3]; // [count][row] -> columns in that row (0 = unused)
extern const int kPersonGridRows[9];       // [count] -> number of rows used

// gilde.exe 0x553154 (the first placement loop, before the AddToWindow content pass).
// Compute the (colX,rowY) of each of `count` person cells (count clamped to 1..8 by
// the caller).  `maxCells` caps the output (the original's `a2` / v35 guard).
std::vector<GridCell> Hud_BuildPersonGridCells(int count, int maxCells = 8);

// Person-card cell metrics (the +101/+35/-32 offsets used in the content pass).
inline constexpr int kPersonCellX     = 101; // v10 += 101 column advance / +101 centre
inline constexpr int kPersonCellRowY  = 35;  // v38 = rowY + 35 (card top)
inline constexpr int kPersonCardPortraitDY = 32; // portrait at cardTop - 32
inline constexpr int kPersonRowStrideY = 128; // v33 += 128 between grid rows

// ===========================================================================
// Person column (vertical) — gilde.exe 0x55375c.
// For modes 1/2/3 it lays out 4 column slots with x = {202,202,101,303} and a
// y-base table {0,128,256,256}; the default (mode 4/5/6/8) uses 6 slots with
// x = {202,101,303,0,202,404} and y = {0,128,128,256,256,256}; mode 7 lays out none.
// ===========================================================================
struct ColumnSlot { int x; int y; };
extern const ColumnSlot kPersonColumnMode123[4];
extern const ColumnSlot kPersonColumnModeDefault[6];

// gilde.exe 0x55375c (the a4[..] = {x,y} placement block).  Return the column slot
// table for the given person-mode `a1` (1..8).  Empty for mode 7.
std::vector<ColumnSlot> Hud_BuildPersonColumnSlots(int mode);

// ===========================================================================
// Building choice / price / training rows.
// ===========================================================================
inline constexpr int kChoiceRowStrideY   = 128; // 0x5529f8: v8<<7 / v18 += 128
inline constexpr int kChoiceChildGfx     = 1740; // the row's selectable sprite
inline constexpr int kChoiceChildFlag    = 69;   // word_67EDFC = 69
inline constexpr int kChoiceWindowFlag   = 16;   // window create flag

// gilde.exe 0x5529f8 — VIBE_Hud_BuildBuildingChoiceList.
// Lay out `count` rows (clamped to `capacity`); row i at y = i*128, centred in x at
// (windowW - childW)/2.  Returns the placed sprite x for each row (the child-window
// left).  We expose the placement; the building text fill is routed via the sink.
struct ChoiceRow { int y; int x; };
std::vector<ChoiceRow> Hud_BuildBuildingChoiceRows(int count, int capacity,
                                                   int windowW, int childW);

inline constexpr int kPriceRowStrideY = 64; // 0x552b80: v15<<6 ; +27 second label
inline constexpr int kPriceRowLabel2DY = 27; // ((i<<6)+27)
inline constexpr int kPriceValueLabelX = 128; // VIBE_Object_AddTextLabel(128, ...)
inline constexpr int kPriceRowGfxBase  = 1010; // *Begin + 1010 (room sprite)

// gilde.exe 0x552b80 — VIBE_Hud_BuildBuildingPriceRows.
// Each accepted room becomes a row: a sprite at (32, i*64), a "%G" value label at
// (128, i*64), a "%T" worth label at (128, i*64 + 27).  We model the placement for a
// supplied set of room codes; eligibility (code<68 && (flag&1)==0) is the caller's.
struct PriceRow { int gfxX; int gfxY; int valueLabelY; int worthLabelY; int roomGfx; };
std::vector<PriceRow> Hud_BuildBuildingPriceRows(const std::vector<int>& roomCodes);

inline constexpr int kTrainingRowStrideY = 120; // 0x550a0c: 120*a1
inline constexpr int kTrainingSlotGfx    = 1726; // the slot backdrop sprite
inline constexpr int kTrainingChildFlag  = 67;   // word_67EDFC = 67

// gilde.exe 0x550a0c — VIBE_Hud_BuildTrainingRow.
// Row `index`: backdrop sprite at (0, index*120), the trainee sprite `gfx` at
// (13, index*120 + 5), and an 87x205 child window at (64, index*120).  Returns the
// three placements.
struct TrainingRow { int backdropX, backdropY; int traineeX, traineeY; int childX, childY; };
TrainingRow Hud_BuildTrainingRow(int index, int gfx);

} // namespace guild::gui
