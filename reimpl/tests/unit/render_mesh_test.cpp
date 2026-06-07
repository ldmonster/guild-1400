#include "tests/framework/test.h"

#include "render/camera.h"
#include "render/mesh.h"
#include "render/scenegraph.h"
#include "render/anim.h"

#include <cmath>

using namespace guild::render;

namespace {
bool feq(float a, float b, float eps = 1e-5f) { return std::fabs(a - b) <= eps; }

// Build an axis-aligned unit cube as a MeshGeometry (8 verts, 12 tris).
struct Cube {
    Vertex v[8];
    Polygon p[12];
    MeshGeometry geom{};
    Cube() {
        const float c[8][3] = {
            {-1,-1,-1},{1,-1,-1},{1,1,-1},{-1,1,-1},
            {-1,-1, 1},{1,-1, 1},{1,1, 1},{-1,1, 1}};
        for (int i = 0; i < 8; ++i) { v[i] = Vertex{}; v[i].x=c[i][0]; v[i].y=c[i][1]; v[i].z=c[i][2]; }
        // 12 triangles (two per face), wound so screen-space front faces face +.
        const int idx[12][3] = {
            {0,1,2},{0,2,3}, {4,6,5},{4,7,6}, {0,4,5},{0,5,1},
            {2,6,7},{2,7,3}, {1,5,6},{1,6,2}, {0,3,7},{0,7,4}};
        for (int i = 0; i < 12; ++i) {
            p[i] = Polygon{};
            p[i].v0 = &v[idx[i][0]]; p[i].v1 = &v[idx[i][1]]; p[i].v2 = &v[idx[i][2]];
        }
        geom.vertices = v; geom.polygons = p; geom.polyCount = 12; geom.polyCap = 12;
        geom.vertexCount = 8;
    }
};
} // namespace

// ---- ProjectPoint: golden vectors from python (VIBE_Coord_ProjectPoint) -----
TEST(RenderGeom, ProjectPointGolden) {
    float cam[5] = {10.0f, 5.0f, 20.0f, 0.0f, 2.0f};  // eye, depth=2
    struct { float p[3]; int sx, sy; } cases[] = {
        {{12.0f, 0.0f, 24.0f}, 1, 2},
        {{0.0f,  0.0f, 0.0f},  -4, -9},
        {{30.0f, 0.0f, 40.0f}, 10, 10},
        {{11.0f, 0.0f, 21.0f}, 1, 1},
    };
    for (auto& c : cases) {
        guild::i32 out[2];
        ProjectPoint(cam, c.p, out);
        CHECK_EQ(out[0], c.sx);
        CHECK_EQ(out[1], c.sy);
    }
}

TEST(RenderGeom, ProjectFramePointWrapper) {
    float cam[5] = {0,0,0,0, 1.0f};
    // tileToWorld stub: returns world (tileX, 0, tileY) if tileX>=0, else fails.
    auto t2w = [](int tx, int ty, float* w, int) -> bool {
        if (tx < 0) return false;
        w[0] = (float)tx; w[1] = 0.0f; w[2] = (float)ty; return true;
    };
    guild::i32 out[2];
    CHECK_EQ(ProjectFramePoint(cam, 4, 6, out, t2w, 0), 1);
    CHECK_EQ(out[0], 4);  // trunc(4*1+0.5)=4
    CHECK_EQ(out[1], 6);
    CHECK_EQ(ProjectFramePoint(cam, -1, 6, out, t2w, 0), 0);
}

