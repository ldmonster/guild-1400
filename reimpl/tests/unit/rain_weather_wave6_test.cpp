// =============================================================================
// Wave-6 RAIN + weather-state golden tests.
//   weather state machine : gilde.exe 0x4c0040 VIBE_Weather_UpdateSky (gate)
//   rain integrate math    : gilde.exe 0x4294d4 VIBE_Rain_UpdateDrop
//   rain colour pack       : gilde.exe 0x429c38 VIBE_Rain_Render head
//   rain seed RNG          : gilde.exe 0x429098 VIBE_Rain_Create
//   rain render-to-surface : gilde.exe 0x429c38 VIBE_Rain_Render tail
// =============================================================================
#include "render/weather.h"
#include "render/rain.h"
#include "render/snow.h"
#include "render/surface.h"
#include "render/colorformat.h"
#include "render/types.h"
#include "crt/rand.h"
#include "tests/framework/test.h"
#include <cmath>
#include <cstring>
#include <vector>

using namespace guild;
using namespace guild::render;

namespace {
// Identity-ish camera that keeps the projection well-defined and deterministic.
SnowCamera IdentityCam() {
    SnowCamera c{};
    c.eye[0] = 0; c.eye[1] = 0; c.eye[2] = 0;
    c.anchor[0] = 0; c.anchor[1] = 0; c.anchor[2] = 1.0f;
    // Row-major-ish 3x3 stored as the kernel reads it (m[0..8]).
    c.m[0] = 1; c.m[1] = 0; c.m[2] = 0;
    c.m[3] = 0; c.m[4] = 1; c.m[5] = 0;
    c.m[6] = 0; c.m[7] = 0; c.m[8] = 1;
    return c;
}
} // namespace

// ---- weather state machine --------------------------------------------------

TEST(Wave6Weather, RainGateBit0) {
    i32 arc[24]; float wx[24]; float wy[24];
    for (int i = 0; i < 24; ++i) { arc[i] = 0; wx[i] = 0; wy[i] = 0; }
    // Even code -> bit0 clear -> rain inactive; odd -> active.
    arc[10] = 40;  // even
    arc[11] = 41;  // odd
    WeatherFrame a = WeatherUpdate(arc, wx, wy, 10);
    CHECK(!a.rainActive);
    CHECK_EQ(a.rainSpawn, 0);
    WeatherFrame b = WeatherUpdate(arc, wx, wy, 11);
    CHECK(b.rainActive);
    CHECK_EQ(b.rainSpawn, 41 / 5); // signed idiv by 5 == 8
    // The no-snow path passes the raw code regardless of the bit.
    CHECK_EQ(b.rainSpawnNoSnow, 41);
    CHECK_EQ(a.rainSpawnNoSnow, 40);
}

TEST(Wave6Weather, IntensityPeakOf3) {
    i32 arc[24]; float wx[24]; float wy[24];
    for (int i = 0; i < 24; ++i) { arc[i] = 0; wx[i] = 0; wy[i] = 0; }
    arc[5] = 30; arc[6] = 100; arc[7] = 70;
    // hour 6: max(arc[5],arc[6],arc[7]) = 100.
    CHECK_EQ(WeatherUpdate(arc, wx, wy, 6).intensity, 100);
    // hour 5: max(arc[4]=0, arc[5]=30, arc[6]=100) = 100.
    CHECK_EQ(WeatherUpdate(arc, wx, wy, 5).intensity, 100);
    // wraparound: hour 0 looks at arc[23], arc[0], arc[1].
    arc[23] = 5; arc[0] = 2; arc[1] = 9;
    CHECK_EQ(WeatherUpdate(arc, wx, wy, 0).intensity, 9);
}

