#pragma once
// =============================================================================
// guild::play — FULL IN-GAME CITY-VIEW FRAME COMPOSITOR (PLAYABLE_PLAN P2).
//
// RenderCityFrame composes the complete city view the live engine draws every
// frame, in the engine's layer order, into ONE software render::Surface and then
// presents it to a headless device. It is the top-level "draw the whole frame"
// step that sits above the three already-reconstructed layer renderers:
//
//   1. TERRAIN  (play::terrain_render)   — the ground floor heightfield, drawn
//                                          first (fills the frame, back-most).
//   2. OBJECTS  (play::world_render)     — the scene objects/buildings at their
//                                          REAL world transforms, drawn OVER the
//                                          terrain (the scene-graph walk layer).
//   3. HUD      (play::hud_render)       — the player bar / money-date caption /
//                                          map markers, drawn OVER everything.
//
// THE ENGINE ORDER WE MIRROR
// ---------------------------------------------------------------------------
// The live game's per-frame view is:
//   VIBE_Render_RenderMainViewFrame @0x5b6074
//     -> RenderUniverseFrame        @0x5b3de8
//        -> DrawUniverseAndStats     @0x5b3bbc
//             (a) the 3D universe: terrain floor, then VIBE_SceneGraph_WalkAndInvoke
//                 @0x5ac738 over the active universe root (off_649D64) — the scene
//                 objects projected/rasterized OVER the terrain, and
//             (b) the "stats" overlay: the HUD player bar + money/date caption,
//                 composited ON TOP of the finished 3D frame.
// RenderCityFrame reproduces exactly that order — terrain fill, then objects over
// terrain, then HUD over objects — into a single 32bpp surface, then runs the REAL
// render::PresentFrame dispatch to blit it to a shim::IGraphicsDevice.
//
// SINGLE-SURFACE COMPOSITION
// ---------------------------------------------------------------------------
// Each layer renderer was reconstructed against its native pixel format (terrain
// rasterizes shade bytes into an 8bpp surface; the HUD draws RGB into any-bpp
// surface). The frame compositor owns one 32bpp ARGB target (the natural readback
// format) and lays each layer into it:
//   * terrain: rendered to its native 8bpp shade surface, then the shade ramp is
//     composited into the target as a green-tinted ground (so a "ground pixel" is
//     recognizable in the final RGB frame),
//   * objects: each scene-object quad (built by play::WorldRenderer at its real
//     transform) is filled into the target as a distinct object colour OVER the
//     terrain pixels,
//   * HUD: play::RenderHud draws straight onto the target (its overlay model).
// The result is one finished frame Surface; PresentFrame copies it to the device.
//
// Everything here is additive (new file). It calls already-reconstructed siblings
// (terrain_render / world_render / hud_render / present) and exposes the device
// edge through the standard shim::IGraphicsDevice; no wiring.cpp / hooks edits.
// =============================================================================
#include "guild/common/types.h"
#include "render/surface.h"

#include "play/camera_controls.h"   // CameraControl (city camera)
#include "play/terrain_render.h"    // Heightfield / TerrainView / RenderTerrain
#include "play/world_render.h"      // WorldRenderer / WorldDrawList (objects)
#include "play/hud_render.h"        // HudRenderState / RenderHud

namespace guild::shim { class IGraphicsDevice; }

namespace guild::play {

// ---------------------------------------------------------------------------
// The composited-frame inputs: the three layers + the camera that frames them.
// Any layer may be left empty (a null/invalid heightfield skips terrain, an empty
// world skips objects, a default HudRenderState draws an empty HUD) so partial
// frames still compose. Coordinates are framebuffer pixel space.
// ---------------------------------------------------------------------------
struct CityFrameScene {
    // Layer 1 — terrain floor.
    Heightfield  terrain{};        // invalid() => terrain layer skipped
    TerrainView  terrainView{};    // top-down projection for the floor

    // Layer 2 — scene objects (built from the live entity arrays by WorldRenderer).
    WorldRenderer::Options worldOpt{};   // fbW/fbH overwritten to the frame size

