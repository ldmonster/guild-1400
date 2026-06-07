// Unit tests for the sim pathfinding / map / interaction / object-search module.
#include "tests/framework/test.h"

#include <cstdlib>
#include <cstring>
#include <vector>

#include "render/heightmap.h"
#include "sim/map.h"
#include "sim/path.h"
#include "sim/interaction.h"
#include "sim/objectsearch.h"

using namespace guild;

namespace {

// Build a size*size grid of 24-byte tile entries. `cells[y*size+x]` gives the
// type byte for each cell. Returns a MapGrid view (and keeps storage alive via
// the supplied vector).
sim::MapGrid MakeGrid(std::vector<u8>& storage, render::Heightmap& hm,
                      int size, const std::vector<u8>& cells) {
    storage.assign(static_cast<size_t>(size) * size * sim::kTileEntryStride, 0);
    for (int y = 0; y < size; ++y)
        for (int x = 0; x < size; ++x)
            storage[sim::kTileEntryStride * (x + size * y)] = cells[y * size + x];
    std::memset(&hm, 0, sizeof(hm));
    hm.size = size;
    hm.entries = storage.data();
    return sim::MapGridFromHeightmap(&hm);
}

// Pathfinding requires the game-map invariant: the outermost ring of cells is
// impassable (the real maps always have a blocked border, which is why the A*
// frontier never expands a node whose neighbour index falls outside the grid).
// This helper stamps a blocked border (type 13) so interior searches stay safe.
void BlockBorder(const sim::MapGrid& g) {
    const int n = g.size;
    for (int x = 0; x < n; ++x) {
        sim::MapSetCellAt(g, x, 0, sim::kCellBlocked);
        sim::MapSetCellAt(g, x, n - 1, sim::kCellBlocked);
    }
    for (int y = 0; y < n; ++y) {
        sim::MapSetCellAt(g, 0, y, sim::kCellBlocked);
        sim::MapSetCellAt(g, n - 1, y, sim::kCellBlocked);
    }
}

} // namespace

// ---- Walkability ----------------------------------------------------------
TEST(SimPath, WalkabilityBasic) {
    // 4x4 grid: cell 1 = walkable terrain, 0 = empty (blocked), 13 = obstacle.
    std::vector<u8> cells = {
        1, 1, 0, 1,
        1, 13, 1, 1,
        1, 1, 1, 0,
        1, 1, 1, 1,
    };
    std::vector<u8> store;
    render::Heightmap hm;
    sim::MapGrid g = MakeGrid(store, hm, 4, cells);

    CHECK(sim::MapIsTileWalkable(g, 0, 0));   // type 1
    CHECK(!sim::MapIsTileWalkable(g, 2, 0));  // type 0 (empty)
    CHECK(!sim::MapIsTileWalkable(g, 1, 1));  // type 13 (obstacle)
    CHECK(sim::MapIsTileWalkable(g, 2, 1));   // type 1
    CHECK(!sim::MapIsTileWalkable(g, 3, 2));  // type 0

    // Out of bounds.
    CHECK(!sim::MapIsTileWalkable(g, -1, 0));
    CHECK(!sim::MapIsTileWalkable(g, 0, -1));
    CHECK(!sim::MapIsTileWalkable(g, 5, 0));
    // Edge case: x == size exactly -> samples seed (0) -> not walkable.
    CHECK(!sim::MapIsTileWalkable(g, 4, 0, 0));
    // x == size with nonzero seed -> walkable (matches original).
    CHECK(sim::MapIsTileWalkable(g, 4, 0, 1));
}

