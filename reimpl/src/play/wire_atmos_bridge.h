#pragma once
// =============================================================================
// guild::play — DE-INERT THE ATMOSPHERE LAYER on the LIVE frame path
// (PLAYABLE_PLAN P6 / Wave 30). Sibling of wire_terrain_bridge: same install-by-
// public-setter, FrameState/FrameHooks-pair pattern; ADDITIVE — touches NO owned
// file (render_binder.cpp / frame.cpp / sky.cpp / water_vertices.cpp).
//
// WHAT "ATMOSPHERE" IS IN THE LIVE FRAME (traced from the binary)
// ---------------------------------------------------------------------------
// gilde.exe 0x5B3900 VIBE_Render_BeginUniverseFrame draws, IN ORDER:
//
//   (a) CLEAR      ClearViewport / ClearRect  — the SKY background. The sky dome
//                  colour the clear shows is the band-blended ambient that
//                  VIBE_Sky_RefreshDomeColors (0x4B236C) / SkyColor_BlendBandLighting
//                  (0x5B85E4) compute. So the sky is the FRAME BACKGROUND, drawn
//                  FIRST, BEHIND the terrain.
//   (b) TERRAIN    VIBE_Floor_RenderTerrain (0x5BF22C). Internally the floor mesh's
//                  WATER quads are animated each frame by VIBE_Floor_AnimateWaterVertices
//                  (0x5BE428) — the water is the ANIMATED FLOOR layer, drawn ON the
//                  ground.  (The ground itself is the wire_terrain_bridge cluster.)
//   (c) SCENE walk objects ...
//   (d) PARTICLES  the dword_1408438 list loop -> VIBE_Particle_RenderSystem(node,a2)
//                  — the per-system emitter/particle OVERLAY.
//   (e) SKY FLARES VIBE_Render_UpdateSkyFlares (0x5EF7CC, gated on dword_64A7C8) —
//                  lens/sun-flare sprites, an OVERLAY drawn after the scene.
//
// WEATHER (rain/snow) is the sibling overlay driven from the GAME frame loop:
//   gilde.exe 0x4C09A0 VIBE_GameLogic_RunFrameLoop calls
//     VIBE_Weather_UpdateSky        (0x4C0040)  — state: intensity + cloud scroll
//     VIBE_Weather_RenderAndThunder (0x4C05AC)  -> VIBE_Rain_Render (0x429C38)
//                                               -> VIBE_Snow_Render (0x42B5B0)
//   i.e. an overlay of projected drop/flake streaks, on TOP of the universe frame.
//
// THE INERT HOOKS WE DE-INERT
// ---------------------------------------------------------------------------
// render::FrameHooks ships `renderParticles == nullptr` and `updateSkyFlares ==
// nullptr` (the engine's "subsystem not present this frame" no-op). render_binder
// leaves them null. So on the live path the particle overlay + sky flares never
// run. The SKY background and the WATER animation have NO dedicated FrameHooks
// slot at all (sky == the clear; water == inside renderTerrain); this bridge adds
// them as caller-invoked atmosphere passes that compose, in engine order, around
// the unmodified BeginUniverseFrame.
//
// SOFTWARE-RASTER TARGETS — WHAT IS REAL vs INERT-BY-NECESSITY
// ---------------------------------------------------------------------------
//   SKY      : REAL. render::BlendBandLighting (0x5B85E4) computes the sky ambient
//              RGB; we FILL the framebuffer background with it (the engine's clear-
//              to-sky-dome-colour). Observable: background pixels gain sky colour.
//   WATER    : the wave GRID is REAL (render::AnimateWaterVertices, 0x5BE428 — the
//              16-vec4 sine/cosine displacement advances with time). The engine
//              rasterizes that grid INSIDE VIBE_Floor_RenderTerrain (no standalone
//              software water-raster leaf exists in the reconstruction). We splat a
//              small water strip whose pixel offsets track the animated grid so the
//              animation is observable in the frame; the underlying VERTEX MOTION
//              is the faithful real output. (SAID SO: the water RASTER target is a
//              bridge-side splat, not a recovered leaf.)
//   PARTICLES: REAL emit. render::SeedParticles / UpdateScatter (0x42BEC0/0x42CDE8)
//              advance a real emitter; render::SnowSeedFlakes / SnowUpdateFlake
//              (0x42A014/0x42A644) + render::WeatherIntensity (0x4C0040 core) drive
//              weather. The engine's VIBE_Particle_RenderSystem sprite rasterizer is
//              not reconstructed as a software leaf, so we splat each live particle/
//              flake at its projected position. (SAID SO: the particle RASTER target
//              is a bridge-side splat; the emitter/flake MOTION is the real output.)
// =============================================================================
#include "guild/common/types.h"
#include "render/frame.h"     // FrameState, FrameHooks (the live frame walk)
#include "render/surface.h"   // Surface (the framebuffer atmosphere composes into)

