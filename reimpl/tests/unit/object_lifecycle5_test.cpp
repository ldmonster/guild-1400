// ===========================================================================
// object_lifecycle5_test.cpp — golden-vector unit tests for the VIBE_Object_*
// geometry / transform / transparency / suspend-state batch (batch 5).
// ===========================================================================
#include <cmath>
#include <cstring>
#include <vector>

#include "test.h"
#include "sim/object_lifecycle5.h"

using namespace guild::sim;

namespace {

// Build the 8-corner array (stride 20 floats) for an axis-aligned box centered
// at (cx,cy,cz) with the given half-extent. Corner ordering does not matter.
std::vector<float>& BoxCorners(float cx, float cy, float cz, float half) {
    static std::vector<float> buf;
    buf.assign(8 * 20, 0.0f);
    int k = 0;
    for (int sx = -1; sx <= 1; sx += 2)
        for (int sy = -1; sy <= 1; sy += 2)
            for (int sz = -1; sz <= 1; sz += 2) {
                buf[k * 20 + 0] = cx + sx * half;
                buf[k * 20 + 1] = cy + sy * half;
                buf[k * 20 + 2] = cz + sz * half;
                ++k;
            }
    return buf;
}

const float* g_corners = nullptr;
const float* FeedCorners(Node5*, const float*) { return g_corners; }

// Store/read a native pointer at a byte offset in an external raw block.
inline void SetPtr(void* base, int off, void* val) {
    *reinterpret_cast<void**>(reinterpret_cast<char*>(base) + off) = val;
}

}  // namespace

// ---------------------------------------------------------------------------
// ComputeBoundingRadius — skinned path (byte533==0): radius = node+104 fixed.
// ---------------------------------------------------------------------------
TEST(ObjLife5_BoundingRadius, SkinnedFixedRadius) {
    ObjLife5ResetHooks();
    Node5 n;
    n.b(533) = 0;            // skinned path
    n.f(104) = 42.5f;        // fixed radius
    float pt[3] = {1, 2, 3};
    float radius = -1.0f;
    float center[3] = {-9, -9, -9};
    char r = ObjectComputeBoundingRadius(&n, pt, &radius, center);
    CHECK_EQ(r, (char)1);
    CHECK(std::fabs(radius - 42.5f) < 1e-5f);
    ObjLife5ResetHooks();
}

// ComputeBoundingRadius — bbox path: golden center=(10,20,30), radius=sqrt(12).
TEST(ObjLife5_BoundingRadius, BBoxGoldenSphere) {
    ObjLife5ResetHooks();
    Node5 n;
    n.b(533) = 1;                 // bbox path
    // Use a real buffer for meshFn so *(meshFn+16) is valid and non-zero.
    static char meshFn[64] = {0};
    static char meshInner[8] = {1, 0, 0, 0, 0, 0, 0, 0};  // *(meshFn+16) != 0
    SetPtr(meshFn, 16, meshInner);
    n.p(460) = meshFn;

    auto& corners = BoxCorners(10, 20, 30, 2.0f);
    g_corners = corners.data();
    ObjLife5Hooks h;
    h.meshBoundingCorners = &FeedCorners;
    ObjLife5SetHooks(h);

    float pt[3] = {0, 0, 0};
    float radius = -1.0f;
    float center[3] = {0, 0, 0};
    char r = ObjectComputeBoundingRadius(&n, pt, &radius, center);
    CHECK_EQ(r, (char)1);
    CHECK(std::fabs(center[0] - 10.0f) < 1e-4f);
    CHECK(std::fabs(center[1] - 20.0f) < 1e-4f);
    CHECK(std::fabs(center[2] - 30.0f) < 1e-4f);
    CHECK(std::fabs(radius - std::sqrt(12.0f)) < 1e-4f);
    ObjLife5ResetHooks();
}

// ComputeBoundingRadius — guards.
TEST(ObjLife5_BoundingRadius, Guards) {
    ObjLife5ResetHooks();
    float out;
    CHECK_EQ(ObjectComputeBoundingRadius(nullptr, nullptr, &out, nullptr),
             (char)0);
    Node5 n;
    n.b(533) = 1;
    // no meshFn -> return 0
    CHECK_EQ(ObjectComputeBoundingRadius(&n, nullptr, &out, nullptr), (char)0);
}

