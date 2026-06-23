// =============================================================================
// guild::play — atmosphere live-frame bridge implementation. See header.
//
// Mirrors the engine's BeginUniverseFrame (0x5B3900) atmosphere order:
//   CLEAR(sky background) -> TERRAIN(+animated water) -> SCENE -> PARTICLES
//   (dword_1408438 loop -> VIBE_Particle_RenderSystem) -> SKY FLARES(0x5EF7CC),
// plus the RunFrameLoop weather overlay (VIBE_Weather_RenderAndThunder 0x4C05AC ->
// Rain/Snow_Render). We route the two real FrameHooks slots (renderParticles,
// updateSkyFlares) into the reconstructed leaves and compose sky+water around the
// unmodified frame, writing only into a caller-owned FrameState/FrameHooks/context.
// =============================================================================
#include "play/wire_atmos_bridge.h"

#include "play/session_atmos.h"    // SessionAtmos (the brightness driver)
#include "render/light_atmos.h"    // the reconstructed lighting-table rebuild

#include <cmath>
#include <cstring>

namespace guild::render {
class Surface;
int SurfaceSetPixelRgb(Surface* s, int x, int y, u8 r, u8 g, u8 b);
void SurfaceGetPixelRgb(const Surface* s, int x, int y, u8 out[3]);
}

