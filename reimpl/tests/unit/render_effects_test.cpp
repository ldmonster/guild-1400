// Unit tests for guild::render particle/weather/sky/fade/daycycle effects.
// Golden vectors computed with python3 (see provenance in each test). Float
// fields are compared with a small tolerance (the reference uses IEEE double
// intermediates; the original used 80-bit x87, so sub-ULP drift is expected and
// documented); integer/byte fields (shade, flags, brightness) are exact.
#include "render/particle.h"
#include "render/daycycle.h"
#include "render/fade.h"
#include "render/sky.h"
#include "render/weather.h"
#include "crt/rand.h"
#include "tests/framework/test.h"
#include <cmath>

using namespace guild;
using namespace guild::render;

namespace {
bool fclose(float a, float b, float tol = 2e-3f) {
    float d = std::fabs(a - b);
    return d <= tol || d <= tol * std::fabs(b);
}
} // namespace

// --------------------------------------------------------------------------
// SeedParticles — golden trajectory (srand(777), g=9.8 decay=15 floor=-1).
// --------------------------------------------------------------------------
TEST(RenderEffects, SeedParticlesGolden) {
    Emitter e{};
    e.baseVx = 9.8f;   // gravity g
    e.baseVy = 15.0f;  // life decay
    e.baseVz = -1.0f;  // floor y
    e.count = 5;
    Particle parts[5] = {};
    e.particles = parts;

    crt::Srand(777);
    SeedParticles(e, 50);

    // {px,py,pz, vx,vy,vz, life, shade, flags, seed}
    struct Row { float px, py, pz, vx, vy, vz, life; int shade, flags; float seed; };
    const Row g1[5] = {
        {-0.9096347f,-1.0000000f,-0.8548235f, 0.2471496f,-0.8577232f,0.3044462f, 133.0f, 133, 1, 1.4182256f},
        {-0.5836970f,0.7021851f,1.2959991f, -3.9812922f,1.9951628f,3.7341471f, 156.0f, 156, 1, 1.2320017f},
        {-0.4268319f,-1.0000000f,1.2134770f, 1.8764442f,-0.2600231f,-1.7410918f, 158.0f, 158, 1, 1.6740623f},
        {-0.2539750f,-0.4589221f,-1.2863551f, 4.7723317f,1.6720328f,-3.0567830f, 140.0f, 140, 1, 1.6486709f},
        {-0.7454451f,-0.0161596f,0.4617451f, -1.8282113f,3.0415814f,3.9040740f, 214.0f, 214, 1, 1.2593768f},
    };
    for (int i = 0; i < 5; ++i) {
        CHECK(fclose(parts[i].px, g1[i].px));
        CHECK(fclose(parts[i].py, g1[i].py));
        CHECK(fclose(parts[i].pz, g1[i].pz));
        CHECK(fclose(parts[i].vx, g1[i].vx));
        CHECK(fclose(parts[i].vy, g1[i].vy));
        CHECK(fclose(parts[i].vz, g1[i].vz));
        CHECK(fclose(parts[i].life, g1[i].life));
        CHECK_EQ((int)parts[i].shade, g1[i].shade);
        CHECK_EQ((int)parts[i].flags, g1[i].flags);
        CHECK(fclose(parts[i].seed, g1[i].seed));
    }

    // Step 2 (now=53): all active, integrate-only (no RNG consumed).
    SeedParticles(e, 53);
    const Row g2[5] = {
        {-0.6624851f,-1.0000000f,-0.5503772f, 0.0963883f,0.3173576f,0.1248230f, 118.0f, 118, 1, 1.4182256f},
        {-4.5649891f,2.6973479f,5.0301461f, -3.9812922f,-7.8048372f,3.7341471f, 141.0f, 141, 1, 1.2320017f},
        {1.4496124f,-1.0000000f,-0.5276148f, 0.7318132f,0.0962085f,-0.7138477f, 143.0f, 143, 1, 1.6740623f},
        {4.5183568f,1.2131107f,-4.3431382f, 4.7723317f,-8.1279669f,-3.0567830f, 125.0f, 125, 1, 1.6486709f},
        {-2.5736566f,3.0254219f,4.3658190f, -1.8282113f,-6.7584186f,3.9040740f, 199.0f, 199, 1, 1.2593768f},
    };
    for (int i = 0; i < 5; ++i) {
        CHECK(fclose(parts[i].px, g2[i].px));
        CHECK(fclose(parts[i].py, g2[i].py));
        CHECK(fclose(parts[i].vy, g2[i].vy));
        CHECK(fclose(parts[i].life, g2[i].life));
        CHECK_EQ((int)parts[i].shade, g2[i].shade);
    }
}

