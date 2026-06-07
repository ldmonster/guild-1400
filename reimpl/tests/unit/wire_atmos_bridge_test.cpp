// tests/unit/wire_atmos_bridge_test.cpp — P6/Wave30 de-inert the live ATMOSPHERE.
//
// Proves InstallRealAtmosBridge swaps render::FrameHooks.renderParticles /
// updateSkyFlares from the inert defaults (no-op) to the REAL reconstructed
// atmosphere leaves, and that each layer flips inert->real on a synthetic frame:
//   * SKY    fills background pixels (render::BlendBandLighting),
//   * WATER  vertices animate over time (render::AnimateWaterVertices),
//   * EMIT   advances the real particle/snow systems (SeedParticles/SnowUpdate).
#include "test.h"

#include "play/wire_atmos_bridge.h"
#include "render/frame.h"
#include "render/surface.h"
#include "crt/rand.h"

#include <cstring>

using namespace guild;

namespace {

render::Surface* MakeFb(int W, int H, u8 bg) {
    render::Surface* fb = render::SurfaceCreate(W, H, 16);
    if (fb) std::memset(fb->pixels, bg, (size_t)fb->pitch * fb->height);
    return fb;
}

int NonZero16(const render::Surface* fb) {
    if (!fb || !fb->pixels) return 0;
    int n = 0;
    const u16* px = (const u16*)fb->pixels;
    for (int i = 0; i < fb->widthPx * fb->height; ++i)
        if (px[i] != 0) ++n;
    return n;
}

} // namespace

// The swap: both inert hooks -> real leaves, observed on the hook pointers + flag.
TEST(WireAtmosBridge, SwapsInertHooksToRealLeaves) {
    render::Particle parts[16];
    render::SnowFlake flakes[16];
    play::AtmosBridgeContext ctx{};
    play::AtmosBridgeContext::MakeSynthetic(ctx, 64, 48, parts, 16, flakes, 16);
    ctx.fb = MakeFb(64, 48, 0);
    CHECK(ctx.fb != nullptr);

    render::FrameState fs{};
    render::FrameHooks hooks{};
    CHECK(hooks.renderParticles == nullptr);   // engine ships them inert (null)
    CHECK(hooks.updateSkyFlares == nullptr);

    // Inert baseline: hooks call into no-ops.
    play::InstallInertAtmosBridge(fs, hooks, &ctx);
    CHECK(hooks.renderParticles == &play::InertRenderParticles);
    CHECK(hooks.updateSkyFlares == &play::InertUpdateSkyFlares);
    render::BeginUniverseFrame(fs, hooks, 1);
    CHECK(ctx.particleDrawCount == 1);         // live walk reached the particle leaf
    CHECK(ctx.flareDrawCount == 1);            // ...and the flare leaf
    CHECK(ctx.drewRealParticles == false);     // ...but they were the inert no-ops
    CHECK_EQ(NonZero16(ctx.fb), 0);            // nothing painted

    // DE-INERT.
    play::AtmosBridgeInstall ins = play::InstallRealAtmosBridge(fs, hooks, &ctx);
    CHECK(ins.installed == true);
    CHECK(ins.wasInert == true);
    CHECK(ins.prevParticles == (void*)&play::InertRenderParticles);
    CHECK(ins.prevFlares == (void*)&play::InertUpdateSkyFlares);
    CHECK(hooks.renderParticles != &play::InertRenderParticles);
    CHECK(hooks.renderParticles != nullptr);
    CHECK(hooks.updateSkyFlares != &play::InertUpdateSkyFlares);

    render::SurfaceDestroy(ctx.fb);
}

// SKY: the real sky pass fills the background; inert paints nothing.
TEST(WireAtmosBridge, SkyFillsBackgroundInertDoesNot) {
    play::AtmosBridgeContext ctx{};
    play::AtmosBridgeContext::MakeSynthetic(ctx, 80, 60, nullptr, 0, nullptr, 0);
    ctx.fb = MakeFb(80, 60, 0);
    CHECK(ctx.fb != nullptr);

    // Inert sky: nothing.
    play::InertDrawSky(&ctx);
    CHECK(ctx.drewRealSky == false);
    CHECK_EQ(NonZero16(ctx.fb), 0);

    // Real sky: fills every pixel with the band-blended ambient.
    int painted = play::DrawAtmosSky(&ctx);
    CHECK(ctx.drewRealSky == true);
    CHECK_EQ(painted, 80 * 60);
    CHECK(ctx.skyAmbient.r > 0.0f);
    CHECK(ctx.skyAmbient.b > 0.0f);
    CHECK(NonZero16(ctx.fb) > (80 * 60) / 2);  // background now carries sky colour

    render::SurfaceDestroy(ctx.fb);
}

