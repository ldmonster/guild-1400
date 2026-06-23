#include "test.h"

#include "render/terrain_scan.h"
#include "render/terrain_tile_query.h"
#include "render/sun_state.h"

#include "sim/actionqueue.h"  // guild::sim::g_gameTick

#include <vector>

using namespace guild::render;

namespace {

// 4x4 height grid used for the ScanRow golden vectors (computed in Python).
const guild::u8 kHeights4[16] = {
    10, 20, 30, 40,
     5, 15, 25, 35,
    50, 60, 70, 80,
     1,  2,  3,  4,
};

TerrainHeightGrid MakeGrid4() {
    TerrainHeightGrid g{};
    g.width = 4;
    g.heights = kHeights4;
    return g;
}

} // namespace

// ---- ScanRowHeightRange golden vectors (oracle: Python reference) -----------

TEST(RenderTerrainScan, FullRow0) {
    TerrainHeightGrid g = MakeGrid4();
    int lo = 255, hi = 0;
    int r = ScanRowHeightRange(&g, &lo, 0, &hi, 3, 0);
    CHECK_EQ(r, 0);
    CHECK_EQ(lo, 10);
    CHECK_EQ(hi, 40);
}

TEST(RenderTerrainScan, SubsetRow2) {
    TerrainHeightGrid g = MakeGrid4();
    int lo = 255, hi = 0;
    int r = ScanRowHeightRange(&g, &lo, 1, &hi, 2, 2);  // cols 1..2 -> 60,70
    CHECK_EQ(r, 0);
    CHECK_EQ(lo, 60);
    CHECK_EQ(hi, 70);
}

TEST(RenderTerrainScan, SwappedSpan) {
    TerrainHeightGrid g = MakeGrid4();
    int lo = 255, hi = 0;
    int r = ScanRowHeightRange(&g, &lo, 3, &hi, 0, 1);  // row1 cols 0..3
    CHECK_EQ(r, 0);
    CHECK_EQ(lo, 5);
    CHECK_EQ(hi, 35);
}

TEST(RenderTerrainScan, RowOutOfRangeUntouched) {
    TerrainHeightGrid g = MakeGrid4();
    int lo = 255, hi = 0;
    int r = ScanRowHeightRange(&g, &lo, 0, &hi, 3, 9);
    CHECK_EQ(r, 1);
    CHECK_EQ(lo, 255);   // accumulators preserved
    CHECK_EQ(hi, 0);
}

TEST(RenderTerrainScan, SpanOffLeft) {
    TerrainHeightGrid g = MakeGrid4();
    int lo = 255, hi = 0;
    int r = ScanRowHeightRange(&g, &lo, -5, &hi, -1, 0);  // hi end < 0
    CHECK_EQ(r, 1);
    CHECK_EQ(lo, 255);
    CHECK_EQ(hi, 0);
}

TEST(RenderTerrainScan, SpanOffRight) {
    TerrainHeightGrid g = MakeGrid4();
    int lo = 255, hi = 0;
    int r = ScanRowHeightRange(&g, &lo, 4, &hi, 7, 0);  // lo end >= width
    CHECK_EQ(r, 1);
    CHECK_EQ(lo, 255);
    CHECK_EQ(hi, 0);
}

TEST(RenderTerrainScan, ClipLow) {
    TerrainHeightGrid g = MakeGrid4();
    int lo = 255, hi = 0;
    int r = ScanRowHeightRange(&g, &lo, -2, &hi, 1, 0);  // cols 0..1 -> 10,20
    CHECK_EQ(r, 0);
    CHECK_EQ(lo, 10);
    CHECK_EQ(hi, 20);
}

TEST(RenderTerrainScan, ClipHigh) {
    TerrainHeightGrid g = MakeGrid4();
    int lo = 255, hi = 0;
    int r = ScanRowHeightRange(&g, &lo, 2, &hi, 10, 0);  // cols 2..3 -> 30,40
    CHECK_EQ(r, 0);
    CHECK_EQ(lo, 30);
    CHECK_EQ(hi, 40);
}

