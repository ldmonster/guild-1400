#include "test.h"

#include "render/render_leaves8.h"

#include <cmath>
#include <cstring>

using namespace guild;
using namespace guild::render;

// ===========================================================================
// Helpers: build the byte-offset records the leaves read. The originals touch
// everything as *(T*)(base + off); we mirror that with a flat byte buffer.
// ===========================================================================
namespace {

template <class T> void PutAt(unsigned char* p, int off, T v) { std::memcpy(p + off, &v, sizeof(T)); }
template <class T> T    GetAt(const unsigned char* p, int off) { T v; std::memcpy(&v, p + off, sizeof(T)); return v; }

} // namespace

// ---------------------------------------------------------------------------
// VIBE_Color_NotEqualRgb / VIBE_Color_SetRgb
// ---------------------------------------------------------------------------
TEST(RenderLeaves8Color, NotEqualRgbDetectsEachChannel) {
    u8 a[3] = {10, 20, 30};
    u8 b[3] = {10, 20, 30};
    CHECK_EQ(NotEqualRgb(a, b), 0);
    b[0] = 11; CHECK(NotEqualRgb(a, b) != 0); b[0] = 10;
    b[1] = 21; CHECK(NotEqualRgb(a, b) != 0); b[1] = 20;
    b[2] = 31; CHECK(NotEqualRgb(a, b) != 0); b[2] = 30;
    CHECK_EQ(NotEqualRgb(a, b), 0);
}

TEST(RenderLeaves8Color, SetRgbUsesVerbatimChannelOrder) {
    // The original stores: dst[0]=r, dst[1]=b, dst[2]=g.
    u8 dst[3] = {0, 0, 0};
    u8* ret = SetRgb(dst, /*r*/0x11, /*g*/0x22, /*b*/0x33);
    CHECK_EQ(ret, dst);
    CHECK_EQ(dst[0], 0x11);   // r
    CHECK_EQ(dst[1], 0x33);   // b
    CHECK_EQ(dst[2], 0x22);   // g
}

// ---------------------------------------------------------------------------
// VIBE_Render_SetWindowRect — eax->xMax, edx->yMax, ebx->xMin, ecx->yMin.
// ---------------------------------------------------------------------------
TEST(RenderLeaves8Render, SetWindowRectStoresInRegisterOrder) {
    WindowRect r;
    // SetWindowRect(out, a1=eax, a2=edx, a3=ecx, a4=ebx)
    i32 ret = SetWindowRect(r, /*a1*/640, /*a2*/480, /*a3*/0, /*a4*/16);
    CHECK_EQ(ret, 640);
    CHECK_EQ(r.xMax, 640);   // dword_75FB40 <- eax
    CHECK_EQ(r.yMax, 480);   // dword_75FB44 <- edx
    CHECK_EQ(r.xMin, 16);    // dword_75FB48 <- ebx (a4)
    CHECK_EQ(r.yMin, 0);     // dword_75FB4C <- ecx (a3)
}

// ---------------------------------------------------------------------------
// VIBE_Render_GetVertexBufferInfo — handle echoed, capacity fixed at 2048.
// ---------------------------------------------------------------------------
TEST(RenderLeaves8Render, GetVertexBufferInfoReportsHandleAndCap) {
    u32 h = 0, cap = 0;
    int rc = GetVertexBufferInfo(0xABCD1234u, &h, &cap);
    CHECK_EQ(rc, 0);
    CHECK_EQ(h, 0xABCD1234u);
    CHECK_EQ(cap, 2048u);
}

// ---------------------------------------------------------------------------
// VIBE_Mesh_ReleaseStockObject — guarded teardown on refcount [+476] > 0.
// ---------------------------------------------------------------------------
namespace {
int g_deleteCalls = 0;
void* DeleteHook(void* o) { ++g_deleteCalls; return o; }
}
TEST(RenderLeaves8Mesh, ReleaseStockObjectGuardsOnRefcount) {
    RenderLeaves8Hooks h = DefaultRenderLeaves8Hooks();
    h.deleteStockObject = &DeleteHook;
    InstallRenderLeaves8Hooks(h);
    g_deleteCalls = 0;

    CHECK_EQ(ReleaseStockObject(nullptr), (void*)nullptr);  // null -> no call
    CHECK_EQ(g_deleteCalls, 0);

    unsigned char rec[600];
    std::memset(rec, 0, sizeof(rec));
    PutAt<i32>(rec, 476, 0);           // refcount 0 -> no free
    CHECK_EQ(ReleaseStockObject(rec), (void*)rec);
    CHECK_EQ(g_deleteCalls, 0);

    PutAt<i32>(rec, 476, 5);           // refcount > 0 -> free
    CHECK_EQ(ReleaseStockObject(rec), (void*)rec);
    CHECK_EQ(g_deleteCalls, 1);

    PutAt<i32>(rec, 476, -3);          // negative -> no free (signed compare)
    CHECK_EQ(ReleaseStockObject(rec), (void*)rec);
    CHECK_EQ(g_deleteCalls, 1);

    InstallRenderLeaves8Hooks(DefaultRenderLeaves8Hooks());  // restore? leave defaults
}

