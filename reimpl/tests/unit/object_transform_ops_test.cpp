// Golden vectors for render::object_transform_ops —
//   VIBE_SceneGraph_RemoveMeshFromTree @0x5f0b18 (top-level mesh removal driver)
//   + the TransformPointToParent affine math (no-parent / inverse-world branches
//     of 0x5b7d14) re-exposed for golden pinning.
#include "test.h"

#include "render/object_transform_ops.h"
#include "render/scene_walk.h"
#include "mem/heap.h"
#include "mem/mempool.h"
#include "mem/memory_debug.h"

#include <cmath>

using namespace guild;
using namespace guild::render;
using guild::mem::Heap;
using guild::mem::MemoryTracker;
using guild::mem::MemPool;

namespace {
bool NearF(float a, float b, float e = 1e-4f) { return std::fabs(a - b) <= e; }
}

// =============================================================================
// RemoveMeshFromTree — null guards (0x5f0b1e): returns node-low-byte unchanged
// without walking when either arg is null.
// =============================================================================
TEST(ObjTransformOps, RemoveMeshFromTreeNullGuards) {
    Heap heap; MemoryTracker tr(heap); tr.Init(2048);
    MemPool listPool, blockPool;
    SceneCellPools pools{&listPool, &blockPool, &tr};
    SceneWalkEnv env;
    SceneNode node;
    SceneCell cell;

    // null region cell -> no walk; original returns (char)a1 (the node pointer's
    // low byte, value unspecified) and crucially performs NO removal: the cell is
    // untouched. We assert the no-side-effect invariant.
    cell.refCount = 7;
    (void)RemoveMeshFromTree(&node, nullptr, env, pools, 0);
    CHECK_EQ(cell.refCount, 7);   // no walk happened
    // null node -> returns (char)node == 0 and no walk.
    CHECK_EQ(RemoveMeshFromTree(nullptr, &cell, env, pools, 0), (char)0);
    CHECK_EQ(cell.refCount, 7);

    tr.Shutdown();
}

// =============================================================================
// RemoveMeshFromTree — single type-1 node: the walk visits it (mask 0x80 fires
// for nodeType 1) and removes its mesh from the region cell.
// =============================================================================
TEST(ObjTransformOps, RemoveMeshFromTreeSingleNode) {
    Heap heap; MemoryTracker tr(heap); tr.Init(4096);
    MemPool listPool, blockPool;
    SceneCellPools pools{&listPool, &blockPool, &tr};
    SceneWalkEnv env;

    // Object node with a mesh registered in the region cell.
    SceneCell objCell; int present = 1; objCell.meshPresent = &present;
    SceneNode obj;
    obj.drawCell = &objCell;
    obj.flags531 = 0x04;          // mesh-in-cell present
    obj.nodeType = 1;             // type 1 -> mask bit 0x80
    obj.flags528 = 0x01;          // sibling-chain terminator (single node)

    SceneCell region;
    CHECK_EQ(AddMeshToCell(&obj, &region, pools), (char)1);
    CHECK_EQ(region.refCount, 1);
    CHECK(region.listHead != nullptr);

    // regionMask 0 -> walk mask 0x280 (0x200 no-advance + 0x80 type-1 visit).
    char r = RemoveMeshFromTree(&obj, &region, env, pools, 0);
    CHECK_EQ(r, (char)1);
    CHECK_EQ(region.refCount, 0);             // mesh removed
    CHECK(region.listHead == nullptr);
    CHECK_EQ(obj.flags531 & 4, 0);            // +531 bit2 cleared by recursive

    MemPoolFreeAll(&listPool, tr);
    MemPoolFreeAll(&blockPool, tr);
    tr.Shutdown();
}

// =============================================================================
// RemoveMeshFromTree — parent + child: both meshes are unlinked (the walk
// descends node->firstChild with mask & 0xFDFF, which still has 0x80 set).
// =============================================================================
TEST(ObjTransformOps, RemoveMeshFromTreeDescendsChild) {
    Heap heap; MemoryTracker tr(heap); tr.Init(8192);
    MemPool listPool, blockPool;
    SceneCellPools pools{&listPool, &blockPool, &tr};
    SceneWalkEnv env;

    SceneCell pCell, cCell; int present = 1;
    pCell.meshPresent = &present; cCell.meshPresent = &present;

    SceneNode parent, child;
    parent.drawCell = &pCell; parent.flags531 = 0x04; parent.nodeType = 1;
    parent.flags528 = 0x01;                  // list head terminator
    parent.firstChild = &child;
    child.drawCell = &cCell; child.flags531 = 0x04; child.nodeType = 1;
    child.flags528 = 0x01;                    // its own sibling terminator

    SceneCell region;
    AddMeshToCell(&parent, &region, pools);
    AddMeshToCell(&child, &region, pools);
    CHECK_EQ(region.refCount, 2);

    char r = RemoveMeshFromTree(&parent, &region, env, pools, 0);
    CHECK_EQ(r, (char)1);
    CHECK_EQ(region.refCount, 0);             // both meshes removed
    CHECK(region.listHead == nullptr);
    CHECK_EQ(parent.flags531 & 4, 0);
    CHECK_EQ(child.flags531 & 4, 0);

    MemPoolFreeAll(&listPool, tr);
    MemPoolFreeAll(&blockPool, tr);
    tr.Shutdown();
}