TEST(RenderTerrainScan, AccumulateAcrossRows) {
    TerrainHeightGrid g = MakeGrid4();
    int lo = 255, hi = 0;
    ScanRowHeightRange(&g, &lo, 0, &hi, 3, 0);   // 10..40
    int r = ScanRowHeightRange(&g, &lo, 0, &hi, 3, 2);  // 50..80
    CHECK_EQ(r, 0);
    CHECK_EQ(lo, 10);
    CHECK_EQ(hi, 80);
}

// ---- LookupTileAttribute ----------------------------------------------------

TEST(RenderTileQuery, LookupHitAndMiss) {
    // bucketSize 4: tile (x,y) -> bucket (x/4, y/4). One run on row 3 covering
    // columns 2..5 with attribute 7, in bucket (0,0).
    static const TileAttrEntry kEntries[] = {
        // _link, row, xMin, xMax, attribute
        {0, 3, 2, 5, 7},
        {0, -1, 0, 0, 0},  // terminator (row < 0)
    };
    // Bucket-head array sized to cover slot index (200*by + 69 + 25*bx) - 2.
    // For bucket (0,0): slot = 69, index into buckets[] = 67.
    std::vector<const TileAttrEntry*> buckets(256, nullptr);
    buckets[69 - 2] = kEntries;

    TileAttributeGrid g{};
    g.size = 16;
    g.bucketSize = 4;
    g.buckets = buckets.data();

    // x in {2,3} share bucket (0,0) with the run; x in {4,5} fall in bucket (1,0)
    // (x/bucketSize == 1) whose head is null, so those query a different bucket.
    CHECK_EQ(LookupTileAttribute(&g, 2, 3), 7);   // run start, bucket 0
    CHECK_EQ(LookupTileAttribute(&g, 3, 3), 7);   // interior, bucket 0
    CHECK_EQ(LookupTileAttribute(&g, 1, 3), -1);  // before run, bucket 0
    CHECK_EQ(LookupTileAttribute(&g, 4, 3), -1);  // different bucket (1,0) -> miss
    CHECK_EQ(LookupTileAttribute(&g, 5, 3), -1);  // different bucket (1,0) -> miss
    CHECK_EQ(LookupTileAttribute(&g, 2, 2), -1);  // wrong row, bucket 0
    CHECK_EQ(LookupTileAttribute(&g, 0, 3), -1);  // x not strictly inside
    CHECK_EQ(LookupTileAttribute(&g, 3, 0), -1);  // y not strictly inside
    CHECK_EQ(LookupTileAttribute(&g, 16, 3), -1); // x >= size
}

TEST(RenderTileQuery, LookupChainsEntries) {
    // Two runs in bucket (0,0): the first does not cover the query column, so the
    // lookup must advance (entry += 20 / ++entry) to the second run.
    static const TileAttrEntry kEntries[] = {
        {0, 1, 1, 2, 9},   // row 1, cols 1..2 -> attr 9
        {0, 1, 5, 6, 11},  // row 1, cols 5..6 -> attr 11
        {0, -1, 0, 0, 0},  // terminator
    };
    std::vector<const TileAttrEntry*> buckets(256, nullptr);
    buckets[69 - 2] = kEntries;
    TileAttributeGrid g{};
    g.size = 16;
    g.bucketSize = 8;   // x in {1,2,5,6} all share bucket (0,0)
    g.buckets = buckets.data();

    CHECK_EQ(LookupTileAttribute(&g, 1, 1), 9);   // first run
    CHECK_EQ(LookupTileAttribute(&g, 2, 1), 9);
    CHECK_EQ(LookupTileAttribute(&g, 5, 1), 11);  // advanced to second run
    CHECK_EQ(LookupTileAttribute(&g, 6, 1), 11);
    CHECK_EQ(LookupTileAttribute(&g, 4, 1), -1);  // gap between runs
    CHECK_EQ(LookupTileAttribute(&g, 5, 2), -1);  // wrong row (none on row 2)
}

// ---- FindNearestWalkableTile (golden via Python reference) -------------------

namespace {
std::vector<guild::u8> MakeWalk(int size,
                                const std::vector<std::pair<int,int>>& walk,
                                const std::vector<std::pair<int,int>>& blocked13) {
    // gilde.exe @0x5c6550: cell index = col + row*size, stride 24, type byte at +0
    // (imul edx,row; add edx,col; imul 0x18). Pairs below are {col, row}.
    std::vector<guild::u8> cells(24 * size * size, 0);
    for (auto& cr : walk)      cells[24 * (cr.first + cr.second * size)] = 1;
    for (auto& cr : blocked13) cells[24 * (cr.first + cr.second * size)] = 13;
    return cells;
}
} // namespace

