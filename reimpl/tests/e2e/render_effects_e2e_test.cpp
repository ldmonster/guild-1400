// End-to-end tests for the guild::render effects flow:
//   (1) seed a particle emitter, step it N frames with a fixed RNG seed, and
//       verify the full particle position/life trajectory + final RNG state
//       against a python reference;
//   (2) run a day-cycle across 24h and verify the brightness / sky-band keyframe
//       sequence against the recovered table.
#include "render/particle.h"
#include "render/daycycle.h"
#include "render/sky.h"
#include "render/fade.h"
#include "crt/rand.h"
#include "tests/framework/test.h"
#include <cmath>

using namespace guild;
using namespace guild::render;

namespace {
bool fcloseE(float a, float b, float tol = 3e-3f) {
    float d = std::fabs(a - b);
    return d <= tol || d <= tol * std::fabs(b);
}
} // namespace

// --------------------------------------------------------------------------
// (1) Particle emitter trajectory: SeedParticles over 6 frames, fixed seed.
// --------------------------------------------------------------------------
TEST(RenderEffectsE2E, SeedEmitterTrajectory) {
    Emitter e{};
    e.baseVx = 9.8f;   // gravity
    e.baseVy = 12.0f;  // life decay
    e.baseVz = 0.0f;   // floor y
    e.count = 4;
    Particle parts[4] = {};
    e.particles = parts;

    crt::Srand(2024);
    const u32 frames[6] = {10, 13, 16, 19, 22, 25};
    for (u32 t : frames)
        SeedParticles(e, t);

    // python golden after 6 frames: {px,py,pz, vx,vy,vz, life, shade, flags}
    struct Row { float px, py, pz, vx, vy, vz, life; int shade, flags; };
    const Row g[4] = {
        {-2.1509204f,0.0000000f,-1.1702781f, -0.0112859f,2.2942200f,0.0174063f, 136.0f, 136, 1},
        {-2.0909822f,0.0000000f,1.3773000f, -0.0029265f,2.3456740f,0.0004760f, 101.0f, 101, 1},
        {8.5422935f,2.5069141f,7.9421110f, 0.4643026f,-7.2930861f,0.4475060f, 175.0f, 175, 1},
        {10.2176991f,2.5018337f,-9.4607315f, 0.5168020f,-7.2981663f,-0.6072717f, 112.0f, 112, 1},
    };
    for (int i = 0; i < 4; ++i) {
        CHECK(fcloseE(parts[i].px, g[i].px));
        CHECK(fcloseE(parts[i].py, g[i].py));
        CHECK(fcloseE(parts[i].pz, g[i].pz));
        CHECK(fcloseE(parts[i].vx, g[i].vx));
        CHECK(fcloseE(parts[i].vy, g[i].vy));
        CHECK(fcloseE(parts[i].vz, g[i].vz));
        CHECK(fcloseE(parts[i].life, g[i].life));
        CHECK_EQ((int)parts[i].shade, g[i].shade);
        CHECK_EQ((int)parts[i].flags, g[i].flags);
    }
    // RNG determinism: after the whole flow the LCG state is fixed.
    CHECK_EQ(*crt::RandStatePtr(), 3363674696u);
}

// --------------------------------------------------------------------------
// (1b) Reseed determinism: the same seed + same frame schedule reproduces the
// identical trajectory bit-for-bit.
// --------------------------------------------------------------------------
TEST(RenderEffectsE2E, SeedReseedDeterminism) {
    auto run = [](Particle out[4]) {
        Emitter e{};
        e.baseVx = 9.8f; e.baseVy = 12.0f; e.baseVz = 0.0f; e.count = 4;
        e.particles = out;
        crt::Srand(555);
        for (u32 t : {10u, 13u, 16u, 19u}) SeedParticles(e, t);
    };
    Particle a[4] = {}, b[4] = {};
    run(a);
    run(b);
    for (int i = 0; i < 4; ++i) {
        CHECK_EQ(a[i].shade, b[i].shade);
        CHECK_EQ(a[i].flags, b[i].flags);
        CHECK(a[i].px == b[i].px);
        CHECK(a[i].vy == b[i].vy);
        CHECK(a[i].life == b[i].life);
    }
}

