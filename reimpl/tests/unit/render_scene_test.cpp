#include "tests/framework/test.h"

#include "render/scene.h"
#include "render/mesh.h"
#include "render/frame.h"
#include "render/light.h"  // ShadeRampOffset (768*idx)

#include <string>
#include <vector>

using namespace guild::render;
using guild::i16;
using guild::i32;
using guild::u8;
using guild::u32;

// ---------------------------------------------------------------------------
// Draw-list append (via the mesh project stage) + radix sort.
// ---------------------------------------------------------------------------

namespace {
// Build a draw list directly from a set of light indices, then sort it.
// Golden order (python reference, stable LSB radix on key=768*L):
//   lights  [5,1,3,1,7,0,3,2] -> sorted ptr order [5,1,3,7,2,6,0,4]
//   keys    [0,768,768,1536,2304,2304,3840,5376]
void buildEntries(std::vector<DrawListEntry>& e, const std::vector<int>& lights,
                  std::vector<Polygon>& polyStore) {
    polyStore.assign(lights.size(), Polygon{});
    e.assign(lights.size(), DrawListEntry{});
    for (size_t i = 0; i < lights.size(); ++i) {
        e[i].sortKey = (guild::u32)(768 * lights[i]);
        e[i].poly = &polyStore[i];
    }
}
} // namespace

TEST(RenderSceneUnit, SortKeyIs768TimesLight) {
    // The sort key is 768 * max vertex light index (256*3 = one shade ramp stride).
    CHECK_EQ((int)ShadeRampOffset(0), 0);
    CHECK_EQ((int)ShadeRampOffset(60), 768 * 60);
    CHECK_EQ((int)ShadeRampOffset(255), 768 * 255);
    CHECK(768 * 255 > 0xFFFF);  // exceeds 16 bits -> needs the full 4-pass sort
}

TEST(RenderSceneUnit, RadixSortStableByKey) {
    std::vector<int> lights = {5, 1, 3, 1, 7, 0, 3, 2};
    std::vector<Polygon> polyStore;
    std::vector<DrawListEntry> e;
    buildEntries(e, lights, polyStore);

    // Two ping-pong buffers, capacity = count.
    std::vector<DrawListEntry> list1 = e;
    std::vector<DrawListEntry> list2(e.size());
    DrawListBuffers db{};
    db.base1 = list1.data();
    db.base2 = list2.data();
    db.count = (int)e.size();
    db.capacity = (int)e.size();

    RadixSortDrawList(db, (guild::u32)e.size(), /*twoPassOnly*/ false);

    // Final sorted order lives in base1 (PolyList1) after 4 passes.
    const int wantPtr[8] = {5, 1, 3, 7, 2, 6, 0, 4};
    const int wantKey[8] = {0, 768, 768, 1536, 2304, 2304, 3840, 5376};
    for (int i = 0; i < 8; ++i) {
        CHECK_EQ((int)db.base1[i].sortKey, wantKey[i]);
        // Stable: equal keys keep their original (input) relative order.
        CHECK_EQ((int)(db.base1[i].poly - polyStore.data()), wantPtr[i]);
    }
}

TEST(RenderSceneUnit, RadixSortSingleEntryNoOp) {
    std::vector<int> lights = {42};
    std::vector<Polygon> polyStore;
    std::vector<DrawListEntry> e;
    buildEntries(e, lights, polyStore);
    std::vector<DrawListEntry> l1 = e, l2(1);
    DrawListBuffers db{};
    db.base1 = l1.data(); db.base2 = l2.data(); db.count = 1; db.capacity = 1;
    guild::u32 r = RadixSortDrawList(db, 1, false);
    CHECK_EQ((int)r, 1);
    CHECK_EQ((int)db.base1[0].sortKey, 768 * 42);  // untouched
}