namespace guild::play {

// The active atmosphere context for the C-ABI FrameHooks trampolines. The engine's
// renderParticles/updateSkyFlares take only the frame-flag byte (no context arg),
// so we latch the installed context here (single live frame at a time, matching the
// engine's single-active-world model). Set by InstallRealAtmosBridge.
namespace {
AtmosBridgeContext* g_activeAtmos = nullptr;
}

// ---------------------------------------------------------------------------
// Inert defaults — faithful no-ops (== the engine's null hooks). Only bump the
// counter so a test can prove the live walk reached the leaf even when inert.
// ---------------------------------------------------------------------------
void InertRenderParticles(char /*a2*/) {
    if (g_activeAtmos) {
        ++g_activeAtmos->particleDrawCount;
        g_activeAtmos->drewRealParticles = false;
    }
}

void InertUpdateSkyFlares() {
    if (g_activeAtmos)
        ++g_activeAtmos->flareDrawCount;
}

void InertDrawSky(AtmosBridgeContext* ctx) {
    if (ctx) { ++ctx->skyDrawCount; ctx->drewRealSky = false; ctx->skyPixels = 0; }
}

// ---------------------------------------------------------------------------
// (a) SKY — REAL: blend the band ambient (render::BlendBandLighting @0x5B85E4),
// fill the framebuffer background with the resulting RGB. This is the engine's
// clear-to-sky-dome-colour (the sky is the frame background, behind terrain).
// ---------------------------------------------------------------------------
int DrawAtmosSky(AtmosBridgeContext* ctx) {
    if (!ctx) return 0;
    ++ctx->skyDrawCount;
    if (!ctx->fb || !ctx->fb->pixels) { ctx->drewRealSky = false; return 0; }

    ctx->skyAmbient = render::BlendBandLighting(ctx->skyBands, ctx->skyBandIndex,
                                                ctx->skyBandFrac, ctx->skyScale);
    // The ambient triple is a [0,1]-ish lightness; map to a visible 8-bit sky.
    auto clamp8 = [](float v) -> u8 {
        float s = v * 255.0f;
        if (s < 1.0f) s = 1.0f;          // never zero -> distinguishable from a blank fb
        if (s > 255.0f) s = 255.0f;
        return (u8)(s + 0.5f);
    };
    u8 r = clamp8(ctx->skyAmbient.r);
    u8 g = clamp8(ctx->skyAmbient.g);
    u8 b = clamp8(ctx->skyAmbient.b);

    int painted = 0;
    for (int y = 0; y < ctx->fb->height; ++y)
        for (int x = 0; x < ctx->fb->width; ++x) {
            render::SurfaceSetPixelRgb(ctx->fb, x, y, r, g, b);
            ++painted;
        }
    ctx->skyPixels = painted;
    ctx->drewRealSky = true;
    return painted;
}

// ---------------------------------------------------------------------------
// (b) WATER — REAL grid motion (render::AnimateWaterVertices @0x5BE428). Advance
// the wave grid one frame, count the floats that moved, then splat a water strip
// whose pixel x-offsets track the animated grid (the bridge-side raster target;
// SAID SO in the header). No texture group bank -> findGroupMember is a no-op.
// ---------------------------------------------------------------------------
namespace {
u32 NoGroupMember(i32 /*groupId*/, u8 /*frameByte*/, void* /*ctx*/) { return 0; }
}

int AnimateAtmosWater(AtmosBridgeContext* ctx) {
    if (!ctx) return 0;
    float before[64];
    std::memcpy(before, ctx->water.waveOut, sizeof before);
    ctx->waterPrevWave0 = ctx->water.waveOut[0];

    ++ctx->waterTime;  // advance the animation clock (the per-frame dt source)
    render::AnimateWaterVertices(&ctx->water, 1, ctx->waterTime, &NoGroupMember, nullptr);

    int moved = 0;
    for (int i = 0; i < 64; ++i)
        if (ctx->water.waveOut[i] != before[i]) ++moved;
    ctx->waterMoved = moved;

    // Splat a water strip whose horizontal ripple tracks waveOut[0..15].x.
    if (ctx->fb && ctx->fb->pixels) {
        int y0 = ctx->waterStripY;
        for (int dy = 0; dy < 4; ++dy) {
            int y = y0 + dy;
            if (y < 0 || y >= ctx->fb->height) continue;
            for (int x = 0; x < ctx->fb->width; ++x) {
                // sample the 16-vec4 grid; vec4 stride is 4 floats, .x at [0].
                float wave = ctx->water.waveOut[(x % 16) * 4];
                int ripple = (int)std::lround(wave * 4.0f);
                u8 blue = (u8)(180 + ((x + ripple + dy) & 0x3f));
                render::SurfaceSetPixelRgb(ctx->fb, x, y, 0, 40, blue);
            }
        }
    }
    return moved;
}

// ---------------------------------------------------------------------------
// (d)/weather PARTICLES — REAL emit. Advance the emitter (SeedParticles, then
// UpdateScatter for motion), the snow system (seed once, then UpdateFlake), and
// the weather intensity (peak-of-3 + grow amounts). Splat each live particle/flake
// at its position/projection (the bridge-side raster target; SAID SO).
// ---------------------------------------------------------------------------
int EmitAtmosParticles(AtmosBridgeContext* ctx) {
    if (!ctx) return 0;
    ++ctx->particleDrawCount;
    if (!ctx->fb || !ctx->fb->pixels) { ctx->drewRealParticles = false; return 0; }

    ++ctx->particleTime;
    u32 now = (u32)ctx->particleTime;

    // Particle emitter: seed inactive slots, then integrate (scatter motion).
    render::SeedParticles(ctx->particleEmitter, now);
    render::UpdateScatter(ctx->particleEmitter, now);

    // Snow: seed once (when no flakes are placed yet), then integrate per frame.
    if (ctx->snow.flakes && ctx->snow.count > 0) {
        bool seeded = false;
        for (int i = 0; i < ctx->snow.count; ++i)
            if (ctx->snow.flakes[i].px != 0.0f || ctx->snow.flakes[i].py != 0.0f) { seeded = true; break; }
        if (!seeded) render::SnowSeedFlakes(ctx->snow);
        render::SnowUpdateFlake(ctx->snow, 1.0f, ctx->snowCam, ctx->snowVp);
    }

    // Weather intensity (peak-of-3 over the 24h arc) + grow amounts.
    ctx->weatherIntensity = render::WeatherIntensity(ctx->weatherArc, ctx->weatherHour);

    // Splat live particles.
    int painted = 0;
    int aliveP = 0;
    if (ctx->particleEmitter.particles) {
        for (int i = 0; i < ctx->particleEmitter.count; ++i) {
            const render::Particle& p = ctx->particleEmitter.particles[i];
            if ((p.flags & 1) == 0) continue;
            ++aliveP;
            int sx = ctx->fb->width / 2 + (int)std::lround(p.px);
            int sy = ctx->fb->height / 2 + (int)std::lround(p.py);
            if (sx >= 0 && sx < ctx->fb->width && sy >= 0 && sy < ctx->fb->height) {
                render::SurfaceSetPixelRgb(ctx->fb, sx, sy, 255, 200, 80);
                ++painted;
            }
        }
    }
    ctx->particlesAlive = aliveP;

    // Splat snow flakes at their projected screen points.
    if (ctx->snow.flakes) {
        for (int i = 0; i < ctx->snow.count; ++i) {
            const render::SnowFlake& f = ctx->snow.flakes[i];
            int sx = (int)std::lround(f.sx);
            int sy = (int)std::lround(f.sy);
            if (sx >= 0 && sx < ctx->fb->width && sy >= 0 && sy < ctx->fb->height) {
                render::SurfaceSetPixelRgb(ctx->fb, sx, sy, 240, 240, 255);
                ++painted;
            }
        }
    }

    ctx->overlayPixels = painted;
    ctx->drewRealParticles = true;
    return painted;
}

// ---------------------------------------------------------------------------
// The REAL FrameHooks trampolines (C-ABI matching render::FrameHooks).
// ---------------------------------------------------------------------------
namespace {
void RealRenderParticles(char /*a2*/) {
    if (g_activeAtmos) EmitAtmosParticles(g_activeAtmos);
}
void RealUpdateSkyFlares() {
    if (g_activeAtmos) ++g_activeAtmos->flareDrawCount;  // flare overlay reached
}
} // namespace

// ---------------------------------------------------------------------------
void InstallInertAtmosBridge(render::FrameState& fs, render::FrameHooks& hooks,
                             AtmosBridgeContext* ctx) {
    fs.engineOn = true;
    fs.hasWorld = true;
    g_activeAtmos = ctx;
    hooks.renderParticles = &InertRenderParticles;
    hooks.updateSkyFlares = &InertUpdateSkyFlares;
    // gilde.exe 0x5f4428 — BeginUniverseFrame always rebuilds the shadow-light list. Wire
    // the reconstructed collector (render::ShadowResetLightListActive over the active
    // universe; a null active root is a safe no-op until SetActiveShadowUniverse is set).
    hooks.resetLights = &render::ShadowResetLightListActive;
}

AtmosBridgeInstall InstallRealAtmosBridge(render::FrameState& fs,
                                          render::FrameHooks& hooks,
                                          AtmosBridgeContext* ctx) {
    AtmosBridgeInstall r{};
    r.prevParticles = reinterpret_cast<void*>(hooks.renderParticles);
    r.prevFlares    = reinterpret_cast<void*>(hooks.updateSkyFlares);
    r.wasInert =
        (hooks.renderParticles == nullptr || hooks.renderParticles == &InertRenderParticles) &&
        (hooks.updateSkyFlares == nullptr || hooks.updateSkyFlares == &InertUpdateSkyFlares);

    fs.engineOn = true;
    fs.hasWorld = true;
    g_activeAtmos = ctx;
    hooks.renderParticles = &RealRenderParticles;
    hooks.updateSkyFlares = &RealUpdateSkyFlares;

    r.installed = (hooks.renderParticles == &RealRenderParticles) &&
                  (hooks.updateSkyFlares == &RealUpdateSkyFlares);
    return r;
}

AtmosBridgeInstall ComposeAtmosphereFrame(render::FrameState& fs,
                                          render::FrameHooks& hooks,
                                          AtmosBridgeContext* ctx) {
    AtmosBridgeInstall ins = InstallRealAtmosBridge(fs, hooks, ctx);
    // Engine order: (a) sky background, then the frame (terrain+scene fires the
    // real particle + flare hooks), then (b) the animated water floor.
    DrawAtmosSky(ctx);
    render::BeginUniverseFrame(fs, hooks, /*a2=*/1);
    AnimateAtmosWater(ctx);
    return ins;
}

// ---------------------------------------------------------------------------
// Synthetic atmosphere builder (deterministic; for unit/integration frames).
// ---------------------------------------------------------------------------
void AtmosBridgeContext::MakeSynthetic(AtmosBridgeContext& ctx, int W, int H,
                                       render::Particle* particleStore, int slots,
                                       render::SnowFlake* flakeStore, int flakes) {
    // 7 sky bands: a dawn->day gradient (distinct, non-zero colours).
    for (int i = 0; i < render::kSkyBands; ++i) {
        float t = (float)i / (float)(render::kSkyBands - 1);
        ctx.skyBands[i].r = 0.20f + 0.30f * t;
        ctx.skyBands[i].g = 0.30f + 0.40f * t;
        ctx.skyBands[i].b = 0.50f + 0.40f * t;
    }
    ctx.skyBandIndex = 2;
    ctx.skyBandFrac  = 0.5f;
    ctx.skyScale     = 0.9f;

    // One water mesh with non-zero wave amplitudes + phases (so the grid animates).
    ctx.water = render::WaterMesh{};
    for (int k = 0; k < 4; ++k) {
        ctx.water.amp[k]       = 0.5f + 0.1f * k;
        ctx.water.waveSpeed[k] = 0.3f + 0.05f * k;
        ctx.water.phase[k]     = 0.1f * k;
    }
    ctx.water.lastTime = 0;
    ctx.waterTime = 0;
    ctx.waterStripY = H * 3 / 4;

    // Particle emitter over the caller-owned slot store.
    ctx.particleEmitter = render::Emitter{};
    if (particleStore && slots > 0) {
        std::memset(particleStore, 0, sizeof(render::Particle) * (size_t)slots);
        ctx.particleEmitter.particles = particleStore;
        ctx.particleEmitter.count = slots;
        ctx.particleEmitter.maxAlive = (u32)slots;
        ctx.particleEmitter.baseVx = 9.8f;   // gravity-ish (scatter bounce uses it)
        ctx.particleEmitter.baseVz = 0.0f;    // ground plane
    }
    ctx.particleTime = 0;

    // Snow system over the caller-owned flake store.
    ctx.snow = render::SnowSystem{};
    if (flakeStore && flakes > 0) {
        std::memset(flakeStore, 0, sizeof(render::SnowFlake) * (size_t)flakes);
        ctx.snow.flakes = flakeStore;
        ctx.snow.count = flakes;
        ctx.snow.capacity = flakes;
    }
    // Snow camera: identity basis + a simple viewport so flakes project on-screen.
    ctx.snowCam = render::SnowCamera{};
    ctx.snowCam.m[0] = 1.0f; ctx.snowCam.m[4] = 1.0f; ctx.snowCam.m[8] = 1.0f;
    ctx.snowVp = render::SnowViewport{0, 0, W, H};

    // Noon weather arc (a peak mid-day) + a steady wind.
    for (int h = 0; h < 24; ++h)
        ctx.weatherArc[h] = (h >= 10 && h <= 14) ? 120 : 40;
    ctx.weatherHour = 12;
    ctx.windX = -1.0f;
    ctx.windY = 0.5f;
}

// ===========================================================================
// BRIGHTNESS -> LIGHTING-TABLE REBUILD (see header). The exact application
// step of one SessionAtmos frame:
//   * brightnessStep ran BlendBandLighting (atmos.lightingRebuilt) ->
//       - the flt_64A074/78/7C ambient store half of 0x5b85e4
//         (render::LightAtmosStoreAmbient; gated on the caller-supplied sky
//          rig exactly as SessionAtmos gates its own ambient output), then
//       - the 0x5b88cf tail call: VIBE_Light_RefreshAllObjects(force)
//         (render::LightAtmosRefreshAllObjects @0x5c886c — serial bump always;
//          force<=1 invalidate walk; force>1 eager rebuild walk; floor hook).
//   * otherwise (hysteresis skip / flash window / relightDisabled): nothing —
//     the original never reaches BlendBandLighting on those frames.
// ===========================================================================
AtmosLightingApplyResult ApplyAtmosLightingFrame(const SessionAtmos& atmos,
                                                 int frameStampMs,
                                                 int& appliedRebuilds) {
    AtmosLightingApplyResult r;
    r.serial = render::LightAtmos().rebuildSerial;
    const int pending = atmos.lightRebuilds - appliedRebuilds;
    if (pending <= 0)
        return r;                       // no BlendBandLighting since last apply
    appliedRebuilds = atmos.lightRebuilds;
    if (atmos.hasSkyBands) {            // the band rows are caller-supplied
        render::LightAtmosStoreAmbient(atmos.ambient);
        r.ambientStored = true;
    }
    r.force = atmos.refreshForce;       // BlendBandLighting's a4 (last rebuild)
    // One RefreshAllObjects per BlendBandLighting run (the 0x5b88cf tail call),
    // so the dword_64A064 serial advances exactly as the original's call count.
    for (int i = 0; i < pending; ++i)
        r.walkResult = render::LightAtmosRefreshAllObjects(atmos.refreshForce,
                                                           frameStampMs);
    r.rebuilds = pending;
    r.refreshed = true;
    r.serial = render::LightAtmos().rebuildSerial;
    return r;
}

} // namespace guild::play
