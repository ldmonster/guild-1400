#include "test.h"

#include "render/falloff_lut.h"
#include "render/texture_mip.h"
#include "render/shadow_render.h"
#include "render/particle_render.h"
#include "render/snow.h"
#include "render/rain.h"

#include <cmath>
#include <cstring>
#include <vector>

using namespace guild;
using namespace guild::render;

// ===========================================================================
// Falloff LUT — table[i] = 1 - acos(i/1024)*(2/pi), vs a Python/<cmath> oracle.
// (Harden fix @0x5f0b9c: AcosGuarded is ACOS, not asin — see falloff_lut.cpp.)
// ===========================================================================
TEST(RenderFalloff, MatchesAcosOracle) {
    static float table[kFalloffEntries];
    InitFalloffTable(table);
    for (int i = 0; i < kFalloffEntries; ++i) {
        double x = (double)i * (1.0 / 1024.0);
        float ref = (float)(1.0 - std::acos(x) * (2.0 / M_PI));
        CHECK(std::fabs(table[i] - ref) <= 1e-6f);
    }
}

TEST(RenderFalloff, GoldenVectors) {
    static float table[kFalloffEntries];
    InitFalloffTable(table);
    // Golden values computed in float by python (acos form; old asin goldens
    // replaced by the @0x5f0b9c harden fix, e.g. table[0] 1.0 -> ~4.03e-8).
    auto close = [](float a, float b) { return std::fabs(a - b) <= 1e-6f; };
    CHECK(close(table[0],    4.034205858260975e-08f));
    CHECK(close(table[1],    0.0006217394256964326f));
    CHECK(close(table[16],   0.00994762871414423f));
    CHECK(close(table[64],   0.039814725518226624f));
    CHECK(close(table[256],  0.1608612835407257f));
    CHECK(close(table[512],  0.3333333730697632f));
    CHECK(close(table[768],  0.5398930907249451f));
    CHECK(close(table[1023], 0.9718628525733948f));
    // Monotone non-decreasing and bounded.
    for (int i = 1; i < kFalloffEntries; ++i)
        CHECK(table[i] >= table[i - 1] - 1e-7f);
}

TEST(RenderFalloff, AcosGuardedIsAcosShadow) {
    CHECK(std::fabs(AcosGuarded(0.0) - M_PI / 2.0) <= 1e-12);
    CHECK(std::fabs(AcosGuarded(0.5) - std::acos(0.5)) <= 1e-12);
    CHECK(std::fabs(AcosGuarded(-0.25) - std::acos(-0.25)) <= 1e-12);
    // |x|==1 guard.
    CHECK(std::fabs(AcosGuarded(1.0) - 0.0) <= 1e-12);
    CHECK(std::fabs(AcosGuarded(-1.0) - M_PI) <= 1e-12);
}

// ===========================================================================
// Texture mip: blend LUT (byte-exact golden), channel LUT, 2x2 downsample.
// ===========================================================================
TEST(RenderMip, BlendLutGolden) {
    std::vector<u8> lut(4 * 4 * 4);
    u32 stride = BuildBlendLut(lut.data(), 4);
    CHECK_EQ(stride, 16u);
    static const u8 golden[64] = {
        255, 0, 0, 0, 192, 63, 0, 0, 128, 127, 0, 0, 64, 191, 0, 0,
        191, 0, 64, 0, 144, 47, 48, 16, 96, 95, 32, 32, 48, 143, 16, 48,
        127, 0, 128, 0, 96, 31, 96, 32, 64, 63, 64, 64, 32, 95, 32, 96,
        63, 0, 192, 0, 48, 15, 144, 48, 32, 31, 96, 96, 16, 47, 48, 144,
    };
    for (int i = 0; i < 64; ++i)
        CHECK_EQ(lut[i], golden[i]);
}

