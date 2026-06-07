// ===========================================================================
// object_lifecycle5_e2e_test.cpp — end-to-end flow across the batch-5
// VIBE_Object_* geometry / transform / transparency / suspend functions.
//
// Scenario: a freshly bound scene node gets a LOD mesh assigned, is attached to
// a skeleton bone, has its material made transparent and then restored, is
// suspended and restored, promoted to the active camera, and finally detached.
// We install captor hooks for the unreconstructed cross-module leaves and assert
// the observable side effects + ordering end to end.
// ===========================================================================
#include <cmath>
#include <cstring>
#include <vector>

#include "test.h"
#include "sim/object_lifecycle5.h"

using namespace guild::sim;

namespace {

// Shared scenario state (file-static; this e2e file is its own executable).
Node5 g_lodMesh;
int g_meshFinalizeCalls = 0;
int g_assignBones = 0, g_computeBones = 0;
int g_vertexShade = 0;
int g_lightRefresh = 0;
int g_invalidate = 0;
int g_unlink = 0, g_dispose = 0;
char g_scratch[512];

inline void SetPtr(void* base, int off, void* val) {
    *reinterpret_cast<void**>(reinterpret_cast<char*>(base) + off) = val;
}

}  // namespace

TEST(ObjLife5_E2E, FullObjectLifecycleFlow) {
    ObjLife5ResetHooks();
    g_meshFinalizeCalls = g_assignBones = g_computeBones = 0;
    g_vertexShade = g_lightRefresh = g_invalidate = 0;
    g_unlink = g_dispose = 0;
    g_activeCamera5 = nullptr;

    ObjLife5Hooks h;
    h.meshSelectLodFrame = [](Node5*) { return &g_lodMesh; };
    h.meshFinalize = [](Node5*, Node5*) { ++g_meshFinalizeCalls; };
    h.animAssignSubMeshBones = [](Node5*) { ++g_assignBones; };
    h.animComputeBoneMatrices = [](Node5*) { ++g_computeBones; };
    h.lightApplyVertexShading = [](Node5*, void*) -> guild::u8 {
        ++g_vertexShade;
        return 1;
    };
    h.lightRefreshAllObjects = []() { ++g_lightRefresh; };
    h.objInvalidateCurrent = [](guild::u8) { ++g_invalidate; };
    h.objUnlinkFromList = [](Node5*) { ++g_unlink; };
    h.objDispose = [](Node5*) { ++g_dispose; };
    h.memAlloc = [](int, const char*) -> void* { return (void*)g_scratch; };
    h.memFree = [](void*) {};
    h.textureCloneIfPaletteMatch = [](int tex, int) { return tex + 1000; };
    h.textureDetachClone = [](int tex) { return tex - 1000; };
    ObjLife5SetHooks(h);

    // --- the node + its skeleton/mesh blocks ---
    Node5 node;
    node.b(533) = 5;          // active nodeType
    node.b(528) = 0;
    node.p(460) = nullptr;    // no mesh yet

    static char skeleton[4000] = {0};
    node.p(492) = skeleton;

    // parent bone for AttachToBone.
    static char parentBone[600] = {0};
    static char parentSkel[600] = {0};
    static char boneTable[700] = {0};
    boneTable[520] = 2;
    std::strcpy(boneTable + 116 + 0 * 88, "root");
    std::strcpy(boneTable + 116 + 1 * 88, "head");
    SetPtr(parentSkel, 260, boneTable);
    SetPtr(parentBone, 492, parentSkel);
    node.p(504) = parentBone;

    // 1) Assign LOD mesh.
    int meshRet = ObjectAssignMeshData(&node, false);
    CHECK_EQ(meshRet, (int)(intptr_t)&g_lodMesh);
    CHECK(node.p(460) == &g_lodMesh);
    CHECK_EQ(node.b(528) & 4u, 4u);
    CHECK_EQ(g_meshFinalizeCalls, 1);

    // 2) Attach to a valid bone.
    char attached = ObjectAttachToBone(&node, "head");
    CHECK_EQ(attached, (char)1);
    CHECK_EQ(std::strcmp(skeleton + 180, "head"), 0);
    CHECK_EQ(g_assignBones, 1);
    CHECK_EQ(g_computeBones, 1);

    // 3) Make one submesh transparent, then restore — across the subtree driver.
    skeleton[2316] = 1;  // 1 submesh
    char* sm = skeleton + 244;
    static char meshObj[600] = {0};
    *reinterpret_cast<int*>(meshObj + 480) = 2;  // cap
    SetPtr(sm, 16, meshObj);
    static char polys[80] = {0};
    *reinterpret_cast<int*>(polys + 20) = 222;
    SetPtr(sm, 4, polys);
    *reinterpret_cast<int*>(sm + 12) = 1;  // 1 poly
    *reinterpret_cast<unsigned char*>(sm + 376) = 0xFF;
    *reinterpret_cast<unsigned char*>(sm + 378) = 0;

    int color = 0x40;
    char appliedTree = ObjectChangeTransparencySubMeshes(&node, &color);
    CHECK_EQ(appliedTree, (char)1);
    CHECK_EQ(*reinterpret_cast<int*>(polys + 20), 1222);  // cloned
    CHECK_EQ(node.b(529) & 0x80u, 0x80u);
    CHECK_EQ(g_vertexShade, 1);

    int restore = 0xFF;
    ObjectChangeTransparencySubMeshes(&node, &restore);
    CHECK_EQ(*reinterpret_cast<int*>(polys + 20), 222);  // detached back
    CHECK_EQ(node.b(529) & 0x80u, 0u);
    CHECK_EQ(g_vertexShade, 2);

    // ApplyTransparencyTree drives the same leaf on this node.
    int treeColor = 0x40;
    char tree = ObjectApplyTransparencyTree(&node, treeColor);
    CHECK_EQ(tree, (char)1);
    CHECK_EQ(*reinterpret_cast<int*>(polys + 20), 1222);  // re-cloned
    CHECK_EQ(g_vertexShade, 3);
    // restore again for a clean state.
    int treeRestore = 0xFF;
    ObjectChangeTransparencySubMeshes(&node, &treeRestore);

    // 4) Suspend (push), then restore — state5 path refreshes light on restore.
    char push = ObjectToggleSuspendStateNamed(&node, 0);
    CHECK_EQ(push, (char)1);
    CHECK_EQ(node.b(533), (guild::u8)1);
    CHECK_EQ(node.b(534), (guild::u8)5);

    char restored = ObjectToggleSuspendStateNamed(&node, 1);
    CHECK_EQ(restored, (char)1);
    CHECK_EQ(node.b(533), (guild::u8)5);
    CHECK_EQ(g_lightRefresh, 1);  // 534 == 5

    // 5) Promote to active camera (needs nodeType 3).
    node.b(533) = 3;
    int cam = ObjectSetActiveCamera(&node);
    CHECK_EQ(cam, 1);
    CHECK(g_activeCamera5 == &node);
    CHECK_EQ(g_invalidate, 1);

    // 6) Detach + dispose.
    char detached = ObjectDetachAndRelease(&node);
    CHECK_EQ(detached, (char)1);
    CHECK_EQ(g_unlink, 1);
    CHECK_EQ(g_dispose, 1);

    ObjLife5ResetHooks();
    g_activeCamera5 = nullptr;
}