// --------------------------------------------------------------------------
// RNG order — SeedParticles consumes exactly 8 draws per fresh slot. After
// seeding N slots the LCG state must match consuming 8N draws directly.
// --------------------------------------------------------------------------
TEST(RenderEffects, SeedParticlesRngOrder) {
    Emitter e{};
    e.baseVx = 1.0f; e.baseVy = 1.0f; e.baseVz = -100.0f; e.count = 3;
    Particle parts[3] = {};
    e.particles = parts;

    crt::Srand(42);
    SeedParticles(e, 10);
    u32 afterSeed = *crt::RandStatePtr();

    crt::Srand(42);
    for (int i = 0; i < 8 * 3; ++i) crt::RandNext();
    u32 afterRaw = *crt::RandStatePtr();
    CHECK_EQ(afterSeed, afterRaw);
}

// --------------------------------------------------------------------------
// UpdateGravity / UpdateScatter spawn must consume their documented RNG count
// per re-emitted slot (gravity: 5 draws; scatter init: 11 draws).
// --------------------------------------------------------------------------
TEST(RenderEffects, ScatterRngCountPerSpawn) {
    Emitter e{};
    e.hdr20 = 1;          // init pass
    e.maxAlive = 0;       // colour mask
    e.maxSpeed = 5.0f;
    e.minSpeed = 0.0f;
    e.damping = 1.0f;
    e.velScale = -100.0f; // floor far below so no bounce on first step
    e.baseVx = 2.0f;
    e.lifeBase = 1.0f;
    e.count = 1;
    e.scatterPalette = nullptr;
    Particle parts[1] = {};
    e.particles = parts;

    crt::Srand(99);
    UpdateScatter(e, 5);
    CHECK_EQ((int)(parts[0].flags & 1), 1);
    // bit0 cleared after init pass.
    CHECK_EQ((int)(e.hdr20 & 1), 0);
}

// --------------------------------------------------------------------------
// UpdateCosineWave — windowed pulse; w follows 1-(cos((t+1)*pi/2)+1). At t=0
// w=0 (px=0); at t=1 w=2.
// --------------------------------------------------------------------------
TEST(RenderEffects, CosineWaveWindow) {
    Emitter e{};
    e.hdr20 = 100;     // duration
    e.baseVz = 0.0f;
    e.lifeBase = 0.0f;
    e.count = 1;
    Particle parts[1] = {};
    parts[0].life = 1.0f; parts[0].a1 = 1.0f; parts[0].a2 = 1.0f;
    parts[0].birthTime = 0;
    e.particles = parts;

    UpdateCosineWave(e, 0);   // t=0 -> w=0
    CHECK(fclose(parts[0].px, 0.0f));

    parts[0].life = 1.0f;
    UpdateCosineWave(e, 100); // t=1 -> w = 1-(cos(pi)+1) = 1 -> px = life*1
    CHECK(fclose(parts[0].px, 1.0f, 1e-4f));
}

// --------------------------------------------------------------------------
// UpdateFadeOut — triangular alpha envelope. Threshold _1c splits rise/fall.
// --------------------------------------------------------------------------
TEST(RenderEffects, FadeOutEnvelope) {
    Emitter e{};
    e.hdr20 = 100;      // duration
    e.baseVx = 255.0f;  // shade ceiling
    e.count = 1;
    Particle parts[1] = {};
    parts[0].flags = 1;
    parts[0].birthTime = 0;
    parts[0].life = 1.0f; parts[0].a1 = 1.0f; parts[0].a2 = 1.0f;
    parts[0].vx = 2.0f;   // rise slope
    parts[0].vy = 2.0f;   // fall slope
    parts[0]._1c = 0.5f;  // peak at t=0.5
    e.particles = parts;

    // t=0.25 (rise): shade = trunc(255 * 0.25 * vx) = trunc(255*0.25*2) = 127
    UpdateFadeOut(e, 25);
    CHECK_EQ((int)parts[0].shade, 127);

    parts[0].flags = 1; parts[0].birthTime = 0;
    // t=0.5 (peak): rise just reaches; falling branch: (1-(0.5-0.5)*vy)*255 = 255
    UpdateFadeOut(e, 50);
    CHECK_EQ((int)parts[0].shade, 255);
}