// ---------------------------------------------------------------------------
// AttachToBone — table scan over bone names (stride 88 from +116, count @+520).
// ---------------------------------------------------------------------------
TEST(ObjLife5_AttachToBone, MatchAndRebuild) {
    ObjLife5ResetHooks();
    // skeleton block: name dest at +180.
    static char skeleton[600] = {0};
    // parentBone block: +492 -> parentSkel; parentSkel+260 -> table.
    static char parentSkel[600] = {0};
    static char table[700] = {0};
    // table: count @+520, entries from +116 stride 88.
    table[520] = 3;  // 3 bones
    std::strcpy(table + 116 + 0 * 88, "root");
    std::strcpy(table + 116 + 1 * 88, "spine");
    std::strcpy(table + 116 + 2 * 88, "hand_L");
    SetPtr(parentSkel, 260, table);

    Node5 n;
    static char parentBone[600] = {0};
    SetPtr(parentBone, 492, parentSkel);
    n.p(504) = parentBone;     // parentBone
    n.p(492) = skeleton;       // skeleton

    int assigned = 0, computed = 0;
    static int* asg = &assigned;
    static int* cmp = &computed;
    ObjLife5Hooks h;
    h.animAssignSubMeshBones = [](Node5*) { ++*asg; };
    h.animComputeBoneMatrices = [](Node5*) { ++*cmp; };
    ObjLife5SetHooks(h);

    char r = ObjectAttachToBone(&n, "spine");
    CHECK_EQ(r, (char)1);
    CHECK_EQ(std::strcmp(skeleton + 180, "spine"), 0);
    CHECK_EQ(assigned, 1);
    CHECK_EQ(computed, 1);

    // not in table -> 0, bone matrices not rebuilt again.
    char r2 = ObjectAttachToBone(&n, "tail");
    CHECK_EQ(r2, (char)0);
    CHECK_EQ(assigned, 1);
    ObjLife5ResetHooks();
}

TEST(ObjLife5_AttachToBone, Guards) {
    ObjLife5ResetHooks();
    Node5 n;
    CHECK_EQ(ObjectAttachToBone(nullptr, "x"), (char)0);
    CHECK_EQ(ObjectAttachToBone(&n, nullptr), (char)0);
    CHECK_EQ(ObjectAttachToBone(&n, "x"), (char)0);  // node+504 == 0
}

// ---------------------------------------------------------------------------
// TransformPointToParent — no-parent passthrough (identity copy of point).
// ---------------------------------------------------------------------------
TEST(ObjLife5_TransformPoint, NoParentPassthrough) {
    ObjLife5ResetHooks();
    Node5 n;
    n.d(504) = 0;  // no parent bone
    float pt[3] = {3, 5, 7};
    float ang[3] = {0.1f, 0.2f, 0.3f};
    float outPt[3] = {0, 0, 0};
    float outAng[3] = {0, 0, 0};
    ObjectTransformPointToParent(&n, pt, outPt, ang, outAng);
    CHECK(std::fabs(outPt[0] - 3.0f) < 1e-6f);
    CHECK(std::fabs(outPt[1] - 5.0f) < 1e-6f);
    CHECK(std::fabs(outPt[2] - 7.0f) < 1e-6f);
    CHECK(std::fabs(outAng[0] - 0.1f) < 1e-6f);
}

// TransformPointToParent — parent path with identity world matrix (default
// fetchWorldMatrix returns identity, matrixInverse copies) -> point unchanged.
TEST(ObjLife5_TransformPoint, ParentIdentity) {
    ObjLife5ResetHooks();
    Node5 n;
    n.p(504) = &n;  // non-null parent bone
    float pt[3] = {2, 4, 6};
    float ang[3] = {0, 0, 0};
    float outPt[3] = {0, 0, 0};
    float outAng[3] = {0, 0, 0};
    ObjectTransformPointToParent(&n, pt, outPt, ang, outAng);
    // identity 4x4: out = point (translation row 12..14 == 0).
    CHECK(std::fabs(outPt[0] - 2.0f) < 1e-5f);
    CHECK(std::fabs(outPt[1] - 4.0f) < 1e-5f);
    CHECK(std::fabs(outPt[2] - 6.0f) < 1e-5f);
}

// ---------------------------------------------------------------------------
// ApplyParentTransform — orders SetPosition then SetWorldTranslation.
// ---------------------------------------------------------------------------
static int g_order = 0;
static int g_posCall = 0, g_wtCall = 0;
TEST(ObjLife5_ApplyParentTransform, OrdersSetters) {
    ObjLife5ResetHooks();
    g_order = 0;
    g_posCall = g_wtCall = 0;
    Node5 n;
    n.d(504) = 0;  // no parent: outPt == point
    ObjLife5Hooks h;
    h.objSetPosition = [](Node5*, const float*) { g_posCall = ++g_order; };
    h.objSetWorldTranslation = [](Node5*, const float*) {
        g_wtCall = ++g_order;
    };
    ObjLife5SetHooks(h);
    float pt[3] = {1, 1, 1};
    float ang[3] = {0, 0, 0};
    char r = ObjectApplyParentTransform(&n, pt, ang);
    CHECK_EQ(r, (char)1);
    CHECK_EQ(g_posCall, 1);
    CHECK_EQ(g_wtCall, 2);
    ObjLife5ResetHooks();
}

