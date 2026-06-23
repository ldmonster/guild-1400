// Boundary / malformed hardening tests for the city population census:
//   world/city_population_census.{h,cpp}
//     VIBE_City_AggregateDistrictStats 0x578abc
//     VIBE_City_ComputeWealthGrid       0x577e74
//     VIBE_City_SnapshotStats           0x5783e4
//
// Focus (wave-12): out-of-range district tiles (the wealth-grid / aggregate
// resident bins are 8x8; a malformed tile must not corrupt the stack / index
// g_cityGrid out of bounds), full-grid (7,7) corner cells, empty inputs, and the
// snapshot block copy. Runs clean under ASan+UBSan; valid 0..7 tiles stay
// byte-identical (the guards only reject tiles outside the 8x8 district grid).
#include "test.h"

#include "world/city_population_census.h"
#include "world/city_satisfaction_grid.h"   // CityGridReset / GridSatCount
#include "world/city.h"                      // g_capDivisor

#include <cstring>
#include <vector>

using namespace guild;
using namespace guild::world;

// --- 1:1 GRID-STRIDE PINS (wave-14): the step-5 reduction in
// CityAggregateDistrictStats walks the grid with a 192-byte ROW stride, a 24-byte
// CELL stride and the SatWeight(+36)/SatDenom(+40)/SatCount(+44) field offsets,
// stopping the row loop at 1536 (== 8 * 192). Those literals MUST equal the named
// grid geometry constants; pin the identities so a drift on either side is caught.
TEST(CityCensusHarden, GridStrideConstantsMatchReductionLiterals) {
    CHECK_EQ(kCityGridRowBytes, 192);
    CHECK_EQ(kCityGridCellBytes, 24);
    CHECK_EQ(kCityGridRowBytes * 8, 1536);     // the v20 != 1536 row-loop bound
    CHECK_EQ(kCityGridCellBytes * 8, 192);     // 8 cells per row == one row stride
    CHECK_EQ(kGridOffSatWeight, 36);           // +36 read in the reduction
    CHECK_EQ(kGridOffSatDenom, 40);            // +40 clamp >= 1.0f
    CHECK_EQ(kGridOffSatCount, 44);            // +44 / (v1 * 2.0f)
}

// --- ComputeWealthGrid: corner tiles (0,0) and (7,7) accumulate in-bounds -----
TEST(CityCensusHarden, WealthGridCornerTiles) {
    std::vector<WealthResident> rs;
    auto mk = [](int x, int y, i32 w) {
        WealthResident r; r.hasScene = true; r.onMap = true;
        r.tileX = x; r.tileY = y; r.roomWorth = w; return r;
    };
    rs.push_back(mk(0, 0, 100));
    rs.push_back(mk(7, 7, 250));

    i32 out[64];
    std::memset(out, 0xCC, sizeof(out));
    CityComputeWealthGrid(rs, out);
    // out[8*x + y]: mean worth with one occupant == the worth itself (truncated).
    CHECK_EQ(out[8 * 0 + 0], 100);
    CHECK_EQ(out[8 * 7 + 7], 250);
    // An untouched cell becomes 0 (count == 0 branch writes 0).
    CHECK_EQ(out[8 * 3 + 4], 0);
}

// --- ComputeWealthGrid: out-of-range tiles are rejected (no stack OOB) ---------
// The worth/count accumulators are tightly-sized 8x8 stack arrays; a tile of 8
// (or larger / negative) would corrupt the stack. The guard drops them; valid
// residents in the same batch are still binned.
TEST(CityCensusHarden, WealthGridOutOfRangeTilesRejected) {
    std::vector<WealthResident> rs;
    auto mk = [](int x, int y, i32 w) {
        WealthResident r; r.hasScene = true; r.onMap = true;
        r.tileX = x; r.tileY = y; r.roomWorth = w; return r;
    };
    rs.push_back(mk(8, 8, 999));      // off the grid (one past)
    rs.push_back(mk(-1, 0, 999));     // negative
    rs.push_back(mk(100, 100, 999));  // far out
    rs.push_back(mk(2, 3, 77));       // valid -> binned

    i32 out[64];
    std::memset(out, 0, sizeof(out));
    CityComputeWealthGrid(rs, out);   // must not corrupt the stack / crash
    CHECK_EQ(out[8 * 2 + 3], 77);
}

