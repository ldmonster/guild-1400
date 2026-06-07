// Golden-vector unit tests for the sim pathfinding / locomotion queries newly
// translated from gilde.exe:
//   VIBE_Map_TraceLineOfSight        0x406f10
//   VIBE_Path_FindNearestTileToPoint 0x4861d8
//   VIBE_Path_ResamplePolyline       0x406810
//   VIBE_Map_StampEntityCollision    0x404ef8
//   VIBE_Map_CheckPathWalkable       0x408740
//   VIBE_Map_FindNearestDoorCell     0x577320  (scan portion)
//
// The golden grid is an 8x8 (power-of-two) heightmap: scaleX=scaleZ=10, scaleY=1,
// all origins 0, all heights 0 (so TileToWorld(col,row) == (col*10, 0, row*10)).
// Cell types: 1 = walkable; 0 = empty(blocked); 13 = blocked; 6/11 = door.
// Expected values were precomputed with python3 mirroring the exact C++ logic.
#include "test.h"

#include <vector>

#include "render/heightmap.h"
#include "sim/map.h"
#include "sim/path.h"

using namespace guild;

namespace {

constexpr int kSize = 8;

// Build the golden 8x8 heightmap. Caller owns the backing buffers.
struct GoldenMap {
    render::Heightmap hm{};
    std::vector<guild::u8> entries;   // size*size * 24-byte tile records
    std::vector<guild::u8> heights;   // size*size elevation bytes (all 0)

    GoldenMap() {
        entries.assign(static_cast<size_t>(kSize) * kSize * 24, 0);
        heights.assign(static_cast<size_t>(kSize) * kSize, 0);
        // Default every cell walkable (type 1).
        for (int r = 0; r < kSize; ++r)
            for (int c = 0; c < kSize; ++c)
                setCell(c, r, 1);
        // Specials.
        setCell(0, 0, 0);    // empty
        setCell(3, 3, 13);   // blocked
        setCell(5, 2, 6);    // door (type 6)
        setCell(1, 6, 11);   // door (type 11)

        hm.originX = hm.originY = hm.originZ = 0.0f;
        hm.scaleX = 10.0f; hm.scaleY = 1.0f; hm.scaleZ = 10.0f;
        hm.size = kSize;
        hm.entries = entries.data();
        hm.heights = heights.data();
    }
    void setCell(int col, int row, guild::u8 type) {
        entries[static_cast<size_t>(24) * (col + kSize * row)] = type;
    }
    guild::u8 cell(int col, int row) const {
        return entries[static_cast<size_t>(24) * (col + kSize * row)];
    }
    sim::MapGrid grid() const { return sim::MapGridFromHeightmap(&hm); }
};

} // namespace

// ---------------------------------------------------------------------------
TEST(SimPathQuery, ResamplePolyline_Stride3) {
    // pts (0,0)..(9,9); scaleX=4 -> stride=trunc(12/4+0.5)=3, step 3.
    // Kept indices 1,4,7 then final (9,9) appended.  Expect (1,1)(4,4)(7,7)(9,9).
    std::vector<guild::u8> tiles;
    for (int k = 0; k < 10; ++k) { tiles.push_back((guild::u8)k); tiles.push_back((guild::u8)k); }
    sim::PathPolyline line{};
    line.count = 10;
    line.tiles = tiles.data();

    guild::u8 out[64] = {0};
    int n = sim::PathResamplePolyline(&line, out, 4.0f);
    CHECK_EQ(n, 4);
    CHECK_EQ((int)out[0], 1); CHECK_EQ((int)out[1], 1);
    CHECK_EQ((int)out[2], 4); CHECK_EQ((int)out[3], 4);
    CHECK_EQ((int)out[4], 7); CHECK_EQ((int)out[5], 7);
    CHECK_EQ((int)out[6], 9); CHECK_EQ((int)out[7], 9);
}