// ---- Collision stamping ---------------------------------------------------
TEST(SimPath, StampCollisionArea) {
    // 8x8 all-walkable grid (type 1).
    std::vector<u8> cells(64, 1);
    std::vector<u8> store;
    render::Heightmap hm;
    sim::MapGrid g = MakeGrid(store, hm, 8, cells);
    sim::MapResetDirtyRect();

    // Stamp obstacle (13) as a Manhattan disk centred at (4,4). The original's
    // ring counter runs v21 = 0 .. radius-1, so radius 2 yields the centre + its
    // 4 orthogonal neighbours (radius 1 stamps only the centre).
    int ok = sim::MapStampCollisionArea(g, 4, 4, 13, 2);
    CHECK_EQ(ok, 1);

    // The radius-2 Manhattan disk = centre + 4 orthogonal neighbours.
    CHECK_EQ(sim::MapCellAt(g, 4, 4), (u8)13);
    CHECK_EQ(sim::MapCellAt(g, 3, 4), (u8)13);
    CHECK_EQ(sim::MapCellAt(g, 5, 4), (u8)13);
    CHECK_EQ(sim::MapCellAt(g, 4, 3), (u8)13);
    CHECK_EQ(sim::MapCellAt(g, 4, 5), (u8)13);
    // Diagonal corners NOT in a radius-1 diamond.
    CHECK_EQ(sim::MapCellAt(g, 3, 3), (u8)1);
    CHECK_EQ(sim::MapCellAt(g, 5, 5), (u8)1);
    // Those stamped cells are now blocked.
    CHECK(!sim::MapIsTileWalkable(g, 4, 4));
    CHECK(!sim::MapIsTileWalkable(g, 3, 4));
    CHECK(sim::MapIsTileWalkable(g, 3, 3));

    // Dirty rect covers the stamped bbox.
    CHECK(sim::g_collisionDirtyMinX <= 3);
    CHECK(sim::g_collisionDirtyMaxX >= 5);
}

TEST(SimPath, BuildAndClearCollisionGrid) {
    std::vector<u8> cells(16, 1);
    cells[2 * 4 + 1] = 13;  // an obstacle at (1,2)
    std::vector<u8> store;
    render::Heightmap hm;
    sim::MapGrid g = MakeGrid(store, hm, 4, cells);
    sim::MapResetDirtyRect();

    std::vector<u8> shadow(16, 0);
    sim::MapBuildCollisionGrid(g, shadow.data());
    CHECK_EQ(shadow[2 * 4 + 1], (u8)13);
    CHECK_EQ(shadow[0], (u8)1);

    // Stamp over (0,0), then restore from the shadow.
    sim::MapStampCollisionArea(g, 1, 1, 5, 0);  // radius 0 -> no cells changed
    sim::MapSetCellAt(g, 0, 0, 9);
    sim::g_collisionDirtyMinX = 0;
    sim::g_collisionDirtyMaxX = 4;
    sim::g_collisionDirtyMinY = 0;
    sim::g_collisionDirtyMaxY = 4;
    sim::MapClearCollisionRegion(g, shadow.data());
    CHECK_EQ(sim::MapCellAt(g, 0, 0), (u8)1);  // restored
}

// ---- Pathfinding ----------------------------------------------------------
TEST(SimPath, FindStraightLine) {
    // 8x8 all walkable. Path from (1,1) to (5,1).
    std::vector<u8> cells(64, 1);
    std::vector<u8> store;
    render::Heightmap hm;
    sim::MapGrid g = MakeGrid(store, hm, 8, cells);
    BlockBorder(g);

    sim::PathStep steps[64];
    int n = sim::PathBuildWaypointList(g, 1, 1, 5, 1, 0, steps, 64);
    CHECK(n > 0);
    // First step is the start, last reaches the goal.
    CHECK_EQ(steps[0].x, 1);
    CHECK_EQ(steps[0].y, 1);
    CHECK_EQ(steps[n - 1].x, 5);
    CHECK_EQ(steps[n - 1].y, 1);
    // Every step is walkable and adjacent (8-connected) to the next.
    for (int i = 0; i < n; ++i)
        CHECK(sim::MapIsTileWalkable(g, steps[i].x, steps[i].y));
    for (int i = 0; i + 1 < n; ++i) {
        int dx = steps[i + 1].x - steps[i].x;
        int dy = steps[i + 1].y - steps[i].y;
        CHECK(dx >= -1 && dx <= 1 && dy >= -1 && dy <= 1 && (dx || dy));
    }
}

