#include "gui/hud_grid.h"

#include <cmath>

namespace guild::gui {

// ---------------------------------------------------------------------------
// Object sink.
// ---------------------------------------------------------------------------
namespace {
ObjectSink g_defaultSink;
ObjectSink* g_sink = &g_defaultSink;
} // namespace

void Hud_SetObjectSink(ObjectSink* sink) { g_sink = sink ? sink : &g_defaultSink; }
ObjectSink* Hud_GetObjectSink() { return g_sink; }

// ---------------------------------------------------------------------------
// Tiled row — gilde.exe 0x4bd688.
//   v3   = (double)value * (1/21)
//   half = (int)v3 % 2        (v15: odd half-tile flag)
//   full = (int)v3 / 2        (v17: full-tile count)
//   for i in [0,full): AddToWindow(win, y, x, baseGfx); x += step
//   if half: AddToWindow(win, y, x, baseGfx+1)
// The original advances `i` (the x position) as a 16-bit value, so it wraps; we keep
// it in an int here because the caller never overflows 16 bits in practice (the bar
// length is bounded), and the placement list records the true x.
// ---------------------------------------------------------------------------
int Hud_BuildTiledRow(int value, int step, i16 startX, i16 y, int baseGfx) {
    double v3 = (double)value * (double)kTileScale;
    int q    = (int)v3;
    int half = q % 2;   // v15
    int full = q / 2;   // v17

    int x = (i16)startX; // i
    int placed = 0;      // v10
    for (; placed < full; x = (i16)(x + step)) {
        g_sink->AddToWindow(0, y, x, baseGfx);
        ++placed;
    }
    if (half)
        g_sink->AddToWindow(0, y, x, baseGfx + 1);
    return full;
}

// ---------------------------------------------------------------------------
// Scaled tiled bar — gilde.exe 0x4bd758.
// Helper: tile `value` pixels into half-pairs at gfx `g` (full) / `g+1` (half).
// ---------------------------------------------------------------------------
namespace {
int TileRun(int value, int step, i16 startX, i16 y, int fullGfx, int halfGfx) {
    double v = (double)value * (double)kTileScale; // flt_61E200
    int q    = (int)v;
    int half = q % 2;
    int full = q / 2;
    int x = (i16)startX;
    int placed = 0;
    for (; placed < full; x = (i16)(x + step)) {
        g_sink->AddToWindow(0, y, x, fullGfx);
        ++placed;
    }
    if (half)
        g_sink->AddToWindow(0, y, x, halfGfx);
    return full;
}
} // namespace

int Hud_BuildScaledTiledBar(int cur, int base, int step, i16 startX, i16 y, int baseGfx) {
    int v31 = cur;   // production pixels
    int v25 = base;  // baseline pixels
    int denom = (cur <= base) ? base : cur;            // v6 = max(cur,base)
    int result = 0;

    if (denom != 0 &&
        std::fabs((double)(v31 - base)) / (double)denom <= kTileTolerance) {
        // Equal: a single run of `base` tiles in baseGfx / baseGfx+1.
        result = TileRun(base, step, startX, y, baseGfx, baseGfx + 1);
    } else if (v31 >= base) {
        if (v31 > base) {
            // Up: `cur` tiles in the up colour (gfx+4/+5), then `base` tiles baseline.
            TileRun(v31, step, startX, y, baseGfx + 4, baseGfx + 5);
            result = TileRun(v25, step, startX, y, baseGfx, baseGfx);
        } else {
            result = v31; // cur == base but outside tolerance band (denom 0): mirror
        }
    } else {
        // Down: `base` tiles in the down colour (gfx+2/+3), then `cur` tiles baseline.
        TileRun(base, step, startX, y, baseGfx + 2, baseGfx + 3);
        result = TileRun(v31, step, startX, y, baseGfx, baseGfx + 1);
    }
    return result;
}

// ---------------------------------------------------------------------------
// Person grid — gilde.exe 0x553154.
// Per-count column counts and row count (recovered switch table).
// ---------------------------------------------------------------------------
const int kPersonGridColumns[9][3] = {
    {0, 0, 0}, // 0 (unused)
    {1, 0, 0}, // 1  -> 1 row, 1 col
    {2, 0, 0}, // 2  -> 1 row, 2 cols
    {3, 0, 0}, // 3  -> 1 row, 3 cols
    {2, 2, 0}, // 4  -> 2 rows, 2+2
    {3, 2, 0}, // 5  -> 2 rows, 3+2
    {3, 3, 0}, // 6  -> 2 rows, 3+3
    {3, 2, 2}, // 7  -> 3 rows, 3+2+2
    {3, 2, 3}, // 8  -> 3 rows, 3+2+3
};
const int kPersonGridRows[9] = {0, 1, 1, 1, 2, 2, 2, 3, 3};

// Row base-y (v6): 128 for the 1-row cases, 64 for the 2-row cases, 0 for 3-row.
namespace {
int GridRowBaseY(int count) {
    if (count >= 1 && count <= 3) return 128;
    if (count >= 4 && count <= 6) return 64;
    return 0; // 7,8
}
} // namespace

std::vector<GridCell> Hud_BuildPersonGridCells(int count, int maxCells) {
    std::vector<GridCell> cells;
    if (count < 1 || count > 8)
        return cells;

    int rows = kPersonGridRows[count];
    int rowY = GridRowBaseY(count);   // v33 (starts at v6)
    int placedTotal = 0;              // v7

    for (int r = 0; r < rows; ++r) {
        int cols = kPersonGridColumns[count][r]; // *(&v30 + 4*r)
        int colAdvance = 0;                      // v10 (per-row column offset)
        for (int c = 0; c < cols; ++c) {
            if (placedTotal >= maxCells)
                break;
            int colX;
            if (cols == 3)
                colX = colAdvance;               // {0,101,202}
            else if (cols == 2)
                colX = colAdvance + kPersonCellX; // {101,202}
            else
                colX = 2 * kPersonCellX;          // {202}
            cells.push_back({colX, rowY});
            colAdvance += kPersonCellX;           // v10 += 101
            ++placedTotal;
        }
        rowY += kPersonRowStrideY;                // v33 += 128
    }
    return cells;
}

// ---------------------------------------------------------------------------
// Person column — gilde.exe 0x55375c.
// Modes 1/2/3: 4 slots {x,y} ; default (4/5/6/8): 6 slots ; mode 7: none.
//   a4[4]=202 a4[18]=202 a4[32]=101 a4[46]=303 ; a4[5]=0 a4[19]=128 a4[33]=256 a4[47]=256
//   default a4[4]=202 a4[18]=101 a4[32]=303 a4[46]=0 a4[60]=202 a4[74]=404 ;
//           a4[5]=0 a4[19]=128 a4[33]=128 a4[47]=256 a4[61]=256 a4[75]=256
// (a4[k+1] holds x, a4[k] holds y? — in the code a4[N] is x and a4[N+1] is y where the
// x values are written to indices 4,18,32,.. and y to 5,19,33,.. i.e. stride 14, x at
// +4, y at +5 of each 14-pointer cell.)
// ---------------------------------------------------------------------------
const ColumnSlot kPersonColumnMode123[4] = {
    {202, 0},   // a4[4]=202, a4[5]=0
    {202, 128}, // a4[18]=202, a4[19]=128
    {101, 256}, // a4[32]=101, a4[33]=256
    {303, 256}, // a4[46]=303, a4[47]=256
};
const ColumnSlot kPersonColumnModeDefault[6] = {
    {202, 0},   // a4[4]=202,  a4[5]=0
    {101, 128}, // a4[18]=101, a4[19]=128
    {303, 128}, // a4[32]=303, a4[33]=128
    {0,   256}, // a4[46]=0,   a4[47]=256
    {202, 256}, // a4[60]=202, a4[61]=256
    {404, 256}, // a4[74]=404, a4[75]=256
};

std::vector<ColumnSlot> Hud_BuildPersonColumnSlots(int mode) {
    std::vector<ColumnSlot> out;
    if (mode == 1 || mode == 2 || mode == 3) {
        for (const auto& s : kPersonColumnMode123)
            out.push_back(s);
    } else if (mode == 7) {
        // none (v30 = 0)
    } else {
        for (const auto& s : kPersonColumnModeDefault)
            out.push_back(s);
    }
    return out;
}

// ---------------------------------------------------------------------------
// Building choice list — gilde.exe 0x5529f8.
//   row i: y = i << 7 (128*i) ; x = ((windowW - childW) >> 1).
// ---------------------------------------------------------------------------
std::vector<ChoiceRow> Hud_BuildBuildingChoiceRows(int count, int capacity,
                                                   int windowW, int childW) {
    std::vector<ChoiceRow> rows;
    int n = (count >= capacity) ? capacity : count; // v6 = min(count,capacity)
    if (n <= 0)
        return rows;
    int x = (windowW - childW) >> 1; // ((*(w+6)>>16) - v16) >> 1
    for (int i = 0; i < n; ++i) {
        int y = (i16)(i << 7); // (_WORD)v8 << 7
        rows.push_back({y, x});
    }
    return rows;
}

// ---------------------------------------------------------------------------
// Building price rows — gilde.exe 0x552b80.
//   row i: room sprite at (32, i<<6) ; value label at (128, i<<6) ;
//          worth label at (128, (i<<6) + 27).  roomGfx = code + 1010.
// ---------------------------------------------------------------------------
std::vector<PriceRow> Hud_BuildBuildingPriceRows(const std::vector<int>& roomCodes) {
    std::vector<PriceRow> rows;
    int i = 0; // v15
    for (int code : roomCodes) {
        int y = (i16)(i << 6);                 // (_WORD)v15 << 6
        int y2 = (i16)((i << 6) + kPriceRowLabel2DY); // ((v15<<6)+27)
        rows.push_back({32, y, y, y2, code + kPriceRowGfxBase});
        ++i;
    }
    return rows;
}

// ---------------------------------------------------------------------------
// Training row — gilde.exe 0x550a0c.
//   backdrop (0, 120*i) ; trainee gfx (13, 120*i + 5) ; child window (64, 120*i).
// ---------------------------------------------------------------------------
TrainingRow Hud_BuildTrainingRow(int index, int gfx) {
    int base = (i16)(120 * index); // v10 = 120*a1
    TrainingRow r{};
    r.backdropX = 0;  r.backdropY = base;
    r.traineeX  = 13; r.traineeY  = base + 5;
    r.childX    = 64; r.childY    = base;
    (void)gfx; // gfx is the trainee sprite id placed via the sink by the caller
    return r;
}

} // namespace guild::gui
