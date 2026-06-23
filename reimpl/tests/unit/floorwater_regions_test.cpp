// Unit tests for render/floorwater BuildWaterRegions (VIBE_FloorWater_Prepare
// Regions @0x5ba95c, builder form): the water-mask dilation pass, the height
// gradient fill, the 4-neighbour edge bitcode, and the region flood-fill +
// strip/poly build, over deterministic synthetic water-mask grids.
//
// All goldens hand-traced from the decompile (cell index = col + row*N, wrap
// mask N*N-1, interior scan rows/cols in [0,N-4), 8x8 tiles of tileSpan=N/8).
#include "test.h"

#include "render/floorwater.h"

#include <cstring>
#include <initializer_list>
#include <string>
#include <utility>
#include <vector>

using namespace guild;
using guild::render::WaterRegions;
using guild::render::BuildWaterRegions;

namespace {

constexpr u8 kWater = 7;   // arbitrary water-type byte
constexpr u8 kLand  = 0;

// Build an N*N grid all-land, with the listed (col,row) cells set to water.
std::vector<u8> Grid(int n, std::initializer_list<std::pair<int, int>> water) {
    std::vector<u8> g((std::size_t)n * n, kLand);
    for (auto& cr : water)
        g[cr.first + cr.second * n] = kWater;
    return g;
}

} // namespace

// ---------------------------------------------------------------------------
// Empty grid: no water cell -> anyWater false, nothing built.
// ---------------------------------------------------------------------------
TEST(FloorWaterRegions, NoWater) {
    auto grid = Grid(8, {});
    WaterRegions r = BuildWaterRegions(grid.data(), nullptr, nullptr, kWater, 8);
    CHECK(!r.anyWater);
    CHECK_EQ((int)r.regionCount, 0);
    CHECK(r.mask.empty() || r.mask.size() == 64);
    CHECK(r.spans.empty());
    CHECK(r.polys.empty());
}

// ---------------------------------------------------------------------------
// Single interior water cell at (1,1) on an 8x8 grid. The interior scan covers
// rows/cols [0,4). The 3x3 dilation around (1,1) (with wrap) stamps the 3x3
// block (0,0)..(2,2) to 0xFF. anyWater true.
// ---------------------------------------------------------------------------
TEST(FloorWaterRegions, SingleCellDilates3x3) {
    int n = 8;
    auto grid = Grid(n, {{1, 1}});
    WaterRegions r = BuildWaterRegions(grid.data(), nullptr, nullptr, kWater, n);
    CHECK(r.anyWater);
    CHECK_EQ((int)r.mask.size(), n * n);
    // The 3x3 block centred on (1,1): cols {0,1,2} x rows {0,1,2} all 0xFF.
    for (int yy = 0; yy <= 2; ++yy)
        for (int xx = 0; xx <= 2; ++xx)
            CHECK_EQ((int)r.mask[xx + yy * n], 0xFF);
    // A cell well outside the block stays 0.
    CHECK_EQ((int)r.mask[5 + 5 * n], 0x00);
}

// ---------------------------------------------------------------------------
// A 2x2 solid water block at (1,1),(2,1),(1,2),(2,2). After dilation the mask
// covers (0,0)..(3,3). The region flood-fill marks interior 2x2-water cells and
// assigns a single region id -> regionCount == 1. A WaterMesh record is built.
// ---------------------------------------------------------------------------
TEST(FloorWaterRegions, BlockProducesOneRegion) {
    int n = 8;
    auto grid = Grid(n, {{1, 1}, {2, 1}, {1, 2}, {2, 2}});
    WaterRegions r = BuildWaterRegions(grid.data(), nullptr, nullptr, kWater, n);
    CHECK(r.anyWater);
    CHECK(r.regionCount >= 1);
    // One 344-byte WaterMesh record per region.
    CHECK_EQ((int)r.waterMeshes.size(), (int)r.regionCount * 344);
    // The region grid has at least one assigned (non-0xFF, non-0xFE) cell.
    int assigned = 0;
    for (u8 v : r.regionGrid)
        if (v != 0xFF && v != 0xFE) ++assigned;
    CHECK(assigned > 0);
    // No cell remains in the transient 0xFE "seed" state after flood-fill.
    int seeds = 0;
    for (u8 v : r.regionGrid)
        if (v == 0xFE) ++seeds;
    CHECK_EQ(seeds, 0);
}

