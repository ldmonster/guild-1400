#include "test.h"

#include "render/falloff_lut.h"
#include "render/texture_mip.h"
#include "render/shadow_render.h"
#include "render/particle_render.h"
#include "render/snow.h"

#include <cmath>
#include <cstring>
#include <vector>

using namespace guild;
using namespace guild::render;

namespace {

// Project a world-space caster vertex onto the ground plane y=0 along a light
// direction, then map to the shadow surface's pixel space. This mirrors the
// VIBE_Shadow_RenderMeshShadow projection (vertex' = v + t*lightDir where
// t = (groundY - v.y)/lightDir.y), reduced to the testable math the deferred
// engine routine performs before handing triangles to RasterizeTriangle.
struct V3 { float x, y, z; };

V3 ProjectToGround(const V3& v, const V3& lightDir, float groundY) {
    float t = (groundY - v.y) / lightDir.y;
    return {v.x + t * lightDir.x, groundY, v.z + t * lightDir.z};
}

} // namespace

// ===========================================================================
// E2E: project a triangular mesh's shadow onto the ground for a known light,
// rasterize it into a shadow surface, and verify the lit/shadowed pixels.
// ===========================================================================
TEST(RenderShadowE2E, ProjectAndRasterize) {
    // A small triangle floating above the ground (apex toward -z, flat +z edge),
    // wound so the projected ground shadow is a clean apex-up triangle.
    V3 mesh[3] = {
        { 8.0f, 5.0f,  4.0f}, // apex (smallest z -> smallest surface Y)
        {16.0f, 5.0f, 14.0f}, // bottom-right
        { 2.0f, 5.0f, 14.0f}, // bottom-left
    };
    // Light pointing straight down (-y) with a slight +x slant so the shadow
    // lands offset on the ground.
    V3 light = {1.0f, -2.0f, 0.0f};

    V3 g0 = ProjectToGround(mesh[0], light, 0.0f);
    V3 g1 = ProjectToGround(mesh[1], light, 0.0f);
    V3 g2 = ProjectToGround(mesh[2], light, 0.0f);

    // t = (0-5)/-2 = 2.5 ; x += 2.5*1 = +2.5 ; z unchanged.
    CHECK(std::fabs(g0.x - 10.5f) <= 1e-4f);
    CHECK(std::fabs(g1.x - 18.5f) <= 1e-4f);
    CHECK(std::fabs(g0.z - 4.0f) <= 1e-4f);

    // Map ground (x,z) to shadow-surface pixels (1 unit = 1 px), into a 32x32
    // 8bpp surface. The rasterizer writes the shadow index (1) for covered px.
    ShadowSurface surf;
    std::vector<u8> px(32 * 32, 0);
    surf.pixels = px.data();
    surf.pitch = 32; surf.width = 32; surf.height = 32; surf.is16bpp = false;

    ShadowTri tri;
    tri.x[0] = g0.x; tri.y[0] = g0.z;
    tri.x[1] = g1.x; tri.y[1] = g1.z;
    tri.x[2] = g2.x; tri.y[2] = g2.z;
    tri.backFlag = true;

    ShadowRasterState s;
    RasterizeTriangle(s, tri, surf);

    int filled = 0;
    for (u8 v : px) if (v == 1) ++filled;
    CHECK(filled > 0);

    // The shadow centroid: ((10.5+18.5+4.5)/3, (4+14+14)/3) = (11.2, 10.7).
    CHECK(px[10 * 32 + 11] == 1);
    // Outside the shadow triangle stays clear.
    CHECK(px[0] == 0);
    CHECK(px[31 * 32 + 31] == 0);

    // The shadow must NOT bleed left of the leftmost ground vertex (x=4.5).
    for (int y = 0; y < 32; ++y)
        for (int x = 0; x < 4; ++x)
            CHECK(px[y * 32 + x] == 0);
    // Nor below the flat bottom edge (z = 14 -> surface row 14).
    for (int y = 15; y < 32; ++y)
        for (int x = 0; x < 32; ++x)
            CHECK(px[y * 32 + x] == 0);
}

