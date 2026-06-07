// Unit test for the end-to-end guild::render software pipeline (the same flow the
// demo/mesh_demo.cpp playable path drives): build a synthetic cube mesh, run
//   ProjectVerticesToScreen -> RadixSortDrawList -> RasterizeMeshList
// headlessly into a software Surface, and assert the draw list is sorted by the
// 768*lightIdx key and that specific framebuffer pixels are painted for a known
// (un-rotated) camera. UNIQUE suite: RenderPipelineUnit.
#include "tests/framework/test.h"

#include "render/geometry_types.h"
#include "render/mesh.h"
#include "render/scene.h"
#include "render/meshlist.h"
#include "render/clip.h"
#include "render/surface.h"

#include <cstring>
#include <vector>

using namespace guild;

namespace {

// A self-contained cube fixture matching the demo geometry: a unit cube of
// half-extent `h`, 8 vertices, 12 triangles (CCW, 2 per face).
struct CubeFixture {
    render::Vertex  verts[8];
    render::Polygon polys[12];
    render::MeshGeometry geom{};

    void build(float h) {
        const float P[8][3] = {
            {-h,-h,-h}, { h,-h,-h}, { h, h,-h}, {-h, h,-h},
            {-h,-h, h}, { h,-h, h}, { h, h, h}, {-h, h, h},
        };
        std::memset(verts, 0, sizeof(verts));
        for (int i = 0; i < 8; ++i) {
            verts[i].x = P[i][0]; verts[i].y = P[i][1]; verts[i].z = P[i][2];
            ((u8*)&verts[i])[76] = 0; // clear the +76 clip flag byte the flush reads
        }
        const int F[6][4] = {
            {0,1,2,3},{5,4,7,6},{4,0,3,7},{1,5,6,2},{3,2,6,7},{4,5,1,0},
        };
        int t = 0;
        for (int f = 0; f < 6; ++f) {
            const int tris[2][3] = {{0,1,2},{0,2,3}};
            for (int k = 0; k < 2; ++k, ++t) {
                std::memset(&polys[t], 0, sizeof(render::Polygon));
                polys[t].v0 = &verts[F[f][tris[k][0]]];
                polys[t].v1 = &verts[F[f][tris[k][1]]];
                polys[t].v2 = &verts[F[f][tris[k][2]]];
            }
        }
        geom.vertices = verts; geom.polygons = polys;
        geom.polyCount = 12; geom.polyCap = 12; geom.vertexCount = 8;
    }
};

// The fixed projection parameters used by the test (= the demo's, un-rotated):
// world X/Z -> screen, world Y -> light/depth term. biasX centres the cube.
render::ProjectParams MakeParams() {
    render::ProjectParams pp{};
    pp.eye[0] = 0.0f; pp.eye[1] = -160.0f; pp.eye[2] = 0.0f;
    pp.invDepth[0] = pp.invDepth[1] = pp.invDepth[2] = 1.0f;
    pp.biasX = 64.0f; pp.scaleX = 1.0f; pp.scaleY = 1.0f;
    pp.lightCap = 254.0f; pp.screenW = 128.0f;
    return pp;
}

} // namespace

// ---------------------------------------------------------------------------
// Projection appends front-facing on-screen polys with the 768*lightIdx key.
// ---------------------------------------------------------------------------
TEST(RenderPipelineUnit, ProjectAppendsSortKeyIs768TimesMaxLight) {
    CubeFixture cube; cube.build(22.0f);
    render::ProjectParams pp = MakeParams();

    std::vector<render::DrawListEntry> l1(64), l2(64);
    render::DrawListBuffers db{}; db.base1 = l1.data(); db.base2 = l2.data();
    db.capacity = 64; db.count = 0;

    render::DrawList sink = db.AppendSink();
    render::ProjectVerticesToScreen(&cube.geom, pp, 0x40, 0, &sink);
    db.count = sink.count;

    CHECK(db.count > 0);                 // some faces survive backface cull
    CHECK(db.count <= 12);
    // Every appended entry's key == 768 * max(vertex lightIdx) of its polygon.
    for (i32 i = 0; i < db.count; ++i) {
        render::Polygon* p = db.base1[i].poly;
        int m = p->v0->lightIdx;
        if (p->v1->lightIdx > m) m = p->v1->lightIdx;
        if (p->v2->lightIdx > m) m = p->v2->lightIdx;
        CHECK_EQ(db.base1[i].sortKey, (u32)(768 * m));
    }
}

