#include "test.h"

// =============================================================================
// Unit goldens — wave-5 W5-TX: GROUND TILE TEXTURING (the consumer that turns
// the FloorTextureResolver's per-tile texture into a TEXTURED ground raster).
//
// Covered:
//   * The per-quad UV mapping recovered from VIBE_Floor_RenderTerrain @0x5bf22c
//     (the @0x5c1fcd subTexId*0x60 + @0x5c1ff5/0x5c2015 poly+0x10 / poly+0x38
//     reads into flt_13FE540): the corner-inset UV record BuildTerrainUvTable
//     @0x5b94cc builds — T0 (floats 0..5) for the first triangle of a quad, T1
//     (floats 6..11) for the second.
//   * GroundFrame textured-vs-shaded SELECTION: with a tex binder bound + an
//     active getTileTexture hook resolving a known texture, the ground polys
//     flush TEXTURED (the texture's texel colours appear); with NO binder the
//     flush is the established white-default level-shaded fallback (and the two
//     renders DIFFER — the binder is the only change). The unbound render is
//     verified to match a fresh GroundFrame's render (additive: byte-identical).
//
// All expected UV values are hand-computed from the captured decompile.
// =============================================================================
#include "play/city_view3d.h"        // BuildEngineFrustum / AimCamera
#include "play/terrain_render.h"
#include "render/colorformat.h"
#include "render/scene_floor.h"
#include "render/surface.h"
#include "render/terrain_uvtable.h"
#include "render/texture.h"

#include <cmath>
#include <cstring>
#include <vector>

using namespace guild;
using namespace guild::play;

namespace {

// A floor block whose texture grid uses ONE slot byte everywhere (so every quad
// resolves the same mock texture) — a 64-grid, tileSpan 8.
render::SceneFloorBlock MakeUniformBlock(int n, u8 typeByte) {
    render::SceneFloorBlock b;
    b.ok = true;
    b.floorPresent = true;
    b.gridN = (u32)n;
    b.cellScale = 4.0f;
    b.heightScale = 3.0f;
    b.heights.accepted = true;
    b.heights.data.assign((size_t)n * n, 0);
    for (int i = 0; i < n * n; ++i)
        b.heights.data[(size_t)i] = (u8)((i * 7) & 0x3F);   // gentle relief
    b.textureGrid.accepted = true;
    b.textureGrid.data.assign((size_t)n * n, typeByte);
    b.typeNameCount = 8;
    b.typeNames[0] = "MOCK";
    return b;
}

// A 4x4 mock texture: a deterministic checker of two distinct palette indices,
// with a 63-row HiColTab-style palette (row L entry i scaled to L/62) so the
// textured span's palBase[(avg<<8)|texel] is in range for any avg in [0,62].
struct MockTex {
    render::Texture rec;
    std::vector<u16> palBlock;   // 63 * 256 entries (the *(tex+72) layout)
};

MockTex MakeMockTex() {
    MockTex m;
    render::TextureSetSize(m.rec, 4);   // 4x4 -> mipWidth 4, 16 texels
    // two distinct source indices: 1 (red) and 2 (blue), checkered.
    for (int i = 0; i < (int)m.rec.texels.size(); ++i)
        m.rec.texels[(size_t)i] = (u8)((i & 1) ? 1 : 2);
    // a 256-RGB source palette: index 1 = red, 2 = blue, rest black.
    m.rec.paletteStore.assign(256 * 3, 0);
    m.rec.paletteStore[3 * 1 + 0] = 248;  // red (already 565-aligned bits)
    m.rec.paletteStore[3 * 2 + 2] = 248;  // blue
    m.rec.palette = m.rec.paletteStore.data();
    // 63-row ramp block keyed by source index (the CityView3D::groundPalette565For
    // layout; row L entry i = palette[i] * L/62).
    const render::ColorFormat fmt = render::Format565();
    m.palBlock.assign((size_t)63 * 256, 0);
    for (int i = 0; i < 256; ++i) {
        double r = m.rec.paletteStore[3 * i + 0];
        double g = m.rec.paletteStore[3 * i + 1];
        double bch = m.rec.paletteStore[3 * i + 2];
        for (int L = 0; L < 63; ++L) {
            u8 rr = (u8)(int)(r / 62.0 * (double)L + 0.5);
            u8 gg = (u8)(int)(g / 62.0 * (double)L + 0.5);
            u8 bb = (u8)(int)(bch / 62.0 * (double)L + 0.5);
            m.palBlock[(size_t)(L << 8) + (size_t)i] = (u16)render::PackColor(fmt, rr, gg, bb);
        }
    }
    return m;
}

// The mock binder trampoline state (single-active, like the engine's globals).
MockTex* g_mock = nullptr;
const render::Texture* MockRec(u8 /*typeByte*/) { return g_mock ? &g_mock->rec : nullptr; }
const u16* MockPal(const render::Texture* /*rec*/) { return g_mock ? g_mock->palBlock.data() : nullptr; }
const void* MockGetTileTexture(u8 /*t*/) { return g_mock ? (const void*)&g_mock->rec : nullptr; }

GroundViewParams MakeView(int W, int H) {
    GroundViewParams vp;
    CityCamera3D cam;
    const float eye[3] = {0.0f, 250.0f, -120.0f};
    const float tgt[3] = {0.0f, 0.0f, 0.0f};
    AimCamera(cam, eye, tgt);
    for (int k = 0; k < 3; ++k) { vp.eye[k] = cam.eye[k]; vp.rot[k] = cam.rot[k]; }
    static render::Frustum fr{};
    static float planes[6][4];
    const float halfW = (float)W * 0.5f;
    BuildEngineFrustum((float)W, (float)H, halfW, 1.0f, 5000.0f, fr);
    for (int pi = 0; pi < 4; ++pi)
        for (int k = 0; k < 4; ++k) planes[pi][k] = fr.plane[pi][k];
    planes[4][0] = 0; planes[4][1] = 0; planes[4][2] = 1.0f;  planes[4][3] = 1.0f;
    planes[5][0] = 0; planes[5][1] = 0; planes[5][2] = -1.0f; planes[5][3] = -5000.0f;
    vp.frustum = &fr;
    vp.clipPlanes = planes;
    vp.clipPlaneCount = 6;
    vp.proj.xScale = halfW;  vp.proj.xOffset = halfW;
    vp.proj.yScale = -halfW; vp.proj.yOffset = (float)H * 0.5f;
    return vp;
}

std::vector<u8> Snap(render::Surface* fb, int W, int H) {
    std::vector<u8> s; s.reserve((size_t)W * H * 3);
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x) {
            u8 p[3]; render::SurfaceGetPixelRgb(fb, x, y, p);
            s.push_back(p[0]); s.push_back(p[1]); s.push_back(p[2]);
        }
    return s;
}

} // namespace

