#include "tests/framework/test.h"

// ===========================================================================
// Unit tests for the RECONSTRUCTION-ONLY surface clip of the RGBZ textured
// rasterizer (suite prefix: RasterClip).
//
// gilde.exe 0x5F6C30 VIBE_Raster_RasterizeMirrorTriangle wrote spans unclipped,
// trusting the upstream poly clip (+ the DDraw guard band). The software
// reconstruction clamps every span against the destination Surface's clip rect
// (SurfaceCreate inits [0,width) x [0,height)) so plane-clip float-rounding
// overshoot can never write outside fb->pixels — the same defensive clip the
// shaded path (raster.cpp FillTexturedSpansShaded) has always carried. Caught
// live by ASAN: a CityView3D city frame wrote 590 bytes past the framebuffer
// through this path (see progress/session-integration-wave2.md).
//
// The clamp must be PIXEL-NEUTRAL for the visible region: a triangle drawn
// overhanging a small surface must produce exactly the pixels of the same
// triangle drawn fully inside a larger surface, cropped — the per-span U/V
// start re-derives from the clamped xL through the same gradient expression,
// and the skipped top rows advance the edge accumulators by the identical
// k-scaled integer step.
// ===========================================================================
#include "render/raster_textured.h"
#include "render/surface.h"
#include "render/texture.h"

#include <cstring>
#include <vector>

using namespace guild;
using namespace guild::render;

namespace {

// A deterministic 8x8 palettized texture + 565 palette.
void MakeTexture(Texture& tex, std::vector<u16>& pal) {
    TextureSetSize(tex, 8);
    for (std::size_t i = 0; i < tex.texels.size(); ++i)
        tex.texels[i] = (u8)(1 + (i % 17));    // never 0 (no colour-key holes)
    pal.assign(256, 0);
    for (int i = 0; i < 256; ++i)
        pal[(std::size_t)i] = (u16)(0x1111 * (i % 13) + i);
}

// Read pixel (x, y) of a 16bpp surface.
u16 Px(const Surface* s, int x, int y) {
    const u8* base = s->pixels + (std::size_t)y * (std::size_t)s->pitch;
    u16 v;
    std::memcpy(&v, base + 2 * x, 2);
    return v;
}

} // namespace

// A triangle overhanging EVERY edge of a small surface draws exactly the crop
// of the same triangle rendered fully inside a larger surface.
TEST(RasterClip, OverhangingTriangleEqualsCropOfUnclippedDraw) {
    Texture tex;
    std::vector<u16> pal;
    MakeTexture(tex, pal);

    // Big surface: triangle fully inside, anchored so the [32,64) x [32,64)
    // window of the big draw corresponds to the whole 32x32 small surface.
    Surface* big = SurfaceCreate(96, 96, 16);
    CHECK(big != nullptr);
    RgbzVertex vb[3];
    vb[0] = {20.0f, 12.0f, 0.10f, 0.15f};
    vb[1] = {80.0f, 40.0f, 0.90f, 0.20f};
    vb[2] = {30.0f, 88.0f, 0.30f, 0.95f};
    CHECK_EQ(RasterizeTexturedTriangleRgbz(big, vb, tex, pal.data()), 1);

    // Small surface: same triangle shifted by (-32,-32) -> overhangs left, top,
    // right and bottom of the 32x32 clip rect.
    Surface* small = SurfaceCreate(32, 32, 16);
    CHECK(small != nullptr);
    RgbzVertex vs[3];
    for (int i = 0; i < 3; ++i) {
        vs[i] = vb[i];
        vs[i].x -= 32.0f;
        vs[i].y -= 32.0f;
    }
    CHECK_EQ(RasterizeTexturedTriangleRgbz(small, vs, tex, pal.data()), 1);

    // Pixel-for-pixel: small == big cropped at (32,32).
    int mismatches = 0;
    int covered = 0;
    for (int y = 0; y < 32; ++y) {
        for (int x = 0; x < 32; ++x) {
            const u16 a = Px(small, x, y);
            const u16 b = Px(big, x + 32, y + 32);
            if (a != b) ++mismatches;
            if (b != 0) ++covered;
        }
    }
    CHECK_EQ(mismatches, 0);
    CHECK(covered > 200);          // the window genuinely intersects the tri

    SurfaceDestroy(small);
    SurfaceDestroy(big);
}