TEST(RenderSceneUnit, RadixSortTwoPassSortsLow16Bits) {
    // Keys whose low 16 bits already determine the order (all L < 86, key < 65536).
    std::vector<int> lights = {10, 2, 7, 2, 80};
    std::vector<Polygon> polyStore;
    std::vector<DrawListEntry> e;
    buildEntries(e, lights, polyStore);
    std::vector<DrawListEntry> l1 = e, l2(e.size());
    DrawListBuffers db{};
    db.base1 = l1.data(); db.base2 = l2.data();
    db.count = (int)e.size(); db.capacity = (int)e.size();
    RadixSortDrawList(db, (guild::u32)e.size(), /*twoPassOnly*/ true);
    // After 2 passes the result is in base1, ascending by key.
    int prev = -1;
    for (int i = 0; i < (int)e.size(); ++i) {
        CHECK((int)db.base1[i].sortKey >= prev);
        prev = (int)db.base1[i].sortKey;
    }
    // Stable for the two equal keys (768*2): ptrs 1 then 3.
    int firstTwo = -1, secondTwo = -1;
    for (int i = 0; i < (int)e.size(); ++i) {
        if (db.base1[i].sortKey == (guild::u32)(768 * 2)) {
            int idx = (int)(db.base1[i].poly - polyStore.data());
            if (firstTwo < 0) firstTwo = idx; else secondTwo = idx;
        }
    }
    CHECK_EQ(firstTwo, 1);
    CHECK_EQ(secondTwo, 3);
}

// ---------------------------------------------------------------------------
// Project + append through the mesh stage, feeding the draw-list buffers, then
// sort: the full append->sort path the frame walk performs.
// ---------------------------------------------------------------------------

namespace {
// Two independent single-triangle meshes with different light indices, both
// front-facing + on-screen, so each appends exactly one draw-list entry.
struct TriMesh {
    Vertex v[3];
    Polygon p[1];
    MeshGeometry geom{};
    TriMesh(float baseY) {
        for (auto& vx : v) vx = Vertex{};
        // model verts chosen so the projected winding is back-facing (we set the
        // double-sided flag so the single tri is always appended).
        v[0] = Vertex{}; v[0].x = -1; v[0].y = baseY + 0; v[0].z = 4;
        v[1] = Vertex{}; v[1].x =  3; v[1].y = baseY + 0; v[1].z = 4;
        v[2] = Vertex{}; v[2].x =  1; v[2].y = baseY + 4; v[2].z = 12;
        p[0] = Polygon{}; p[0].v0 = &v[0]; p[0].v1 = &v[1]; p[0].v2 = &v[2];
        geom.vertices = v; geom.polygons = p;
        geom.polyCount = 1; geom.polyCap = 1; geom.vertexCount = 3;
    }
    MeshGeometry* View() { return &geom; }
};

ProjectParams stdParams() {
    ProjectParams pp{};
    pp.eye[0] = pp.eye[1] = pp.eye[2] = 0;
    pp.invDepth[0] = 0; pp.invDepth[1] = 0.5f; pp.invDepth[2] = 0;
    pp.biasX = 0.875f; pp.scaleX = 0.25f; pp.scaleY = 10.0f;
    pp.lightCap = 254.0f; pp.screenW = 1000.0f;
    return pp;
}
} // namespace

TEST(RenderSceneUnit, ProjectAppendThenSortOrders) {
    // mesh A: baseY=0 -> verts at y 0/0/4 -> light terms 0->1, 4*10=40 => maxlight 40
    // mesh B: baseY=5 -> verts at y 5/5/9 -> light terms 50,50,90 => maxlight 90
    // After sort by key=768*L the lower-light mesh A (40) precedes mesh B (90).
    TriMesh a(0.0f), b(5.0f);
    ProjectParams pp = stdParams();

    std::vector<DrawListEntry> l1(8), l2(8);
    DrawListBuffers db{};
    db.base1 = l1.data(); db.base2 = l2.data(); db.count = 0; db.capacity = 8;

    // Append mesh B first, mesh A second, to prove the SORT (not append order)
    // determines the final order.
    DrawList sinkB = db.AppendSink();
    ProjectVerticesToScreen(b.View(), pp, /*objFlags530*/ 0x40, /*viewCull42*/ 0, &sinkB);
    db.count = sinkB.count;

    DrawList sinkA = db.AppendSink();
    sinkA.count = db.count;  // continue appending after B
    ProjectVerticesToScreen(a.View(), pp, 0x40, 0, &sinkA);
    db.count = sinkA.count;

    CHECK_EQ(db.count, 2);
    // Before sort: entry 0 = B (key 768*90), entry 1 = A (key 768*40).
    CHECK_EQ((int)db.base1[0].sortKey, 768 * 90);
    CHECK_EQ((int)db.base1[1].sortKey, 768 * 40);

    RadixSortDrawList(db, (guild::u32)db.count, /*twoPassOnly*/ false);

    // After sort: ascending key -> A (40) then B (90).
    CHECK_EQ((int)db.base1[0].sortKey, 768 * 40);
    CHECK(db.base1[0].poly == &a.p[0]);
    CHECK_EQ((int)db.base1[1].sortKey, 768 * 90);
    CHECK(db.base1[1].poly == &b.p[0]);
}