TEST(SimPathQuery, ResamplePolyline_Stride1) {
    // scaleX=12 -> stride=trunc(1.5)=1, step1. Indices 1..9 (9 pts), then since idx
    // ends at 10 != count-1(9) the last point (9,9) is appended again -> 10 total.
    std::vector<guild::u8> tiles;
    for (int k = 0; k < 10; ++k) { tiles.push_back((guild::u8)k); tiles.push_back((guild::u8)k); }
    sim::PathPolyline line{};
    line.count = 10;
    line.tiles = tiles.data();

    guild::u8 out[64] = {0};
    int n = sim::PathResamplePolyline(&line, out, 12.0f);
    CHECK_EQ(n, 10);
    CHECK_EQ((int)out[0], 1);             // first kept index
    CHECK_EQ((int)out[16], 9);            // 9th written == point (9,9)  (idx 9)
    CHECK_EQ((int)out[18], 9);            // appended last duplicate
}

TEST(SimPathQuery, FindNearestDoorCell) {
    GoldenMap m;
    sim::MapGrid g = m.grid();

    int dc = -1, dr = -1;
    CHECK(sim::MapFindNearestDoorCell(g, 4, 2, &dc, &dr) == 1);
    CHECK_EQ(dc, 5); CHECK_EQ(dr, 2);     // door (5,2), offset (0,1)

    dc = -1; dr = -1;
    CHECK(sim::MapFindNearestDoorCell(g, 0, 0, &dc, &dr) == 1);
    CHECK_EQ(dc, 5); CHECK_EQ(dr, 2);     // (5,2) dist29 < (1,6) dist37

    dc = -1; dr = -1;
    CHECK(sim::MapFindNearestDoorCell(g, 1, 6, &dc, &dr) == 1);
    CHECK_EQ(dc, 1); CHECK_EQ(dr, 6);     // the door under the start itself

    // Already-known door is kept (no scan).
    dc = 99; dr = 88;
    CHECK(sim::MapFindNearestDoorCell(g, 0, 0, &dc, &dr) == 1);
    CHECK_EQ(dc, 99); CHECK_EQ(dr, 88);
}

TEST(SimPathQuery, FindNearestDoorCell_NoDoors) {
    GoldenMap m;
    m.setCell(5, 2, 1);   // remove both doors
    m.setCell(1, 6, 1);
    sim::MapGrid g = m.grid();
    int dc = -1, dr = -1;
    CHECK_EQ(sim::MapFindNearestDoorCell(g, 0, 0, &dc, &dr), 0);
}

TEST(SimPathQuery, CheckPathWalkable) {
    GoldenMap m;
    sim::MapGrid g = m.grid();
    // Diagonal path (0,0),(1,1),...,(7,7); (3,3) is blocked.
    std::vector<guild::u8> wp;
    for (int k = 0; k < 8; ++k) { wp.push_back((guild::u8)k); wp.push_back((guild::u8)k); }

    // curIdx 0: only checks waypoint idx1 (hi = curIdx+2 = 2) -> clear -> seed.
    CHECK_EQ(sim::MapCheckPathWalkable(g, wp.data(), 8, 0, 42), 42);
    // curIdx 2: range [2,4) includes (3,3) blocked -> 0.
    CHECK_EQ(sim::MapCheckPathWalkable(g, wp.data(), 8, 2, 42), 0);
    // curIdx 4: range [4,6) all walkable -> seed.
    CHECK_EQ(sim::MapCheckPathWalkable(g, wp.data(), 8, 4, 42), 42);
    // cap < 2 short-circuits to seed.
    CHECK_EQ(sim::MapCheckPathWalkable(g, wp.data(), 1, 0, 7), 7);
}