// --------------------------------------------------------------------------
// (2) Day cycle across 24 hours (season 1) -> brightness keyframe sequence.
// --------------------------------------------------------------------------
TEST(RenderEffectsE2E, DayCycle24h) {
    i32 kf[6];
    BuildTimeTable(1, kf); // season 1: 5:00 7:00 9:00 19:00 21:00 23:00
    const int expect[24] = {
        0,0,0,0,0,0, 50,100,150,200, 220,240,260,280,300,320,340,360,380,400, 450,500,550,600
    };
    int prev = -1;
    for (int h = 0; h < 24; ++h) {
        int b = UpdateBrightness(kf, h, 0);
        CHECK_EQ(b, expect[h]);
        // brightness is non-decreasing across the day for this season's table.
        CHECK(b >= prev);
        prev = b;
    }

    // The sky-band index derived from brightness must stay in [0,6] and the
    // band/blend at a few keyframes matches the recovered scale (0.01).
    auto noon = BrightnessToBand(UpdateBrightness(kf, 12, 0)); // 260 -> band 2 blend .6
    CHECK_EQ(noon.band, 2);
    CHECK(fcloseE(noon.blend, 0.6f, 1e-4f));
    auto night = BrightnessToBand(UpdateBrightness(kf, 23, 0)); // 600 -> band 6
    CHECK_EQ(night.band, 6);
}

// --------------------------------------------------------------------------
// (2b) Sky colour keyframe sweep: blend across all 7 bands stays bounded and
// the wrap (band 6 -> 0) is continuous.
// --------------------------------------------------------------------------
TEST(RenderEffectsE2E, SkyColorBandSweep) {
    SkyBandColor bands[7];
    for (int i = 0; i < 7; ++i) bands[i] = {float(i*30), float(i*15), float(i*45)};

    // At t=1 in band a, the result equals band (a+1) (the lerp endpoint) * scale.
    for (int a = 0; a < 7; ++a) {
        int b = (a + 1) % 7;
        auto r = BlendBandLighting(bands, a, 1.0f, 1.0f);
        CHECK(fcloseE(r.r, bands[b].r));
        CHECK(fcloseE(r.g, bands[b].g));
        CHECK(fcloseE(r.b, bands[b].b));
    }
    // At t=0 the result equals band a itself.
    for (int a = 0; a < 7; ++a) {
        auto r = BlendBandLighting(bands, a, 0.0f, 1.0f);
        CHECK(fcloseE(r.r, bands[a].r));
        CHECK(fcloseE(r.b, bands[a].b));
    }
}

// --------------------------------------------------------------------------
// (3) Fade transition flow: a full fade-in then fade-out reaches the terminal
// alphas and reports done at the right frame.
// --------------------------------------------------------------------------
TEST(RenderEffectsE2E, FadeTransitionFlow) {
    const int dur = 200;
    // fade-in from start=0. Advance in small (<=3) steps so the per-frame step
    // clamp does not throttle progress; alpha should rise monotonically to 1.
    bool sawDone = false;
    u32 last = 0;
    float prev = 0.0f;
    for (u32 now = 0; now <= 210; now += 3) {
        float a = FadeAlpha(kFadeIn, now, 0, last, dur);
        CHECK(a >= 0.0f && a <= 1.0f);
        CHECK(a + 1e-4f >= prev); // non-decreasing
        if (FadeIsDone(kFadeIn, a)) sawDone = true;
        prev = a;
        last = now;
    }
    CHECK(sawDone);

    // fade-out reaches 0 at now==start+dur
    float aEnd = FadeAlpha(kFadeOut, 200, 0, 197, dur);
    CHECK(fcloseE(aEnd, 0.0f, 1e-5f));
    CHECK(FadeIsDone(kFadeOut, aEnd));
}