// ---------------------------------------------------------------------------
// 1) The per-quad UV record (flt_13FE540) — BuildTerrainUvTable corner insets.
//    The textured span reads T0 = floats[0..5] (the first triangle of a quad)
//    and T1 = floats[6..11] (the second), as poly+0x10 / poly+0x38 (uvPtr,
//    uvPtr+0x18) at @0x5c2008/0x5c201b. Verify the exact inset values.
// ---------------------------------------------------------------------------
TEST(TerrainTexturing, UvRecordCornerInsets) {
    float uv[render::kTerrainUvTableSize];
    render::BuildTerrainUvTable(uv, 64);
    const float e  = 1.0f / 64.0f;          // 0.015625
    const float f  = 1.0f - e - e;          // 0.96875
    const float ef = e + f;                 // 0.984375
    CHECK(std::fabs(uv[0] - 0.015625f) < 1e-7f);
    // T0 (the first triangle's 3 (u,v) pairs): (e,ef),(e,e),(ef,e).
    CHECK(std::fabs(uv[0] - e)  < 1e-6f); CHECK(std::fabs(uv[1] - ef) < 1e-6f);
    CHECK(std::fabs(uv[2] - e)  < 1e-6f); CHECK(std::fabs(uv[3] - e)  < 1e-6f);
    CHECK(std::fabs(uv[4] - ef) < 1e-6f); CHECK(std::fabs(uv[5] - e)  < 1e-6f);
    // T1 (the second triangle's 3 (u,v) pairs): (ef,e),(ef,ef),(e,ef).
    CHECK(std::fabs(uv[6]  - ef) < 1e-6f); CHECK(std::fabs(uv[7]  - e)  < 1e-6f);
    CHECK(std::fabs(uv[8]  - ef) < 1e-6f); CHECK(std::fabs(uv[9]  - ef) < 1e-6f);
    CHECK(std::fabs(uv[10] - e)  < 1e-6f); CHECK(std::fabs(uv[11] - ef) < 1e-6f);
    // The selector the span uses: sel==0 -> &uv[0] (T0); sel==6 -> &uv[6] (T1).
    CHECK_EQ((int)(&uv[6] - &uv[0]), 6);
}