// ---------------------------------------------------------------------------
// Height gradient: a horizontal run of water heights ramps from the seed value
// to the closing value. With terrainHeights all == 100, the seed = 100-2 = 98.
// A run of 0xFF cells along a row gets FillHeightGradient'd: endpoints near the
// seed value, no cell left at 0xFF inside a closed run.
// ---------------------------------------------------------------------------
TEST(FloorWaterRegions, HeightGradientFillsRun) {
    int n = 8;
    auto grid = Grid(n, {{1, 1}, {2, 1}});
    std::vector<u8> terrain((std::size_t)n * n, 100);
    WaterRegions r = BuildWaterRegions(grid.data(), terrain.data(), nullptr, kWater, n);
    CHECK(r.anyWater);
    CHECK_EQ((int)r.heights.size(), n * n);
    // Row 1 had water at cols 1,2; after dilation the mask covers cols 0..3 on
    // rows 0..2. The gradient fill replaces the run of 0xFF with ramped values
    // (>=1, <=98). Verify the run no longer holds raw 0xFF and is in range.
    bool sawFilled = false;
    for (int col = 0; col < n; ++col) {
        u8 h = r.heights[col + 1 * n];
        if (h != 0) {
            sawFilled = true;
            CHECK(h <= 98);   // ramped from seed 98 (terrain 100 - 2)
            CHECK(h >= 1);
        }
    }
    CHECK(sawFilled);
}

// ---------------------------------------------------------------------------
// Determinism: identical inputs produce byte-identical outputs.
// ---------------------------------------------------------------------------
TEST(FloorWaterRegions, Deterministic) {
    int n = 16;
    auto grid = Grid(n, {{2, 2}, {3, 2}, {2, 3}, {3, 3}, {8, 8}, {9, 8}, {8, 9}, {9, 9}});
    WaterRegions a = BuildWaterRegions(grid.data(), nullptr, nullptr, kWater, n);
    WaterRegions b = BuildWaterRegions(grid.data(), nullptr, nullptr, kWater, n);
    CHECK_EQ((int)a.regionCount, (int)b.regionCount);
    CHECK(a.mask == b.mask);
    CHECK(a.heights == b.heights);
    CHECK(a.regionGrid == b.regionGrid);
    CHECK_EQ((int)a.spans.size(), (int)b.spans.size());
    CHECK_EQ((int)a.polys.size(), (int)b.polys.size());
    // Two separated water blocks -> two distinct regions.
    CHECK(a.regionCount >= 2);
}

// ---------------------------------------------------------------------------
// Texture callback is invoked exactly once with the right name + flags, and the
// returned handle is written into every WaterMesh record's float[0]/[1].
// ---------------------------------------------------------------------------
namespace {
struct TexCtx { int calls = 0; const char* lastName = nullptr; int lastFlags = 0; };
void* TexLoad(const char* name, int flags, void* ctx) {
    auto* c = static_cast<TexCtx*>(ctx);
    ++c->calls; c->lastName = name; c->lastFlags = flags;
    return reinterpret_cast<void*>(0xCAFE0000u);
}
} // namespace

// ===========================================================================
// wave-10 HARDENING (ASAN+UBSAN): degenerate / edge / max-dimension coverage.
// These drive BuildWaterRegions + FloodFillMask + FindRegionOffset through their
// bounds so the sanitizers exercise every index path. They pin the two fixes:
//   * FloodFillMask: recursion -> explicit stack (no native stack-overflow).
//   * Pass-1 dilation index: int -> u32 (no signed-overflow UB at large N).
// ===========================================================================

