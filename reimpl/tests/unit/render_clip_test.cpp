#include "test.h"
#include "render/clip.h"
#include "render/meshlist.h"
#include "render/raster.h"
#include "render/surface.h"

#include <cstring>
#include <cmath>

using namespace guild;
using namespace guild::render;

// ---------------------------------------------------------------------------
// Vertex byte-offset helpers (the original reads colours/flags at raw offsets).
// ---------------------------------------------------------------------------
static void SetXYZ(Vertex& v, float x, float y, float z) { v.x = x; v.y = y; v.z = z; }
static void SetClipByte(Vertex& v, u8 b) { ((u8*)&v)[76] = b; }
static u8&  ColorByte(Vertex& v, int off) { return ((u8*)&v)[off]; }

// near plane: inside when z >= 1.0  => a=0,b=0,c=1,d=1
static const ClipPlane kNearPlane = {0.0f, 0.0f, 1.0f, 1.0f};

// ---------------------------------------------------------------------------
// CLIP: triangle straddling the near plane -> 4 vertices (python golden).
//   tri v0(0,0,2 in) v1(4,0,2 in) v2(2,4,0 out)
//   expected: v0, v1, (3,2,1), (1,2,1)
// ---------------------------------------------------------------------------
TEST(RenderClip, StraddleNearPlaneProducesFourVerts) {
    Vertex tri[3];
    std::memset(tri, 0, sizeof(tri));
    SetXYZ(tri[0], 0.0f, 0.0f, 2.0f);
    SetXYZ(tri[1], 4.0f, 0.0f, 2.0f);
    SetXYZ(tri[2], 2.0f, 4.0f, 0.0f);

    ClipScratch sc;
    std::memset(&sc, 0, sizeof(sc));
    sc.listPtrs[0][0] = &tri[0];
    sc.listPtrs[0][1] = &tri[1];
    sc.listPtrs[0][2] = &tri[2];

    ClipContext ctx{1, &kNearPlane};
    Vertex** out = ClipPolygonToPlane(sc, ctx, 3);

    CHECK(out != nullptr);
    CHECK_EQ(sc.outCount, 4);

    // v0, v1 are the original (in) vertices, passed by pointer.
    CHECK(out[0] == &tri[0]);
    CHECK(out[1] == &tri[1]);
    // out[2] = interpolated on v1->v2 edge -> (3,2,1)
    CHECK(std::fabs(out[2]->x - 3.0f) < 1e-5f);
    CHECK(std::fabs(out[2]->y - 2.0f) < 1e-5f);
    CHECK(std::fabs(out[2]->z - 1.0f) < 1e-5f);
    // out[3] = interpolated on v2->v0 edge -> (1,2,1)
    CHECK(std::fabs(out[3]->x - 1.0f) < 1e-5f);
    CHECK(std::fabs(out[3]->y - 2.0f) < 1e-5f);
    CHECK(std::fabs(out[3]->z - 1.0f) < 1e-5f);
}

// CLIP: colour-byte lerp at the two new vertices (python golden: t=0.5 both).
TEST(RenderClip, NewVertexColourByteLerp) {
    Vertex tri[3];
    std::memset(tri, 0, sizeof(tri));
    SetXYZ(tri[0], 0.0f, 0.0f, 2.0f);
    SetXYZ(tri[1], 4.0f, 0.0f, 2.0f);
    SetXYZ(tri[2], 2.0f, 4.0f, 0.0f);
    ColorByte(tri[0], 64) = 40;
    ColorByte(tri[1], 64) = 100;
    ColorByte(tri[2], 64) = 200;

    ClipScratch sc;
    std::memset(&sc, 0, sizeof(sc));
    sc.listPtrs[0][0] = &tri[0];
    sc.listPtrs[0][1] = &tri[1];
    sc.listPtrs[0][2] = &tri[2];
    ClipContext ctx{1, &kNearPlane};
    Vertex** out = ClipPolygonToPlane(sc, ctx, 3);

    CHECK_EQ(sc.outCount, 4);
    // new vertex on v1(100)->v2(200) at t=0.5 -> 150
    CHECK_EQ((int)ColorByte(*out[2], 64), 150);
    // new vertex on v2(200)->v0(40) at t=0.5 -> 120
    CHECK_EQ((int)ColorByte(*out[3], 64), 120);
}

// CLIP: fully-inside triangle survives unchanged (3 verts, all original ptrs).
TEST(RenderClip, FullyInsideUnchanged) {
    Vertex tri[3];
    std::memset(tri, 0, sizeof(tri));
    SetXYZ(tri[0], 0.0f, 0.0f, 5.0f);
    SetXYZ(tri[1], 4.0f, 0.0f, 5.0f);
    SetXYZ(tri[2], 2.0f, 4.0f, 5.0f);
    ClipScratch sc;
    std::memset(&sc, 0, sizeof(sc));
    sc.listPtrs[0][0] = &tri[0];
    sc.listPtrs[0][1] = &tri[1];
    sc.listPtrs[0][2] = &tri[2];
    ClipContext ctx{1, &kNearPlane};
    Vertex** out = ClipPolygonToPlane(sc, ctx, 3);
    CHECK_EQ(sc.outCount, 3);
    CHECK(out[0] == &tri[0]);
    CHECK(out[1] == &tri[1]);
    CHECK(out[2] == &tri[2]);
}

