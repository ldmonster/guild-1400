#include "test.h"

#include "sim/terrain_collision.h"
#include "render/terrain_scan.h"

#include <cstring>
#include <cmath>
#include <vector>

using namespace guild;
using namespace guild::sim;

namespace {

// Build a square grid with a programmable elevation function. width==height.
struct Grid {
    int width;
    std::vector<unsigned char> cells;
    TerrainGrid view{};
    Grid(int w, float ox, float oz, float sx, float sz, float hbase, float hscale)
        : width(w), cells(static_cast<size_t>(w) * w, 0) {
        view.width = w;
        view.heights = cells.data();
        view.originX = ox; view.originZ = oz;
        view.scaleX = sx;  view.scaleZ = sz;
        view.heightBase = hbase; view.heightScale = hscale;
    }
    unsigned char& at(int x, int row) { return cells[static_cast<size_t>(row) * width + x]; }
};

// A world-space vertex {x, _, z}.
struct V { float x, y, z; const float* p() const { return &x; } };

}  // namespace

// --------------------------------------------------------------------------
// ScanLineHeightRange: a single fully-in-grid triangle covering a flat region.
// --------------------------------------------------------------------------
TEST(TerrainScan, FlatTriangleFoldsConstantHeight) {
    Grid g(8, /*ox*/0, /*oz*/0, /*sx*/1, /*sz*/1, /*hbase*/100.0f, /*hscale*/2.0f);
    for (int r = 0; r < 8; ++r)
        for (int x = 0; x < 8; ++x)
            g.at(x, r) = 50;   // constant elevation byte

    V a{1, 0, 1}, b{5, 0, 1}, c{3, 0, 5};   // a flat-topped-ish triangle
    float lo = -1, hi = -1;
    int clipped = ScanLineHeightRange(&g.view, &lo, a.p(), &hi, b.p(), c.p());
    CHECK_EQ(clipped, 0);
    // world-Y = byte*hscale + hbase = 50*2 + 100 = 200 for both ends.
    CHECK(std::fabs(lo - 200.0f) < 1e-3f);
    CHECK(std::fabs(hi - 200.0f) < 1e-3f);
}

// --------------------------------------------------------------------------
// ScanLineHeightRange: a varying-elevation ridge gives a real [min,max] range.
// --------------------------------------------------------------------------
TEST(TerrainScan, VaryingHeightProducesRange) {
    Grid g(8, 0, 0, 1, 1, /*hbase*/0.0f, /*hscale*/1.0f);
    for (int r = 0; r < 8; ++r)
        for (int x = 0; x < 8; ++x)
            g.at(x, r) = static_cast<unsigned char>(10 + x * 5);  // 10..45 along x

    V a{0, 0, 1}, b{7, 0, 1}, c{3, 0, 5};
    float lo = -1, hi = -1;
    int clipped = ScanLineHeightRange(&g.view, &lo, a.p(), &hi, b.p(), c.p());
    CHECK_EQ(clipped, 0);
    // min byte over the covered columns is 10 (x=0), max is 45 (x=7).
    CHECK(std::fabs(lo - 10.0f) < 1e-3f);
    CHECK(std::fabs(hi - 45.0f) < 1e-3f);
}

// --------------------------------------------------------------------------
// ScanLineHeightRange: a triangle entirely off the grid returns 1 (clipped) and
// leaves the outputs untouched.
// --------------------------------------------------------------------------
TEST(TerrainScan, OffGridTriangleClips) {
    Grid g(8, 0, 0, 1, 1, 0, 1);
    V a{-50, 0, -50}, b{-40, 0, -50}, c{-45, 0, -40};
    float lo = 12345.0f, hi = 67890.0f;
    int clipped = ScanLineHeightRange(&g.view, &lo, a.p(), &hi, b.p(), c.p());
    CHECK_EQ(clipped, 1);
    CHECK(lo == 12345.0f);   // untouched
    CHECK(hi == 67890.0f);
}

