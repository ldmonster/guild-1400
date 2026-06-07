#include "test.h"

#include "render/scene_walk.h"
#include "mem/heap.h"
#include "mem/mempool.h"
#include "mem/memory_debug.h"

#include <cstdio>
#include <vector>

using namespace guild;
using namespace guild::render;
using guild::mem::Heap;
using guild::mem::MemoryTracker;
using guild::mem::MemPool;

// =============================================================================
// E2E: a whole scene-graph flow across the walk family.
//
// Build a typed scene tree, then drive the two real engine flows that
// VIBE_Object_SetPosition / SetWorldTranslation drive in the original:
//   1) DIRTY-FLAG PROPAGATION: WalkAndInvoke(root, node, MarkDirtyFlag, 511, 1)
//      stamps +528 bit2 on every node whose type passes TestNodeFlag(.,511) and
//      clears the no-pivot/active bits — mirroring the SetPosition pre-pass.
//   2) AABB ACCUMULATION: a TraverseTree-style pre-order visit collects nodes and
//      grows a running box, mirroring ComputeObjectAabb's WalkAndInvoke over
//      AccumulateVertexAabb.
// =============================================================================

namespace {
// MarkDirtyFlag adapted to the WalkAndInvoke callback ABI (the original passes a5
// == 1 as the clearNoPivot arg; the visited node arrives in eax).
char MarkDirtyAdapter(SceneNode* n, std::intptr_t clearNoPivot) {
    return MarkDirtyFlag(n, (char)clearNoPivot);
}

// A simple AABB accumulator callback: each node carries a position in its
// drawCell's listHead-less payload; here we reuse flags as a tiny per-node value
// and OR-collect a visited bitmask so the walk order/coverage is observable.
std::vector<SceneNode*>* g_collected = nullptr;
char CollectAabb(SceneNode* n, std::intptr_t) { g_collected->push_back(n); return 1; }
} // namespace

TEST(SceneWalkE2E, DirtyPropagationThenAccumulate) {
    // Tree (all type 3 -> fire on mask bit0, which 511 (0x1FF) includes). Each
    // top-level child (A, B) is a sibling-list HEAD (+528 bit0) per the engine's
    // root layout, so the root walk drives them cleanly:
    //   root.childHead = A(head) -> B(head) -> term ; A.child = C ; C.child = D
    SceneNode A, B, C, D, term;
    for (auto* p : {&A,&B,&C,&D}) {
        p->nodeType = 3;
        p->flags528 = 0x00;
        p->flagNoPivot = 0x80;  // no-pivot set; the dirty pass should clear it
        p->flags531 = 0x01;     // bit0 set; the dirty pass clears it
    }
    A.flags528 = 0x01;  B.flags528 = 0x01;  // each root child is a list head
    term.flags528 = 0x01;       // sentinel terminator for the root list

    A.nextSibling = &B;  A.firstChild = &C;
    C.firstChild = &D;
    B.nextSibling = &term;

    UniverseRoot root;
    root.childHead = &A;
    SceneWalkEnv env;
    env.root = &root;
    env.listTerminator = &term;

    // --- Flow 1: dirty-flag propagation over the whole tree (mask 511). ---
    char r = WalkAndInvoke(&root, nullptr, MarkDirtyAdapter, 511, 1, env);
    CHECK_EQ(r, (char)1);
    for (auto* p : {&A,&B,&C,&D}) {
        CHECK(p->flags528 & 0x04);        // dirty stamped
        CHECK_EQ(p->flagNoPivot & 0x80, 0); // no-pivot cleared (clearNoPivot==1)
        CHECK_EQ(p->flags531 & 0x01, 0);    // active bit cleared
    }

    // --- Flow 2: pre-order accumulation walk (collects A,C,D,B). ---
    std::vector<SceneNode*> collected; g_collected = &collected;
    WalkAndInvoke(&root, nullptr, CollectAabb, 511, 0, env);
    // A's pre-order subtree = A, C, D; then root child B.
    CHECK_EQ((int)collected.size(), 4);
    CHECK(collected[0] == &A);
    CHECK(collected[1] == &C);
    CHECK(collected[2] == &D);
    CHECK(collected[3] == &B);
}