// ---------------------------------------------------------------------------
// AssignMeshData — binds the LOD mesh into +460 and sets the dirty bit (0x04).
// ---------------------------------------------------------------------------
static Node5 g_mesh;
TEST(ObjLife5_AssignMeshData, BindsMeshAndDirty) {
    ObjLife5ResetHooks();
    Node5 n;
    n.p(460) = nullptr;
    n.b(528) = 0;
    int finalize = 0;
    static int* fin = &finalize;
    ObjLife5Hooks h;
    h.meshSelectLodFrame = [](Node5*) { return &g_mesh; };
    h.meshFinalize = [](Node5*, Node5*) { ++*fin; };
    ObjLife5SetHooks(h);
    int r = ObjectAssignMeshData(&n, false);
    CHECK_EQ(r, (int)(intptr_t)&g_mesh);
    CHECK(n.p(460) == &g_mesh);
    CHECK_EQ(n.b(528) & 4u, 4u);
    CHECK_EQ(finalize, 1);
    ObjLife5ResetHooks();
}

TEST(ObjLife5_AssignMeshData, NoMeshNoChange) {
    ObjLife5ResetHooks();
    Node5 n;
    n.d(460) = 0x1234;
    n.b(528) = 0;
    // no hook -> SelectLodFrame returns null
    int r = ObjectAssignMeshData(&n, false);
    CHECK_EQ(r, 0);
    CHECK_EQ(n.d(460), (guild::i32)0x1234);
    CHECK_EQ(n.b(528), (guild::u8)0);
}

// ---------------------------------------------------------------------------
// RebindParentMesh — guard chain + light-cache rebuild gating.
// ---------------------------------------------------------------------------
TEST(ObjLife5_RebindParentMesh, RebuildsWhenSkeletonValid) {
    ObjLife5ResetHooks();
    Node5 n;
    n.p(508) = nullptr;
    n.p(496) = nullptr;
    static char skel[400] = {0};
    static char boneTable[8] = {1, 0, 0, 0, 0, 0, 0, 0};
    SetPtr(skel, 260, boneTable);   // boneTable present
    n.p(492) = skel;

    int cache = 0, upload = 0, attach = 0;
    static int *pc = &cache, *pu = &upload, *pa = &attach;
    ObjLife5Hooks h;
    h.lightBuildObjectCache = [](Node5*) { ++*pc; };
    h.textureUploadAllRecords = []() { ++*pu; };
    h.meshAttachStockObjectLods = [](Node5*, const char*, int) { ++*pa; };
    ObjLife5SetHooks(h);

    char r = ObjectRebindParentMesh(&n, 7, "newmesh");
    CHECK_EQ(r, (char)1);
    CHECK_EQ(cache, 1);
    CHECK_EQ(upload, 1);
    CHECK_EQ(attach, 1);
    ObjLife5ResetHooks();
}

TEST(ObjLife5_RebindParentMesh, NoCacheWhenNoBoneTable) {
    ObjLife5ResetHooks();
    Node5 n;
    n.p(492) = nullptr;  // no skeleton -> return 0
    CHECK_EQ(ObjectRebindParentMesh(&n, 0, "m"), (char)0);
    CHECK_EQ(ObjectRebindParentMesh(nullptr, 0, "m"), (char)0);
}