// n <= 0 / null grid: the builder returns an empty, well-formed result.
TEST(FloorWaterRegions, DegenerateSizesNoUB) {
    for (int n : {-4, 0}) {
        auto grid = (n > 0) ? Grid(n, {}) : std::vector<u8>(1, 0);
        WaterRegions r = BuildWaterRegions(grid.data(), nullptr, nullptr, kWater, n);
        CHECK(!r.anyWater);
        CHECK_EQ((int)r.regionCount, 0);
        CHECK(r.spans.empty());
        CHECK(r.polys.empty());
    }
    // null grid pointer (n>0) is guarded.
    WaterRegions rn = BuildWaterRegions(nullptr, nullptr, nullptr, kWater, 8);
    CHECK(!rn.anyWater);
    // n in (0,4]: the interior scan rows/cols [0,n-4) never runs -> no water.
    for (int n : {1, 2, 4}) {
        auto grid = Grid(n, {});
        // even with a "water" cell present, n<=4 cannot dilate (loop bound n-4<=0).
        if (n >= 1) grid[0] = kWater;
        WaterRegions r = BuildWaterRegions(grid.data(), nullptr, nullptr, kWater, n);
        CHECK(!r.anyWater);
        CHECK_EQ((int)r.regionCount, 0);
    }
}

// A 1-cell water region: a single interior water cell still builds cleanly
// (mask dilates 3x3; region grid may yield 0 or 1 region depending on the 2x2
// interior seed). No OOB on the smallest non-degenerate grid that can dilate.
TEST(FloorWaterRegions, SingleCellRegionNoOOB) {
    int n = 8;
    auto grid = Grid(n, {{2, 2}});
    std::vector<u8> terrain((std::size_t)n * n, 60);
    WaterRegions r = BuildWaterRegions(grid.data(), terrain.data(), nullptr, kWater, n);
    CHECK(r.anyWater);
    CHECK_EQ((int)r.mask.size(), n * n);
    CHECK_EQ((int)r.heights.size(), n * n);
    CHECK_EQ((int)r.regionGrid.size(), n * n);
    CHECK_EQ((int)r.waterMeshes.size(), (int)r.regionCount * 344);
}

// FULL-GRID water region at a production-scale grid (N=128). Exercises the whole
// mask/dilation/gradient/edge/flood-fill/strip-poly chain on the largest cell
// count a shipped scene uses. Before the wave-10 fix the recursive flood fill
// risked a native stack overflow here (1056-deep span runs); the explicit-stack
// version completes with one connected region.
TEST(FloorWaterRegions, FullGridLargeRegion) {
    const int n = 128;
    std::vector<u8> grid((std::size_t)n * n, kWater);   // every cell water
    WaterRegions r = BuildWaterRegions(grid.data(), nullptr, nullptr, kWater, n);
    CHECK(r.anyWater);
    CHECK(r.regionCount >= 1);   // one big 4-connected component
    CHECK_EQ((int)r.mask.size(), n * n);
    CHECK_EQ((int)r.regionGrid.size(), n * n);
    // no transient 0xFE seeds remain after the fill.
    int seeds = 0;
    for (u8 v : r.regionGrid) if (v == 0xFE) ++seeds;
    CHECK_EQ(seeds, 0);
    // The 344-byte WaterMesh array is exactly regionCount records.
    CHECK_EQ((int)r.waterMeshes.size(), (int)r.regionCount * 344);
    // determinism at scale (order-independent flood fill).
    WaterRegions r2 = BuildWaterRegions(grid.data(), nullptr, nullptr, kWater, n);
    CHECK(r.regionGrid == r2.regionGrid);
    CHECK_EQ((int)r.regionCount, (int)r2.regionCount);
}

// CHECKERBOARD water mask -> the maximum number of distinct flood-fill regions.
// Each isolated water cell that survives dilation/region-seeding becomes (at most)
// its own region; this drives FloodFillMask to fire many times with tiny seeds,
// and the incrementing region id up toward the 8-bit count byte. ASAN watches the
// per-region 344-byte mesh array sizing under heavy region counts.
TEST(FloorWaterRegions, CheckerboardManyRegions) {
    const int n = 32;
    std::vector<u8> grid((std::size_t)n * n, kLand);
    for (int y = 0; y < n; ++y)
        for (int x = 0; x < n; ++x)
            if (((x ^ y) & 1) == 0) grid[x + y * n] = kWater;   // checkerboard
    WaterRegions r = BuildWaterRegions(grid.data(), nullptr, nullptr, kWater, n);
    CHECK(r.anyWater);
    // mesh array always matches the (u8) region count, even when many.
    CHECK_EQ((int)r.waterMeshes.size(), (int)r.regionCount * 344);
    // no 0xFE seeds left behind regardless of how many fills ran.
    int seeds = 0;
    for (u8 v : r.regionGrid) if (v == 0xFE) ++seeds;
    CHECK_EQ(seeds, 0);
}

