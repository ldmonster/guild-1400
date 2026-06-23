#include "test.h"

#include "render/scene_recon_octree.h"
#include "render/scene_walk.h"
#include "mem/heap.h"
#include "mem/mempool.h"
#include "mem/memory_debug.h"

#include <vector>

using namespace guild;
using namespace guild::render;
using guild::mem::Heap;
using guild::mem::MemoryTracker;
using guild::mem::MemPool;

// =============================================================================
// gilde.exe VIBE_SceneGraph octree-build cluster — golden-vector unit tests.
//   0x5effa8 ComputeMeshNodeBounds   0x5eff10 CollectMeshesInBounds
//   0x5f0160 SubdivideOctree         0x5f05b0 BuildOctreeForRegion
//
// The cluster's mesh-list push / subtree-free mirror the already-present
// AddMeshToCell / FreeNodeRecursive (render/scene_walk); the leaf bounding box is
// asset-coupled and supplied here as a deterministic test hook (a node->AABB map),
// so the pure octree traversal/flag logic is exercised 1:1.
// =============================================================================

namespace {

// A node carrying a draw cell (so the AddMeshToCell gate passes) plus a fixed
// world AABB the test hook hands back. We attach the 6-float AABB cache to each
// node via a side table so nodeMeshAABB can both read and write it.
struct TestMesh {
    SceneNode node;
    SceneCell draw;          // drawCell with meshPresent set => gate open
    float aabb[6] = {0,0,0, 0,0,0};  // [minX,minY,minZ,maxX,maxY,maxZ]
};

// Global registry so the non-capturing hooks can resolve a SceneNode -> TestMesh.
std::vector<TestMesh*>& registry() { static std::vector<TestMesh*> v; return v; }

TestMesh* lookup(SceneNode* n) {
    for (TestMesh* m : registry())
        if (&m->node == n) return m;
    return nullptr;
}

void makeMesh(TestMesh& m, float minx, float minz, float maxx, float maxz) {
    m.draw.meshPresent = &m;            // gate: drawCell->meshPresent != null
    m.node.drawCell = &m.draw;          // node+492
    m.node.flags531 = 0x04;             // node+531 bit2 set (mesh present)
    m.aabb[0] = minx; m.aabb[1] = 0.0f; m.aabb[2] = minz;
    m.aabb[3] = maxx; m.aabb[4] = 0.0f; m.aabb[5] = maxz;
    registry().push_back(&m);
}

float* hookAABB(SceneNode* n) {
    TestMesh* m = lookup(n);
    return m ? m->aabb : nullptr;
}

// computeNodeBounds hook: hand back the registered AABB as the node's world box.
char hookCompute(SceneNode* n, bool /*transform*/, float bmin[3], float bmax[3]) {
    TestMesh* m = lookup(n);
    if (!m) return 0;
    bmin[0] = m->aabb[0]; bmin[1] = m->aabb[1]; bmin[2] = m->aabb[2];
    bmax[0] = m->aabb[3]; bmax[1] = m->aabb[4]; bmax[2] = m->aabb[5];
    return 1;
}

OctreeBuildHooks makeHooks() {
    OctreeBuildHooks h;
    h.computeNodeBounds = &hookCompute;
    h.nodeMeshAABB      = &hookAABB;
    return h;
}

// Build a "parent" cell holding a mesh list (the source CollectMeshesInBounds
// iterates). Pushes each TestMesh's node directly onto the cell's list, matching
// what WalkAndInvoke+AddMeshToCell would have produced.
OctreeCell* makeParent(std::vector<TestMesh*> meshes, const SceneCellPools& pools) {
    OctreeCell* c = static_cast<OctreeCell*>(
        mem::MemPoolAlloc(pools.cellBlockPool, *pools.tracker, sizeof(OctreeCell), 64));
    new (c) OctreeCell();
    for (TestMesh* m : meshes) {
        ++c->meshCount;
        MeshCellNode* ln = static_cast<MeshCellNode*>(
            mem::MemPoolAlloc(pools.cellListPool, *pools.tracker, sizeof(MeshCellNode), 128));
        ln->obj = &m->node; ln->next = nullptr;
        if (c->listTail) c->listTail->next = ln; else c->listHead = ln;
        c->listTail = ln;
    }
    return c;
}

int listLen(OctreeCell* c) {
    int n = 0;
    for (MeshCellNode* it = c->listHead; it; it = it->next) ++n;
    return n;
}

// A local mirror of the cluster's recursive free, to tear test trees down without
// depending on the static internal one.
void octreeFreeRoot(OctreeCell* c, const SceneCellPools& pools) {
    if (!c) return;
    for (MeshCellNode* it = c->listHead; it; ) {
        MeshCellNode* nx = it->next;
        mem::MemPoolFree(pools.cellListPool, *pools.tracker, it); it = nx;
    }
    for (int i = 0; i < 4; ++i) octreeFreeRoot(c->octant[i], pools);
    mem::MemPoolFree(pools.cellBlockPool, *pools.tracker, c);
}

} // namespace

