#include "tests/framework/test.h"

#include "render/scene.h"
#include "render/frame.h"
#include "render/mesh.h"
#include "render/camera.h"
#include "render/raster.h"
#include "render/surface.h"
#include "render/geometry_types.h"

#include <vector>

using namespace guild::render;
using guild::i16;
using guild::i32;
using guild::u8;
using guild::u32;

// =============================================================================
// End-to-end: a small scene (two mesh nodes in a scene graph + a camera), run the
// frame walk to produce a sorted draw list, rasterize it into a framebuffer, and
// verify the draw-list order + a few framebuffer pixels against a reference.
//
// Wiring: the FrameHooks.sceneWalk callback runs the real WalkAndInvoke over a
// two-node sibling list; each node's invoke callback projects its mesh via the
// real ProjectVerticesToScreen, appending into the real DrawListBuffers. After the
// frame walk, the draw list is radix-sorted (real RadixSortDrawList) and flushed
// to the software flat-triangle rasterizer.
// =============================================================================

namespace {

// A scene node owning one single-triangle mesh + the projection it appends with.
struct SceneMeshNode {
    Vertex   v[3];
    Polygon  p[1];
    MeshGeometry geom{};
    SceneMeshNode* next = nullptr;
    bool flag = true;
    int  id;

    // build a triangle whose projected screen verts (px) and light index are known.
    SceneMeshNode(int id_, float x0, float y0, float x1, float y1,
                  float x2, float y2, int light)
        : id(id_) {
        for (auto& vx : v) vx = Vertex{};
        // With the projection params below: screenX = x, screenY = z, light = y.
        // We store the desired screen-x in model x, desired screen-y in model z,
        // and the desired light index in model y.
        v[0].x = x0; v[0].z = y0; v[0].y = (float)light;
        v[1].x = x1; v[1].z = y1; v[1].y = (float)light;
        v[2].x = x2; v[2].z = y2; v[2].y = (float)light;
        p[0] = Polygon{}; p[0].v0 = &v[0]; p[0].v1 = &v[1]; p[0].v2 = &v[2];
        geom.vertices = v; geom.polygons = p;
        geom.polyCount = 1; geom.polyCap = 1; geom.vertexCount = 3;
    }
};

// Identity-ish projection: screenX = x, screenY = z, lightIdx = clamp(y,1,254).
ProjectParams identityProject() {
    ProjectParams pp{};
    pp.eye[0] = pp.eye[1] = pp.eye[2] = 0;
    pp.invDepth[0] = 0; pp.invDepth[1] = 1.0f; pp.invDepth[2] = 0;
    pp.biasX = 0.0f; pp.scaleX = 1.0f; pp.scaleY = 1.0f;
    pp.lightCap = 254.0f; pp.screenW = 1000.0f;
    return pp;
}

// Shared state the sceneWalk hook reaches.
struct E2EScene {
    DrawListBuffers db{};
    ProjectParams pp = identityProject();
    std::vector<int> visited;
};
E2EScene* g_scene = nullptr;

// WalkVTable accessors for SceneMeshNode.
bool nTestFlag(void* n, guild::i16) { return ((SceneMeshNode*)n)->flag; }
char nInvoke(void* n, void* /*ctx*/, guild::i32) {
    auto* node = (SceneMeshNode*)n;
    g_scene->visited.push_back(node->id);
    DrawList sink = g_scene->db.AppendSink();
    sink.count = g_scene->db.count;
    // double-sided (0x40) so the single tri is appended regardless of winding.
    ProjectVerticesToScreen(&node->geom, g_scene->pp, /*objFlags530*/ 0x40,
                            /*viewCull42*/ 0, &sink);
    g_scene->db.count = sink.count;
    return 1;  // descend (no children here)
}
void* nChild(void*) { return nullptr; }
void* nSibling(void* n) { return ((SceneMeshNode*)n)->next; }
bool  nStop(void*) { return false; }

// The root of the scene graph; its child list head is the first mesh node.
struct Root { SceneMeshNode* childHead = nullptr; };
Root* g_root = nullptr;

// FrameHooks.sceneWalk: run the real WalkAndInvoke over the scene graph.
i32 e2eSceneWalk(char) {
    WalkVTable vt{};
    vt.testFlag = nTestFlag; vt.invoke = nInvoke;
    vt.child = nChild; vt.sibling = nSibling; vt.stopAtSibling = nStop;
    // Custom root accessor: the null-node branch descends root's child list. We
    // adapt by walking the child list directly through WalkAndInvoke with the
    // first node as the start.
    SceneMeshNode* first = g_root->childHead;
    if (first)
        WalkAndInvoke(g_root, first, nullptr, /*walkMask*/ 0x1FF, 0, vt);
    return g_scene->db.count;
}

// Minimal no-op subsystem hooks.
void nopClearRect() {}
i32  nopTime() { return 0; }

} // namespace