// A second flow: ComputeBoundingRadius feeding a transform pipeline.
namespace {
std::vector<float> g_box;
const float* FeedBox(Node5*, const float*) { return g_box.data(); }
}  // namespace

TEST(ObjLife5_E2E, BoundingRadiusThenTransform) {
    ObjLife5ResetHooks();
    Node5 node;
    node.b(533) = 1;  // bbox path
    static char meshFn[64] = {0};
    static char meshInner[8] = {1, 0, 0, 0, 0, 0, 0, 0};  // *(meshFn+16) != 0
    SetPtr(meshFn, 16, meshInner);
    node.p(460) = meshFn;

    // box centered at (4,8,12), half 1 -> radius sqrt(3).
    g_box.assign(8 * 20, 0.0f);
    int k = 0;
    for (int sx = -1; sx <= 1; sx += 2)
        for (int sy = -1; sy <= 1; sy += 2)
            for (int sz = -1; sz <= 1; sz += 2) {
                g_box[k * 20 + 0] = 4 + sx;
                g_box[k * 20 + 1] = 8 + sy;
                g_box[k * 20 + 2] = 12 + sz;
                ++k;
            }

    ObjLife5Hooks h;
    h.meshBoundingCorners = &FeedBox;
    ObjLife5SetHooks(h);

    float pt[3] = {0, 0, 0};
    float radius = 0, center[3] = {0, 0, 0};
    char ok = ObjectComputeBoundingRadius(&node, pt, &radius, center);
    CHECK_EQ(ok, (char)1);
    CHECK(std::fabs(center[0] - 4.0f) < 1e-4f);
    CHECK(std::fabs(center[1] - 8.0f) < 1e-4f);
    CHECK(std::fabs(center[2] - 12.0f) < 1e-4f);
    CHECK(std::fabs(radius - std::sqrt(3.0f)) < 1e-4f);

    // Feed the computed center through the no-parent transform passthrough.
    node.p(504) = nullptr;
    float outPt[3] = {0, 0, 0}, ang[3] = {0, 0, 0}, outAng[3] = {0, 0, 0};
    ObjectTransformPointToParent(&node, center, outPt, ang, outAng);
    CHECK(std::fabs(outPt[0] - 4.0f) < 1e-4f);
    CHECK(std::fabs(outPt[2] - 12.0f) < 1e-4f);

    ObjLife5ResetHooks();
}