// ---------------------------------------------------------------------------
// VIBE_Light_RefreshAllToggle — forwards *flag, returns 1.
// ---------------------------------------------------------------------------
namespace {
u8 g_lastRefreshFlag = 0xFF;
int g_refreshCalls = 0;
u8 RefreshHook(u8 f) { g_lastRefreshFlag = f; ++g_refreshCalls; return f; }
}
TEST(RenderLeaves8Light, RefreshAllToggleForwardsFlag) {
    RenderLeaves8Hooks h = DefaultRenderLeaves8Hooks();
    h.refreshAllObjects = &RefreshHook;
    InstallRenderLeaves8Hooks(h);
    g_refreshCalls = 0;

    u8 flag = 1;
    CHECK_EQ(RefreshAllToggle(&flag), 1);
    CHECK_EQ(g_refreshCalls, 1);
    CHECK_EQ(g_lastRefreshFlag, 1);

    flag = 0;
    CHECK_EQ(RefreshAllToggle(&flag), 1);
    CHECK_EQ(g_lastRefreshFlag, 0);

    InstallRenderLeaves8Hooks(DefaultRenderLeaves8Hooks());
}

// ---------------------------------------------------------------------------
// VIBE_Light_RequestObjectCache — null root short-circuits; else walks subtree.
// ---------------------------------------------------------------------------
namespace {
void* g_walkRoot = nullptr;
void* g_walkNode = (void*)0xDEAD;
i32   g_walkMask = -1;
u8 WalkHook(void* root, void* node, void (*)(void*), i32 mask) {
    g_walkRoot = root; g_walkNode = node; g_walkMask = mask; return 0x42;
}
}
TEST(RenderLeaves8Light, RequestObjectCacheWalksSubtreeOrShortCircuits) {
    RenderLeaves8Hooks h = DefaultRenderLeaves8Hooks();
    h.traverseTree = &WalkHook;
    InstallRenderLeaves8Hooks(h);

    CHECK_EQ((int)RequestObjectCache(nullptr), 0);  // null -> 0, no walk

    unsigned char root[600];
    std::memset(root, 0, sizeof(root));
    void* subRoot = (void*)0xCAFE;
    PutAt<void*>(root, 520, subRoot);   // [root+520] subtree head handle
    g_walkNode = (void*)0xBADBAD;
    u8 rc = RequestObjectCache(root);
    CHECK_EQ((int)rc, 0x42);
    CHECK_EQ(g_walkRoot, subRoot);
    CHECK_EQ(g_walkNode, (void*)root);   // node arg is the root object
    CHECK_EQ(g_walkMask, 576);

    InstallRenderLeaves8Hooks(DefaultRenderLeaves8Hooks());
}