TEST(RenderMip, ChannelLut565) {
    // RGB565: R mask 0xF800, G 0x07E0, B 0x001F.
    ChannelShifts s = MipChannelShifts(0xF800, 0x07E0, 0x001F);
    // R: 5-bit field at bit 11 -> down = 8-5 = 3, up = 11.
    CHECK_EQ((int)s.downR, 3); CHECK_EQ((int)s.upR, 11);
    // G: 6-bit at bit 5 -> down = 2, up = 5.
    CHECK_EQ((int)s.downG, 2); CHECK_EQ((int)s.upG, 5);
    // B: 5-bit at bit 0 -> down = 3, up = 0.
    CHECK_EQ((int)s.downB, 3); CHECK_EQ((int)s.upB, 0);

    static u32 lut[768];
    BuildChannelLUT(lut, 0xF800, 0x07E0, 0x001F);
    // Full-intensity 255 maps to the top of each channel field.
    CHECK_EQ(lut[255], (u32)((255 >> 3) << 11));   // R = 0xF800
    CHECK_EQ(lut[256 + 255], (u32)((255 >> 2) << 5)); // G = 0x07E0
    CHECK_EQ(lut[512 + 255], (u32)((255 >> 3) << 0)); // B = 0x001F
    CHECK_EQ(lut[0], 0u);
    CHECK_EQ(lut[256], 0u);
}

TEST(RenderMip, BlockSizeAndWidth) {
    CHECK_EQ(MipBlockSize(256, 256), 16); // ratio 1 -> clamp to 16
    CHECK_EQ(MipBlockSize(512, 16), 32);  // ratio 32 in [16,64]
    CHECK_EQ(MipBlockSize(4096, 16), 64); // ratio 256 -> clamp 64
    CHECK_EQ(MipWidth(256, 0), 256);
    CHECK_EQ(MipWidth(256, 2), 64);
    CHECK_EQ(MipWidth(2, 4), 0);          // 0x5db350 `shr eax,cl`: 2>>4==0, no clamp
    CHECK_EQ(MipLevelCount(256), 9);      // 256..1
    CHECK_EQ(MipLevelCount(1), 1);
}

TEST(RenderMip, IndexDownsamplePreservesPalette) {
    // 4x4 index buffer; 2x2 box-select keeps the top-left index of each block.
    u8 src[16] = {
        10, 11, 20, 21,
        12, 13, 22, 23,
        30, 31, 40, 41,
        32, 33, 42, 43,
    };
    u8 dst[4];
    DownsampleIndex2x(src, 4, dst);
    CHECK_EQ(dst[0], 10); CHECK_EQ(dst[1], 20);
    CHECK_EQ(dst[2], 30); CHECK_EQ(dst[3], 40);

    auto chain = BuildIndexMipChain(src, 4);
    CHECK_EQ((int)chain.size(), 3);      // 4x4, 2x2, 1x1
    CHECK_EQ((int)chain[0].size(), 16);
    CHECK_EQ((int)chain[1].size(), 4);
    CHECK_EQ((int)chain[2].size(), 1);
    CHECK_EQ(chain[2][0], 10);            // top-left propagates to the 1x1 level
}

// ===========================================================================
// Shadow rasterizer: edge interp + flat triangle fill into an 8bpp surface.
// ===========================================================================
TEST(RenderShadowRaster, EdgeSlope1616) {
    ShadowRasterState s;
    // A vertical-ish edge: vertex 0 at (px=0, py=0), vertex 1 at (px=2<<16, py=4<<16).
    s.px[0] = 0;          s.py[0] = 0;
    s.px[1] = 2 << 16;    s.py[1] = 4 << 16;
    int start = ComputeEdgeSlope(s, 0, 1);
    // slope dx/dy = (2<<16)/4 in 16.16 == 0.5 px/scanline == 0x8000.
    CHECK_EQ(s.leftDxDy, 0x8000);
    CHECK_EQ(start, 0); // top vertex already on an integer scanline
}

TEST(RenderShadowRaster, FillTriangleCoversInterior) {
    // 16x16 8bpp surface, clear to 0.
    ShadowSurface surf;
    std::vector<u8> px(16 * 16, 0);
    surf.pixels = px.data();
    surf.pitch = 16; surf.width = 16; surf.height = 16; surf.is16bpp = false;

    ShadowTri tri;
    // A well-wound triangle (apex on top, flat bottom) inside the surface.
    tri.x[0] = 6.0f;  tri.y[0] = 2.0f;
    tri.x[1] = 12.0f; tri.y[1] = 10.0f;
    tri.x[2] = 2.0f;  tri.y[2] = 10.0f;
    tri.backFlag = true; // allow the reverse-winding branch too

    ShadowRasterState s;
    RasterizeTriangle(s, tri, surf);

    // Count filled pixels (value 1).
    int filled = 0;
    for (u8 v : px) if (v == 1) ++filled;
    CHECK(filled > 0);
    // A point near the centroid ((6+12+2)/3, (2+10+10)/3) = (6.67, 7.3).
    CHECK(px[7 * 16 + 6] == 1);
    // The apex row is above the body; far corners stay 0.
    CHECK(px[0] == 0);
    CHECK(px[15 * 16 + 15] == 0);
    // No fill below the flat bottom edge (y >= 10).
    for (int x = 0; x < 16; ++x)
        CHECK(px[11 * 16 + x] == 0);
}