#include "render/sky.h"       // BlendBandLighting, SkyBandColor (sky background)
#include "render/water_vertices.h" // WaterMesh, AnimateWaterVertices (animated floor)
#include "render/particle.h"  // Emitter, Particle (particle overlay)
#include "render/snow.h"      // SnowSystem, SnowCamera, SnowViewport (weather)
#include "render/weather.h"   // WeatherIntensity (weather state)

namespace guild::play {

// ---------------------------------------------------------------------------
// The live atmosphere context the de-inerted hooks receive (passed opaque through
// hooks.terrain-style slots). Holds the framebuffer + the three layers' live state
// + a read-back so a test can prove each layer flipped inert->real on the live walk.
// Caller-owned; must outlive the frame walk.
// ---------------------------------------------------------------------------
struct AtmosBridgeContext {
    render::Surface* fb = nullptr;     // framebuffer the atmosphere composes into

    // -- (a) SKY background (BlendBandLighting) ------------------------------
    render::SkyBandColor skyBands[render::kSkyBands]{}; // 7-band gradient table
    int   skyBandIndex = 0;            // band `a` to blend a -> (a+1)%7
    float skyBandFrac  = 0.0f;         // t in [0,1]
    float skyScale     = 1.0f;         // time-of-day scale
    render::SkyAmbient skyAmbient{};   // read-back: the blended ambient triple

    // -- (b) WATER animated floor (AnimateWaterVertices) ---------------------
    render::WaterMesh water{};         // one animated water mesh
    int   waterTime    = 0;            // current animation time (advances per frame)
    int   waterStripY  = 0;            // fb row the water strip splats into
    float waterPrevWave0 = 0.0f;       // read-back: waveOut[0] before last advance

    // -- (d)/weather PARTICLES + SNOW overlay --------------------------------
    render::Emitter particleEmitter{}; // the live particle emitter
    render::SnowSystem snow{};         // the live snow system
    render::SnowCamera snowCam{};      // camera basis for flake projection
    render::SnowViewport snowVp{};     // viewport rect for flake projection
    i32   weatherArc[24]{};            // 24-hour weather intensity arc
    int   weatherHour  = 12;           // current hour (peak-of-3 intensity)
    float windX = -1.0f, windY = 0.0f; // wind (snow/rain grow + cloud scroll)
    int   particleTime = 0;            // emitter tick (advances per frame)

    // read-back
    int  skyPixels      = 0;  // background pixels the sky fill painted last frame
    int  waterMoved     = 0;  // # water grid floats that changed in the last advance
    int  particlesAlive = 0;  // live particle slots after the last emit
    int  weatherIntensity = 0;// peak-of-3 intensity computed last frame
    int  overlayPixels  = 0;  // particle+flake pixels splatted last frame

    int  skyDrawCount = 0;    // # times the sky pass ran (live walk reached it)
    int  particleDrawCount = 0;// # times the particle hook fired on the live walk
    int  flareDrawCount = 0;  // # times the sky-flare hook fired on the live walk
    bool drewRealSky = false; // true once the REAL sky fill ran
    bool drewRealParticles = false; // true once the REAL particle emit ran

