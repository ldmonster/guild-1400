// =============================================================================
// WAVE-6 W6-INTEGRATE — unit tests for the asset-free integration seams:
//   * the raster-span fog hook (render::SpanFog() global) — default-disabled keeps
//     FillSpanTextured byte-identical; enabled blends each written pixel via
//     BlendFog565.
//   * the sun/day-cycle driver (render::ComputeSunState) day/night brightness the
//     CityView3D sky/light passes read.
// =============================================================================
#include "test.h"

#include "render/fog.h"
#include "render/raster.h"
#include "render/raster_textured.h"
#include "render/surface.h"
#include "render/daycycle.h"

#include <vector>

using namespace guild;

namespace {

// Build a tiny 2x2 textured triangle setup and rasterize one span into a 16bpp
// surface, returning the resolved pixels of the first row's span.
struct MiniTex {
    std::vector<u8>  texels;   // 2x2 indices
    std::vector<u16> pal;      // palette (lightRow8|idx) -> 565
};

} // namespace

// ---------------------------------------------------------------------------
TEST(FrameIntegrationWave6, SpanFogDefaultDisabledIsIdentity) {
    // Default SpanFog() is disabled (factor 255). A textured span renders exactly
    // the palette colours.
    render::SpanFog() = render::SpanFogState{};   // reset to default
    CHECK(!render::SpanFog().enabled);
    CHECK_EQ(render::SpanFog().factor, 255);

    render::RasterState rs{};
    rs.spanLen = 4;
    std::vector<u8> tex = {0, 1, 2, 3};
    std::vector<u16> pal(256, 0);
    pal[0] = 0x1234; pal[1] = 0x5678; pal[2] = 0x9ABC; pal[3] = 0xDEF0;
    render::SpanTexParams p{};
    p.texBase = tex.data();
    p.palBase = pal.data();
    p.texelMask = 0x3;
    p.widthShift = 1;
    p.uStepFrac = 1 << 16;   // step one texel per pixel
    p.vStep = 0;
    p.lightRow8 = 0;

    std::vector<u16> dst(4, 0);
    render::FillSpanTextured(rs, dst.data(), 0, 0, p);
    CHECK_EQ((int)dst[0], 0x1234);
    CHECK_EQ((int)dst[1], 0x5678);
    CHECK_EQ((int)dst[2], 0x9ABC);
    CHECK_EQ((int)dst[3], 0xDEF0);
}

// ---------------------------------------------------------------------------
TEST(FrameIntegrationWave6, SpanFogEnabledBlendsTowardFog) {
    // Enable fog with a mid factor -> every written pixel blends toward the fog
    // colour (BlendFog565). With factor 255 the result equals the raw palette;
    // with a lower factor the pixels shift toward the fog colour.
    render::RasterState rs{};
    rs.spanLen = 2;
    std::vector<u8> tex = {0, 1};
    std::vector<u16> pal(256, 0);
    pal[0] = 0xFFFF;   // white texel
    pal[1] = 0xFFFF;
    render::SpanTexParams p{};
    p.texBase = tex.data();
    p.palBase = pal.data();
    p.texelMask = 0x1;
    p.widthShift = 1;
    p.uStepFrac = 1 << 16;
    p.vStep = 0;
    p.lightRow8 = 0;

    // fog colour BLACK, factor 0 == FULL fog -> pixels become the fog colour.
    render::SpanFog().enabled = true;
    render::SpanFog().color   = 0x000000;   // black
    render::SpanFog().factor  = 0;          // full fog

    std::vector<u16> dst(2, 0xAAAA);
    render::FillSpanTextured(rs, dst.data(), 0, 0, p);
    // BlendFog565(white, black, 0) == black (565 0).
    CHECK_EQ((int)dst[0], 0x0000);
    CHECK_EQ((int)dst[1], 0x0000);

    // A mid factor leaves the pixel between white and black (not equal to either).
    render::SpanFog().factor = 128;
    std::vector<u16> dst2(2, 0xAAAA);
    render::FillSpanTextured(rs, dst2.data(), 0, 0, p);
    CHECK(dst2[0] != 0xFFFF);
    CHECK(dst2[0] != 0x0000);
    CHECK_EQ(dst2[0], render::BlendFog565(0xFFFF, 0x000000, 128));

    render::SpanFog() = render::SpanFogState{};   // restore default for other tests
}

// ---------------------------------------------------------------------------
TEST(FrameIntegrationWave6, SunStateNoonBrighterThanNight) {
    // The day/night driver the sky/light passes read: noon is far brighter than
    // 02:00, and noon falls in a daytime band (sunrise regime, raise=0).
    render::SunState noon = render::ComputeSunState(/*day*/0, /*hour*/12, /*minute*/0);
    render::SunState night = render::ComputeSunState(/*day*/0, /*hour*/2, /*minute*/0);
    CHECK(noon.brightness > night.brightness);
    CHECK(noon.band >= 0 && noon.band <= 6);
    // The sky colour scale CityView3D derives (brightness/600) is brighter at noon.
    float kDay = (float)noon.brightness / 600.0f;
    float kNight = (float)night.brightness / 600.0f;
    CHECK(kDay > kNight);
}

// ---------------------------------------------------------------------------
TEST(FrameIntegrationWave6, MaskedSpanFogOnlyTouchesWrittenPixels) {
    // The masked span (index-0 transparent) leaves transparent pixels untouched
    // even with fog on — fog only blends WRITTEN pixels.
    render::RasterState rs{};
    rs.spanLen = 2;
    std::vector<u8> tex = {0, 1};   // idx 0 = transparent, idx 1 = opaque
    std::vector<u16> pal(256, 0);
    pal[1] = 0xFFFF;
    render::SpanTexParams p{};
    p.texBase = tex.data();
    p.palBase = pal.data();
    p.texelMask = 0x1;
    p.widthShift = 1;
    p.uStepFrac = 1 << 16;
    p.vStep = 0;
    p.lightRow8 = 0;
    p.useColorKey = false;

    render::SpanFog().enabled = true;
    render::SpanFog().color   = 0x000000;
    render::SpanFog().factor  = 0;

    std::vector<u16> dst(2, 0x1357);
    render::FillSpanTexturedMasked(rs, dst.data(), 0, 0, p);
    CHECK_EQ((int)dst[0], 0x1357);   // idx 0 transparent -> untouched (no fog)
    CHECK_EQ((int)dst[1], 0x0000);   // idx 1 written -> fully fogged to black

    render::SpanFog() = render::SpanFogState{};
}
