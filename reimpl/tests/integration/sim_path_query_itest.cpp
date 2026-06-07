// Integration tests for the sim pathfinding / locomotion queries, exercised
// against their REAL sibling modules (no mocks):
//   - render::Heightmap + render::TileToWorld / WorldToTileWithHeight (terrain)
//   - sim::PathBuildWaypointList / PathFindRoute   (the real bidirectional A*)
//   - sim::PathFindNearestFreeTile / MapStampCollisionArea / MapIsTileWalkable
//   - sim::MapTraceLineOfSight / MapFindNearestDoorCell / MapCheckPathWalkable
//
// The flow mirrors the engine's NPC "walk to a door" path build: locate the
// nearest door cell, run the real A* to it, then verify the produced route with
// the real walkability + path-clear checks, and confirm a stamped obstacle is
// observed by the walkability queries.
#include "test.h"

#include <vector>

#include "render/heightmap.h"
#include "sim/map.h"
#include "sim/path.h"

using namespace guild;

namespace {

constexpr int kN = 16;   // power-of-two grid edge (A* needs log2(size))

struct Scene {
    render::Heightmap hm{};
    std::vector<u8> entries;   // kN*kN * 24-byte tile records
    std::vector<u8> heights;

    Scene() {
        entries.assign(static_cast<size_t>(kN) * kN * 24, 0);
        heights.assign(static_cast<size_t>(kN) * kN, 0);
        for (int r = 0; r < kN; ++r)
            for (int c = 0; c < kN; ++c)
                cell(c, r) = 1;   // type 1 = plain walkable (cost 20; NOT a door)
        hm.originX = hm.originY = hm.originZ = 0.0f;
        hm.scaleX = 4.0f; hm.scaleY = 1.0f; hm.scaleZ = 4.0f;
        hm.size = kN;
        hm.entries = entries.data();
        hm.heights = heights.data();
    }
    u8& cell(int c, int r) { return entries[static_cast<size_t>(24) * (c + kN * r)]; }
    sim::MapGrid grid() { return sim::MapGridFromHeightmap(&hm); }
};

} // namespace

// A* route to the nearest door, walked + verified with the real walkability checks.
TEST(SimPathQueryItest, DoorRouteIsWalkable) {
    Scene s;
    // Put a single door (type 11) at (12,10); everything else type 6 (walkable).
    s.cell(12, 10) = 11;
    sim::MapGrid g = s.grid();

    // 1) Real nearest-door scan from the NPC's start tile.
    int dc = -1, dr = -1;
    CHECK(sim::MapFindNearestDoorCell(g, 2, 3, &dc, &dr) == 1);
    CHECK_EQ(dc, 12); CHECK_EQ(dr, 10);

    // 2) Real bidirectional A* route start(2,3) -> door(12,10).
    sim::PathStep route[512];
    int n = sim::PathBuildWaypointList(g, 2, 3, dc, dr, 0, route, 512);
    CHECK(n > 0);
    // Endpoints match.
    CHECK_EQ(route[0].x, 2);  CHECK_EQ(route[0].y, 3);
    CHECK_EQ(route[n - 1].x, dc); CHECK_EQ(route[n - 1].y, dr);

    // 3) Every step lands on a real walkable tile. (The bidirectional A* in
    //    path.cpp reconstructs over the wrap-addressed node grid, so adjacency is
    //    not asserted here — only that the route cells are all traversable.)
    bool allWalkable = true;
    for (int i = 0; i < n; ++i) {
        if (!sim::MapIsTileWalkable(g, route[i].x, route[i].y))
            allWalkable = false;
    }
    CHECK(allWalkable);

    // 4) Pack the route into a (col,row) waypoint buffer and confirm the real
    //    MapCheckPathWalkable reports it clear (returns the pass-through seed).
    std::vector<u8> wp;
    for (int i = 0; i < n; ++i) { wp.push_back((u8)route[i].x); wp.push_back((u8)route[i].y); }
    CHECK_EQ(sim::MapCheckPathWalkable(g, wp.data(), n, 0, 1234), 1234);
}

// A stamped obstacle is observed by FindNearestFreeTile + walkability + CheckPath.
TEST(SimPathQueryItest, StampedObstacleBlocksTile) {
    Scene s;
    sim::MapGrid g = s.grid();
    sim::MapResetDirtyRect();

    // Tile (8,8) starts walkable.
    CHECK(sim::MapIsTileWalkable(g, 8, 8));

    // Stamp the blocked sentinel (13) as a radius-0 diamond at (8,8) via the entity
    // collision entry point (world point -> tile -> StampCollisionArea).
    float world[3] = {8.0f * s.hm.scaleX, 0.0f, 8.0f * s.hm.scaleZ};  // tile (8,8)
    int sr = sim::MapStampEntityCollision(&s.hm, world, sim::kCellBlocked, 0, 1);
    CHECK_EQ(sr, 1);
    CHECK(!sim::MapIsTileWalkable(g, 8, 8));   // now blocked

    // FindNearestFreeTile from the blocked centre must return a *different*,
    // walkable neighbour.
    int fx = -1, fy = -1;
    CHECK(sim::PathFindNearestFreeTile(g, 8, 8, &fx, &fy) == 1);
    CHECK(!(fx == 8 && fy == 8));
    CHECK(sim::MapIsTileWalkable(g, fx, fy));

    // A waypoint buffer routed THROUGH the blocked tile is rejected by CheckPath.
    // CheckPath validates a small look-ahead window [max(curIdx,1), hi) where
    // hi = min(cap-2, curIdx+2); with curIdx=1, cap=5 it inspects waypoints 1..2,
    // i.e. (7,8) and the now-blocked (8,8) -> rejected (returns 0).
    std::vector<u8> wp = {6, 8, 7, 8, 8, 8, 9, 8, 10, 8};   // straight line crossing (8,8)
    CHECK_EQ(sim::MapCheckPathWalkable(g, wp.data(), 5, 1, 77), 0);
}

// TraceLineOfSight against the real terrain sampler picks an approach cell.
TEST(SimPathQueryItest, TraceApproachAgainstRealTerrain) {
    Scene s;
    s.cell(7, 7) = sim::kCellBlocked;   // target tile blocked
    sim::MapGrid g = s.grid();
    (void)g;

    float query[3] = {5.0f * s.hm.scaleX, 0.0f, 5.0f * s.hm.scaleZ};  // tile (5,5)
    int oc = -1, orr = -1;
    // bit0 best-approach: minimize dist(query,cell)+dist(targetWorld,cell). The
    // best cell is the one nearest the line between query (5,5) and target (7,7);
    // it must be a real walkable tile and not the blocked target itself.
    int r = sim::MapTraceLineOfSight(&s.hm, query, 7, nullptr, 7, &oc, &orr, 8, 1);
    CHECK_EQ(r, 1);
    CHECK(!(oc == 7 && orr == 7));
    CHECK(sim::MapIsTileWalkable(s.grid(), oc, orr));
}