// ---------------------------------------------------------------------------
// The radix sort orders the appended draw list ascending by sort key (stable).
// ---------------------------------------------------------------------------
TEST(RenderPipelineUnit, RadixSortOrdersDrawListAscending) {
    CubeFixture cube; cube.build(22.0f);
    render::ProjectParams pp = MakeParams();

    std::vector<render::DrawListEntry> l1(64), l2(64);
    render::DrawListBuffers db{}; db.base1 = l1.data(); db.base2 = l2.data();
    db.capacity = 64; db.count = 0;

    render::DrawList sink = db.AppendSink();
    render::ProjectVerticesToScreen(&cube.geom, pp, 0x40, 0, &sink);
    db.count = sink.count;
    CHECK(db.count >= 2);

    render::RadixSortDrawList(db, (u32)db.count, /*twoPassOnly=*/false);

    // After the 4-pass sort the result lives in base1, non-decreasing by key.
    for (i32 i = 1; i < db.count; ++i)
        CHECK(db.base1[i - 1].sortKey <= db.base1[i].sortKey);
}

// ---------------------------------------------------------------------------
// Full flush: rasterize the sorted list into a software surface and assert the
// expected framebuffer pixels are painted for the known un-rotated camera. The
// front face spans screen [42,86] x [42,86] (model {-22,+22} + bias 64), so the
// surface centre (64,64) must be a non-background light index; corners stay 0.
// ---------------------------------------------------------------------------
TEST(RenderPipelineUnit, RasterizePaintsExpectedPixels) {
    CubeFixture cube; cube.build(22.0f);
    render::ProjectParams pp = MakeParams();

    std::vector<render::DrawListEntry> l1(64), l2(64);
    render::DrawListBuffers db{}; db.base1 = l1.data(); db.base2 = l2.data();
    db.capacity = 64; db.count = 0;
    render::DrawList sink = db.AppendSink();
    render::ProjectVerticesToScreen(&cube.geom, pp, 0x40, 0, &sink);
    db.count = sink.count;
    render::RadixSortDrawList(db, (u32)db.count, false);

    // 128x128 8-bit software surface, cleared to background index 0.
    const int W = 128, H = 128;
    std::vector<u8> pixels((size_t)W * H, 0);
    render::Surface fb{};
    fb.width = W; fb.height = H; fb.pitch = W; fb.widthPx = W; fb.bpp = 8;
    fb.pixels = pixels.data();
    fb.clipX0 = 0; fb.clipY0 = 0; fb.clipX1 = W; fb.clipY1 = H;

    render::SpanDispatch disp;
    render::ClipContext clipCtx{0, nullptr};
    render::ProjectScalars proj{1, 0, 1, 0};
    render::ClipScratch scratch{};
    render::MeshList ml{db.base1, db.count};
    int drawn = render::RasterizeMeshList(ml, &fb, disp, clipCtx, proj, scratch);
    CHECK_EQ(drawn, (int)db.count);

    auto px = [&](int x, int y) -> u8 { return pixels[(size_t)y * W + x]; };

    // Centre is inside the front face -> painted with a non-zero light index.
    CHECK(px(64, 64) != 0);
    // The depth-shaded value at the centre equals the projected lightIdx band
    // (world Y span [-22,22] + eye offset 160 -> [138,182]); centre must land in
    // that range.
    CHECK(px(64, 64) >= 138 && px(64, 64) <= 182);

    // A point comfortably inside the face is painted...
    CHECK(px(60, 60) != 0);
    CHECK(px(70, 70) != 0);
    // ...and the far corners (outside the [42,86] face box) stay background.
    CHECK_EQ(px(2, 2), (u8)0);
    CHECK_EQ(px(125, 125), (u8)0);
    CHECK_EQ(px(2, 125), (u8)0);

    // Count painted pixels: a ~44x44 face footprint -> well over 1000 pixels.
    int painted = 0;
    for (auto v : pixels) if (v != 0) ++painted;
    CHECK(painted > 1000);
}