// ---------------------------------------------------------------------------
// VIBE_Light_RecomputeForObject — range gate on light[+148], radius^2 cache.
// ---------------------------------------------------------------------------
namespace {
int g_removeCalls = 0, g_illumCalls = 0;
void RemoveHook(void*, void*, void*) { ++g_removeCalls; }
u8 IllumHook(void*, void**) { ++g_illumCalls; return 0; }
}
TEST(RenderLeaves8Light, RecomputeForObjectRadiusSquaredWhenRanged) {
    RenderLeaves8Hooks h = DefaultRenderLeaves8Hooks();
    h.removeCacheEntry = &RemoveHook;
    h.illuminateObject = &IllumHook;
    // pointToBoneLocalSpace stays at the inert default.
    InstallRenderLeaves8Hooks(h);

    unsigned char light[600];
    unsigned char obj[600];
    std::memset(light, 0, sizeof(light));
    std::memset(obj, 0, sizeof(obj));

    // No range (light[+148] == 0): only RemoveCacheEntry runs, no radius write.
    g_removeCalls = g_illumCalls = 0;
    PutAt<float>(light, 148, 0.0f);
    PutAt<float>(light, 484, 7.0f);   // sentinel: must remain untouched
    CHECK_EQ(RecomputeForObject(light, obj, nullptr), 1);
    CHECK_EQ(g_removeCalls, 1);
    CHECK_EQ(g_illumCalls, 0);
    CHECK(std::fabs(GetAt<float>(light, 484) - 7.0f) < 1e-6f);

    // Ranged (light[+148] != 0): light[+484] = light[+144]^2, illuminate runs.
    g_removeCalls = g_illumCalls = 0;
    PutAt<float>(light, 148, 1.0f);
    PutAt<float>(light, 144, 3.0f);   // radius
    PutAt<float>(light, 484, 0.0f);
    CHECK_EQ(RecomputeForObject(light, obj, nullptr), 1);
    CHECK_EQ(g_removeCalls, 1);
    CHECK_EQ(g_illumCalls, 1);
    CHECK(std::fabs(GetAt<float>(light, 484) - 9.0f) < 1e-5f);   // 3^2

    InstallRenderLeaves8Hooks(DefaultRenderLeaves8Hooks());
}

// ---------------------------------------------------------------------------
// VIBE_Light_PrepareObjectCache — LOD-frame resolve + radius^2 (0 in isolation).
// ---------------------------------------------------------------------------
namespace {
int g_selectCalls = 0, g_assignCalls = 0;
void* g_fakeFrame = nullptr;
void* SelectHook(void*) { ++g_selectCalls; return g_fakeFrame; }
void AssignHook(void*) { ++g_assignCalls; }
}
TEST(RenderLeaves8Light, PrepareObjectCacheResolvesFrameAndCachesRadius) {
    RenderLeaves8Hooks h = DefaultRenderLeaves8Hooks();
    h.selectLodFrame = &SelectHook;
    h.assignMeshData = &AssignHook;
    InstallRenderLeaves8Hooks(h);

    unsigned char obj[600];
    unsigned char frame[64];
    std::memset(obj, 0, sizeof(obj));
    std::memset(frame, 0, sizeof(frame));
    g_fakeFrame = frame;
    PutAt<void*>(frame, 16, (void*)nullptr);   // mesh record null -> radius stays 0

    // No frame and SelectLodFrame returns one: math runs, radius^2 = 0.
    g_selectCalls = g_assignCalls = 0;
    PutAt<void*>(obj, 460, (void*)nullptr);
    PutAt<u8>(obj, 528, 4);                    // flag bit 4 -> AssignMeshData
    PutAt<float>(obj, 484, 5.0f);              // sentinel
    PrepareObjectCache(obj);
    CHECK_EQ(g_selectCalls, 1);
    CHECK_EQ(GetAt<void*>(obj, 460), (void*)frame);   // resolved frame stored back
    CHECK_EQ(g_assignCalls, 1);                       // bit 4 set
    CHECK(std::fabs(GetAt<float>(obj, 484)) < 1e-6f); // radius^2 = 0 in isolation

    // SelectLodFrame returns null -> early bail, no AssignMeshData, sentinel kept.
    g_selectCalls = g_assignCalls = 0;
    g_fakeFrame = nullptr;
    std::memset(obj, 0, sizeof(obj));
    PutAt<u8>(obj, 528, 4);
    PutAt<float>(obj, 484, 5.0f);
    PrepareObjectCache(obj);
    CHECK_EQ(g_selectCalls, 1);
    CHECK_EQ(g_assignCalls, 0);
    CHECK(std::fabs(GetAt<float>(obj, 484) - 5.0f) < 1e-6f);

    InstallRenderLeaves8Hooks(DefaultRenderLeaves8Hooks());
}