TEST(RenderTileQuery, WalkableSingleFar) {
    auto cells = MakeWalk(8, {{5, 6}}, {});   // walkable at col=5,row=6
    TileWalkGrid g{}; g.size = 8; g.cells = cells.data();
    int fx = -99, fy = -99;
    bool ok = FindNearestWalkableTile(&g, /*centerY*/2, &fx, /*centerX*/2, &fy);
    CHECK(ok);
    CHECK_EQ(fx, 5);   // foundX == col
    CHECK_EQ(fy, 6);   // foundY == row
}

TEST(RenderTileQuery, WalkableCenterImmediate) {
    auto cells = MakeWalk(8, {{3, 3}, {5, 6}}, {});
    TileWalkGrid g{}; g.size = 8; g.cells = cells.data();
    int fx = -99, fy = -99;
    bool ok = FindNearestWalkableTile(&g, 3, &fx, 3, &fy);
    CHECK(ok);
    CHECK_EQ(fx, 3);
    CHECK_EQ(fy, 3);
}

TEST(RenderTileQuery, WalkableNone) {
    auto cells = MakeWalk(8, {}, {});
    TileWalkGrid g{}; g.size = 8; g.cells = cells.data();
    int fx = -99, fy = -99;
    bool ok = FindNearestWalkableTile(&g, 4, &fx, 4, &fy);
    CHECK(!ok);
    CHECK_EQ(fx, -99);  // unchanged on failure
    CHECK_EQ(fy, -99);
}

TEST(RenderTileQuery, WalkableType13IsBlocked) {
    auto cells = MakeWalk(8, {{2, 3}}, {{2, 2}});  // walkable col2,row3; blocked col2,row2
    TileWalkGrid g{}; g.size = 8; g.cells = cells.data();
    int fx = -99, fy = -99;
    bool ok = FindNearestWalkableTile(&g, /*centerY*/2, &fx, /*centerX*/2, &fy);
    CHECK(ok);
    CHECK_EQ(fx, 2);   // foundX == col
    CHECK_EQ(fy, 3);   // foundY == row
}

TEST(RenderTileQuery, NullGuards) {
    int fx = 0, fy = 0;
    CHECK(!FindNearestWalkableTile(nullptr, 0, &fx, 0, &fy));
    TileWalkGrid g{}; g.size = 8; g.cells = nullptr;
    CHECK(!FindNearestWalkableTile(&g, 0, &fx, 0, &fy));
}

// ---- Sun state setters ------------------------------------------------------

TEST(RenderSunState, SetSunDirection) {
    guild::sim::g_gameTick = 12345;
    guild::i32 ret = SetSunDirection(0, 42);
    CHECK_EQ(ret, 12345);
    CHECK_EQ(g_sunDirState, -1);
    CHECK_EQ(g_sunDirStamp, 12345);
    CHECK_EQ(g_sunDirParam, 42);
}

TEST(RenderSunState, EnableSun) {
    guild::sim::g_gameTick = 777;
    guild::i32 ret = EnableSun();
    CHECK_EQ(ret, 777);
    CHECK_EQ(g_sunDirState, 1);
    CHECK_EQ(g_sunDirStamp, 777);
    CHECK_EQ(g_sunDirParam, 0);
}

TEST(RenderSunState, ResetGlobalState) {
    g_lightState54 = g_lightState58 = g_lightState5C = 1;
    g_lightState60 = g_lightState64 = 1;
    ResetGlobalState();
    CHECK_EQ(g_lightState54, 0);
    CHECK_EQ(g_lightState58, 0);
    CHECK_EQ(g_lightState5C, 0);
    CHECK_EQ(g_lightState60, 0);
    CHECK_EQ(g_lightState64, 0);
}

// =============================================================================
// WAVE-10 HARDENING: degenerate/edge memory-safety coverage for the tile-query
// diamond search (off-grid centre, full grid corner, border row/col, size 1) and
// the row-scan span normalisation at the row borders. Run under ASAN+UBSAN.
// =============================================================================