TEST(RenderSceneE2E, FrameWalkSortRasterize) {
    // ---- 1. Build the scene: two triangles, different light indices ----------
    // A: screen verts (2,2),(10,2),(6,10), light 40 -> covers rows ~2..10.
    // B: screen verts (2,12),(10,12),(6,20), light 90 -> covers rows ~12..20.
    SceneMeshNode a(/*id*/ 1, 2,2, 10,2, 6,10, /*light*/ 40);
    SceneMeshNode b(/*id*/ 2, 2,12, 10,12, 6,20, /*light*/ 90);
    // Append order B then A (sibling list b -> a) to prove the sort reorders them.
    b.next = &a;
    Root root; root.childHead = &b;

    // ---- 2. Draw-list buffers (ping-pong) -----------------------------------
    std::vector<DrawListEntry> l1(16), l2(16);
    E2EScene scene;
    scene.db.base1 = l1.data();
    scene.db.base2 = l2.data();
    scene.db.count = 0;
    scene.db.capacity = 16;
    g_scene = &scene;
    g_root = &root;

    // ---- 3. Run the frame walk ----------------------------------------------
    FrameHooks h{};
    h.clearRect = nopClearRect;
    h.sceneWalk = e2eSceneWalk;
    h.timeNow = nopTime;
    h.world = (void*)1;

    FrameState fs{};
    fs.engineOn = true; fs.hasWorld = true; fs.hasTerrain = false;
    fs.useViewportClear = false; fs.uDelay = 1;

    RenderMainViewFrame(fs, h);

    // The walk visited both nodes in sibling order (b then a).
    CHECK_EQ((int)scene.visited.size(), 2);
    CHECK_EQ(scene.visited[0], 2);
    CHECK_EQ(scene.visited[1], 1);

    // Two draw-list entries appended; pre-sort order is append order (B, A).
    CHECK_EQ(scene.db.count, 2);
    CHECK_EQ((int)scene.db.base1[0].sortKey, 768 * 90);  // B
    CHECK_EQ((int)scene.db.base1[1].sortKey, 768 * 40);  // A

    // ---- 4. Sort the draw list ----------------------------------------------
    RadixSortDrawList(scene.db, (guild::u32)scene.db.count, /*twoPassOnly*/ false);

    // Sorted ascending by light key: A (40) then B (90).
    CHECK_EQ((int)scene.db.base1[0].sortKey, 768 * 40);
    CHECK(scene.db.base1[0].poly == &a.p[0]);
    CHECK_EQ((int)scene.db.base1[1].sortKey, 768 * 90);
    CHECK(scene.db.base1[1].poly == &b.p[0]);

    // ---- 5. Rasterize the sorted draw list into an 8-bit framebuffer ---------
    Surface* fb = SurfaceCreate(32, 32, 8);
    CHECK(fb != nullptr);
    SurfaceColorFill(fb, 0, 0, 0);  // clear to 0

    for (int i = 0; i < scene.db.count; ++i) {
        Polygon* poly = scene.db.base1[i].poly;
        RasterVertex rv[3];
        rv[0] = RasterVertex{poly->v0->screenX, poly->v0->screenY, poly->v0->lightIdx};
        rv[1] = RasterVertex{poly->v1->screenX, poly->v1->screenY, poly->v1->lightIdx};
        rv[2] = RasterVertex{poly->v2->screenX, poly->v2->screenY, poly->v2->lightIdx};
        // Flat-fill each triangle with its light index as the 8-bit colour.
        RasterizeFlatTriangle(fb, rv, (guild::u8)poly->v0->lightIdx);
    }

    // ---- 6. Verify framebuffer pixels against the reference ------------------
    // Triangle A (light 40) covers the upper region; sample a pixel well inside it.
    // The triangle spans x=2..10 at y=2 narrowing to x=6 at y=10. Row 4, x=6 is
    // interior.
    guild::u8 px[3];
    SurfaceGetPixelRgb(fb, 6, 4, px);
    // 8-bit surface stores the raw index; SurfaceGetPixelRgb returns it broadcast.
    CHECK_EQ((int)fb->pixels[4 * fb->widthPx + 6], 40);

    // Triangle B (light 90) covers the lower region; row 14, x=6 interior.
    CHECK_EQ((int)fb->pixels[14 * fb->widthPx + 6], 90);

    // A pixel outside both triangles stays at the clear value 0.
    CHECK_EQ((int)fb->pixels[0 * fb->widthPx + 0], 0);

    // The two triangles do not overlap, so A's region is never overwritten by B.
    CHECK_EQ((int)fb->pixels[3 * fb->widthPx + 6], 40);

    SurfaceDestroy(fb);
    (void)px;
}