TEST(SimPathQuery, TraceLineOfSight_FirstWalkable) {
    GoldenMap m;
    int oc = -1, orr = -1;
    // The original always reads the query world point (a2) unconditionally even on
    // the first-walkable path; supply a valid (unused) point.
    float query[3] = {30.0f, 0.0f, 30.0f};
    // flags=0 -> first-walkable spiral. Target (3,3) blocked; first walkable found
    // is (3,2) (ring1, rowLo=2).
    int r = sim::MapTraceLineOfSight(&m.hm, query, 3, nullptr, 3,
                                     &oc, &orr, 8, 0);
    CHECK_EQ(r, 1);
    CHECK_EQ(oc, 3); CHECK_EQ(orr, 2);
}

TEST(SimPathQuery, TraceLineOfSight_BestApproach) {
    GoldenMap m;
    float query[3] = {20.0f, 0.0f, 20.0f};   // tile (2,2)
    int oc = -1, orr = -1;
    // flags=1 -> best-approach: minimize dist(query,cell)+dist(targetWorld,cell).
    // Target (3,3) blocked; query cell (2,2) wins (d1=0).
    int r = sim::MapTraceLineOfSight(&m.hm, query, 3, nullptr, 3,
                                     &oc, &orr, 8, 1);
    CHECK_EQ(r, 1);
    CHECK_EQ(oc, 2); CHECK_EQ(orr, 2);
}

TEST(SimPathQuery, TraceLineOfSight_Bit4TargetWalkable) {
    GoldenMap m;
    int oc = -1, orr = -1;
    // bit4 set and target (4,4) walkable -> returns the target immediately.
    int r = sim::MapTraceLineOfSight(&m.hm, nullptr, 4, nullptr, 4,
                                     &oc, &orr, 8, 0x10);
    CHECK_EQ(r, 1);
    CHECK_EQ(oc, 4); CHECK_EQ(orr, 4);
}

TEST(SimPathQuery, FindNearestTileToPoint) {
    // Drive the algorithm with an identity bone-chain frame so the object world
    // point equals frame[19..21]. PointThroughBoneChain with a null parent link
    // (byte 504 == 0) yields out = point + frame[30..32]; keep frame[30..32]=0 so
    // out == frame[19..21]. We need a frame buffer large enough for byte 504+.
    std::vector<float> frame(200, 0.0f);
    frame[19] = 20.0f; frame[20] = 0.0f; frame[21] = 20.0f;   // object at tile (2,2)
    // Ensure the parent-link pointer at byte 504 (float 126) is null.
    GoldenMap m;

    int x = -1, y = -1;
    int r = sim::PathFindNearestTileToPoint(&m.hm, frame.data(), 0, &x, &y);
    CHECK_EQ(r, 1);
    CHECK_EQ(x, 5); CHECK_EQ(y, 1);   // nearest walkable tile > 30 units away

    x = -1; y = -1;
    r = sim::PathFindNearestTileToPoint(&m.hm, frame.data(), 1, &x, &y);
    CHECK_EQ(r, 1);
    CHECK_EQ(x, 7); CHECK_EQ(y, 7);   // farthest walkable tile
}

TEST(SimPathQuery, StampEntityCollision_ResolvesTileAndStamps) {
    GoldenMap m;
    float world[3] = {35.0f, 0.0f, 25.0f};   // -> tile (3,2)
    // Stamp value 7, radius 1, profile 0.
    int r = sim::MapStampEntityCollision(&m.hm, world, 7, 0, 1);
    CHECK_EQ(r, 1);                          // StampCollisionArea returned 1
    // Centre cell (3,2) is interior [1,size-2] so it was stamped to 7.
    CHECK_EQ((int)m.cell(3, 2), 7);

    // Off-map world point -> -1, nothing stamped.
    float off[3] = {1000.0f, 0.0f, 1000.0f};
    CHECK_EQ(sim::MapStampEntityCollision(&m.hm, off, 9, 0, 1), -1);

    // Null heightmap -> -1.
    CHECK_EQ(sim::MapStampEntityCollision(nullptr, world, 9, 0, 1), -1);
}
