#include "render/scene_recon5_rebuild.h"
#include "test.h"
#include <vector>

using namespace guild::render;

static std::vector<OctreeCell*> g_freed;
static int g_recollect = 0;
static guild::u16 g_recollectMask = 0;

static void FreeChild(OctreeCell* c, const SceneCellPools&) { g_freed.push_back(c); }
static void Recollect(OctreeCell* cell, guild::u16 mask, const SceneWalkEnv&) {
    ++g_recollect;
    g_recollectMask = mask;
    // simulate the live re-collect setting the mask
    (void)cell;
}

// Null cell -> returns the (null) cell unchanged, no hooks fired.
TEST(SceneRecon5, RebuildNullCell) {
    g_freed.clear(); g_recollect = 0;
    SceneCellPools pools{};
    OctreeBuildHooks bh{};
    OctreeRebuildHooks rh{&FreeChild, &Recollect};
    SceneWalkEnv env{};
    OctreeCell* r = RebuildRegionOctree(nullptr, 0, 4, pools, bh, rh, env);
    CHECK(r == nullptr);
    CHECK_EQ((int)g_freed.size(), 0);
    CHECK_EQ(g_recollect, 0);
}

// Non-null cell: frees its octant children, sets mask bit 9, re-collects,
// then subdivides (empty cell -> SubdivideOctree returns the cell itself).
TEST(SceneRecon5, RebuildFreesAndRemask) {
    g_freed.clear(); g_recollect = 0;
    SceneCellPools pools{};
    OctreeBuildHooks bh{};
    OctreeRebuildHooks rh{&FreeChild, &Recollect};
    SceneWalkEnv env{};

    OctreeCell cell{};
    OctreeCell c0{}, c2{};
    cell.octant[0] = &c0;
    cell.octant[2] = &c2;        // octant 1,3 null
    cell.regionMask = 0x0011;    // low bits set; expect bit9 OR'd in

    OctreeCell* r = RebuildRegionOctree(&cell, /*minDepth*/0, /*maxDepth*/4,
                                        pools, bh, rh, env);
    // freed exactly the two non-null octants
    CHECK_EQ((int)g_freed.size(), 2);
    CHECK(g_freed[0] == &c0);
    CHECK(g_freed[1] == &c2);
    // octant pointers cleared
    CHECK(cell.octant[0] == nullptr);
    CHECK(cell.octant[2] == nullptr);
    // mask bit 9 set (HIBYTE |= 2 -> 0x0200), low bits preserved
    CHECK_EQ((int)cell.regionMask, 0x0011 | 0x0200);
    CHECK_EQ(g_recollect, 1);
    CHECK_EQ((int)g_recollectMask, 0x0011 | 0x0200);
    // empty cell -> subdivide returns the cell
    CHECK(r == &cell);
}