TEST(Wave6Weather, GrowAndScroll) {
    i32 arc[24]; float wx[24]; float wy[24];
    for (int i = 0; i < 24; ++i) { arc[i] = 0; wx[i] = 0; wy[i] = 0; }
    arc[8] = 200;          // intensity 200 (heavy)
    wx[8] = -3.0f;
    wy[8] = 4.0f;
    WeatherFrame f = WeatherUpdate(arc, wx, wy, 8);
    CHECK_EQ(f.intensity, 200);
    CHECK_EQ(f.category, kWeatherHeavy);
    // snowGrow = trunc(2.0 * -(-3.0) * 200) = trunc(1200) = 1200.
    CHECK_EQ(f.snowGrow, 1200);
    // rainGrow = trunc(0.5 * 3.0 * 200) = trunc(300) = 300.
    CHECK_EQ(f.rainGrow, 300);
    // scrollMag = sqrt(9+16) * (200+150) * 0.125 = 5 * 350 * 0.125 = 218.75.
    CHECK(std::fabs(f.scrollMag - 218.75f) < 1e-3f);
    CHECK(std::fabs(f.scrollFast - 218.75f * 0.75f) < 1e-3f);
    CHECK(std::fabs(f.scrollBack - 218.75f * 1.5f) < 1e-3f);
}

TEST(Wave6Weather, Categories) {
    CHECK_EQ(CategoryFor(0),   kWeatherFair);
    CHECK_EQ(CategoryFor(49),  kWeatherFair);
    CHECK_EQ(CategoryFor(50),  kWeatherMedium);
    CHECK_EQ(CategoryFor(149), kWeatherMedium);
    CHECK_EQ(CategoryFor(150), kWeatherHeavy);
}

// ---- rain streak colour pack ------------------------------------------------

TEST(Wave6Rain, StreakDiffuseGolden) {
    // count = 0 -> f = 1.0 ; a = round(96) = 96 (0x60) ; b = round(128)=128 (0x80).
    // diffuse = 0x80000000 | (96<<16) | (128<<8) | 128 = 0x80608080.
    CHECK_EQ(RainStreakDiffuse(0), 0x80608080u);
    // Alpha bit always set.
    CHECK((RainStreakDiffuse(500) & 0x80000000u) != 0);
    // Higher count dims the colour (f decreases).
    u8 r0 = (u8)((RainStreakDiffuse(0)   >> 16) & 0xFF);
    u8 r1 = (u8)((RainStreakDiffuse(800) >> 16) & 0xFF);
    CHECK(r1 < r0);
    // R uses 96 scale, G/B use 128 scale; with f=1 R=96,G=B=128.
    u32 d = RainStreakDiffuse(0);
    CHECK_EQ((d >> 16) & 0xFF, 96u);
    CHECK_EQ((d >> 8)  & 0xFF, 128u);
    CHECK_EQ(d & 0xFF, 128u);
}

// ---- rain seed RNG (Create order) -------------------------------------------

TEST(Wave6Rain, SeedDropsRngOrderDeterministic) {
    std::vector<RainDrop> a(4), b(4);
    RainSystem sa{4, 4, a.data()};
    RainSystem sb{4, 4, b.data()};
    guild::crt::Srand(12345);
    RainSeedDrops(sa);
    guild::crt::Srand(12345);
    RainSeedDrops(sb);
    // Same seed -> byte-identical drop field.
    CHECK(std::memcmp(a.data(), b.data(), 4 * sizeof(RainDrop)) == 0);
    // Six draws/drop in order px,py,pz,size,d0,d1; positions in [-1,1].
    for (int i = 0; i < 4; ++i) {
        CHECK(a[i].px >= -1.0f && a[i].px <= 1.0f);
        CHECK(a[i].py >= -1.0f && a[i].py <= 1.0f);
        CHECK(a[i].pz >= -1.0f && a[i].pz <= 1.0f);
        // size,d0,d1 seeded around 0.25 bias.
        CHECK(a[i].size >= 0.24f && a[i].size <= 0.51f);
    }
}

// ---- rain integrate (UpdateDrop) wrap + project -----------------------------