// ---------------------------------------------------------------------------
// ChangeTransparency — APPLY clones a poly texture; RESTORE detaches it.
// ---------------------------------------------------------------------------
TEST(ObjLife5_ChangeTransparency, ApplyAndRestore) {
    ObjLife5ResetHooks();
    // submesh layout: +4 polyArray, +12 polyCount, +16 meshObj,
    //                 +376 currentColor (init 0xFF), +378 stateByte.
    static char submesh[400] = {0};
    static char meshObj[600] = {0};
    *reinterpret_cast<int*>(meshObj + 480) = 4;  // cap >= 0
    SetPtr(submesh, 16, meshObj);

    static char polys[200] = {0};
    // 2 polys, stride 40, texture id @+20.
    *reinterpret_cast<int*>(polys + 0 * 40 + 20) = 111;
    *reinterpret_cast<int*>(polys + 1 * 40 + 20) = 111;  // same texture -> dedup
    SetPtr(submesh, 4, polys);
    *reinterpret_cast<int*>(submesh + 12) = 2;  // polyCount
    *reinterpret_cast<unsigned char*>(submesh + 376) = 0xFF;  // not applied
    *reinterpret_cast<unsigned char*>(submesh + 378) = 0;

    static char scratch[256];
    ObjLife5Hooks h;
    h.memAlloc = [](int, const char*) -> void* { return (void*)scratch; };
    h.memFree = [](void*) {};
    h.textureCloneIfPaletteMatch = [](int tex, int) { return tex + 1000; };
    h.textureDetachClone = [](int tex) { return tex - 1000; };
    h.lightApplyVertexShading = [](Node5*, void*) -> guild::u8 { return 1; };
    ObjLife5SetHooks(h);

    Node5 n;
    n.b(529) = 0;
    char r = ObjectChangeTransparency(&n, submesh, 0x40);  // APPLY color 0x40
    CHECK_EQ(r, (char)1);
    // both polys swapped to the clone (111 -> 1111).
    CHECK_EQ(*reinterpret_cast<int*>(polys + 0 * 40 + 20), 1111);
    CHECK_EQ(*reinterpret_cast<int*>(polys + 1 * 40 + 20), 1111);
    CHECK_EQ(*reinterpret_cast<int*>(submesh + 376), 0x40);
    CHECK_EQ(n.b(529) & 0x80u, 0x80u);

    // RESTORE: color 0xFF (no high bit) -> detach clone back to original.
    char r2 = ObjectChangeTransparency(&n, submesh, 0xFF);
    CHECK_EQ(r2, (char)1);
    CHECK_EQ(*reinterpret_cast<int*>(polys + 0 * 40 + 20), 111);  // 1111 - 1000
    CHECK_EQ(*reinterpret_cast<unsigned char*>(submesh + 376), (unsigned char)0xFF);
    CHECK_EQ(n.b(529) & 0x80u, 0u);
    ObjLife5ResetHooks();
}

TEST(ObjLife5_ChangeTransparency, NullSubmeshNoOp) {
    ObjLife5ResetHooks();
    Node5 n;
    CHECK_EQ(ObjectChangeTransparency(&n, nullptr, 0x40), (char)1);
}

// ---------------------------------------------------------------------------
// ChangeTransparencySubMeshes — iterates submeshes (stride 384, count @+2316).
// ---------------------------------------------------------------------------
static int g_ctCalls = 0;
TEST(ObjLife5_ChangeTransparencySub, DrivesEachSubmesh) {
    ObjLife5ResetHooks();
    static char skeleton[4000] = {0};
    skeleton[2316] = 3;  // 3 submeshes
    // each submesh @ skeleton+244+i*384 must have a valid meshObj for cap test;
    // give each a meshObj with cap 0 and 0 polys so ChangeTransparency is a no-op
    // mutation but still increments our observer through lightApplyVertexShading.
    static char meshObj[600] = {0};
    *reinterpret_cast<int*>(meshObj + 480) = 0;  // cap == 0 (>=0)
    for (int i = 0; i < 3; ++i) {
        char* sm = skeleton + 244 + i * 384;
        SetPtr(sm, 16, meshObj);
        *reinterpret_cast<int*>(sm + 12) = 0;     // 0 polys
        *reinterpret_cast<unsigned char*>(sm + 376) = 0xFF;
        *reinterpret_cast<unsigned char*>(sm + 378) = 0;
    }
    g_ctCalls = 0;
    ObjLife5Hooks h;
    h.lightApplyVertexShading = [](Node5*, void*) -> guild::u8 {
        ++g_ctCalls;
        return 1;
    };
    ObjLife5SetHooks(h);

    Node5 n;
    n.p(492) = skeleton;
    int color = 0x33;
    char r = ObjectChangeTransparencySubMeshes(&n, &color);
    CHECK_EQ(r, (char)1);
    CHECK_EQ(g_ctCalls, 3);  // ChangeTransparency invoked once per submesh
    ObjLife5ResetHooks();
}