// ---------------------------------------------------------------------------
// 2) GroundFrame textured-vs-shaded selection. With the mock binder bound +
//    the active getTileTexture hook resolving the mock texture, the ground
//    flushes TEXTURED (the mock texture's red/blue texel colours appear). The
//    unbound render is the white-default level-shade (greyscale; r==g==b), and
//    differs from the textured one.
// ---------------------------------------------------------------------------
TEST(TerrainTexturing, TexturedVsShadedSelection) {
    FloorGround g = BuildFloorGroundFromBlock(MakeUniformBlock(64, /*type=*/0));
    CHECK(g.valid());

    const int W = 128, H = 96;
    GroundViewParams vp = MakeView(W, H);

    // --- UNBOUND: the established white-default level-shaded ground -----------
    play::SetTerrainRenderHooks(nullptr);   // inert getTileTexture
    GroundFrame gfA;
    CHECK(gfA.Bind(&g));
    render::Surface* fbA = render::SurfaceCreate(W, H, 16);
    CHECK(fbA != nullptr);
    render::SurfaceColorFill(fbA, 0, 0, 64);
    GroundRenderStats sa = gfA.Render(fbA, vp, /*frameFlags=*/1);
    CHECK(sa.rasterTris > 0);
    std::vector<u8> shaded = Snap(fbA, W, H);

    // every non-clear ground pixel in the white-default path is GREY (r==g==b)
    // (the level-shaded white default — no colour). Count them.
    int greyShaded = 0, colourShaded = 0;
    for (int i = 0; i < W * H; ++i) {
        u8 r = shaded[3 * i], gg = shaded[3 * i + 1], b = shaded[3 * i + 2];
        if (r == 0 && gg == 0 && b == 64) continue;   // clear
        if (r == gg && gg == b) ++greyShaded; else ++colourShaded;
    }
    CHECK(greyShaded > 50);          // the white default filled the ground grey
    CHECK_EQ(colourShaded, 0);       // and NOTHING coloured (no texture)

    // --- BOUND: the mock texture -> TEXTURED ground --------------------------
    MockTex mock = MakeMockTex();
    g_mock = &mock;
    play::TerrainRenderHooks hk{};
    hk.getTileTexture = &MockGetTileTexture;
    play::SetTerrainRenderHooks(&hk);
    play::GroundTexBinder binder{};
    binder.getTileTextureRec = &MockRec;
    binder.palette565        = &MockPal;

    GroundFrame gfB;
    CHECK(gfB.Bind(&g));
    gfB.SetTexBinder(&binder);
    render::Surface* fbB = render::SurfaceCreate(W, H, 16);
    CHECK(fbB != nullptr);
    render::SurfaceColorFill(fbB, 0, 0, 64);
    GroundRenderStats sb = gfB.Render(fbB, vp, 1);
    CHECK(sb.rasterTris > 0);
    std::vector<u8> textured = Snap(fbB, W, H);

    // the mock texture has RED (r>0,g=b=0) and BLUE (b>0,r=g=0) texels — count
    // pixels carrying real colour (NOT grey): the textured ground shows them.
    int colourTex = 0, redTex = 0, blueTex = 0;
    for (int i = 0; i < W * H; ++i) {
        u8 r = textured[3 * i], gg = textured[3 * i + 1], b = textured[3 * i + 2];
        if (r == 0 && gg == 0 && b == 64) continue;
        if (!(r == gg && gg == b)) ++colourTex;
        if (r > 0 && gg == 0 && b == 0) ++redTex;
        if (b > 0 && r == 0 && gg == 0) ++blueTex;
    }
    CHECK(colourTex > 50);   // the ground is now genuinely textured (coloured)
    CHECK(redTex  > 0);      // the mock RED texel colour appears
    CHECK(blueTex > 0);      // the mock BLUE texel colour appears

    // the two frames DIFFER (the binder is the only change -> texturing happened)
    CHECK(textured != shaded);

    // --- ADDITIVE SAFETY: a fresh unbound GroundFrame matches the first ------
    play::SetTerrainRenderHooks(nullptr);
    g_mock = nullptr;
    GroundFrame gfC;
    CHECK(gfC.Bind(&g));
    render::Surface* fbC = render::SurfaceCreate(W, H, 16);
    render::SurfaceColorFill(fbC, 0, 0, 64);
    gfC.Render(fbC, vp, 1);
    std::vector<u8> shaded2 = Snap(fbC, W, H);
    CHECK(shaded2 == shaded);   // unbound is byte-identical (additive)

    render::SurfaceDestroy(fbA);
    render::SurfaceDestroy(fbB);
    render::SurfaceDestroy(fbC);
}

// ---------------------------------------------------------------------------
// 3) Determinism: the textured ground render is byte-identical across reruns.
// ---------------------------------------------------------------------------
TEST(TerrainTexturing, TexturedDeterminism) {
    FloorGround g = BuildFloorGroundFromBlock(MakeUniformBlock(64, 0));
    CHECK(g.valid());
    const int W = 128, H = 96;
    GroundViewParams vp = MakeView(W, H);

    MockTex mock = MakeMockTex();
    g_mock = &mock;
    play::TerrainRenderHooks hk{};
    hk.getTileTexture = &MockGetTileTexture;
    play::SetTerrainRenderHooks(&hk);
    play::GroundTexBinder binder{};
    binder.getTileTextureRec = &MockRec;
    binder.palette565        = &MockPal;

    GroundFrame gf;
    CHECK(gf.Bind(&g));
    gf.SetTexBinder(&binder);

    render::Surface* fb = render::SurfaceCreate(W, H, 16);
    render::SurfaceColorFill(fb, 0, 0, 64);
    gf.Render(fb, vp, 1);
    std::vector<u8> a = Snap(fb, W, H);
    render::SurfaceColorFill(fb, 0, 0, 64);
    gf.Render(fb, vp, 1);
    std::vector<u8> b = Snap(fb, W, H);
    CHECK(a == b);

    render::SurfaceDestroy(fb);
    play::SetTerrainRenderHooks(nullptr);
    g_mock = nullptr;
}