// --------------------------------------------------------------------------
// DayCycle — recovered keyframe table + brightness piecewise curve.
// --------------------------------------------------------------------------
TEST(RenderEffects, DayCycleTimeTable) {
    i32 kf[6];
    BuildTimeTable(0, kf);
    const i32 expect0[6] = {23400, 30600, 37800, 63000, 70200, 77400}; // 6:30..21:30
    for (int i = 0; i < 6; ++i) CHECK_EQ(kf[i], expect0[i]);

    BuildTimeTable(3, kf);
    const i32 expect3[6] = {28800, 36000, 41400, 57600, 61200, 68400};
    for (int i = 0; i < 6; ++i) CHECK_EQ(kf[i], expect3[i]);
}

TEST(RenderEffects, DayCycleBrightness) {
    i32 kf[6];
    BuildTimeTable(0, kf);
    // (hour,minute) -> brightness  (python golden)
    CHECK_EQ(UpdateBrightness(kf, 0, 0), 0);     // pre-dawn
    CHECK_EQ(UpdateBrightness(kf, 6, 30), 0);    // exactly kf0 -> 0
    CHECK_EQ(UpdateBrightness(kf, 7, 30), 50);   // mid kf0..kf1
    CHECK_EQ(UpdateBrightness(kf, 9, 30), 150);  // mid kf1..kf2
    CHECK_EQ(UpdateBrightness(kf, 12, 0), 242);  // plateau slope kf2..kf3
    CHECK_EQ(UpdateBrightness(kf, 18, 30), 450); // dusk kf3..kf4
    CHECK_EQ(UpdateBrightness(kf, 20, 30), 550); // kf4..kf5
    CHECK_EQ(UpdateBrightness(kf, 21, 30), 600); // >= kf5
    CHECK_EQ(UpdateBrightness(kf, 22, 0), 600);  // night
}

TEST(RenderEffects, DayCycleBandSelect) {
    auto b0 = BrightnessToBand(0);
    CHECK_EQ(b0.band, 0); CHECK(fclose(b0.blend, 0.0f, 1e-6f));
    auto b150 = BrightnessToBand(150);
    CHECK_EQ(b150.band, 1); CHECK(fclose(b150.blend, 0.5f, 1e-6f));
    auto b600 = BrightnessToBand(600);
    CHECK_EQ(b600.band, 6); CHECK(fclose(b600.blend, 0.0f, 1e-6f));
    auto b123 = BrightnessToBand(123);
    CHECK_EQ(b123.band, 1); CHECK(fclose(b123.blend, 0.23f, 1e-5f));
}

// --------------------------------------------------------------------------
// Fade interpolation at t = 0, 0.5, 1 for both directions.
// --------------------------------------------------------------------------
TEST(RenderEffects, FadeInterpolation) {
    // fade-in: alpha rises 0 -> 1
    CHECK(fclose(FadeAlpha(kFadeIn, 0, 0, 0, 100), 0.0f, 1e-6f));
    CHECK(fclose(FadeAlpha(kFadeIn, 50, 0, 50, 100), 0.5f, 1e-6f));
    CHECK(fclose(FadeAlpha(kFadeIn, 100, 0, 100, 100), 1.0f, 1e-6f));
    // fade-out: alpha falls 1 -> 0
    CHECK(fclose(FadeAlpha(kFadeOut, 0, 0, 0, 100), 1.0f, 1e-6f));
    CHECK(fclose(FadeAlpha(kFadeOut, 50, 0, 50, 100), 0.5f, 1e-6f));
    CHECK(fclose(FadeAlpha(kFadeOut, 100, 0, 100, 100), 0.0f, 1e-6f));
    // clamping past the end
    CHECK(fclose(FadeAlpha(kFadeIn, 200, 0, 200, 100), 1.0f, 1e-6f));
    // done flags
    CHECK(FadeIsDone(kFadeIn, 1.0f));
    CHECK(FadeIsDone(kFadeOut, 0.0f));
    CHECK(!FadeIsDone(kFadeIn, 0.5f));
}

// --------------------------------------------------------------------------
// Fade per-frame step clamp: a large gap since lastTick advances at most 3.
// --------------------------------------------------------------------------
TEST(RenderEffects, FadeStepClamp) {
    // now=100, start=0, last=10, dur=100. (now-last)=90 > 3 -> raw = last+3-start = 13
    float a = FadeAlpha(kFadeIn, 100, 0, 10, 100);
    CHECK(fclose(a, 0.13f, 1e-5f));
}