// ===========================================================================
// E2E: build a texture mipmap chain from an index texture + verify levels.
// ===========================================================================
TEST(RenderShadowE2E, MipmapChain) {
    const int W = 8;
    std::vector<u8> base(W * W);
    for (int v = 0; v < W; ++v)
        for (int u = 0; u < W; ++u)
            base[v * W + u] = (u8)(v * W + u); // 0..63

    auto chain = BuildIndexMipChain(base.data(), W);
    // 8,4,2,1 -> 4 levels.
    CHECK_EQ((int)chain.size(), 4);
    CHECK_EQ((int)chain[0].size(), 64);
    CHECK_EQ((int)chain[1].size(), 16);
    CHECK_EQ((int)chain[2].size(), 4);
    CHECK_EQ((int)chain[3].size(), 1);
    // Level 1 (4x4) takes src[2v][2u]: dst[0][0]=src[0][0]=0, dst[0][1]=src[0][2]=2.
    CHECK_EQ(chain[1][0], 0);
    CHECK_EQ(chain[1][1], 2);
    CHECK_EQ(chain[1][4], base[2 * W + 0]); // dst[1][0] = src[2][0] = 16
    // The level count helper agrees with the produced chain.
    CHECK_EQ(MipLevelCount(W), (int)chain.size());
}

// ===========================================================================
// E2E: falloff LUT drives a believable distance fade across the full range.
// ===========================================================================
TEST(RenderShadowE2E, FalloffCurveShape) {
    static float table[kFalloffEntries];
    InitFalloffTable(table);
    CHECK(std::fabs(table[0] - 1.0f) <= 1e-6f);    // no falloff at distance 0
    CHECK(table[1023] < 0.05f);                     // near-zero at the far edge
    // Halfway index -> asin(0.5)*2/pi subtracted from 1.
    float midRef = (float)(1.0 - std::asin(0.5) * (2.0 / M_PI));
    CHECK(std::fabs(table[512] - midRef) <= 1e-6f);
}

// ===========================================================================
// E2E: a full snow frame — seed flakes (RNG), update/project, build vertices.
// Ties Snow_Create -> Snow_UpdateFlake -> Snow_Render (vertex build).
// ===========================================================================
TEST(RenderShadowE2E, SnowFrameToVertices) {
    SnowFlake flakes[8];
    std::memset(flakes, 0, sizeof(flakes));
    SnowSystem sys; sys.count = 8; sys.capacity = 8; sys.flakes = flakes;

    // Seed (deterministic given the RNG state at this point in the suite).
    SnowSeedFlakes(sys);
    // Every flake position lands in the unit cube [-1,1].
    for (int i = 0; i < 8; ++i) {
        CHECK(flakes[i].px >= -1.0f && flakes[i].px <= 1.0f);
        CHECK(flakes[i].py >= -1.0f && flakes[i].py <= 1.0f);
        CHECK(flakes[i].size > 0.0f);
    }

    // Drive a projection by directly assigning screen coords (the update kernel
    // is exercised in its own test); here we verify the render builder maps a
    // mix of in/out-of-viewport flakes into the right vertex count.
    SnowViewport vp{0, 0, 100, 100};
    for (int i = 0; i < 8; ++i) {
        flakes[i].pz = 0.25f;
        flakes[i].sx = (float)(10 + i * 5);
        flakes[i].sy = 50.0f;
        flakes[i].sx2 = flakes[i].sx + 1.0f;
        flakes[i].sy2 = 52.0f;
    }
    // Push flake 7 out of view.
    flakes[7].sx2 = 250.0f;

    std::vector<Tlvertex> out;
    int n = BuildSnowVertices(sys, vp, out);
    CHECK_EQ(n, 7 * 3);             // 7 visible flakes, 3 verts each
    CHECK_EQ((int)out.size(), 21);
    // Every emitted vertex carries rhw 1.0 and the snow diffuse colour.
    for (const Tlvertex& v : out) {
        CHECK(std::fabs(v.rhw - 1.0f) <= 1e-6f);
        CHECK_EQ(v.diffuse, 1348756580u);
    }
}