// ---------------------------------------------------------------------------
// MeshGeometry::View helper — recompute the geom view of a Model is covered in
// the mesh tests; here we exercise the scene-graph node WALK control flow.
// ---------------------------------------------------------------------------

namespace {
// A tiny scene graph: root -> [n0 -> n1 -> n2(terminator)], n1 has one child c.
// We tag each node visited (callback returns >0 to descend, except n1's child
// callback returns <0 to prove the "visit but no descend" path).
struct WNode {
    int id;
    bool flag = true;          // testFlag result
    WNode* childHead = nullptr;
    WNode* next = nullptr;
    bool stop = false;         // +528 bit0 terminator
};
struct WalkCtx {
    std::vector<int> visited;
};
bool wTestFlag(void* n, guild::i16) { return ((WNode*)n)->flag; }
char wInvoke(void* n, void* ctx, guild::i32) {
    auto* node = (WNode*)n;
    ((WalkCtx*)ctx)->visited.push_back(node->id);
    return 1;  // >0 = descend children
}
void* wChild(void* n) { return ((WNode*)n)->childHead; }
void* wSibling(void* n) { return ((WNode*)n)->next; }
bool wStop(void* n) { return ((WNode*)n)->stop; }
} // namespace

TEST(RenderSceneUnit, SceneGraphWalkVisitsSiblingsAndChildren) {
    WalkCtx ctx;
    WNode c{10};
    WNode n0{0}, n1{1}, n2{2};
    n1.childHead = &c;
    n0.next = &n1; n1.next = &n2;  // sibling chain
    WNode root{-1};
    root.childHead = &n0;          // root's child list head

    WalkVTable vt{};
    vt.testFlag = wTestFlag; vt.invoke = wInvoke;
    vt.child = wChild; vt.sibling = wSibling; vt.stopAtSibling = wStop;

    // node==null path: descend root's child list.
    char r = WalkAndInvoke(&root, nullptr, &ctx, /*walkMask*/ 0x1FF, 0, vt);
    CHECK_EQ((int)r, 1);
    // Visit order: n0, n1, c (n1's child), n2.
    CHECK_EQ((int)ctx.visited.size(), 4);
    CHECK_EQ(ctx.visited[0], 0);
    CHECK_EQ(ctx.visited[1], 1);
    CHECK_EQ(ctx.visited[2], 10);  // child of n1 before sibling n2
    CHECK_EQ(ctx.visited[3], 2);
}

TEST(RenderSceneUnit, SceneGraphWalkEmptyMaskAborts) {
    WalkCtx ctx;
    WNode n0{0};
    WalkVTable vt{};
    vt.testFlag = wTestFlag; vt.invoke = wInvoke;
    vt.child = wChild; vt.sibling = wSibling; vt.stopAtSibling = wStop;
    // walkMask 0 => immediate 0, nothing visited.
    char r = WalkAndInvoke(&n0, &n0, &ctx, /*walkMask*/ 0, 0, vt);
    CHECK_EQ((int)r, 0);
    CHECK_EQ((int)ctx.visited.size(), 0);
}

// ---------------------------------------------------------------------------
// Frame walk orchestration: the reentrancy guard + gate flags + subsystem order.
// ---------------------------------------------------------------------------