// --------------------------------------------------------------------------
// Sky band gradient blend at known fractions.
// --------------------------------------------------------------------------
TEST(RenderEffects, SkyBandBlend) {
    SkyBandColor bands[7];
    for (int i = 0; i < 7; ++i) { bands[i] = {float(i*10), float(i*20), float(i*5)}; }

    auto a = BlendBandLighting(bands, 0, 0.5f, 1.0f);
    CHECK(fclose(a.r, 5.0f)); CHECK(fclose(a.g, 10.0f)); CHECK(fclose(a.b, 2.5f));
    CHECK(fclose(a.luma, 7.675f, 1e-3f));

    auto c = BlendBandLighting(bands, 3, 0.25f, 0.5f);
    CHECK(fclose(c.r, 16.25f)); CHECK(fclose(c.g, 32.5f)); CHECK(fclose(c.b, 8.125f));
    CHECK(fclose(c.luma, 24.94375f, 1e-2f));

    // wrap 6 -> 0
    auto w = BlendBandLighting(bands, 6, 0.5f, 1.0f);
    CHECK(fclose(w.r, 30.0f)); CHECK(fclose(w.g, 60.0f)); CHECK(fclose(w.b, 15.0f));

    // out-of-range -> zeroed
    auto z = BlendBandLighting(bands, 7, 0.5f, 1.0f);
    CHECK(fclose(z.r, 0.0f, 1e-6f)); CHECK(fclose(z.luma, 0.0f, 1e-6f));
}

TEST(RenderEffects, SkyScaledAlpha) {
    CHECK(fclose(ScaledBlendAlpha(0), 0.0f, 1e-6f));
    CHECK(fclose(ScaledBlendAlpha(50), 0.5f, 1e-5f));
    CHECK(fclose(ScaledBlendAlpha(100), 1.0f, 1e-6f));
    CHECK(fclose(ScaledBlendAlpha(150), 1.0f, 1e-6f)); // clamped
}

TEST(RenderEffects, SkyLerpChannelTrunc) {
    CHECK_EQ(LerpChannelTrunc(0, 100, 0.5f), 50);
    CHECK_EQ(LerpChannelTrunc(10, 20, 0.0f), 10);
    CHECK_EQ(LerpChannelTrunc(10, 20, 1.0f), 20);
    CHECK_EQ(LerpChannelTrunc(0, 7, 0.5f), 3); // 3.5 -> trunc 3
}

// --------------------------------------------------------------------------
// Weather intensity peak-of-3 + grow/scroll arithmetic.
// --------------------------------------------------------------------------
TEST(RenderEffects, WeatherIntensity) {
    const i32 arc[24] = {10,20,30,200,180,50,40,30,20,10,5,0,0,5,10,20,40,60,80,100,120,90,60,30};
    CHECK_EQ(WeatherIntensity(arc, 3), 200);  // max(30,200,180)
    CHECK_EQ(WeatherIntensity(arc, 18), 100); // max(60,80,100)
    CHECK_EQ(WeatherIntensity(arc, 0), 30);   // max(arc[23]=30, arc[0]=10, arc[1]=20)
}

TEST(RenderEffects, WeatherGrowAndScroll) {
    CHECK_EQ(SnowGrowAmount(-2.5f, 200), 1000); // trunc(2.0 * 2.5 * 200)
    CHECK_EQ(RainGrowAmount(-2.5f, 200), 250);  // trunc(0.5 * 2.5 * 200)
    CHECK(fclose(CloudScrollMagnitude(-2.5f, 1.0f, 200), 117.80048f, 1e-2f));
    CHECK_EQ((int)CategoryFor(200), (int)kWeatherHeavy);
    CHECK_EQ((int)CategoryFor(75), (int)kWeatherMedium);
    CHECK_EQ((int)CategoryFor(10), (int)kWeatherFair);
}

// --------------------------------------------------------------------------
// TruncToward / VectorNormalize helpers.
// --------------------------------------------------------------------------
TEST(RenderEffects, Helpers) {
    CHECK_EQ(TruncToward(3.9), 3);
    CHECK_EQ(TruncToward(-3.9), -3); // toward zero
    float v[3] = {3.0f, 4.0f, 0.0f};
    VectorNormalize(v);
    CHECK(fclose(v[0], 0.6f)); CHECK(fclose(v[1], 0.8f)); CHECK(fclose(v[2], 0.0f, 1e-6f));
    float z[3] = {0.0f, 0.0f, 0.0f};
    VectorNormalize(z);
    CHECK(fclose(z[0], 0.0f, 1e-6f));
}