// ---- Mesh vertex projection + light index: golden vectors from python --------
TEST(RenderGeom, MeshVertexProjectGolden) {
    Cube cube;
    ProjectParams pp{};
    pp.eye[0]=0; pp.eye[1]=0; pp.eye[2]=0;
    pp.invDepth[0]=0; pp.invDepth[1]=0.5f; pp.invDepth[2]=0;
    pp.biasX = 0.875f; pp.scaleX = 0.25f; pp.scaleY = 10.0f;
    pp.lightCap = 254.0f; pp.screenW = 1000.0f;

    DrawListEntry dl[32];
    DrawList out{dl, 0, 32};
    ProjectVerticesToScreen(&cube.geom, pp, /*objFlags530*/0x40, /*viewCull42*/0, &out);

    // Golden (screenX, screenY, light) per vertex from python.
    struct { float sx, sy; int light; } g[8] = {
        {0.375f, 0.625f, 1}, {1.375f, 0.625f, 1}, {1.375f, 0.625f, 10}, {0.375f, 0.625f, 10},
        {0.375f, 1.125f, 1}, {1.375f, 1.125f, 1}, {1.375f, 1.125f, 10}, {0.375f, 1.125f, 10}};
    for (int i = 0; i < 8; ++i) {
        CHECK(feq(cube.v[i].screenX, g[i].sx));
        CHECK(feq(cube.v[i].screenY, g[i].sy));
        CHECK_EQ((int)cube.v[i].lightIdx, g[i].light);
    }
    // All faces project on-screen; with double-sided (0x40) every front-facing tri
    // is appended. At least the front faces must be present.
    CHECK(out.count > 0);
    CHECK(out.count <= 12);
}