namespace {
struct HookLog {
    std::vector<const char*> calls;
    int appended = 0;
};
HookLog* g_log = nullptr;
void hClearRect() { g_log->calls.push_back("clearRect"); }
void hClearVp()   { g_log->calls.push_back("clearViewport"); }
void hTerrain(void*, char) { g_log->calls.push_back("terrain"); }
void hResetLights() { g_log->calls.push_back("resetLights"); }
i32  hSceneWalk(char) { g_log->calls.push_back("sceneWalk"); return g_log->appended; }
void hParticles(char) { g_log->calls.push_back("particles"); }
void hSkyFlares() { g_log->calls.push_back("skyFlares"); }
void hMirrors(char) { g_log->calls.push_back("mirrors"); }
i32  hTimeNow() { return 1000; }
// wave-15: DrawUniverseAndStats a2-block hooks (ScrollUvCoords, the unconditional
// projection walk, and the a3-gated 64-list animation pose walk). One non-empty
// list (index 0, single node) so "anim" fires exactly once.
void hScrollUv(i32) { g_log->calls.push_back("scrollUv"); }
void hProjectWalk(i16, i32) { g_log->calls.push_back("projectWalk"); }
static int s_sentinel = 0, s_node = 0;
void* hAnimHead(int k) { return (k == 0) ? (void*)&s_node : nullptr; }
void* hAnimSentinel() { return (void*)&s_sentinel; }
void* hAnimNext(void*) { return (void*)&s_sentinel; }
void  hAnimPose(void*, i16, i32) { g_log->calls.push_back("anim"); }

FrameHooks fullHooks() {
    FrameHooks h{};
    h.clearRect = hClearRect; h.clearViewport = hClearVp;
    h.renderTerrain = hTerrain; h.resetLights = hResetLights;
    h.sceneWalk = hSceneWalk; h.renderParticles = hParticles;
    h.updateSkyFlares = hSkyFlares; h.buildMirrors = hMirrors;
    h.scrollUvCoords = hScrollUv; h.projectWalk = hProjectWalk;
    h.animListHead = hAnimHead; h.animSentinel = hAnimSentinel;
    h.animNext = hAnimNext; h.animPose = hAnimPose;
    h.timeNow = hTimeNow;
    h.terrain = (void*)1; h.world = (void*)1;
    return h;
}
} // namespace

TEST(RenderSceneUnit, FrameWalkOrderAndGate) {
    HookLog log; g_log = &log; log.appended = 7;
    FrameHooks h = fullHooks();

    FrameState fs{};
    fs.engineOn = true; fs.hasWorld = true; fs.hasTerrain = true;
    fs.useViewportClear = false; fs.uDelay = 1;

    RenderMainViewFrame(fs, h);

    // Expected subsystem order across Begin + DrawUniverseAndStats (wave-15 1:1):
    //   main-view clearRect, begin's clearRect, terrain, resetLights, sceneWalk,
    //   particles, skyFlares, mirrors, then DrawUniverseAndStats' a2 block:
    //   scrollUv, projectWalk, anim (the 64-list pose walk, one non-empty list).
    const char* want[] = {"clearRect", "clearRect", "terrain", "resetLights",
                          "sceneWalk", "particles", "skyFlares", "mirrors",
                          "scrollUv", "projectWalk", "anim"};
    CHECK_EQ((int)log.calls.size(), 11);
    for (int i = 0; i < 11; ++i) CHECK(std::string(log.calls[i]) == want[i]);

    // sceneWalk's appended count is snapshotted.
    CHECK_EQ(fs.appendedPolys, 7);
    // Reentrancy guard balanced back to 0.
    CHECK_EQ(fs.reentrancy, 0);
}

TEST(RenderSceneUnit, FrameWalkDisabledEngineDoesNothing) {
    HookLog log; g_log = &log;
    FrameHooks h = fullHooks();
    FrameState fs{};
    fs.engineOn = false;  // master gate off
    RenderMainViewFrame(fs, h);
    CHECK_EQ((int)log.calls.size(), 0);
}

TEST(RenderSceneUnit, FrameWalkReentrancyGuardBlocks) {
    HookLog log; g_log = &log;
    FrameHooks h = fullHooks();
    FrameState fs{};
    fs.engineOn = true; fs.hasWorld = true; fs.reentrancy = 1;  // already inside
    RenderMainViewFrame(fs, h);
    CHECK_EQ((int)log.calls.size(), 0);
}

TEST(RenderSceneUnit, FrameWalkViewportClearSelects) {
    HookLog log; g_log = &log; log.appended = 0;
    FrameHooks h = fullHooks();
    FrameState fs{};
    fs.engineOn = true; fs.hasWorld = true; fs.useViewportClear = true;
    fs.hasTerrain = false; fs.uDelay = 1;
    RenderMainViewFrame(fs, h);
    // useViewportClear => clearViewport (twice: main view + begin), never clearRect.
    int vp = 0, rect = 0;
    for (auto* c : log.calls) {
        if (std::string(c) == "clearViewport") ++vp;
        if (std::string(c) == "clearRect") ++rect;
    }
    CHECK_EQ(vp, 2);
    CHECK_EQ(rect, 0);
}