// CLIP: fully-outside triangle is removed (0 verts).
TEST(RenderClip, FullyOutsideRemoved) {
    Vertex tri[3];
    std::memset(tri, 0, sizeof(tri));
    SetXYZ(tri[0], 0.0f, 0.0f, -1.0f);
    SetXYZ(tri[1], 4.0f, 0.0f, -1.0f);
    SetXYZ(tri[2], 2.0f, 4.0f, -1.0f);
    ClipScratch sc;
    std::memset(&sc, 0, sizeof(sc));
    sc.listPtrs[0][0] = &tri[0];
    sc.listPtrs[0][1] = &tri[1];
    sc.listPtrs[0][2] = &tri[2];
    ClipContext ctx{1, &kNearPlane};
    Vertex** out = ClipPolygonToPlane(sc, ctx, 3);
    CHECK(out != nullptr);
    CHECK_EQ(sc.outCount, 0);
}

// ---------------------------------------------------------------------------
// DISPATCH: key>>24 routes to the expected raster variant.
//   slot 0..2,5,6 = NullStub ; slot 4 = opaque ; slot 3 = blend.
// ---------------------------------------------------------------------------
TEST(RenderClip, SpanDispatchTableSlots) {
    SpanDispatch disp;
    CHECK(disp.slot[0] == &SpanFillNullStub);
    CHECK(disp.slot[1] == &SpanFillNullStub);
    CHECK(disp.slot[2] == &SpanFillNullStub);
    CHECK(disp.slot[3] == &SpanFillTexturedBlend);
    CHECK(disp.slot[4] == &SpanFillTexturedOpaque);
    CHECK(disp.slot[5] == &SpanFillNullStub);
    CHECK(disp.slot[6] == &SpanFillNullStub);

    // key>>24 selects the slot: key 0x04000000 -> 4 (opaque), 0x03000000 -> 3.
    u32 keyOpaque = 0x04000000u;
    u32 keyBlend  = 0x03000000u;
    CHECK(disp.slot[keyOpaque >> 24] == &SpanFillTexturedOpaque);
    CHECK(disp.slot[keyBlend  >> 24] == &SpanFillTexturedBlend);
}

// DISPATCH: NullStub draws nothing and returns continue (nonzero).
TEST(RenderClip, NullStubReturnsContinue) {
    Polygon tri{};
    CHECK_EQ(SpanFillNullStub(nullptr, tri), 1);
}

// ---------------------------------------------------------------------------
// REPROJECT scalars: screenX = D0C*x*(1/z)+D18 ; screenY = AF8*y*(1/z)+D10.
// python golden with D0C=2,D18=320,AF8=2,D10=240.
// ---------------------------------------------------------------------------
TEST(RenderClip, ReprojectScalarsViaFlush) {
    // Build a single straddling poly and flush it; the clip survivors get
    // reprojected. We verify the apex (0,0,2)->(320,240) and an interp vert.
    Vertex tri[3];
    std::memset(tri, 0, sizeof(tri));
    SetXYZ(tri[0], 0.0f, 0.0f, 2.0f);  SetClipByte(tri[0], 0x01); // straddle bit
    SetXYZ(tri[1], 4.0f, 0.0f, 2.0f);  SetClipByte(tri[1], 0x01);
    SetXYZ(tri[2], 2.0f, 4.0f, 0.0f);  SetClipByte(tri[2], 0x01);

    Polygon poly{};
    poly.v0 = &tri[0]; poly.v1 = &tri[1]; poly.v2 = &tri[2];

    DrawListEntry e{0u, &poly};
    MeshList list{&e, 1};
    SpanDispatch disp;
    ClipContext ctx{1, &kNearPlane};
    ProjectScalars proj{2.0f, 320.0f, 2.0f, 240.0f};
    ClipScratch sc;
    std::memset(&sc, 0, sizeof(sc));

    Surface* fb = SurfaceCreate(640, 480, 16);
    CHECK(fb != nullptr);
    int drawn = RasterizeMeshList(list, fb, disp, ctx, proj, sc);
    CHECK_EQ(drawn, 1);

    // After clip+reproject the survivors are tri[0],tri[1] + 2 pool verts.
    // tri[0] (0,0,2): sx=320, sy=240.
    CHECK(std::fabs(tri[0].screenX - 320.0f) < 1e-3f);
    CHECK(std::fabs(tri[0].screenY - 240.0f) < 1e-3f);
    // tri[1] (4,0,2): sx=324, sy=240.
    CHECK(std::fabs(tri[1].screenX - 324.0f) < 1e-3f);
    CHECK(std::fabs(tri[1].screenY - 240.0f) < 1e-3f);
    // Pool vertex (3,2,1): sx=326, sy=244.
    CHECK(std::fabs(sc.newVerts[0].screenX - 326.0f) < 1e-3f);
    CHECK(std::fabs(sc.newVerts[0].screenY - 244.0f) < 1e-3f);
    SurfaceDestroy(fb);
}