// --------------------------------------------------------------------------
// ConvertX truncation: a vertex at world X=3.9 must map to column 3 (truncate,
// NOT round to 4). With a 1-unit cell and origin 0, column = trunc(3.9) = 3.
// --------------------------------------------------------------------------
TEST(TerrainScan, CoordTruncatesTowardZero) {
    Grid g(8, 0, 0, 1, 1, 0, 1);
    // Put a unique tall cell only at column 3 row 2; column 4 is low.
    for (int r = 0; r < 8; ++r) for (int x = 0; x < 8; ++x) g.at(x, r) = 1;
    g.at(3, 2) = 200;
    g.at(4, 2) = 1;
    // Degenerate single-row triangle spanning x in [3.9 .. 3.9] at row 2.
    V a{3.9f, 0, 2.0f}, b{3.9f, 0, 2.0f}, c{3.9f, 0, 2.0f};
    float lo = -1, hi = -1;
    int clipped = ScanLineHeightRange(&g.view, &lo, a.p(), &hi, b.p(), c.p());
    CHECK_EQ(clipped, 0);
    // trunc(3.9)=3 -> sees the tall cell 200, not column 4's low cell.
    CHECK(std::fabs(hi - 200.0f) < 1e-3f);
}

// --------------------------------------------------------------------------
// Degenerate single-row triangle: the original scans the FULL column span
// [min(Ax,Bx,Cx) .. max(Ax,Bx,Cx)] on that row (0x426f93 min nest + 0x426fd3
// max nest), NOT [min .. Ax]. Here the max column belongs to vertex B, not A,
// so an earlier "/*xEnd*/Ax" reading would have missed the tall cell at the B
// end. Evidence: disasm 0x426fd3..0x426ff7 builds eax = max(Ax,Bx,Cx) and
// pushes it raw as the ScanRowHeightRange xEnd arg at 0x427011.
// --------------------------------------------------------------------------
TEST(TerrainScan, DegenerateRowSpansFullColumnRange) {
    Grid g(8, 0, 0, 1, 1, 0, 1);
    for (int r = 0; r < 8; ++r) for (int x = 0; x < 8; ++x) g.at(x, r) = 1;
    g.at(5, 3) = 222;   // tall cell at the far (B) end of the span
    // All three at row 3; A.x=2 (min), B.x=6 (max), C.x=2. max column = 6 (B),
    // so the scanned span [2..6] includes column 5 -> the tall cell is folded.
    V a{2, 0, 3}, b{6, 0, 3}, c{2, 0, 3};
    float lo = -1, hi = -1;
    int clipped = ScanLineHeightRange(&g.view, &lo, a.p(), &hi, b.p(), c.p());
    CHECK_EQ(clipped, 0);
    CHECK(std::fabs(hi - 222.0f) < 1e-3f);   // would be 1.0 with the old xEnd=Ax
    CHECK(std::fabs(lo - 1.0f) < 1e-3f);
}

// --------------------------------------------------------------------------
// Flat-top triangle over multiple rows: the original steps BOTH the left (A->C,
// 0x427347) and right (B->C, 0x427357) edges every scanline. A version that
// only stepped the left edge would keep the right span pinned at Bx for every
// row, folding columns that the converging right edge should have excluded.
// Here the right edge converges from x=6 down toward x=3; a tall cell sits at
// (6, row 4) which the converged right edge must NOT reach.
// --------------------------------------------------------------------------
TEST(TerrainScan, FlatTopStepsBothEdges) {
    Grid g(10, 0, 0, 1, 1, 0, 1);
    for (int r = 0; r < 10; ++r) for (int x = 0; x < 10; ++x) g.at(x, r) = 5;
    g.at(5, 4) = 99;   // cell on a lower row, just outside the converged span
    // Flat top at row 1 (A,B), apex C below-right. Right edge B->C steps from
    // 6 toward 3: pre-stepped to 5.5, then 5.0 (row3), 4.5 (row4 -> trunc 4).
    // So row 4's right end is column 4 and column 5 is excluded -> max stays 5.
    // If xR were frozen (the old single-edge step), it would sit at 5.5 every
    // row, scan column 5, and fold the 99.
    V a{1, 0, 1}, b{6, 0, 1}, c{3, 0, 7};
    float lo = -1, hi = -1;
    int clipped = ScanLineHeightRange(&g.view, &lo, a.p(), &hi, b.p(), c.p());
    CHECK_EQ(clipped, 0);
    CHECK(std::fabs(hi - 5.0f) < 1e-3f);   // would fold the 99 if xR were not stepped
    CHECK(std::fabs(lo - 5.0f) < 1e-3f);
}

