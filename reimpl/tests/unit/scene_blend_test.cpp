// gilde.exe 0x5e0358 VIBE_Render_SetBlendMode — additive/alpha transparency in the
// scene draw list. Deterministic (no assets): build a draw list with an opaque
// background and a transparent overlay, rasterise, and check the composite math.
#include "test.h"
#include "render/scene_drawlist.h"
#include "render/surface.h"
#include <cstdint>

using namespace guild;

namespace {
// A full-screen quad (two triangles) at a constant world depth, constant colour, no
// texture (texel = white, so the pixel == colour modulator). Appended to `dl`.
void AddQuad(render::Scene3DDrawList& dl, float r, float g, float b, int blend, float opacity,
             float z = 10.0f) {
    render::SceneDrawBatch batch;
    batch.firstVertex = (int)dl.verts.size();
    batch.texId = -1; batch.blend = blend; batch.opacity = opacity;
    // Two CCW triangles spanning a wide area in front of the camera (view space == world
    // here: eye at origin, fwd +z). Positions chosen so the projection covers the centre.
    const float P[6][3] = {
        {-5,-5,z}, {5,-5,z}, {5,5,z},
        {-5,-5,z}, {5,5,z}, {-5,5,z},
    };
    for (int i = 0; i < 6; ++i) {
        render::SceneDrawVertex v;
        v.pos[0]=P[i][0]; v.pos[1]=P[i][1]; v.pos[2]=P[i][2];
        v.color[0]=r; v.color[1]=g; v.color[2]=b; v.uv[0]=0; v.uv[1]=0;
        dl.verts.push_back(v);
    }
    batch.vertexCount = 6;
    dl.batches.push_back(batch);
}

render::Scene3DDrawList MakeList() {
    render::Scene3DDrawList dl;
    dl.width = 64; dl.height = 64;
    dl.backfaceCull = 0;             // both windings kept (no culling for this synthetic quad)
    dl.clearFirst = true; dl.clearR = 0; dl.clearG = 0; dl.clearB = 0;
    // Simple non-engine projection so the quad lands on-screen deterministically.
    dl.cam.engineProjection = false;
    dl.cam.eye[0]=0; dl.cam.eye[1]=0; dl.cam.eye[2]=0;
    dl.cam.right[0]=1; dl.cam.right[1]=0; dl.cam.right[2]=0;
    dl.cam.up[0]=0; dl.cam.up[1]=1; dl.cam.up[2]=0;
    dl.cam.fwd[0]=0; dl.cam.fwd[1]=0; dl.cam.fwd[2]=1;
    dl.cam.nearZ = 0.05f; dl.cam.farZ = 4000.0f;
    dl.cam.fproj = 1.0f; dl.cam.aspect = 1.0f;
    return dl;
}

void CenterPixel(render::Surface* s, int& r, int& g, int& b) {
    const int x = 32, y = 32;
    auto* row = reinterpret_cast<const std::uint32_t*>(
        static_cast<const std::uint8_t*>(s->pixels) + (std::size_t)y * s->pitch);
    const std::uint32_t p = row[x];
    r = (p >> 16) & 0xFF; g = (p >> 8) & 0xFF; b = p & 0xFF;
}
} // namespace

// Additive: dst += src*opacity. Background grey (0.5 -> 127), overlay white (1.0) at
// opacity 0.5 -> 127 added -> ~254.
TEST(SceneBlend, AdditiveAddsScaledSource) {
    render::Scene3DDrawList dl = MakeList();
    AddQuad(dl, 0.5f, 0.5f, 0.5f, render::kBlendOpaque, 1.0f, 10.0f);    // background (far)
    AddQuad(dl, 1.0f, 1.0f, 1.0f, render::kBlendAdditive, 0.5f, 9.0f);   // additive overlay (near)
    render::Surface* fb = render::SurfaceCreate(64, 64, 32); CHECK(fb);
    render::RasterizeDrawList(dl, fb);
    int r,g,b; CenterPixel(fb, r,g,b);
    // 127 (bg) + floor(255*0.5)=127 -> 254
    CHECK_EQ(r, 254); CHECK_EQ(g, 254); CHECK_EQ(b, 254);
}

// Additive over black with a faint opacity (0.1) -> a dim glow, NOT full brightness
// (the "moon ray" look — the window light shaft op=25/255 ~= 0.098).
TEST(SceneBlend, AdditiveFaintGlow) {
    render::Scene3DDrawList dl = MakeList();
    AddQuad(dl, 1.0f, 1.0f, 1.0f, render::kBlendAdditive, 25.0f/255.0f);
    render::Surface* fb = render::SurfaceCreate(64, 64, 32); CHECK(fb);
    render::RasterizeDrawList(dl, fb);
    int r,g,b; CenterPixel(fb, r,g,b);
    // 0 (black bg) + floor(255 * 25/255) = 25
    CHECK_EQ(r, 25); CHECK_EQ(g, 25); CHECK_EQ(b, 25);
}

// Alpha (mode 1): dst*(1-op) + src*op. Background (200) under white overlay op=0.25.
TEST(SceneBlend, AlphaLerpsTowardSource) {
    render::Scene3DDrawList dl = MakeList();
    AddQuad(dl, 200.0f/255.0f, 0.0f, 0.0f, render::kBlendOpaque, 1.0f, 10.0f); // red 200 bg (far)
    AddQuad(dl, 1.0f, 1.0f, 1.0f, render::kBlendAlpha, 0.25f, 9.0f);           // white 25% (near)
    render::Surface* fb = render::SurfaceCreate(64, 64, 32); CHECK(fb);
    render::RasterizeDrawList(dl, fb);
    int r,g,b; CenterPixel(fb, r,g,b);
    // R: 200*0.75 + 255*0.25 = 150 + 63 = 213 ; G,B: 0*0.75 + 255*0.25 = 63
    CHECK_EQ(r, 213); CHECK_EQ(g, 63); CHECK_EQ(b, 63);
}

// Transparent geometry does NOT write depth: a second opaque quad BEHIND an additive
// overlay still draws (the additive layer didn't occlude it). Draw order: opaque-front
// is closer; here we verify an additive layer leaves the z-buffer free for nothing
// closer — instead test that additive never blocks a *closer* opaque drawn afterwards.
TEST(SceneBlend, AdditiveDoesNotWriteDepth) {
    render::Scene3DDrawList dl = MakeList();
    // Additive overlay NEAR (z=9, drawn first), then an opaque quad FARTHER (z=10).
    // If additive had written depth (9), the farther opaque (10) would be occluded and
    // the result would be the additive glow. Because additive writes NO depth, the
    // opaque draws over it -> the grey opaque colour proves no z-write.
    AddQuad(dl, 1.0f, 1.0f, 1.0f, render::kBlendAdditive, 0.5f, 9.0f);
    AddQuad(dl, 0.2f, 0.2f, 0.2f, render::kBlendOpaque, 1.0f, 10.0f);
    render::Surface* fb = render::SurfaceCreate(64, 64, 32); CHECK(fb);
    render::RasterizeDrawList(dl, fb);
    int r,g,b; CenterPixel(fb, r,g,b);
    // 0.2*255 = 51 (the opaque grey), NOT the additive 127.
    CHECK_EQ(r, 51); CHECK_EQ(g, 51); CHECK_EQ(b, 51);
}