    // Layer 3 — HUD overlay state + anchors.
    HudRenderState hud{};
    HudPalette     hudPal{};
    int hudBarOriginX = 4,  hudBarOriginY = 0;   // bottom player-bar strip top-left
    int hudCaptionX   = 4,  hudCaptionY   = 4;   // money/date caption anchor
    int hudMapOriginX = 0,  hudMapOriginY = 0;   // map-region top-left (markers)
    bool drawHud = true;
};

// ---------------------------------------------------------------------------
// Per-frame composition statistics, read back for assertions.
// ---------------------------------------------------------------------------
struct CityFrameStats {
    // terrain layer
    bool terrainDrawn   = false;
    int  terrainTiles   = 0;     // tiles the floor walk visited
    int  terrainTris    = 0;     // triangles the floor rasterizer emitted
    int  terrainPixels  = 0;     // ground pixels composited into the target

    // object layer
    int  objectsBuilt   = 0;     // scene-object quads WorldRenderer produced
    int  objectsDrawn   = 0;     // quads actually filled into the target (on-frame)
    int  objectPixels   = 0;     // pixels the object layer painted OVER terrain

    // HUD layer
    bool hudDrawn       = false;
    int  hudBarSlots    = 0;
    int  hudCaptionGlyphs = 0;
    int  hudMarkers     = 0;
    int  hudPixels      = 0;     // pixels the HUD painted OVER the world

    // whole frame
    int  fbW = 0, fbH = 0;
    int  nonBlankPixels = 0;     // pixels != the sky-clear colour
    bool presented      = false; // PresentFrame succeeded into the device
    int  presentCount   = 0;     // device present() invocations so far
};

// ---------------------------------------------------------------------------
// Object-layer fill colour helper: map a WorldObject light/shade byte to a
// distinct object RGB so objects read as "not terrain, not sky" in the final
// frame. Deterministic; the unit golden checks pin it down.
// ---------------------------------------------------------------------------
struct CityFramePalette {
    // Sky clear (background the frame is cleared to before any layer draws).
    u8 skyR = 0, skyG = 0, skyB = 64;
    // Terrain ground tint: the 8bpp shade ramps the GREEN channel of the ground
    // (groundR/groundB are a fixed earthy floor), so a ground pixel is recognizable.
    u8 groundBaseR = 24, groundBaseB = 16;
    // Object fill: a warm structure colour, modulated by the object's light byte.
    u8 objR = 200, objG = 160, objB = 90;
};

// Composite one terrain shade byte into a target ARGB pixel value (ground tint).
void CityFrameGroundRgb(u8 shade, const CityFramePalette& pal, u8& r, u8& g, u8& b);
// Object fill colour for a WorldObject light byte.
void CityFrameObjectRgb(u8 light, const CityFramePalette& pal, u8& r, u8& g, u8& b);

// ===========================================================================
// COMPOSITORS
// ===========================================================================

// Compose the full city frame into `target` (which MUST be a non-null 32bpp
// surface already allocated to the frame size). Clears to the sky colour, then
// lays terrain -> objects -> HUD in the engine's order. Does NOT present. The
// camera frames the layers: its eye recenters the object layer and its zoom is
// carried into the world placement scale. Returns the composition stats.
CityFrameStats ComposeCityFrame(render::Surface* target, const CameraControl& camera,
                                const CityFrameScene& scene,
                                const CityFramePalette& pal = CityFramePalette());

// The top-level entry: allocate a 32bpp `fbW`x`fbH` frame, ComposeCityFrame the
// scene into it, then run the REAL render::PresentFrame dispatch to blit it to
// `device` (which must already be init()'d to fbW x fbH x 32bpp). Returns the
// composition + present stats. `outFrame`, when non-null, receives the composited
// surface (caller owns it; free with render::SurfaceDestroy); otherwise the frame
// is destroyed internally after presenting.
//
// This mirrors VIBE_Render_RenderMainViewFrame @0x5b6074 / DrawUniverseAndStats
// @0x5b3bbc: build the frame in the layer order, then present it.
CityFrameStats RenderCityFrame(const CameraControl& camera,
                               const CityFrameScene& scene,
                               int fbW, int fbH,
                               shim::IGraphicsDevice& device,
                               render::Surface** outFrame = nullptr,
                               const CityFramePalette& pal = CityFramePalette());

} // namespace guild::play