    // Convenience: build a deterministic synthetic atmosphere (7 sky bands, one
    // water mesh, an emitter with `slots` particles + a snow system with `flakes`
    // flakes + a noon weather arc) sized to a W x H framebuffer. Allocates the
    // particle/flake arrays into the supplied backing storage (caller owns those).
    static void MakeSynthetic(AtmosBridgeContext& ctx, int W, int H,
                              render::Particle* particleStore, int slots,
                              render::SnowFlake* flakeStore, int flakes);
};

// Result of installing the bridge (for the caller / tests to assert against).
struct AtmosBridgeInstall {
    bool installed = false;     // both hooks now point at the real atmosphere leaves
    bool wasInert  = false;     // the hooks were inert (null / inert-default) before
    void* prevParticles = nullptr; // hooks.renderParticles before the swap
    void* prevFlares    = nullptr; // hooks.updateSkyFlares before the swap
};

// ---------------------------------------------------------------------------
// THE INERT DEFAULTS (defined in wire_atmos_bridge.cpp). Faithful no-ops == the
// engine's null hooks ("atmosphere subsystem not present this frame"). They only
// bump their draw counter so a unit test can observe the swap inert -> real on an
// identical FrameHooks, and so the symbols are defined in src/.
// ---------------------------------------------------------------------------
void InertRenderParticles(char a2);  // the inert particle hook
void InertUpdateSkyFlares();         // the inert sky-flare hook
// Inert sky pass: paints nothing (background stays as the caller cleared it).
void InertDrawSky(AtmosBridgeContext* ctx);

// Install the INERT atmosphere bridge: hooks.renderParticles/updateSkyFlares point
// at the inert no-ops; fs gated on so the live walk CALLS them (and they paint
// nothing). The "before" baseline for the difference assertions.
void InstallInertAtmosBridge(render::FrameState& fs, render::FrameHooks& hooks,
                             AtmosBridgeContext* ctx);

// ---------------------------------------------------------------------------
// DE-INERT: point hooks.renderParticles + hooks.updateSkyFlares at the REAL
// reconstructed atmosphere leaves (particle emit + weather), bind `ctx`, and gate
// fs on (engineOn/hasWorld). After this, running the UNMODIFIED
// render::BeginUniverseFrame advances the real emitter/flakes and splats the
// overlay into ctx->fb. The SKY background + WATER floor have no FrameHooks slot;
// call DrawAtmosSky() BEFORE the frame (sky is behind terrain) and AnimateAtmosWater()
// as part of the floor — or use ComposeAtmosphereFrame() to run the whole layer in
// engine order around one BeginUniverseFrame. Returns what was swapped.
// ---------------------------------------------------------------------------
AtmosBridgeInstall InstallRealAtmosBridge(render::FrameState& fs,
                                          render::FrameHooks& hooks,
                                          AtmosBridgeContext* ctx);

// ---- the de-inerted atmosphere passes (exposed for the C trampolines + tests) --

// (a) SKY: blend the band ambient (render::BlendBandLighting) and FILL the fb
// background with it (the engine's clear-to-sky-colour). Runs BEFORE terrain.
// Returns the number of background pixels painted. No-op on a null/blank ctx.
int DrawAtmosSky(AtmosBridgeContext* ctx);

// (b) WATER: advance the wave grid (render::AnimateWaterVertices) by one frame and
// splat a water strip whose pixels track the animated grid. Returns the number of
// wave-grid floats that changed (the real vertex motion). Advances ctx->waterTime.
int AnimateAtmosWater(AtmosBridgeContext* ctx);

// (d)/weather PARTICLES+SNOW: advance the real emitter (SeedParticles/UpdateScatter)
// + snow (SnowSeedFlakes/SnowUpdateFlake) + weather intensity, then splat each live
// particle/flake. Returns overlay pixels splatted. This is what the renderParticles
// hook is de-inerted to. Advances ctx->particleTime.
int EmitAtmosParticles(AtmosBridgeContext* ctx);

// Run the FULL atmosphere layer in the engine's order around one unmodified frame:
//   DrawAtmosSky (background) -> BeginUniverseFrame (terrain+scene, fires the real
//   particle hook -> EmitAtmosParticles, then the real flare hook) -> AnimateAtmosWater
//   (animated floor). Installs the real bridge onto `fs`/`hooks` first. The caller's
//   fb should be pre-cleared; ctx must carry a valid fb + synthetic/loaded layers.
// Returns the install result; per-layer read-back lives in ctx.
AtmosBridgeInstall ComposeAtmosphereFrame(render::FrameState& fs,
                                          render::FrameHooks& hooks,
                                          AtmosBridgeContext* ctx);

} // namespace guild::play
