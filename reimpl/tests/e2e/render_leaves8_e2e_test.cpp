#include "test.h"

#include "render/render_leaves8.h"

#include <cmath>
#include <cstring>

using namespace guild;
using namespace guild::render;

// ===========================================================================
// E2E: a small "prepare a light-affected object then bound it" flow that walks
// across several render_leaves8 leaves the way the per-object lighting pass does:
//
//   1. PrepareObjectCache resolves the object's LOD frame and caches radius^2.
//   2. RecomputeForObject drops + rebuilds the light's cache rows for the object
//      and stores the light's range^2.
//   3. ComputeWorldAabb folds the object's bound-mesh 8 corners into a world box.
//   4. Color leaves tag the object's debug colour; NotEqualRgb confirms a change.
//
// All cross-module callees are routed through one installed hook table; we count
// the side effects and assert the deterministic math results end to end.
// ===========================================================================

namespace {

template <class T> void PutAt(unsigned char* p, int off, T v) { std::memcpy(p + off, &v, sizeof(T)); }
template <class T> T    GetAt(const unsigned char* p, int off) { T v; std::memcpy(&v, p + off, sizeof(T)); return v; }

int g_assign = 0, g_remove = 0, g_illum = 0, g_select = 0;
unsigned char g_frame[64];

void* SelectFrame(void* /*obj*/) { ++g_select; return g_frame; }
void  Assign(void* /*o*/) { ++g_assign; }
void  Remove(void*, void*, void*) { ++g_remove; }
u8    Illum(void*, void**) { ++g_illum; return 1; }

} // namespace

TEST(RenderLeaves8E2E, LightPrepareRecomputeAndBoundFlow) {
    std::memset(g_frame, 0, sizeof(g_frame));
    PutAt<void*>(g_frame, 16, (void*)nullptr);   // no mesh record -> radius 0

    RenderLeaves8Hooks h = DefaultRenderLeaves8Hooks();
    h.selectLodFrame   = &SelectFrame;
    h.assignMeshData   = &Assign;
    h.removeCacheEntry = &Remove;
    h.illuminateObject = &Illum;
    InstallRenderLeaves8Hooks(h);
    g_assign = g_remove = g_illum = g_select = 0;

    // --- The scene object (carries pose, flags, bound mesh, debug colour) ---
    unsigned char obj[600];
    std::memset(obj, 0, sizeof(obj));
    PutAt<void*>(obj, 460, (void*)nullptr);   // no resolved frame yet
    PutAt<u8>(obj, 528, 0x04);                // flag bit 4 -> AssignMeshData in prepare

    // Step 1: prepare cache -> resolves frame, caches radius^2 (= 0 in isolation).
    PrepareObjectCache(obj);
    CHECK_EQ(g_select, 1);
    CHECK_EQ(GetAt<void*>(obj, 460), (void*)g_frame);
    CHECK_EQ(g_assign, 1);
    CHECK(std::fabs(GetAt<float>(obj, 484)) < 1e-6f);

    // --- A ranged light over the object ---
    unsigned char light[600];
    std::memset(light, 0, sizeof(light));
    PutAt<float>(light, 148, 1.0f);   // nonzero range gate
    PutAt<float>(light, 144, 4.0f);   // radius 4 -> range^2 16

    // Step 2: recompute the light's contribution -> stores range^2, re-illuminates.
    CHECK_EQ(RecomputeForObject(light, obj, nullptr), 1);
    CHECK_EQ(g_remove, 1);
    CHECK_EQ(g_illum, 1);
    CHECK(std::fabs(GetAt<float>(light, 484) - 16.0f) < 1e-4f);

    // Step 3: bound the object's bound mesh. Give it a real 8-corner mesh.
    static unsigned char corners[8 * 80];
    std::memset(corners, 0, sizeof(corners));
    float xs[8] = {-1, 2, -3, 4, -5, 6, -7, 8};
    float ys[8] = { 0, 0,  0, 0,  0, 0,  0, 0};
    float zs[8] = {-2,-2, -2,-2, -2,-2, -2, 50};
    for (int i = 0; i < 8; ++i) {
        PutAt<float>(corners, i*80 + 0, xs[i]);
        PutAt<float>(corners, i*80 + 4, ys[i]);
        PutAt<float>(corners, i*80 + 8, zs[i]);
    }
    unsigned char mesh[16];
    std::memset(mesh, 0, sizeof(mesh));
    PutAt<void*>(mesh, 0, corners);
    PutAt<i32>(mesh, 8, 0);
    PutAt<void*>(obj, 460, mesh);            // re-point bound mesh to a real one
    PutAt<void*>(obj, 508, (void*)nullptr);  // no children

    float mn[3] = {0,0,0}, mx[3] = {0,0,0};
    CHECK(ComputeWorldAabb(mn, obj, mx));
    CHECK(std::fabs(mn[0] - (-7.0f)) < 1e-5f);
    CHECK(std::fabs(mx[0] - ( 8.0f)) < 1e-5f);
    CHECK(std::fabs(mn[2] - (-2.0f)) < 1e-5f);
    CHECK(std::fabs(mx[2] - (50.0f)) < 1e-5f);

    // Step 4: colour-tag the object's debug colour and confirm the change.
    u8 before[3] = {0, 0, 0};
    u8 colour[3];
    std::memcpy(colour, before, 3);
    SetRgb(colour, /*r*/200, /*g*/100, /*b*/50);
    CHECK_EQ(colour[0], 200);   // r
    CHECK_EQ(colour[1], 50);    // b
    CHECK_EQ(colour[2], 100);   // g
    CHECK(NotEqualRgb(colour, before) != 0);
    u8 same[3] = {200, 50, 100};
    CHECK_EQ(NotEqualRgb(colour, same), 0);

    InstallRenderLeaves8Hooks(DefaultRenderLeaves8Hooks());
}

// A scene-level bound with no objects in the walk: ComputeObjectAabb must report
// the sentinel-seeded empty box (the engine's "nothing visible" extents).
TEST(RenderLeaves8E2E, SceneBoundEmptyWalkSentinels) {
    InstallRenderLeaves8Hooks(DefaultRenderLeaves8Hooks());
    float mn[3], mx[3];
    ComputeObjectAabb(mn, mx);
    for (int i = 0; i < 3; ++i) {
        CHECK(mn[i] > 9.0e9f);     // +1e10 seed
        CHECK(mx[i] < -9.0e9f);    // -1e10 seed
    }
}