TEST(SimPath, FindAroundObstacle) {
    // 16x16 walkable (blocked border) with a vertical wall (type 13) at x=8 for
    // rows 1..11, leaving a gap at rows 12..14. Path from (4,4) to (12,4) must
    // detour down through the gap. (Grid edge must be a power of two: the node
    // index packs as (row<<log2(size))+col.)
    std::vector<u8> cells(256, 1);
    std::vector<u8> store;
    render::Heightmap hm;
    sim::MapGrid g = MakeGrid(store, hm, 16, cells);
    BlockBorder(g);
    for (int y = 1; y <= 11; ++y)
        sim::MapSetCellAt(g, 8, y, 13);

    sim::PathStep steps[512];
    int n = sim::PathBuildWaypointList(g, 4, 4, 12, 4, 0, steps, 512);
    CHECK(n > 0);
    CHECK_EQ(steps[0].x, 4);
    CHECK_EQ(steps[0].y, 4);
    CHECK_EQ(steps[n - 1].x, 12);
    CHECK_EQ(steps[n - 1].y, 4);
    // No step lands on the wall.
    for (int i = 0; i < n; ++i)
        CHECK(sim::MapCellAt(g, steps[i].x, steps[i].y) != 13);
}

TEST(SimPath, NoPathWhenWalled) {
    // 16x16: a full vertical wall at x=8 (all interior rows) -> unreachable.
    std::vector<u8> cells(256, 1);
    std::vector<u8> store;
    render::Heightmap hm;
    sim::MapGrid g = MakeGrid(store, hm, 16, cells);
    BlockBorder(g);
    for (int y = 1; y <= 14; ++y)
        sim::MapSetCellAt(g, 8, y, 13);

    sim::PathStep steps[512];
    int n = sim::PathBuildWaypointList(g, 2, 2, 12, 2, 0, steps, 512);
    CHECK_EQ(n, -1);
}

TEST(SimPath, NearestFreeTile) {
    // Centre blocked, ring around it free.
    std::vector<u8> cells(64, 1);
    cells[4 * 8 + 4] = 13;
    std::vector<u8> store;
    render::Heightmap hm;
    sim::MapGrid g = MakeGrid(store, hm, 8, cells);

    int ox = -1, oy = -1;
    int ok = sim::PathFindNearestFreeTile(g, 4, 4, &ox, &oy);
    CHECK_EQ(ok, 1);
    CHECK(sim::MapIsTileWalkable(g, ox, oy));
    // The nearest free tile is one ring out (Manhattan distance 1).
    CHECK(std::abs(ox - 4) + std::abs(oy - 4) <= 2);
}

// ---- Interaction resolution ----------------------------------------------
TEST(SimPath, ResolveHandlerToken) {
    // Build a descriptor table for one object type whose string is
    // "open|close|use". Field 1 -> "open", field 2 -> "close", etc.
    // The descriptor format terminates every extractable field with a '|', so a
    // 3-field descriptor is "open|close|use|" (the binary only extracts a token
    // that is followed by another '|'; a token at end-of-string is not copied).
    std::vector<u8> table(sim::kInteractionDescStride, 0);
    const char* desc = "open|close|use|";
    std::memcpy(table.data() + sim::kInteractionDescStringOffset, desc,
                std::strlen(desc) + 1);

    // Field 0 -> the head token "open". Fields >0 begin AT the N-th '|' and
    // include that leading separator, exactly as the binary returns them.
    char out[64];
    sim::InteractionResolveHandler(table.data(), 0, 0, out);
    CHECK_EQ(std::strcmp(out, "open"), 0);

    bool ok = sim::InteractionLookupToken(table.data(), 0, 1, out, sizeof(out));
    CHECK(ok);
    CHECK_EQ(std::strcmp(out, "|close"), 0);

    ok = sim::InteractionLookupToken(table.data(), 0, 2, out, sizeof(out));
    CHECK(ok);
    CHECK_EQ(std::strcmp(out, "|use"), 0);
}

TEST(SimPath, DispatchInteractionQueue) {
    sim::InteractionEvent q[3] = {
        {10, 20, 30, 40},
        {11, 21, 31, 41},
        {12, 22, 32, 42},
    };
    static std::vector<long> seen;
    seen.clear();
    struct H { static void fn(i32 a, i32 b, i32 c, i32 d, i32 ca, i32 cb) {
        seen.push_back(a);
        seen.push_back(b);
        seen.push_back(ca);
        seen.push_back(cb);
        (void)c; (void)d;
    } };
    sim::InteractionDispatch(q, 3, &H::fn, 99, 77);
    CHECK_EQ((int)seen.size(), 12);
    CHECK_EQ(seen[0], 10L);
    CHECK_EQ(seen[1], 20L);
    CHECK_EQ(seen[2], 99L);
    CHECK_EQ(seen[3], 77L);
    CHECK_EQ(seen[4], 11L);
}