// FindNearestWalkableTile with the search centre OUTSIDE the grid: the ring scan
// must clamp every (row,col) it reads to [0,size) and never index cells[] OOB.
TEST(RenderTileQueryHardening, WalkableOffGridCenter) {
    auto cells = MakeWalk(8, {{0, 0}, {7, 7}}, {});
    TileWalkGrid g{}; g.size = 8; g.cells = cells.data();
    int fx = -99, fy = -99;
    // Centre way past the high corner: clamped scan still finds (7,7) (or nothing),
    // never reading outside the 8x8 grid.
    bool ok = FindNearestWalkableTile(&g, /*centerY*/20, &fx, /*centerX*/20, &fy);
    if (ok) { CHECK(fx >= 0 && fx < 8); CHECK(fy >= 0 && fy < 8); }
    // Centre below the low corner (negative): same — clamped, no OOB.
    fx = fy = -99;
    ok = FindNearestWalkableTile(&g, /*centerY*/-7, &fx, /*centerX*/-7, &fy);
    if (ok) { CHECK(fx >= 0 && fx < 8); CHECK(fy >= 0 && fy < 8); }
}

// Full walkable grid: the immediate centre is returned (ring 0), even at the border
// corner (0,0) and (size-1,size-1).
TEST(RenderTileQueryHardening, WalkableFullGridCorners) {
    const int n = 8;
    std::vector<guild::u8> cells(24 * n * n, 5);   // every cell walkable (type 5)
    TileWalkGrid g{}; g.size = n; g.cells = cells.data();
    int fx = -1, fy = -1;
    CHECK(FindNearestWalkableTile(&g, 0, &fx, 0, &fy));
    CHECK_EQ(fx, 0); CHECK_EQ(fy, 0);
    fx = fy = -1;
    CHECK(FindNearestWalkableTile(&g, n - 1, &fx, n - 1, &fy));
    CHECK(fx >= 0 && fx < n); CHECK(fy >= 0 && fy < n);
}

// Size-1 grid: the single cell is the only candidate; both walkable and blocked.
TEST(RenderTileQueryHardening, WalkableSizeOne) {
    std::vector<guild::u8> walk(24, 0); walk[0] = 1;       // the one cell walkable
    TileWalkGrid g{}; g.size = 1; g.cells = walk.data();
    int fx = -1, fy = -1;
    CHECK(FindNearestWalkableTile(&g, 0, &fx, 0, &fy));
    CHECK_EQ(fx, 0); CHECK_EQ(fy, 0);
    std::vector<guild::u8> empty(24, 0);                   // type 0 -> not walkable
    g.cells = empty.data();
    fx = fy = -7;
    CHECK(!FindNearestWalkableTile(&g, 0, &fx, 0, &fy));
}

// ScanRowHeightRange at the row borders: row 0, the last row, and a span straddling
// both edges. The span is normalised + clamped to [0,width-1]; no OOB read of the
// height grid, and a fully-clipped row leaves the seeded accumulators untouched.
TEST(RenderTileQueryHardening, ScanRowBorders) {
    const int w = 8;
    std::vector<guild::u8> hgrid((size_t)w * w);
    for (int r = 0; r < w; ++r)
        for (int x = 0; x < w; ++x)
            hgrid[(size_t)r * w + x] = (guild::u8)(r * 8 + x);
    TerrainHeightGrid grid{}; grid.width = w; grid.heights = hgrid.data();

    // Row 0, span straddling both edges [-3, 99] -> clamps to [0,7], folds all 8.
    int lo = 255, hi = 0;
    CHECK_EQ(ScanRowHeightRange(&grid, &lo, -3, &hi, 99, 0), 0);
    CHECK_EQ(lo, 0); CHECK_EQ(hi, 7);
    // Last row (w-1), single in-range column.
    lo = 255; hi = 0;
    CHECK_EQ(ScanRowHeightRange(&grid, &lo, 4, &hi, 4, w - 1), 0);
    CHECK_EQ(lo, (w - 1) * 8 + 4);
    CHECK_EQ(hi, (w - 1) * 8 + 4);
    // Row == width (out of range): early-out, accumulators untouched.
    lo = 200; hi = 5;
    CHECK_EQ(ScanRowHeightRange(&grid, &lo, 0, &hi, 7, w), 1);
    CHECK_EQ(lo, 200); CHECK_EQ(hi, 5);
}