// WATER: the wave grid animates over time (the real vertex motion advances).
TEST(WireAtmosBridge, WaterVerticesAnimateOverTime) {
    play::AtmosBridgeContext ctx{};
    play::AtmosBridgeContext::MakeSynthetic(ctx, 64, 64, nullptr, 0, nullptr, 0);
    ctx.fb = MakeFb(64, 64, 0);
    CHECK(ctx.fb != nullptr);

    int moved1 = play::AnimateAtmosWater(&ctx);
    float snap[64];
    std::memcpy(snap, ctx.water.waveOut, sizeof snap);
    int moved2 = play::AnimateAtmosWater(&ctx);

    CHECK(moved1 > 0);                  // first advance moved grid floats
    CHECK(moved2 > 0);                  // second advance moved them again
    // The grid actually changed between the two frames (animation, not static).
    int changed = 0;
    for (int i = 0; i < 64; ++i) if (ctx.water.waveOut[i] != snap[i]) ++changed;
    CHECK(changed > 0);
    CHECK(ctx.waterTime == 2);          // animation clock advanced per frame
    CHECK(NonZero16(ctx.fb) > 0);       // water strip splatted

    render::SurfaceDestroy(ctx.fb);
}

// EMIT: the real particle + snow systems advance (emit), inert does not.
TEST(WireAtmosBridge, ParticleEmitAdvances) {
    render::Particle parts[24];
    render::SnowFlake flakes[24];
    play::AtmosBridgeContext ctx{};
    play::AtmosBridgeContext::MakeSynthetic(ctx, 96, 72, parts, 24, flakes, 24);
    ctx.fb = MakeFb(96, 72, 0);
    CHECK(ctx.fb != nullptr);
    crt::Srand(12345);

    int overlay = play::EmitAtmosParticles(&ctx);
    CHECK(ctx.drewRealParticles == true);
    CHECK(ctx.particlesAlive > 0);     // SeedParticles brought slots alive
    CHECK(ctx.particleTime == 1);
    CHECK(ctx.weatherIntensity == 120);// peak-of-3 over the noon arc
    CHECK(overlay >= 0);

    // A second emit keeps the systems live + advances the clock.
    play::EmitAtmosParticles(&ctx);
    CHECK(ctx.particleTime == 2);
    CHECK(ctx.particlesAlive > 0);

    render::SurfaceDestroy(ctx.fb);
}

// ComposeAtmosphereFrame runs the whole layer in engine order around one frame.
TEST(WireAtmosBridge, ComposeRunsAllLayersInOrder) {
    render::Particle parts[16];
    render::SnowFlake flakes[16];
    play::AtmosBridgeContext ctx{};
    play::AtmosBridgeContext::MakeSynthetic(ctx, 96, 72, parts, 16, flakes, 16);
    ctx.fb = MakeFb(96, 72, 0);
    CHECK(ctx.fb != nullptr);
    crt::Srand(999);

    render::FrameState fs{}; render::FrameHooks hooks{};
    play::AtmosBridgeInstall ins = play::ComposeAtmosphereFrame(fs, hooks, &ctx);

    CHECK(ins.installed == true);
    CHECK(ctx.drewRealSky == true);          // (a) sky ran
    CHECK(ctx.drewRealParticles == true);    // (d) particle hook fired on the walk
    CHECK(ctx.particleDrawCount == 1);
    CHECK(ctx.flareDrawCount == 1);          // (e) flare hook fired on the walk
    CHECK(ctx.waterTime == 1);               // (b) water advanced after the frame
    CHECK(ctx.skyPixels == 96 * 72);
    CHECK(NonZero16(ctx.fb) > 0);

    render::SurfaceDestroy(ctx.fb);
}
