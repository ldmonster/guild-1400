// End-to-end test for the sim pathfinding flow: build a map with obstacles,
// stamp entity collisions, find a path across it, and verify the path against a
// hand-computed reference.
#include "tests/framework/test.h"

#include <cstring>
#include <vector>

#include "render/heightmap.h"
#include "sim/map.h"
#include "sim/path.h"

using namespace guild;

namespace {

sim::MapGrid MakeGrid(std::vector<u8>& storage, render::Heightmap& hm, int size,
                      u8 fill) {
    storage.assign(static_cast<size_t>(size) * size * sim::kTileEntryStride, 0);
    for (int i = 0; i < size * size; ++i)
        storage[sim::kTileEntryStride * i] = fill;
    std::memset(&hm, 0, sizeof(hm));
    hm.size = size;
    hm.entries = storage.data();
    sim::MapGrid g = sim::MapGridFromHeightmap(&hm);
    // Game-map invariant: impassable border ring (see sim_path_test helper).
    for (int x = 0; x < size; ++x) {
        sim::MapSetCellAt(g, x, 0, sim::kCellBlocked);
        sim::MapSetCellAt(g, x, size - 1, sim::kCellBlocked);
    }
    for (int y = 0; y < size; ++y) {
        sim::MapSetCellAt(g, 0, y, sim::kCellBlocked);
        sim::MapSetCellAt(g, size - 1, y, sim::kCellBlocked);
    }
    return g;
}

} // namespace

TEST(SimPathE2E, BuildStampPathVerify) {
    // 16x16 open field of walkable terrain (type 1).
    std::vector<u8> store;
    render::Heightmap hm;
    sim::MapGrid g = MakeGrid(store, hm, 16, /*fill*/ 1);
    sim::MapResetDirtyRect();

    // 1) Carve a static wall: a horizontal barrier at row 8, columns 0..12,
    //    leaving a gap at columns 13..15 so the bottom half is reachable from
    //    the top only by going around the right edge.
    for (int x = 0; x <= 12; ++x)
        sim::MapSetCellAt(g, x, 8, sim::kCellBlocked);

    // 2) Stamp two entity collisions (radius-2 Manhattan disks) as dynamic
    //    obstacles (radius 2 -> centre + 4 orthogonal neighbours).
    int s1 = sim::MapStampCollisionArea(g, 4, 3, sim::kCellBlocked, 2);
    int s2 = sim::MapStampCollisionArea(g, 9, 12, sim::kCellBlocked, 2);
    CHECK_EQ(s1, 1);
    CHECK_EQ(s2, 1);
    // The stamped centres are blocked.
    CHECK(!sim::MapIsTileWalkable(g, 4, 3));
    CHECK(!sim::MapIsTileWalkable(g, 9, 12));
    // And their orthogonal neighbours.
    CHECK(!sim::MapIsTileWalkable(g, 3, 3));
    CHECK(!sim::MapIsTileWalkable(g, 9, 11));

    // 3) Find a path from the top-left region (2,2) to the bottom (6,13).
    //    The barrier at row 8 forces a detour through the gap at x>=13.
    sim::PathStep steps[512];
    int n = sim::PathBuildWaypointList(g, 2, 2, 6, 13, 0, steps, 512);
    CHECK(n > 0);

    // Endpoints match.
    CHECK_EQ(steps[0].x, 2);
    CHECK_EQ(steps[0].y, 2);
    CHECK_EQ(steps[n - 1].x, 6);
    CHECK_EQ(steps[n - 1].y, 13);

    // Reference invariants on the path:
    //  - every step is walkable (no cell on a wall/stamped obstacle),
    //  - consecutive steps are 8-connected,
    //  - the path crosses row 8 only through the gap (column >= 13).
    bool crossedThroughGap = false;
    for (int i = 0; i < n; ++i) {
        CHECK(sim::MapCellAt(g, steps[i].x, steps[i].y) != sim::kCellBlocked);
        CHECK(sim::MapIsTileWalkable(g, steps[i].x, steps[i].y));
        if (steps[i].y == 8) {
            CHECK(steps[i].x >= 13);
            crossedThroughGap = true;
        }
    }
    CHECK(crossedThroughGap);

    for (int i = 0; i + 1 < n; ++i) {
        int dx = steps[i + 1].x - steps[i].x;
        int dy = steps[i + 1].y - steps[i].y;
        CHECK(dx >= -1 && dx <= 1);
        CHECK(dy >= -1 && dy <= 1);
        CHECK(dx != 0 || dy != 0);
    }

    // 4) Restore the dynamic obstacles via the shadow-buffer clear path, then
    //    confirm the previously stamped cells are walkable again.
    std::vector<u8> shadow(16 * 16, 1);  // pristine field (all type 1)
    // Rebuild the wall into the shadow so clearing only removes the stamps.
    for (int x = 0; x <= 12; ++x)
        shadow[8 * 16 + x] = sim::kCellBlocked;
    sim::MapClearCollisionRegion(g, shadow.data());
    CHECK(sim::MapIsTileWalkable(g, 4, 3));
    CHECK(sim::MapIsTileWalkable(g, 9, 12));
    // The static wall remains.
    CHECK(!sim::MapIsTileWalkable(g, 0, 8));
}

TEST(SimPathE2E, DirectPathNoObstacle) {
    std::vector<u8> store;
    render::Heightmap hm;
    sim::MapGrid g = MakeGrid(store, hm, 16, 1);

    sim::PathStep steps[256];
    int n = sim::PathBuildWaypointList(g, 1, 1, 1, 6, 0, steps, 256);
    CHECK(n > 0);
    CHECK_EQ(steps[0].x, 1);
    CHECK_EQ(steps[0].y, 1);
    CHECK_EQ(steps[n - 1].x, 1);
    CHECK_EQ(steps[n - 1].y, 6);
    // A straight vertical run on an open field should be at most a few steps
    // longer than the Manhattan distance (5).
    CHECK(n <= 8);
}