// A triangle entirely outside the clip rect paints nothing and reports no draw
// rows touched the surface (drew stays 0 only when no span ran; fully-above
// triangles clamp every row count to zero).
TEST(RasterClip, FullyOffscreenTrianglePaintsNothing) {
    Texture tex;
    std::vector<u16> pal;
    MakeTexture(tex, pal);

    Surface* s = SurfaceCreate(32, 32, 16);
    CHECK(s != nullptr);

    // Entirely above the surface.
    RgbzVertex above[3] = {
        {4.0f, -40.0f, 0.1f, 0.1f}, {30.0f, -25.0f, 0.8f, 0.2f},
        {10.0f, -8.0f, 0.3f, 0.9f}};
    RasterizeTexturedTriangleRgbz(s, above, tex, pal.data());
    // Entirely right of the surface.
    RgbzVertex right[3] = {
        {40.0f, 4.0f, 0.1f, 0.1f}, {70.0f, 12.0f, 0.8f, 0.2f},
        {45.0f, 28.0f, 0.3f, 0.9f}};
    RasterizeTexturedTriangleRgbz(s, right, tex, pal.data());

    int nonZero = 0;
    for (int y = 0; y < 32; ++y)
        for (int x = 0; x < 32; ++x)
            if (Px(s, x, y) != 0) ++nonZero;
    CHECK_EQ(nonZero, 0);

    SurfaceDestroy(s);
}

// Direct FillSpanLoop callers with a zero-initialised RgbzRasterState keep the
// legacy unclipped behaviour (clipX1 <= clipX0 disables the clamp) — the
// additive-field guarantee for the existing span tests/tools.
TEST(RasterClip, ZeroInitStateKeepsUnclippedSpanLoop) {
    Texture tex;
    std::vector<u16> pal;
    MakeTexture(tex, pal);

    std::vector<u16> fb(64 * 4, 0);
    RgbzRasterState rs;
    std::memset(&rs, 0, sizeof rs);
    rs.fbPitchPx = 64;
    rs.fbRow0 = fb.data();
    rs.xLeft = 2 << 16;
    rs.xRight = 10 << 16;
    rs.uLeft = 0;
    rs.vLeft = 0;
    SpanTexParams p = BuildSpanTexParams(tex, pal.data(), 1 << 14, 1 << 14);
    FillSpanLoop(rs, 1, p);

    int painted = 0;
    for (int x = 0; x < 64; ++x)
        if (fb[(std::size_t)x] != 0) ++painted;
    CHECK_EQ(painted, 8);          // [ceil(2), ceil(10)) exactly, no clamping
}

// ===========================================================================
// WAVE-10 HARDENING: more surface-clip edge cases. ASAN is the real assertion;
// these drive the clamp against tiny / odd-stride surfaces and the fully-clipped
// row branches so no span write escapes fb->pixels.
// ===========================================================================

// A triangle overhanging only the RIGHT + BOTTOM edges: xR clamps to clipX1,
// the row walk clamps the bottom rows. No write past the surface (ASAN).
TEST(RasterClip, OverhangRightBottomClamps) {
    Texture tex;
    std::vector<u16> pal;
    MakeTexture(tex, pal);
    Surface* s = SurfaceCreate(16, 16, 16);
    CHECK(s != nullptr);
    RgbzVertex v[3] = {
        {8.0f, 6.0f, 0.1f, 0.1f},
        {40.0f, 10.0f, 0.9f, 0.2f},   // far right
        {12.0f, 40.0f, 0.3f, 0.9f},   // far below
    };
    RasterizeTexturedTriangleRgbz(s, v, tex, pal.data());
    SurfaceDestroy(s);                // clean free == no heap corruption
}

// A triangle entirely BELOW the surface: every row count clamps to zero, the
// fbRow0 cursor is never advanced into invalid memory, nothing is painted.
TEST(RasterClip, FullyBelowSurfacePaintsNothing) {
    Texture tex;
    std::vector<u16> pal;
    MakeTexture(tex, pal);
    Surface* s = SurfaceCreate(24, 24, 16);
    CHECK(s != nullptr);
    RgbzVertex v[3] = {
        {4.0f, 40.0f, 0.1f, 0.1f}, {18.0f, 48.0f, 0.8f, 0.2f},
        {8.0f, 60.0f, 0.3f, 0.9f}};
    RasterizeTexturedTriangleRgbz(s, v, tex, pal.data());
    int nonZero = 0;
    for (int y = 0; y < 24; ++y)
        for (int x = 0; x < 24; ++x)
            if (Px(s, x, y) != 0) ++nonZero;
    CHECK_EQ(nonZero, 0);
    SurfaceDestroy(s);
}

// A 1x1 destination surface with a triangle covering it: the clamp keeps the
// single span at column 0 only; the smallest possible framebuffer is in bounds.
TEST(RasterClip, OnePixelSurface) {
    Texture tex;
    std::vector<u16> pal;
    MakeTexture(tex, pal);
    Surface* s = SurfaceCreate(1, 1, 16);
    CHECK(s != nullptr);
    RgbzVertex v[3] = {
        {-2.0f, -2.0f, 0.0f, 0.0f},
        {4.0f, -1.0f, 1.0f, 0.0f},
        {-1.0f, 4.0f, 0.0f, 1.0f},
    };
    RasterizeTexturedTriangleRgbz(s, v, tex, pal.data());
    SurfaceDestroy(s);                // no OOB on the single-pixel buffer (ASAN)
}