// --------------------------------------------------------------------------
// General triangle, lower part: the B->C edge is pre-stepped back one delta
// (0x427202 k -= dBC) and stepped at the loop BOTTOM (0x42725a), so row Br+1
// uses xBC == Bx (not Bx + dBC). A flat-elevation triangle simply must fold a
// real range and not clip — exercises the lower-edge loop end-to-end.
// --------------------------------------------------------------------------
TEST(TerrainScan, GeneralTriangleLowerEdgeWalks) {
    Grid g(12, 0, 0, 1, 1, 0, 1);
    for (int r = 0; r < 12; ++r) for (int x = 0; x < 12; ++x)
        g.at(x, r) = static_cast<unsigned char>(30 + r);   // 30..41 by row
    // A high, B mid-right, C low-left -> a genuine 3-distinct-row triangle.
    V a{2, 0, 1}, b{9, 0, 4}, c{4, 0, 9};
    float lo = -1, hi = -1;
    int clipped = ScanLineHeightRange(&g.view, &lo, a.p(), &hi, b.p(), c.p());
    CHECK_EQ(clipped, 0);
    // Rows covered run 1..9 -> byte min 31 (row 1), max 39 (row 9).
    CHECK(std::fabs(lo - 31.0f) < 1e-3f);
    CHECK(std::fabs(hi - 39.0f) < 1e-3f);
}

// --------------------------------------------------------------------------
// ScanSegmentHeightRange: a quad over a flat region merges both triangles.
// --------------------------------------------------------------------------
TEST(TerrainScan, SegmentMergesQuad) {
    Grid g(8, 0, 0, 1, 1, 0, 1);
    for (int r = 0; r < 8; ++r) for (int x = 0; x < 8; ++x)
        g.at(x, r) = static_cast<unsigned char>(20 + r);   // 20..27 by row

    V c0{1, 0, 1}, c1{5, 0, 1}, c2{5, 0, 5}, c3{1, 0, 5};
    float lo = -1, hi = -1;
    int clipped = ScanSegmentHeightRange(&g.view, &lo, c0.p(), &hi, c1.p(), c2.p(), c3.p());
    CHECK_EQ(clipped, 0);
    // rows 1..5 -> bytes 21..25.
    CHECK(std::fabs(lo - 21.0f) < 1e-3f);
    CHECK(std::fabs(hi - 25.0f) < 1e-3f);
}

// --------------------------------------------------------------------------
// ScanSegmentHeightRange: both triangles off-grid returns 1.
// --------------------------------------------------------------------------
TEST(TerrainScan, SegmentBothClipped) {
    Grid g(8, 0, 0, 1, 1, 0, 1);
    V c0{-9, 0, -9}, c1{-5, 0, -9}, c2{-5, 0, -5}, c3{-9, 0, -5};
    float lo = 1.0f, hi = 2.0f;
    int clipped = ScanSegmentHeightRange(&g.view, &lo, c0.p(), &hi, c1.p(), c2.p(), c3.p());
    CHECK_EQ(clipped, 1);
}

// --------------------------------------------------------------------------
// Reuse check: the new module's row primitive is the SAME render::ScanRowHeightRange
// reconstruction (no ODR fork). A direct call folds a known span.
// --------------------------------------------------------------------------
TEST(TerrainScan, RowPrimitiveReused) {
    guild::render::TerrainHeightGrid rv{};
    unsigned char cells[16];
    for (int i = 0; i < 16; ++i) cells[i] = static_cast<unsigned char>(i);
    rv.width = 4; rv.heights = cells;
    int lo = 255, hi = 0;
    int r = guild::render::ScanRowHeightRange(&rv, &lo, 0, &hi, 3, /*row*/1);
    CHECK_EQ(r, 0);
    CHECK_EQ(lo, 4);   // row 1 = cells[4..7] = 4,5,6,7
    CHECK_EQ(hi, 7);
}