// =============================================================================
// RemoveMeshFromTree — a node whose type does NOT pass the mask is visited but
// its mesh is left intact (callback not invoked for that node).
// =============================================================================
TEST(ObjTransformOps, RemoveMeshFromTreeTypeFiltered) {
    Heap heap; MemoryTracker tr(heap); tr.Init(4096);
    MemPool listPool, blockPool;
    SceneCellPools pools{&listPool, &blockPool, &tr};
    SceneWalkEnv env;

    SceneCell objCell; int present = 1; objCell.meshPresent = &present;
    SceneNode obj;
    obj.drawCell = &objCell; obj.flags531 = 0x04;
    obj.nodeType = 3;             // type 3 -> mask bit 0x01, NOT set in 0x280
    obj.flags528 = 0x01;

    SceneCell region;
    AddMeshToCell(&obj, &region, pools);
    CHECK_EQ(region.refCount, 1);

    char r = RemoveMeshFromTree(&obj, &region, env, pools, 0);
    CHECK_EQ(r, (char)1);
    CHECK_EQ(region.refCount, 1);             // unchanged (type filtered out)

    MemPoolFreeAll(&listPool, tr);
    MemPoolFreeAll(&blockPool, tr);
    tr.Shutdown();
}

// =============================================================================
// TransformPointPassThrough — 0x5b7d14 no-parent branch: position is copied
// verbatim; angles pass through MatrixToEuler in place.
// =============================================================================
TEST(ObjTransformOps, TransformPassThrough) {
    float point[3] = {1.5f, -2.0f, 3.25f};
    // A pure-identity rotation matrix laid out as the engine's 4x4 row buffer
    // (16 floats): MatrixToEuler reads m[0],m[4] (col0 first two) etc. Use a
    // matrix that yields zero euler (identity-ish): all zero except the diagonal.
    float angles[16] = {0};
    angles[0]  = 1.0f;   // m[0]
    angles[5]  = 1.0f;   // m[5]
    angles[10] = 1.0f;   // m[10]
    float outPoint[3], outAngles[3];
    TransformPointPassThrough(point, angles, outPoint, outAngles);

    CHECK(NearF(outPoint[0], 1.5f));
    CHECK(NearF(outPoint[1], -2.0f));
    CHECK(NearF(outPoint[2], 3.25f));
    // Identity rotation -> euler angles all ~0.
    CHECK(NearF(outAngles[0], 0.0f, 1e-3f));
    CHECK(NearF(outAngles[1], 0.0f, 1e-3f));
    CHECK(NearF(outAngles[2], 0.0f, 1e-3f));
}

// =============================================================================
// TransformPointByInverseWorld — 0x5b7dce affine: point * inverse-world.
// With an identity inverse + a translation in the last column, the result is
// point + translation (matching the row layout m1/m5/m9/m13, etc.).
// =============================================================================
TEST(ObjTransformOps, TransformByInverseWorldIdentity) {
    float point[3] = {2.0f, 3.0f, 4.0f};
    float inv[16] = {0};
    inv[0] = inv[5] = inv[10] = inv[15] = 1.0f;   // identity rotation
    inv[12] = 10.0f;  // tx (m13 in 0-based the decompile names m12/13/14)
    inv[13] = 20.0f;  // ty
    inv[14] = 30.0f;  // tz
    float out[3];
    TransformPointByInverseWorld(point, inv, out);
    // x = p.x*m0 + p.y*m4 + p.z*m8 + m12 = 2 + 10 = 12
    CHECK(NearF(out[0], 12.0f));
    // y = p.x*m1 + p.y*m5 + p.z*m9 + m13 = 3 + 20 = 23
    CHECK(NearF(out[1], 23.0f));
    // z = p.x*m2 + p.y*m6 + p.z*m10 + m14 = 4 + 30 = 34
    CHECK(NearF(out[2], 34.0f));
}

// =============================================================================
// TransformPointByInverseWorld — non-trivial rotation block: verify the exact
// row-pick from the decompile (y/z computed before x is stored).
// =============================================================================
TEST(ObjTransformOps, TransformByInverseWorldRotation) {
    float point[3] = {1.0f, 0.0f, 0.0f};
    float inv[16] = {0};
    // 90-degree-ish rotation about Z encoded in the m0/m1/m4/m5 block.
    inv[0] = 0.0f; inv[1] = 1.0f;   // x picks m0, y picks m1
    inv[4] = -1.0f; inv[5] = 0.0f;
    inv[10] = 1.0f; inv[15] = 1.0f;
    inv[12] = 0.0f; inv[13] = 0.0f; inv[14] = 0.0f;
    float out[3];
    TransformPointByInverseWorld(point, inv, out);
    // x = 1*m0 + 0*m4 + 0*m8 + m12 = 0
    CHECK(NearF(out[0], 0.0f));
    // y = 1*m1 + 0*m5 + 0*m9 + m13 = 1
    CHECK(NearF(out[1], 1.0f));
    // z = 0
    CHECK(NearF(out[2], 0.0f));
}