TEST(Wave6Rain, UpdateWrapsIntoUnitCube) {
    RainDrop drops[2];
    std::memset(drops, 0, sizeof(drops));
    // Place positions just inside / outside the wrap range.
    drops[0].px = 0.9f; drops[0].py = -0.9f; drops[0].pz = 0.0f;
    drops[0].size = 0.25f; drops[0].d0 = 0.0f; drops[0].d1 = 0.0f;
    drops[1] = drops[0];
    RainSystem sys{2, 2, drops};
    SnowCamera cam = IdentityCam();
    SnowViewport vp{0, 0, 640, 480};
    RainUpdateDrop(sys, 0.016f, cam, vp);
    // After integration every position is wrapped to [-1,1).
    for (int i = 0; i < 2; ++i) {
        CHECK(drops[i].px >= -1.0f && drops[i].px < 1.0f);
        CHECK(drops[i].py >= -1.0f && drops[i].py < 1.0f);
        CHECK(drops[i].pz >= -1.0f && drops[i].pz < 1.0f);
        // Screen points populated (finite).
        CHECK(std::isfinite(drops[i].sx) && std::isfinite(drops[i].sy));
        CHECK(std::isfinite(drops[i].sx2) && std::isfinite(drops[i].sy2));
    }
}

TEST(Wave6Rain, OverflowResetsToZero) {
    RainDrop d;
    std::memset(&d, 0, sizeof(d));
    // Force an out-of-range position; the integrator resets it to 0 (>1000).
    d.px = 5000.0f; d.size = 0.25f;
    RainSystem sys{1, 1, &d};
    SnowCamera cam = IdentityCam();
    SnowViewport vp{0, 0, 640, 480};
    RainUpdateDrop(sys, 0.0f, cam, vp);
    // px was >1000 so reset to 0 then wrapped (stays 0, in range).
    CHECK(d.px >= -1.0f && d.px < 1.0f);
}

// ---- rain render-to-surface -------------------------------------------------

TEST(Wave6Rain, RenderDrawsVisibleStreaks) {
    Surface* surf = SurfaceCreate(64, 64, 32, Format8888());
    CHECK(surf != nullptr);
    if (!surf) return;
    SurfaceColorFill(surf, 0, 0, 0);

    RainDrop drops[3];
    std::memset(drops, 0, sizeof(drops));
    // Drop 0: fully inside -> drawn.
    drops[0].sx = 10; drops[0].sy = 10; drops[0].sx2 = 20; drops[0].sy2 = 25;
    drops[0].pz = 0.0f;
    // Drop 1: head outside (negative) -> clipped.
    drops[1].sx = -5; drops[1].sy = 10; drops[1].sx2 = 20; drops[1].sy2 = 25;
    // Drop 2: tail outside (>= width) -> clipped.
    drops[2].sx = 10; drops[2].sy = 10; drops[2].sx2 = 200; drops[2].sy2 = 25;

    RainSystem sys{3, 3, drops};
    SnowViewport vp{0, 0, 64, 64};
    u32 diffuse = RainStreakDiffuse(0); // 0x80608080
    int drawn = RainRenderToSurface(sys, vp, diffuse, surf);
    CHECK_EQ(drawn, 1); // only drop 0 passes the clip

    // The drawn streak left non-black pixels along (10,10)->(20,25).
    u8 px[3];
    SurfaceGetPixelRgb(surf, 10, 10, px);
    CHECK(px[0] != 0 || px[1] != 0 || px[2] != 0);

    SurfaceDestroy(surf);
}

TEST(Wave6Rain, RenderEmptyAndNullSafe) {
    Surface* surf = SurfaceCreate(16, 16, 32, Format8888());
    CHECK(surf != nullptr);
    RainSystem empty{0, 0, nullptr};
    SnowViewport vp{0, 0, 16, 16};
    CHECK_EQ(RainRenderToSurface(empty, vp, 0x80608080u, surf), 0);
    CHECK_EQ(RainRenderToSurface(empty, vp, 0x80608080u, nullptr), 0);
    if (surf) SurfaceDestroy(surf);
}