// ---- Object search --------------------------------------------------------
TEST(SimPath, MatchEntityFilterOwnerMode) {
    // Build one 169-byte object: alive type 2, id 555, owner 7.
    std::vector<u8> rec(sim::kSearchObjectStride, 0);
    rec[sim::kObjAlive] = 2;
    i32 id = 555;
    std::memcpy(rec.data() + sim::kObjId, &id, 4);
    u16 owner = 7;
    std::memcpy(rec.data() + sim::kObjOwner, &owner, 2);

    sim::ObjectSearchContext ctx{};
    ctx.objectArray = nullptr;
    ctx.aiPlayerTable = nullptr;
    ctx.queryFaction = 7;
    ctx.extraFaction = 0xFFFF;

    // Owner-mode 4: owner == queryFaction.
    sim::EntityFilter f{};
    f.factionMask = 0;          // any faction
    f.ownerMode = 4;
    CHECK(sim::ObjectSearchMatchEntityFilter(ctx, &f, rec.data()));

    // Owner-mode 5: owner != queryFaction -> should fail (owner==7==query).
    f.ownerMode = 5;
    CHECK(!sim::ObjectSearchMatchEntityFilter(ctx, &f, rec.data()));

    // Owner-mode 1: owner == -1 -> fail (owner is 7).
    f.ownerMode = 1;
    CHECK(!sim::ObjectSearchMatchEntityFilter(ctx, &f, rec.data()));
}

TEST(SimPath, FindEntitiesByFilter) {
    // 256-slot array: place 3 alive objects owned by faction 7, the rest empty.
    std::vector<u8> arr(static_cast<size_t>(sim::kSearchObjectCapacity) *
                            sim::kSearchObjectStride,
                        0);
    auto put = [&](int slot, i32 id, u16 owner) {
        u8* r = arr.data() + sim::kSearchObjectStride * slot;
        r[sim::kObjAlive] = 1;
        std::memcpy(r + sim::kObjId, &id, 4);
        std::memcpy(r + sim::kObjOwner, &owner, 2);
    };
    put(10, 100, 7);
    put(50, 101, 7);
    put(200, 102, 3);  // different owner

    sim::ObjectSearchContext ctx{};
    ctx.objectArray = arr.data();
    ctx.aiPlayerTable = nullptr;
    ctx.queryFaction = 7;
    ctx.extraFaction = 0xFFFF;

    sim::EntityFilter f{};
    f.factionMask = 0;   // any faction
    f.ownerMode = 4;     // owner == 7

    i32 ids[16];
    int n = sim::ObjectSearchFindEntitiesByCount(ctx, &f, /*strideIndex*/ 0,
                                                 /*probeStart*/ 0, 16, ids);
    CHECK_EQ(n, 2);  // ids 100 and 101 (owner 7); 102 excluded
    bool saw100 = false, saw101 = false, saw102 = false;
    for (int i = 0; i < n; ++i) {
        if (ids[i] == 100) saw100 = true;
        if (ids[i] == 101) saw101 = true;
        if (ids[i] == 102) saw102 = true;
    }
    CHECK(saw100 && saw101 && !saw102);

    // FindNearestEntity returns one of the two matching ids.
    i32 one = -1;
    bool ok = sim::ObjectSearchFindNearestEntity(ctx, &f, 0, 0, &one);
    CHECK(ok);
    CHECK(one == 100 || one == 101);
}

TEST(SimPath, ObjectRingAdvance) {
    // With bias 1 the cursor advances one 12-byte slot per call, wrapping at
    // count. (Bias 0 would leave the cursor pinned — the rotation is the bias.)
    std::vector<u8> base(64, 0);
    int cursor = 0;
    sim::ObjectRing ring{base.data(), &cursor, 1, 4};
    int a = sim::ObjectRingAdvance(ring);
    CHECK_EQ(a, 0);   // returns the old cursor (slot 0)
    int b = sim::ObjectRingAdvance(ring);
    CHECK_EQ(b, 12);  // old cursor now at slot 1 (byte 12)
    int c = sim::ObjectRingAdvance(ring);
    CHECK_EQ(c, 24);  // slot 2
}