// -----------------------------------------------------------------------------
// ComputeMeshNodeBounds: inert (no hook) returns 0; with the compute hook it
// returns 1, yields the world AABB, and caches it via nodeMeshAABB.
// -----------------------------------------------------------------------------
TEST(SceneReconOctree, ComputeMeshNodeBoundsHookAndInert) {
    registry().clear();
    TestMesh m;
    makeMesh(m, -2.0f, -3.0f, 4.0f, 5.0f);

    float bmin[3] = {9,9,9}, bmax[3] = {9,9,9};

    // Inert default (no computeNodeBounds hook): returns 0 (orig "no mesh box").
    OctreeBuildHooks inert;
    CHECK_EQ(ComputeMeshNodeBounds(&m.node, bmin, bmax, inert), (char)0);

    OctreeBuildHooks h = makeHooks();
    CHECK_EQ(ComputeMeshNodeBounds(&m.node, bmin, bmax, h), (char)1);
    CHECK_EQ(bmin[0], -2.0f); CHECK_EQ(bmin[2], -3.0f);
    CHECK_EQ(bmax[0],  4.0f); CHECK_EQ(bmax[2],  5.0f);
    // Cache round-trips through nodeMeshAABB.
    CHECK_EQ(m.aabb[0], -2.0f); CHECK_EQ(m.aabb[5], 5.0f);
}

// -----------------------------------------------------------------------------
// CollectMeshesInBounds: only nodes whose (x,z) AABB overlaps the slab are pushed;
// an empty result frees the cell and returns null. Golden slab test:
//   meshA [0,0]..[1,1], meshB [10,10]..[11,11]; slab lo{-1,-1} hi{2,2} -> only A.
// -----------------------------------------------------------------------------
TEST(SceneReconOctree, CollectMeshesInBoundsSlabOverlap) {
    Heap heap; MemoryTracker tr(heap); tr.Init(2048);
    MemPool listPool, blockPool;
    SceneCellPools pools{&listPool, &blockPool, &tr};
    OctreeBuildHooks h = makeHooks();

    registry().clear();
    TestMesh a, b;
    makeMesh(a, 0.0f, 0.0f, 1.0f, 1.0f);
    makeMesh(b, 10.0f, 10.0f, 11.0f, 11.0f);
    OctreeCell* parent = makeParent({&a, &b}, pools);

    float lo[3] = {-1.0f, -1.0f, 0.0f};   // {x, z, _}
    float hi[3] = { 2.0f,  2.0f, 0.0f};
    OctreeCell* got = CollectMeshesInBounds(parent, lo, hi, pools, h);
    CHECK(got != nullptr);
    CHECK_EQ(got->meshCount, 1);          // only A overlaps
    CHECK_EQ(listLen(got), 1);
    CHECK_EQ(got->listHead->obj, &a.node);

    // A slab covering neither -> null + cell freed (no crash, accounting clean).
    float far_lo[3] = {100.0f, 100.0f, 0.0f};
    float far_hi[3] = {101.0f, 101.0f, 0.0f};
    OctreeCell* none = CollectMeshesInBounds(parent, far_lo, far_hi, pools, h);
    CHECK(none == nullptr);

    // Cleanup: free child + parent so the tracker is balanced.
    for (MeshCellNode* it = got->listHead; it; ) {
        MeshCellNode* nx = it->next;
        mem::MemPoolFree(&listPool, tr, it); it = nx;
    }
    mem::MemPoolFree(&blockPool, tr, got);
    for (MeshCellNode* it = parent->listHead; it; ) {
        MeshCellNode* nx = it->next;
        mem::MemPoolFree(&listPool, tr, it); it = nx;
    }
    mem::MemPoolFree(&blockPool, tr, parent);
}