// =============================================================================
// E2E: octree mesh-cell lifecycle across Add/Remove/Free with the real pool.
// Add N objects into a small octree (root + 2 octant children), remove half,
// then free the remainder — asserting refcounts and list integrity throughout.
// =============================================================================
TEST(SceneWalkE2E, OctreeCellLifecycle) {
    Heap heap;
    MemoryTracker tr(heap);
    tr.Init(4096);
    MemPool listPool, blockPool;
    SceneCellPools pools{&listPool, &blockPool, &tr};

    int present = 1;
    const int N = 6;
    std::vector<SceneCell> objCells(N);
    std::vector<SceneNode> objs(N);
    for (int i = 0; i < N; ++i) {
        objCells[i].meshPresent = &present;
        objs[i].drawCell = &objCells[i];
        objs[i].flags531 = 0x04;
    }

    SceneCell* root = static_cast<SceneCell*>(MemPoolAlloc(&blockPool, tr, sizeof(SceneCell), 16));
    SceneCell* c0 = static_cast<SceneCell*>(MemPoolAlloc(&blockPool, tr, sizeof(SceneCell), 16));
    SceneCell* c1 = static_cast<SceneCell*>(MemPoolAlloc(&blockPool, tr, sizeof(SceneCell), 16));
    new (root) SceneCell(); new (c0) SceneCell(); new (c1) SceneCell();
    root->octant[0] = c0;
    root->octant[2] = c1;

    // Put every object into root, even objects into c0, odd into c1.
    for (int i = 0; i < N; ++i) {
        AddMeshToCell(&objs[i], root, pools);
        AddMeshToCell(&objs[i], (i % 2 == 0) ? c0 : c1, pools);
    }
    CHECK_EQ(root->refCount, N);
    CHECK_EQ(c0->refCount, 3);
    CHECK_EQ(c1->refCount, 3);

    // Remove the first 3 objects from the root subtree (recurses into children).
    for (int i = 0; i < 3; ++i)
        RemoveMeshRecursive(&objs[i], root, pools);
    // objs 0,1,2 removed: root -3, c0 loses 0 and 2 (-2), c1 loses 1 (-1).
    CHECK_EQ(root->refCount, N - 3);
    // c0 had {0,2,4}; removed {0,2} -> 1 left, still attached.
    CHECK(root->octant[0] == c0);
    CHECK_EQ(c0->refCount, 1);
    // c1 had {1,3,5}; removed {1} -> 2 left.
    CHECK(root->octant[2] == c1);
    CHECK_EQ(c1->refCount, 2);

    // Free the whole remaining subtree.
    CHECK(FreeNodeRecursive(root, pools) == root);

    MemPoolFreeAll(&listPool, tr);
    MemPoolFreeAll(&blockPool, tr);
    tr.Shutdown();
}

// =============================================================================
// GUARDED real-asset e2e. The original scene tree is loaded from scenes.BIN
// (97 real .ed3 scenes). When that asset is present this would exercise the walk
// over a real graph; when absent (the usual CI case) we SKIP-PASS. The scene
// loader/binding lives in other modules, so here we only gate on file presence
// and assert the guard path so the suite stays green without the asset.
// =============================================================================
TEST(SceneWalkE2E, RealScenesGuarded) {
    const char* candidates[] = {
        "scenes.BIN", "assets/scenes.BIN", "data/scenes.BIN",
        "../scenes.BIN", "reimpl/scenes.BIN",
    };
    std::FILE* f = nullptr;
    for (const char* path : candidates) {
        f = std::fopen(path, "rb");
        if (f) break;
    }
    if (!f) {
        std::printf("    [skip] scenes.BIN absent — real-scene walk skipped\n");
        CHECK(true);  // skip-pass
        return;
    }
    // Present: assert it is non-empty; full .ed3 parsing belongs to scene_load.
    std::fseek(f, 0, SEEK_END);
    long sz = std::ftell(f);
    std::fclose(f);
    CHECK(sz > 0);
}