// ===========================================================================
// Snow / rain render vertex build: viewport clip + TLVERTEX layout.
// ===========================================================================
TEST(RenderWeather, SnowVertexBuild) {
    SnowFlake flakes[2];
    std::memset(flakes, 0, sizeof(flakes));
    // Flake 0 inside the viewport.
    flakes[0].pz = 0.5f;
    flakes[0].sx = 40.0f; flakes[0].sy = 30.0f;
    flakes[0].sx2 = 42.0f; flakes[0].sy2 = 33.0f;
    // Flake 1 outside (tail X beyond x1).
    flakes[1].sx = 40.0f; flakes[1].sy = 30.0f;
    flakes[1].sx2 = 500.0f; flakes[1].sy2 = 33.0f;

    SnowSystem sys; sys.count = 2; sys.flakes = flakes;
    SnowViewport vp{0, 0, 100, 100};

    std::vector<Tlvertex> out;
    int n = BuildSnowVertices(sys, vp, out);
    CHECK_EQ(n, 3);                 // only flake 0 visible -> 1 triangle
    CHECK_EQ((int)out.size(), 3);
    // Vertex 0: midpoint X = (40+42)/2 = 41, Y = head 30.
    CHECK(std::fabs(out[0].x - 41.0f) <= 1e-5f);
    CHECK(std::fabs(out[0].y - 30.0f) <= 1e-5f);
    CHECK(std::fabs(out[0].u - 0.5f) <= 1e-5f);
    CHECK_EQ(out[0].diffuse, 1348756580u);
    CHECK(std::fabs(out[0].rhw - 1.0f) <= 1e-6f);
    // z = (1-0.5)*0.025 = 0.0125.
    CHECK(std::fabs(out[0].z - 0.0125f) <= 1e-6f);
    // Vertex 1 = tail; vertex 2 = (head X, tail Y).
    CHECK(std::fabs(out[1].x - 42.0f) <= 1e-5f);
    CHECK(std::fabs(out[2].x - 40.0f) <= 1e-5f);
    CHECK(std::fabs(out[2].y - 33.0f) <= 1e-5f);
}

TEST(RenderWeather, RainVertexBuild) {
    RainDrop drops[2];
    std::memset(drops, 0, sizeof(drops));
    drops[0].pz = 0.0f;
    drops[0].sx = 10.0f; drops[0].sy = 10.0f;
    drops[0].sx2 = 12.0f; drops[0].sy2 = 20.0f;
    // Drop 1 head Y outside.
    drops[1].sx = 10.0f; drops[1].sy = -5.0f;
    drops[1].sx2 = 12.0f; drops[1].sy2 = 20.0f;

    RainSystem sys; sys.count = 2; sys.drops = drops;
    SnowViewport vp{0, 0, 200, 200};

    std::vector<Tlvertex> out;
    int n = BuildRainVertices(sys, vp, out, 0xDEADBEEF);
    CHECK_EQ(n, 2);                 // only drop 0 -> 1 line
    CHECK(std::fabs(out[0].x - 10.0f) <= 1e-5f);
    CHECK(std::fabs(out[0].y - 10.0f) <= 1e-5f);
    CHECK(std::fabs(out[1].x - 12.0f) <= 1e-5f);
    CHECK(std::fabs(out[1].y - 20.0f) <= 1e-5f);
    CHECK_EQ(out[0].diffuse, 0xDEADBEEFu);
    // z = (1-0)*0.025 = 0.025.
    CHECK(std::fabs(out[0].z - 0.025f) <= 1e-6f);
}

TEST(RenderWeather, FadeInterp) {
    // Inside the window: linear blend.
    // start=0,end=10, valA=100 (result[3]), valB=200 (result[2]), now=5.
    // = 100*(5-0)/10 + (10-5)*200/10 = 50 + 100 = 150.
    CHECK_EQ(InterpolateFade(0, 10, 100, 200, 5), 150);
    // now >= end -> valA.
    CHECK_EQ(InterpolateFade(0, 10, 100, 200, 10), 100);
    CHECK_EQ(InterpolateFade(0, 10, 100, 200, 99), 100);
}