// ---------------------------------------------------------------------------
// Suspend-state push/restore round trip.
// ---------------------------------------------------------------------------
TEST(ObjLife5_Suspend, ToggleNamedPushAndRestore) {
    ObjLife5ResetHooks();
    Node5 n;
    n.b(533) = 5;  // active nodeType
    n.b(534) = 0;
    // push (enable==0): saves 533 into 534, sets 533=1.
    char r1 = ObjectToggleSuspendStateNamed(&n, 0);
    CHECK_EQ(r1, (char)1);
    CHECK_EQ(n.b(533), (guild::u8)1);
    CHECK_EQ(n.b(534), (guild::u8)5);

    // restore (enable!=0 && 533==1): restores 533 from 534 (==5).
    int refresh = 0;
    static int* pr = &refresh;
    ObjLife5Hooks h;
    h.lightRefreshAllObjects = []() { ++*pr; };
    ObjLife5SetHooks(h);
    char r2 = ObjectToggleSuspendStateNamed(&n, 1);
    CHECK_EQ(r2, (char)1);
    CHECK_EQ(n.b(533), (guild::u8)5);
    CHECK_EQ(refresh, 1);  // 534==5 triggers light refresh
    ObjLife5ResetHooks();
}

TEST(ObjLife5_Suspend, ToggleNamedStripsBangPrefix) {
    ObjLife5ResetHooks();
    Node5 n;
    n.b(533) = 1;       // already suspended
    n.b(534) = 6;
    std::strcpy(n.str(0), "!chest");  // leading '!'
    char r = ObjectToggleSuspendStateNamed(&n, 1);
    CHECK_EQ(r, (char)1);
    CHECK_EQ(std::strcmp(n.str(0), "chest"), 0);  // '!' stripped
    CHECK_EQ(n.b(533), (guild::u8)6);
}

TEST(ObjLife5_Suspend, RestoreOnlyState5Refreshes) {
    ObjLife5ResetHooks();
    int refresh = 0;
    static int* pr2 = &refresh;
    ObjLife5Hooks h;
    h.lightRefreshAllObjects = []() { ++*pr2; };
    ObjLife5SetHooks(h);

    Node5 n;
    n.b(533) = 1;
    n.b(534) = 6;  // NOT 5 -> no refresh in RestoreSuspendState
    char r = ObjectRestoreSuspendState(&n, 1);
    CHECK_EQ(r, (char)1);
    CHECK_EQ(n.b(533), (guild::u8)6);
    CHECK_EQ(refresh, 0);

    n.b(533) = 1;
    n.b(534) = 5;  // 5 -> refresh
    ObjectRestoreSuspendState(&n, 1);
    CHECK_EQ(refresh, 1);
    ObjLife5ResetHooks();
}

// ---------------------------------------------------------------------------
// DetachAndRelease — unlink then dispose.
// ---------------------------------------------------------------------------
TEST(ObjLife5_DetachAndRelease, UnlinkThenDispose) {
    ObjLife5ResetHooks();
    static int order = 0, unl = 0, dis = 0;
    order = 0;
    ObjLife5Hooks h;
    h.objUnlinkFromList = [](Node5*) { unl = ++order; };
    h.objDispose = [](Node5*) { dis = ++order; };
    ObjLife5SetHooks(h);
    Node5 n;
    CHECK_EQ(ObjectDetachAndRelease(&n), (char)1);
    CHECK_EQ(unl, 1);
    CHECK_EQ(dis, 2);
    CHECK_EQ(ObjectDetachAndRelease(nullptr), (char)0);
    ObjLife5ResetHooks();
}

// ---------------------------------------------------------------------------
// SetActiveCamera — only swaps when node is a camera (533==3) and differs.
// ---------------------------------------------------------------------------
TEST(ObjLife5_SetActiveCamera, SwapsOnlyForCameraNode) {
    ObjLife5ResetHooks();
    g_activeCamera5 = nullptr;
    int inval = 0;
    static int* pi = &inval;
    ObjLife5Hooks h;
    h.objInvalidateCurrent = [](guild::u8) { ++*pi; };
    ObjLife5SetHooks(h);

    Node5 cam;
    cam.b(533) = 3;  // camera
    int r = ObjectSetActiveCamera(&cam);
    CHECK_EQ(r, 1);
    CHECK(g_activeCamera5 == &cam);
    CHECK_EQ(inval, 1);

    // calling again: same node -> no change.
    int r2 = ObjectSetActiveCamera(&cam);
    CHECK_EQ(inval, 1);
    (void)r2;

    // non-camera node: no swap.
    Node5 other;
    other.b(533) = 5;
    ObjectSetActiveCamera(&other);
    CHECK(g_activeCamera5 == &cam);
    CHECK_EQ(inval, 1);
    ObjLife5ResetHooks();
    g_activeCamera5 = nullptr;
}