// --- ComputeWealthGrid: unplaced / off-map residents skipped ------------------
TEST(CityCensusHarden, WealthGridSkipsUnplaced) {
    std::vector<WealthResident> rs;
    WealthResident a; a.hasScene = false; a.onMap = true; a.tileX = 1; a.tileY = 1; a.roomWorth = 50;
    WealthResident b; b.hasScene = true; b.onMap = false; b.tileX = 1; b.tileY = 1; b.roomWorth = 60;
    rs.push_back(a); rs.push_back(b);

    i32 out[64];
    std::memset(out, 0, sizeof(out));
    CityComputeWealthGrid(rs, out);
    CHECK_EQ(out[8 * 1 + 1], 0);   // both skipped -> cell stays 0
}

TEST(CityCensusHarden, WealthGridEmpty) {
    std::vector<WealthResident> rs;
    i32 out[64];
    std::memset(out, 0xAB, sizeof(out));
    CityComputeWealthGrid(rs, out);   // all cells -> 0 (count==0 branch)
    for (int i = 0; i < 64; ++i)
        CHECK_EQ(out[i], 0);
}

// --- SnapshotStats: copies the 300-byte block + four scalars + cap divisor -----
TEST(CityCensusHarden, SnapshotCopiesBlockAndScalars) {
    u8 src[300];
    for (int i = 0; i < 300; ++i) src[i] = static_cast<u8>(i & 0xFF);
    SnapshotScalars sc;
    sc.stat1234914 = 11; sc.stat1234918 = 22; sc.stat123491C = 33; sc.stat1234934 = 44;

    g_capDivisor = 12.9f;             // truncates toward zero -> 12

    u8 dst[300];
    std::memset(dst, 0, sizeof(dst));
    i32 outScalars[4] = {0};
    i32 div = CitySnapshotStats(src, sc, dst, outScalars);

    CHECK_EQ(std::memcmp(src, dst, 300), 0);   // block copied verbatim
    CHECK_EQ(outScalars[0], 11);
    CHECK_EQ(outScalars[1], 22);
    CHECK_EQ(outScalars[2], 33);
    CHECK_EQ(outScalars[3], 44);
    CHECK_EQ(div, 12);                          // (int)trunc(12.9)
}

// --- AggregateDistrictStats: out-of-range resident tiles do not index OOB ------
// The aggregate's loop-1 bins residents into g_cityGrid (flt_12349B4); a tile
// outside 0..7 must be rejected before the GridSatCount write. Valid tiles at the
// (7,7) corner are accepted.
TEST(CityCensusHarden, AggregateRejectsOutOfRangeResidentTiles) {
    CityGridReset();

    CityCensusInputs in;
    in.liveSlotCount = 4;
    in.useLiveLawTable = true;
    in.popFloor641DB8 = 1;

    auto mkRes = [](int col, int tx, int ty) {
        CensusResident r;
        r.hasObject = true; r.hasScene = true; r.onMap = true;
        r.typeByte = 1;       // eligible (not 0xB..0xD, not 0x10)
        r.column = col; r.tileX = tx; r.tileY = ty;
        return r;
    };
    in.residents.push_back(mkRes(0, 7, 7));     // valid corner
    in.residents.push_back(mkRes(0, 8, 8));     // out of range -> rejected
    in.residents.push_back(mkRes(1, 99, -3));   // wildly out -> rejected

    // One building so the loop-2 / clamp arithmetic has non-degenerate inputs.
    CensusBuilding b{};
    b.pop641DB0 = 10; b.word641DB2 = 2; b.word641DB4 = 4; b.word641DB6 = 1;
    b.security583 = 3; b.maxLevel584 = 5;
    in.buildings.push_back(b);

    CityStats out{};
    CityStats* r = CityAggregateDistrictStats(in, &out);  // must not OOB / crash
    CHECK(r == &out);
    CHECK_EQ(out.residents4, 4);
    // The valid (7,7) resident bumped its district count before the step-5
    // reduction normalises the SatCount field; the out-of-range residents were
    // rejected (no crash / OOB). The aggregate completed and wrote the stats.
    CHECK_EQ(out.popClamped8, 10);   // v3 = max(pop sum 10, 2*floor 2) = 10
    CHECK_EQ(out.v47sum7, 4);        // word_641DB4 sum
}

// --- AggregateDistrictStats: empty inputs (degenerate divisors) do not crash ---
// With liveSlotCount and building sums at zero the original produces inf/nan via
// double division (the engine's degenerate envelope) — we only assert it does not
// fault / index OOB and returns the out pointer.
TEST(CityCensusHarden, AggregateEmptyInputsNoCrash) {
    CityGridReset();
    CityCensusInputs in;
    in.liveSlotCount = 0;
    in.popFloor641DB8 = 0;
    CityStats out{};
    CityStats* r = CityAggregateDistrictStats(in, &out);
    CHECK(r == &out);
    CHECK_EQ(out.residents4, 0);
}
