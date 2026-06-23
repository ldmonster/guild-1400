#include "test.h"

// Integration: the fog COLOUR-BLEND handoff rendered into a real 16bpp Surface.
// This proves the reconstructed D3D-vertex-fog blend (BlendFog565) applied across
// a span produces a visible near->far fog gradient, exactly as the raster layer
// will call it after fetching each textured/shaded texel.
//
// Pipeline modelled (the live raster's per-pixel tail, see fog-render-wave6.md):
//   1. fog state from ConfigureFog (near/far/colour),
//   2. per-vertex fog factor from ComputeFogFactor (the vertex+79 FVF byte),
//   3. linear interpolation of the factor across the span (what D3D does),
//   4. BlendFog565(texel, fogColor, factor) -> the surface pixel.
#include "render/fog.h"
#include "render/surface.h"
#include "render/colorformat.h"

#include <cstdint>

using namespace guild;
using namespace guild::render;

// Render one horizontal span of a constant texel colour into a 565 Surface, with
// the per-vertex fog factor interpolated from left (near) to right (far). Verify
// the left end is the texel and the right end is the fog colour, monotone between.
TEST(RenderFogBlendItest, SpanGradientIntoSurface) {
    const int W = 64, H = 1;
    Surface* surf = SurfaceCreate(W, H, 16, Format565());
    CHECK(surf != nullptr);

    // Fog: near 10, far 110, dark-grey fog colour 0x404040.
    FogState fog{};
    CHECK(ConfigureFog(fog, 10.0f, 110.0f, 0x404040, /*global*/true, /*feature*/true));
    CHECK(fog.enabled);

    // The textured texel for this span: a bright orange 0xFF8000.
    const u8 texR = 0xFF, texG = 0x80, texB = 0x00;
    const u16 texel565 = (u16)PackColor(surf->fmt, texR, texG, texB);

    // Endpoints: left vertex near (factor 255, no fog), right vertex far
    // (factor 0, full fog). The raster interpolates the factor linearly.
    int fL = ComputeFogFactor(fog, fog.nearSq);    // d2 == nearSq -> 255
    int fR = ComputeFogFactor(fog, 110.0f * 110.0f); // d2 == far^2 -> 0
    CHECK_EQ(fL, 255);
    CHECK_EQ(fR, 0);

    for (int x = 0; x < W; ++x) {
        // Linear factor interpolation across the span (D3D Gouraud-fog).
        int factor = fL + (fR - fL) * x / (W - 1);
        u16 px = BlendFog565(texel565, (u32)fog.color, factor);
        u8 r = (u8)((px >> 11) << 3), g = (u8)(((px >> 5) & 0x3F) << 2),
           b = (u8)((px & 0x1F) << 3);
        SurfaceSetPixelRgb(surf, x, 0, r, g, b);
    }

    // Left pixel == the unfogged texel.
    u8 left[3];  SurfaceGetPixelRgb(surf, 0, 0, left);
    CHECK_EQ((int)left[0], (int)(texR & 0xF8));
    CHECK_EQ((int)left[1], (int)(texG & 0xFC));
    CHECK_EQ((int)left[2], (int)(texB & 0xF8));

    // Right pixel == the fog colour (quantised to 565).
    u8 right[3]; SurfaceGetPixelRgb(surf, W - 1, 0, right);
    CHECK_EQ((int)right[0], 0x40 & 0xF8);
    CHECK_EQ((int)right[1], 0x40 & 0xFC);
    CHECK_EQ((int)right[2], 0x40 & 0xF8);

    // Red channel falls monotonically near->far (texel red 0xFF > fog red 0x40).
    int prevR = 256;
    bool monotone = true;
    for (int x = 0; x < W; ++x) {
        u8 p[3]; SurfaceGetPixelRgb(surf, x, 0, p);
        if (p[0] > prevR) monotone = false;
        prevR = p[0];
    }
    CHECK(monotone);

    SurfaceDestroy(surf);
}

// When fog is disabled the per-vertex factor stays 255 (no-op blend): the span is
// the raw texel — the engine simply never wrote a < 255 factor (byte_649DD8 = 0).
TEST(RenderFogBlendItest, DisabledIsPassthrough) {
    Surface* surf = SurfaceCreate(8, 1, 16, Format565());
    CHECK(surf != nullptr);
    const u16 texel = (u16)PackColor(surf->fmt, 0x12, 0x34, 0x56);
    for (int x = 0; x < 8; ++x) {
        u16 px = BlendFog565(texel, 0x000000u, 255); // factor 255 -> unchanged
        CHECK_EQ((int)px, (int)texel);
    }
    SurfaceDestroy(surf);
}

// WAVE-10 HARDENING: a degenerate fog range (near == far) into a 1-pixel surface.
// ConfigureFog gates on near < far, so fog stays disabled — the single pixel is
// the raw texel (no /0 slope used in the blend, no OOB on a 1x1 buffer).
TEST(RenderFogBlendItest, NearEqualsFarOnePixel) {
    Surface* surf = SurfaceCreate(1, 1, 16, Format565());
    CHECK(surf != nullptr);
    FogState fog{};
    // near == far -> not applied as an enabled range.
    CHECK(ConfigureFog(fog, 50.0f, 50.0f, 0x404040, true, true));
    CHECK(!fog.enabled);
    const u16 texel = (u16)PackColor(surf->fmt, 0x10, 0x20, 0x30);
    // The raster only blends when enabled; mirror that — factor 255 passthrough.
    u16 px = BlendFog565(texel, (u32)fog.color, fog.enabled ? 0 : 255);
    CHECK_EQ((int)px, (int)texel);
    SurfaceSetPixelRgb(surf, 0, 0,
                       (u8)((px >> 11) << 3),
                       (u8)(((px >> 5) & 0x3F) << 2),
                       (u8)((px & 0x1F) << 3));
    u8 got[3]; SurfaceGetPixelRgb(surf, 0, 0, got);
    CHECK_EQ((int)got[0], (int)(0x10 & 0xF8));
    SurfaceDestroy(surf);
}