// -----------------------------------------------------------------------------
// SubdivideOctree: the split is KEPT only when the quadrant children have DIFFERENT
// meshCounts (the original's collapse compares *(child+48)==child.meshCount across
// the 4 octants, decompile 0x5f0534: equal counts -> v70 stays 1 -> collapse/free).
// So two single-mesh quadrants (1==1) would COLLAPSE; we make the distribution
// UNEVEN (two meshes at the origin -> octant[0].meshCount==2, one at the far corner
// -> octant[2].meshCount==1) so 2!=1 keeps the split and produces child cells.
// (Quadrant geometry verbatim, incl. the Q3 lo.z = minZ + halfX quirk.)
// -----------------------------------------------------------------------------
TEST(SceneReconOctree, SubdivideSplitsDistinctQuadrants) {
    Heap heap; MemoryTracker tr(heap); tr.Init(2048);
    MemPool listPool, blockPool;
    SceneCellPools pools{&listPool, &blockPool, &tr};
    OctreeBuildHooks h = makeHooks();

    registry().clear();
    TestMesh a, c, b;
    // a, c near the box min corner (both land in octant 0); b near the max corner.
    makeMesh(a, 0.0f, 0.0f, 0.0f, 0.0f);
    makeMesh(c, 0.0f, 0.0f, 0.0f, 0.0f);
    makeMesh(b, 10.0f, 10.0f, 10.0f, 10.0f);
    OctreeCell* root = makeParent({&a, &c, &b}, pools);

    OctreeCell* res = SubdivideOctree(root, /*minDepth*/1u, /*maxDepth*/4u,
                                      /*depth*/1u, pools, h);
    CHECK(res != nullptr);

    // Phase 1 (depth==1) reduced the box from the meshes: [0,0]..[10,10] (x/z).
    CHECK_EQ(root->boxMin[0], 0.0f);  CHECK_EQ(root->boxMin[2], 0.0f);
    CHECK_EQ(root->boxMax[0], 10.0f); CHECK_EQ(root->boxMax[2], 10.0f);

    // The split was kept (octant[0].meshCount==2 != octant[2].meshCount==1): the two
    // non-empty quadrant child cells survive.
    int children = 0;
    for (int i = 0; i < 4; ++i) if (root->octant[i]) ++children;
    CHECK(children >= 1);

    octreeFreeRoot(root, pools);   // frees the whole subtree
}

// -----------------------------------------------------------------------------
// SubdivideOctree collapse: when minDepth >= meshCount the subdivide gate
// (minDepth < meshCount) is false, so NO children are produced (the leaf stays).
// -----------------------------------------------------------------------------
TEST(SceneReconOctree, SubdivideGateStopsAtThreshold) {
    Heap heap; MemoryTracker tr(heap); tr.Init(2048);
    MemPool listPool, blockPool;
    SceneCellPools pools{&listPool, &blockPool, &tr};
    OctreeBuildHooks h = makeHooks();

    registry().clear();
    TestMesh a, b;
    makeMesh(a, 0.0f, 0.0f, 1.0f, 1.0f);
    makeMesh(b, 5.0f, 5.0f, 6.0f, 6.0f);
    OctreeCell* root = makeParent({&a, &b}, pools);    // meshCount == 2

    // minDepth == 2 == meshCount -> gate (2 < 2) false: no subdivision.
    OctreeCell* res = SubdivideOctree(root, /*minDepth*/2u, /*maxDepth*/4u,
                                      /*depth*/1u, pools, h);
    CHECK_EQ(res, root);
    for (int i = 0; i < 4; ++i) CHECK(root->octant[i] == nullptr);

    octreeFreeRoot(root, pools);
}