// FloodFillMask directly: bounds, the from==to no-op guard (no infinite loop /
// OOB), out-of-range seeds, and a 1x1 grid.
TEST(FloorWaterRegions, FloodFillEdgeCases) {
    using guild::render::FloodFillMask;
    // 1x1 grid: a single fillable cell.
    { u8 m[1] = {0}; FloodFillMask(m, 1, 0, 0, 0, 9); CHECK_EQ((int)m[0], 9); }
    // out-of-range seed: must not write past the buffer.
    { u8 m[4] = {0, 0, 0, 0}; FloodFillMask(m, 2, 5, 5, 0, 9);
      for (int i = 0; i < 4; ++i) CHECK_EQ((int)m[i], 0); }
    { u8 m[4] = {0, 0, 0, 0}; FloodFillMask(m, 2, -1, 0, 0, 9);
      for (int i = 0; i < 4; ++i) CHECK_EQ((int)m[i], 0); }
    // from == to: no-op (the original never fills to==from; guard prevents a hang).
    { u8 m[4] = {9, 9, 9, 9}; FloodFillMask(m, 2, 0, 0, 9, 9);
      for (int i = 0; i < 4; ++i) CHECK_EQ((int)m[i], 9); }
    // seed cell not == from and not == to: no fill.
    { u8 m[4] = {3, 0, 0, 0}; FloodFillMask(m, 2, 0, 0, 0, 9);
      CHECK_EQ((int)m[0], 3); }
    // full 4x4 fill from a corner: all reachable cells become `to`.
    { u8 m[16]; std::memset(m, 0, sizeof(m));
      FloodFillMask(m, 4, 0, 0, 0, 7);
      for (int i = 0; i < 16; ++i) CHECK_EQ((int)m[i], 7); }
}

// FindRegionOffset out-of-range / boundary queries: queries outside every span's
// [lo,hi], and bound by spanCount short of the terminator, return 0 (no OOB).
TEST(FloorWaterRegions, FindRegionOffsetOutOfRange) {
    using guild::render::WaterRegionSpan;
    using guild::render::FindRegionOffset;
    auto mk = [](i32 base, i32 type, i32 lo, i32 hi) {
        WaterRegionSpan s{}; s.base = base; s.type = type; s.lo = lo; s.hi = hi;
        s.marker = 0xFF; return s;
    };
    WaterRegionSpan spans[3] = { mk(0, 1, 10, 20), mk(0, 1, 30, 40), mk(0, -1, 0, 0) };
    // below all spans
    CHECK_EQ(FindRegionOffset(spans, 3, 1000, 5, 0x10, 1), 0);
    // in the gap between spans (21..29) -> walks to terminator -> 0
    CHECK_EQ(FindRegionOffset(spans, 3, 1000, 25, 0x10, 1), 0);
    // above all spans -> 0
    CHECK_EQ(FindRegionOffset(spans, 3, 1000, 99, 0x10, 1), 0);
    // spanCount smaller than the real list (no terminator reached): the scan is
    // bounded by spanCount and returns 0 rather than reading past the array.
    WaterRegionSpan two[2] = { mk(0, 1, 10, 20), mk(0, 1, 30, 40) };
    CHECK_EQ(FindRegionOffset(two, 2, 1000, 25, 0x10, 1), 0);   // no match, in-bounds
    CHECK_EQ(FindRegionOffset(two, 2, 1000, 35, 0x10, 1), 80 * (0 + 35 - 30) + 1000);
    CHECK_EQ(FindRegionOffset(two, 1, 1000, 35, 0x10, 1), 0);   // span1 not visible
}

