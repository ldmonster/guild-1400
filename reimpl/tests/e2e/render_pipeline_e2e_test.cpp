// End-to-end test for the guild::render software pipeline + present path — the
// exact flow demo/mesh_demo.cpp drives, run headlessly: build a cube, animate it
// across frames, project -> radix sort -> clip/flush rasterize into a
// MemoryGraphicsDevice backbuffer, present(), and assert the presented framebuffer
// has the cube painted (specific pixels) and that the animation actually moves the
// projected geometry frame-to-frame. UNIQUE suite: RenderPipelineE2E.
#include "tests/framework/test.h"

#include "render/geometry_types.h"
#include "render/mesh.h"
#include "render/scene.h"
#include "render/meshlist.h"
#include "render/clip.h"
#include "render/surface.h"
#include "shim_impl/memory_graphics.h"

#include <cmath>
#include <cstring>
#include <vector>

using namespace guild;

namespace {

struct Cube {
    render::Vertex  verts[8];
    render::Polygon polys[12];
    render::MeshGeometry geom{};
    float model0[8][3];

    void build(float h) {
        const float P[8][3] = {
            {-h,-h,-h}, { h,-h,-h}, { h, h,-h}, {-h, h,-h},
            {-h,-h, h}, { h,-h, h}, { h, h, h}, {-h, h, h},
        };
        std::memset(verts, 0, sizeof(verts));
        for (int i = 0; i < 8; ++i) {
            model0[i][0] = P[i][0]; model0[i][1] = P[i][1]; model0[i][2] = P[i][2];
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

    // Rotate model -> world about Y by ang, reset per-frame vertex/poly state.
    void rotate(float ang) {
        float c = std::cos(ang), s = std::sin(ang);
        for (int i = 0; i < 8; ++i) {
            float x = model0[i][0], y = model0[i][1], z = model0[i][2];
            verts[i].x = x * c - z * s;
            verts[i].z = x * s + z * c;
            verts[i].y = y;
            verts[i].screenX = verts[i].screenY = 0.0f;
            verts[i].lightIdx = 0;
            ((u8*)&verts[i])[76] = 0;
        }
        for (int t = 0; t < 12; ++t) { polys[t].flags36 = 0; polys[t].flags38 = 0; }
    }
};

render::ProjectParams MakeParams() {
    render::ProjectParams pp{};
    pp.eye[0] = 0.0f; pp.eye[1] = -160.0f; pp.eye[2] = 0.0f;
    pp.invDepth[0] = pp.invDepth[1] = pp.invDepth[2] = 1.0f;
    pp.biasX = 64.0f; pp.scaleX = 1.0f; pp.scaleY = 1.0f;
    pp.lightCap = 254.0f; pp.screenW = 128.0f;
    return pp;
}

// Drive one frame through the real pipeline into `fb`. Returns drawn poly count.
int RenderFrame(Cube& cube, render::Surface& fb,
                std::vector<render::DrawListEntry>& l1,
                std::vector<render::DrawListEntry>& l2) {
    render::ProjectParams pp = MakeParams();
    render::DrawListBuffers db{}; db.base1 = l1.data(); db.base2 = l2.data();
    db.capacity = (i32)l1.size(); db.count = 0;

    render::DrawList sink = db.AppendSink();
    render::ProjectVerticesToScreen(&cube.geom, pp, 0x40, 0, &sink);
    db.count = sink.count;
    render::RadixSortDrawList(db, (u32)db.count, false);

    render::SpanDispatch disp;
    render::ClipContext clipCtx{0, nullptr};
    render::ProjectScalars proj{1, 0, 1, 0};
    render::ClipScratch scratch{};
    render::MeshList ml{db.base1, db.count};
    return render::RasterizeMeshList(ml, &fb, disp, clipCtx, proj, scratch);
}

} // namespace

// ---------------------------------------------------------------------------
// Full present path: render the un-rotated cube into a MemoryGraphicsDevice
// backbuffer and present(); the presented snapshot has the cube painted.
// ---------------------------------------------------------------------------
TEST(RenderPipelineE2E, PresentedFrameHasCubePainted) {
    shim::MemoryGraphicsDevice gfx;
    gfx.init(128, 128, 8, false);

    Cube cube; cube.build(22.0f); cube.rotate(0.0f);
    shim::Surface* sh = gfx.backbuffer();
    std::memset(sh->pixels, 0, (size_t)sh->height * sh->pitch);

    render::Surface fb{};
    fb.width = sh->width; fb.height = sh->height; fb.pitch = sh->pitch;
    fb.widthPx = sh->pitch; fb.bpp = 8; fb.pixels = (u8*)sh->pixels;
    fb.clipX0 = 0; fb.clipY0 = 0; fb.clipX1 = sh->width; fb.clipY1 = sh->height;

    std::vector<render::DrawListEntry> l1(64), l2(64);
    int drawn = RenderFrame(cube, fb, l1, l2);
    CHECK(drawn > 0);

    gfx.present();
    CHECK_EQ(gfx.presentCount(), 1);

    const std::vector<u8>& snap = gfx.lastPresented();
    CHECK_EQ((int)snap.size(), 128 * 128);
    auto px = [&](int x, int y) -> u8 { return snap[(size_t)y * 128 + x]; };

    // Centre of the front face is painted in the depth-shade band; corners empty.
    CHECK(px(64, 64) != 0);
    CHECK(px(64, 64) >= 138 && px(64, 64) <= 182);
    CHECK_EQ(px(2, 2), (u8)0);
    CHECK_EQ(px(125, 125), (u8)0);

    int painted = 0; for (u8 v : snap) if (v) ++painted;
    CHECK(painted > 1000);
}

// ---------------------------------------------------------------------------
// Animation moves the geometry: across a short rotation sequence the projected
// screen positions change, and every frame still draws (and produces a present).
// ---------------------------------------------------------------------------
TEST(RenderPipelineE2E, RotationSequenceAnimatesAndPresents) {
    shim::MemoryGraphicsDevice gfx;
    gfx.init(128, 128, 8, false);

    Cube cube; cube.build(22.0f);
    std::vector<render::DrawListEntry> l1(64), l2(64);

    float prevX = -1.0f;
    int presents = 0;
    for (int frame = 0; frame < 6; ++frame) {
        cube.rotate((float)frame * 0.42f);

        shim::Surface* sh = gfx.backbuffer();
        std::memset(sh->pixels, 0, (size_t)sh->height * sh->pitch);
        render::Surface fb{};
        fb.width = sh->width; fb.height = sh->height; fb.pitch = sh->pitch;
        fb.widthPx = sh->pitch; fb.bpp = 8; fb.pixels = (u8*)sh->pixels;
        fb.clipX0 = 0; fb.clipY0 = 0; fb.clipX1 = sh->width; fb.clipY1 = sh->height;

        int drawn = RenderFrame(cube, fb, l1, l2);
        CHECK(drawn > 0);                 // every frame paints something

        gfx.present();
        ++presents;

        // vertex 0's projected screen X must move as the cube rotates.
        float sx = cube.verts[0].screenX;
        if (frame > 0) CHECK(std::fabs(sx - prevX) > 0.01f);
        prevX = sx;

        // the presented frame is non-empty
        int painted = 0; for (u8 v : gfx.lastPresented()) if (v) ++painted;
        CHECK(painted > 500);
    }
    CHECK_EQ(presents, 6);
    CHECK_EQ(gfx.presentCount(), 6);
}

// ---------------------------------------------------------------------------
// Sort-key correctness through the whole flow: the rasterized draw list is
// ordered ascending by the 768*lightIdx key (back-to-front flush relies on it).
// ---------------------------------------------------------------------------
TEST(RenderPipelineE2E, DrawListSortedByLightKey) {
    Cube cube; cube.build(22.0f); cube.rotate(0.3f);
    render::ProjectParams pp = MakeParams();

    std::vector<render::DrawListEntry> l1(64), l2(64);
    render::DrawListBuffers db{}; db.base1 = l1.data(); db.base2 = l2.data();
    db.capacity = 64; db.count = 0;
    render::DrawList sink = db.AppendSink();
    render::ProjectVerticesToScreen(&cube.geom, pp, 0x40, 0, &sink);
    db.count = sink.count;
    CHECK(db.count >= 2);

    render::RadixSortDrawList(db, (u32)db.count, false);
    for (i32 i = 1; i < db.count; ++i)
        CHECK(db.base1[i - 1].sortKey <= db.base1[i].sortKey);
}
