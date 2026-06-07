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
    // cell index = row + col*size, stride 24, type byte at +0.
    std::vector<guild::u8> cells(24 * size * size, 0);
    for (auto& rc : walk)      cells[24 * (rc.first + rc.second * size)] = 1;
    for (auto& rc : blocked13) cells[24 * (rc.first + rc.second * size)] = 13;
    return cells;
}
} // namespace

TEST(RenderTileQuery, WalkableSingleFar) {
    auto cells = MakeWalk(8, {{5, 6}}, {});
    TileWalkGrid g{}; g.size = 8; g.cells = cells.data();
    int fx = -99, fy = -99;
    bool ok = FindNearestWalkableTile(&g, /*centerY*/2, &fx, /*centerX*/2, &fy);
    CHECK(ok);
    CHECK_EQ(fx, 6);   // foundX == col
    CHECK_EQ(fy, 5);   // foundY == row
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
    auto cells = MakeWalk(8, {{2, 3}}, {{2, 2}});  // (row2,col2)=13, (row2,col3)=1
    TileWalkGrid g{}; g.size = 8; g.cells = cells.data();
    int fx = -99, fy = -99;
    bool ok = FindNearestWalkableTile(&g, /*centerY*/2, &fx, /*centerX*/2, &fy);
    CHECK(ok);
    CHECK_EQ(fx, 3);
    CHECK_EQ(fy, 2);
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