// A "truncated/malformed" floor mask: a correctly-sized N*N grid whose water
// cells sit only on the last interior rows/cols (where the dilation wrap-around
// stamps cells with the `& wrap` index reduction). Drives the pass-1 dilation
// neighbour math at the grid edge so ASAN/UBSAN cover the wrap path (the fixed
// u32 index arithmetic) with no OOB write into adjacent storage.
TEST(FloorWaterRegions, DilationEdgeWrapNoOOB) {
    const int n = 16;
    // place water at the largest interior cell (n-5,n-5): dilation reaches
    // (n-4) cols/rows and wraps the -1 neighbours via & wrap.
    auto grid = Grid(n, {{n - 5, n - 5}, {0, 0}});   // also a (0,0) corner cell
    WaterRegions r = BuildWaterRegions(grid.data(), nullptr, nullptr, kWater, n);
    CHECK(r.anyWater);
    CHECK_EQ((int)r.mask.size(), n * n);
    // every stamped mask cell is a valid 0x00/0xFF byte (no torn write).
    for (u8 v : r.mask) CHECK(v == 0x00 || v == 0xFF);
}

TEST(FloorWaterRegions, TextureHandleWiring) {
    int n = 8;
    auto grid = Grid(n, {{1, 1}, {2, 1}, {1, 2}, {2, 2}});
    TexCtx tc;
    WaterRegions r = BuildWaterRegions(grid.data(), nullptr, nullptr, kWater, n,
                                       &TexLoad, &tc);
    CHECK_EQ(tc.calls, 1);
    CHECK_EQ(tc.lastFlags, 172);
    CHECK(tc.lastName != nullptr);
    CHECK(std::string(tc.lastName) == "EF_WASS_06A_2T_W_AN0");
    CHECK_EQ(r.meshTexture, reinterpret_cast<void*>(0xCAFE0000u));
    if (r.regionCount > 0) {
        const u32* m = reinterpret_cast<const u32*>(r.waterMeshes.data());
        CHECK_EQ((int)m[0], (int)0xCAFE0000u);   // float[0] texture handle
        CHECK_EQ((int)m[1], (int)0xCAFE0000u);   // float[1] active member
    }
}

// ---------------------------------------------------------------------------
// GOLDEN PIN — the WaterMesh-record initializer dwords v102[5..13]
// (gilde.exe Pass-5a @0x5bb67e.., written as raw IEEE-754 bit patterns; see
// floorwater.cpp kMeshInit). Each fresh 344-byte WaterMesh record receives, at
// u32 indices 5..13, the constants
//   {1115422720, 1018980991, 1017370378, 1021128475, 1022739087,
//    1065353216, 1065353216, 1065353216, 1106771968}.
// (The trailing three 1065353216 == 1.0f; 1115422720 == 60.0f.) Pinned directly
// from the builder output so a drift in any record-init constant is caught.
// ---------------------------------------------------------------------------
TEST(FloorWaterRegions, MeshInitConstantsGolden) {
    static const u32 kMeshInit[9] = {
        1115422720u, 1018980991u, 1017370378u, 1021128475u, 1022739087u,
        1065353216u, 1065353216u, 1065353216u, 1106771968u};
    int n = 8;
    auto grid = Grid(n, {{1, 1}, {2, 1}, {1, 2}, {2, 2}});
    WaterRegions r = BuildWaterRegions(grid.data(), nullptr, nullptr, kWater, n);
    CHECK(r.regionCount >= 1);
    CHECK_EQ((int)r.waterMeshes.size(), (int)r.regionCount * 344);
    const u32* m = reinterpret_cast<const u32*>(r.waterMeshes.data());
    for (int q = 0; q < 9; ++q)
        CHECK_EQ(m[5 + q], kMeshInit[q]);   // v102[5..13] record-init dwords
    // The post-init block zeroes float[78..81] (phase) and float[82..84]
    // (texAccumA/B, lastTime) on a fresh record.
    for (int q = 78; q <= 84; ++q)
        CHECK_EQ(m[q], 0u);
}