// ---- Frustum classify + box-corner transform: golden from python -------------
TEST(RenderGeom, FrustumClassifyGolden) {
    // box: max corner (1,1,5), min corner (-1,-1,3).
    float box[11] = {0,0,0,0, 1,1,5, 0,-1,-1,3};
    float origin[3] = {0,0,0};
    float ident[16] = {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
    float corners[8 * 20] = {0};
    TransformNodeBoxCorners(box, origin, ident, corners);

    // Corner 0 = (1,1,5), corner 7 = (-1,-1,3).
    CHECK(feq(corners[0], 1) && feq(corners[1], 1) && feq(corners[2], 5));
    CHECK(feq(corners[7*20+0], -1) && feq(corners[7*20+1], -1) && feq(corners[7*20+2], 3));

    Frustum f{};
    for (int i = 0; i < 4; ++i) { f.plane[i][0]=0; f.plane[i][1]=0; f.plane[i][2]=0; f.plane[i][3]=-1e9f; }

    // Inside (near=0, far=10): not fully culled.
    f.nearZ = 0.0f; f.farZ = 10.0f;
    CHECK_EQ((int)ClassifyBoundingBoxPlanes(corners, f, nullptr, nullptr, nullptr, nullptr), 0x00);

    // Fully in front of nearZ=6 (all z<6): 0x40|0x10 = 0x50.
    f.nearZ = 6.0f; f.farZ = 10.0f;
    CHECK_EQ((int)ClassifyBoundingBoxPlanes(corners, f, nullptr, nullptr, nullptr, nullptr), 0x50);

    // Fully beyond farZ=2 (all z>2): 0x40|0x20 = 0x60.
    f.nearZ = 0.0f; f.farZ = 2.0f;
    CHECK_EQ((int)ClassifyBoundingBoxPlanes(corners, f, nullptr, nullptr, nullptr, nullptr), 0x60);
}

TEST(RenderGeom, FrustumMinMaxZ) {
    float box[11] = {0,0,0,0, 1,1,5, 0,-1,-1,3};
    float ident[16] = {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
    float origin[3] = {0,0,0};
    float corners[8 * 20] = {0};
    TransformNodeBoxCorners(box, origin, ident, corners);
    Frustum f{};
    for (int i = 0; i < 4; ++i) f.plane[i][3] = -1e9f;
    f.nearZ = 0.0f; f.farZ = 10.0f;
    float minZ = 0, maxZ = 0, runNear = 1e9f, runFar = -1e9f;
    ClassifyBoundingBoxPlanes(corners, f, &minZ, &maxZ, &runNear, &runFar);
    CHECK(feq(minZ, 3.0f));
    CHECK(feq(maxZ, 5.0f));
    CHECK(feq(runNear, 3.0f));
    CHECK(feq(runFar, 5.0f));
}

// ---- Animation blend at t=0/0.5/1: golden from python ------------------------
TEST(RenderGeom, VectorLerpGolden) {
    float a[3] = {0,0,0}, b[3] = {10,20,-4}, out[3];
    struct { float t; float r[3]; } g[3] = {
        {0.0f, {0,0,0}}, {0.5f, {5,10,-2}}, {1.0f, {10,20,-4}}};
    for (auto& c : g) {
        VectorLerp(a, b, c.t, out);
        CHECK(feq(out[0], c.r[0]));
        CHECK(feq(out[1], c.r[1]));
        CHECK(feq(out[2], c.r[2]));
    }
}

TEST(RenderGeom, BoneTranslationBlendGolden) {
    BoneKeyframe keys[3] = {{0,0,0},{6,9,-3},{100,100,100}};
    float out[3];
    // from==to==0, phase num/seg=0/2, 1/2, 2/2 => 0%, 50%, 100% of seg0 delta.
    struct { int num; float r[3]; } g[3] = {
        {0, {0,0,0}}, {1, {3,4.5f,-1.5f}}, {2, {6,9,-3}}};
    for (auto& c : g) {
        AccumulateBoneTranslation(keys, 0, 0, c.num, 2, out);
        CHECK(feq(out[0], c.r[0]));
        CHECK(feq(out[1], c.r[1]));
        CHECK(feq(out[2], c.r[2]));
    }
}

// ---- Octree cull: a set of nodes, verify which pass -------------------------
namespace {
struct TestNode {
    float box[11];           // max@4..6, min@8..10
    TestNode* kids[4]{};
    int stamped = 0;
};
Frustum* g_frustum = nullptr;
guild::u8 NodeClassify(void* node, void* /*ctx*/, const Frustum& f) {
    auto* n = static_cast<TestNode*>(node);
    float origin[3] = {0,0,0};
    float ident[16] = {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
    float corners[8 * 20] = {0};
    TransformNodeBoxCorners(n->box, origin, ident, corners);
    return ClassifyBoundingBoxPlanes(corners, f, nullptr, nullptr, nullptr, nullptr);
}
void NodeStamp(void* node, int tag) { static_cast<TestNode*>(node)->stamped = tag; }
void* NodeChild(void* node, int i) { return static_cast<TestNode*>(node)->kids[i]; }
} // namespace

TEST(RenderGeom, OctreeCullPassesAndFails) {
    Frustum f{};
    for (int i = 0; i < 4; ++i) f.plane[i][3] = -1e9f;
    f.nearZ = 0.0f; f.farZ = 100.0f;
    g_frustum = &f;

    // Node A fully behind far (z in [200,210]) => fully culled (0x40) => stamped.
    TestNode a{}; float ab[11] = {0,0,0,0, 1,1,210, 0,-1,-1,200}; for (int i=0;i<11;++i) a.box[i]=ab[i];
    // Node B straddling near/far (z in [-5,5]) => partially visible (not 0x40).
    TestNode b{}; float bb[11] = {0,0,0,0, 1,1,5, 0,-1,-1,-5}; for (int i=0;i<11;++i) b.box[i]=bb[i];

    CullCallbacks cb{};
    cb.classify = NodeClassify; cb.stampLeaf = NodeStamp; cb.child = NodeChild; cb.frustum = &f;

    CullOctreeAgainstFrustum(&a, nullptr, 7, cb);
    CullOctreeAgainstFrustum(&b, nullptr, 7, cb);

    CHECK_EQ(a.stamped, 7);  // fully-culled fast path stamps the leaf
    CHECK_EQ(b.stamped, 0);  // partially visible => not stamped (would recurse)
}