// ---------------------------------------------------------------------------
// VIBE_Mesh_ComputeWorldAabb — 8-corner min/max fold.
// ---------------------------------------------------------------------------
TEST(RenderLeaves8Mesh, ComputeWorldAabbFoldsEightCorners) {
    InstallRenderLeaves8Hooks(DefaultRenderLeaves8Hooks());  // all inert (no frame, no children)

    // mesh record: [+0] corner base, [+8] start index. 8 corners, 80-byte stride.
    unsigned char obj[600];
    std::memset(obj, 0, sizeof(obj));
    unsigned char mesh[16];
    std::memset(mesh, 0, sizeof(mesh));

    static unsigned char corners[8 * 80];
    std::memset(corners, 0, sizeof(corners));
    // Deterministic corner coords; known min/max.
    float xs[8] = { 1, -2,  3, -4,  5, -6,  7, -8};
    float ys[8] = {10, 20, -5, 15,  0, 30, -1,  9};
    float zs[8] = { 2,  2,  2,  2,  2,  2,  2, 99};
    for (int i = 0; i < 8; ++i) {
        PutAt<float>(corners, i * 80 + 0, xs[i]);
        PutAt<float>(corners, i * 80 + 4, ys[i]);
        PutAt<float>(corners, i * 80 + 8, zs[i]);
    }
    PutAt<void*>(mesh, 0, corners);
    PutAt<i32>(mesh, 8, 0);              // start index 0
    PutAt<void*>(obj, 460, mesh);        // obj[+0x1CC] bound mesh
    PutAt<void*>(obj, 508, (void*)nullptr);  // no children

    float mn[3] = {0, 0, 0}, mx[3] = {0, 0, 0};
    bool ok = ComputeWorldAabb(mn, obj, mx);
    CHECK(ok);
    CHECK(std::fabs(mn[0] - (-8.0f)) < 1e-5f);
    CHECK(std::fabs(mn[1] - (-5.0f)) < 1e-5f);
    CHECK(std::fabs(mn[2] - (2.0f))  < 1e-5f);
    CHECK(std::fabs(mx[0] - (7.0f))  < 1e-5f);
    CHECK(std::fabs(mx[1] - (30.0f)) < 1e-5f);
    CHECK(std::fabs(mx[2] - (99.0f)) < 1e-5f);

    // No bound mesh -> returns false, leaves outputs untouched.
    std::memset(obj, 0, sizeof(obj));
    float mn2[3] = {1, 2, 3}, mx2[3] = {4, 5, 6};
    CHECK(!ComputeWorldAabb(mn2, obj, mx2));
    CHECK(std::fabs(mn2[0] - 1.0f) < 1e-6f);
    CHECK(std::fabs(mx2[2] - 6.0f) < 1e-6f);
}

// ---------------------------------------------------------------------------
// VIBE_Mesh_ComputeObjectAabb — sentinel seeds when the scene walk is empty.
// ---------------------------------------------------------------------------
TEST(RenderLeaves8Mesh, ComputeObjectAabbSeedsSentinels) {
    InstallRenderLeaves8Hooks(DefaultRenderLeaves8Hooks());   // empty walk (default)
    float mn[3] = {0, 0, 0}, mx[3] = {0, 0, 0};
    i32 r = ComputeObjectAabb(mn, mx);
    for (int i = 0; i < 3; ++i) {
        CHECK(std::fabs(mn[i] - kAabbSentinelMax) < 1.0f);   // +1e10
        CHECK(std::fabs(mx[i] - kAabbSentinelMin) < 1.0f);   // -1e10
    }
    float expect = kAabbSentinelMin;
    i32 expectBits; std::memcpy(&expectBits, &expect, sizeof(expectBits));
    CHECK_EQ(r, expectBits);   // returns box[5] bit pattern (= -1e10)
}

// ---------------------------------------------------------------------------
// VIBE_Light_AttachAtFrameMatrix — identity matrix -> zero euler (default hooks).
// ---------------------------------------------------------------------------
TEST(RenderLeaves8Light, AttachAtFrameMatrixIdentityYieldsScratch) {
    // Default computeBoneWorldMatrix writes identity; default matrixToEuler is a
    // no-op, so the scratch matrix is the identity verbatim.
    InstallRenderLeaves8Hooks(DefaultRenderLeaves8Hooks());
    unsigned char obj[600];
    std::memset(obj, 0, sizeof(obj));
    PutAt<i8>(obj, 528, 0);   // >= 0 -> relative (uses activeFrameObject, which is null)

    float m[16];
    for (int i = 0; i < 16; ++i) m[i] = 123.0f;   // poison
    AttachAtFrameMatrix(obj, m);
    // identity diagonal
    CHECK(std::fabs(m[0]  - 1.0f) < 1e-6f);
    CHECK(std::fabs(m[5]  - 1.0f) < 1e-6f);
    CHECK(std::fabs(m[10] - 1.0f) < 1e-6f);
    CHECK(std::fabs(m[15] - 1.0f) < 1e-6f);
    CHECK(std::fabs(m[1]) < 1e-6f);
}
