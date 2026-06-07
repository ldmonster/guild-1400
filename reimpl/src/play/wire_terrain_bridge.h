#pragma once
// =============================================================================
// guild::play — DE-INERT THE TERRAIN / WATER RENDER HOOK on the LIVE frame path
// (PLAYABLE_PLAN P6).
//
// THE INERT HOOK WE DE-INERT
// ---------------------------------------------------------------------------
// The live world frame walks render::BeginUniverseFrame (@0x5B3900). Inside it,
// the ORIGINAL draws the ground floor with:
//
//     if ( dword_64A028 )                                  // a terrain root exists
//     {
//        ... SetFogRange(...) ...                          // terrain-coupled fog
//        VIBE_Floor_RenderTerrain(dword_64A028, a2);       // @0x5BF22C
//     }
//
// In the reconstruction (render/frame.cpp) that is:
//
//     if (fs.hasTerrain && hooks.renderTerrain)
//         hooks.renderTerrain(hooks.terrain, a2);
//
// render::FrameHooks ships with `renderTerrain == nullptr` and `terrain == nullptr`
// (the engine's "subsystem not present this frame" no-op). The world render binder
// (src/play/render_binder.cpp) leaves it that way too — it sets `hasTerrain=false`
// and never installs `renderTerrain`, faking the ground as a flat scene-object quad.
// So on the live frame path the terrain leaf is INERT: BeginUniverseFrame never
// invokes the real floor renderer; the per-tile tessellated, per-tile-lit ground
// the engine actually draws does not appear.
//
// WHAT THIS BRIDGE DOES (ADDITIVE — public setters only)
// ---------------------------------------------------------------------------
// InstallRealTerrainBridge() points that inert `renderTerrain` hook at the REAL
// reconstructed terrain leaf cluster — play::RenderTerrain (src/play/terrain_render
// .cpp), which itself drives render::SelectTileMeshLod / render::TileSubdivCount /
// render::BuildTileVertex / render::RasterizeTexturedTriangle — and flips
// `hasTerrain=true` with a live terrain context. It does NOT touch
// render_binder.cpp / frame.cpp / any hook-table .cpp: it only WRITES into a
// caller-owned render::FrameState + render::FrameHooks pair (the same structs the
// engine's frame walk consumes) and then the caller runs the unmodified
// render::BeginUniverseFrame. The inert default lives in this module's .cpp so the
// unified library links (build model: every src-referenced symbol defined in src/).
//
// The bridge owns nothing the caller doesn't hand it: the framebuffer Surface, the
// loaded Heightfield and the TerrainView are caller-provided. Installing the bridge
// is the ONLY behavior change on the live floor draw between an inert frame and a
// real-ground frame.
// =============================================================================
#include "guild/common/types.h"
#include "render/frame.h"       // FrameState, FrameHooks (the live frame walk)
#include "render/surface.h"     // Surface (the framebuffer the floor draws into)

#include "play/terrain_render.h" // Heightfield, TerrainView, TerrainRenderStats

namespace guild::play {

// ---------------------------------------------------------------------------
// The live terrain context the renderTerrain hook receives (passed as the opaque
// `dword_64A028` terrain-root pointer through hooks.terrain). Holds everything the
// real floor leaf needs to draw one frame: the framebuffer, the loaded heightfield,
// the projection/lighting view, the clear/background byte, and a read-back of the
// last draw's stats + an invocation counter (so a test can prove the live walk
// actually reached the leaf). This is the bridge's analogue of the engine's terrain
// root record.
// ---------------------------------------------------------------------------
struct TerrainBridgeContext {
    render::Surface* fb         = nullptr;  // 8bpp floor framebuffer (caller-owned)
    Heightfield      hf{};                  // loaded ground heightfield
    TerrainView      view{};                // projection + lighting
    u8               background = 0;        // surface clear byte (non-blank baseline)

    // read-back (written by the hook each invocation)
    TerrainRenderStats lastStats{};
    int                drawCount = 0;       // # times the live walk invoked the leaf
    bool               drewReal  = false;   // true once the REAL leaf has run
};

// Result of installing the bridge (for the caller / tests to assert against).
struct TerrainBridgeInstall {
    bool installed       = false;  // renderTerrain hook now points at the real leaf
    bool wasInert        = false;  // the hook was inert (null/inert-default) before
    void* previousHook   = nullptr;// the function pointer value before the swap
    void* previousCtx    = nullptr;// hooks.terrain value before the swap
};

// ---------------------------------------------------------------------------
// THE INERT DEFAULT (defined in wire_terrain_bridge.cpp).
// A renderTerrain hook that draws NOTHING — the faithful equivalent of the engine's
// null hook ("subsystem not present"). Installing this onto a FrameHooks leaves the
// floor blank on the live path; it exists so the unit test can observe the swap from
// inert -> real on an identical FrameHooks, and so the symbol is defined in src/.
// ---------------------------------------------------------------------------
void InertRenderTerrain(void* terrainCtx, char frameFlags);

// Install the inert terrain hook (renderTerrain = InertRenderTerrain, terrain = ctx,
// hasTerrain = true). The live walk will CALL the leaf but it paints nothing — the
// "before" state for the difference assertions.
void InstallInertTerrainBridge(render::FrameState& fs, render::FrameHooks& hooks,
                               TerrainBridgeContext* ctx);

// ---------------------------------------------------------------------------
// DE-INERT: point hooks.renderTerrain at the REAL play::RenderTerrain leaf cluster,
// set hooks.terrain to `ctx`, and flip fs.hasTerrain = true (also fs.engineOn /
// fs.hasWorld so the live walk is not gated off). After this call, running the
// UNMODIFIED render::BeginUniverseFrame(fs, hooks, a2) will rasterize the real
// tessellated, per-tile-lit ground into ctx->fb. Returns what was swapped.
// `ctx` MUST outlive the frame walk and carry a valid fb + heightfield + view.
// ---------------------------------------------------------------------------
TerrainBridgeInstall InstallRealTerrainBridge(render::FrameState& fs,
                                              render::FrameHooks& hooks,
                                              TerrainBridgeContext* ctx);

// Convenience: build a fresh FrameState/FrameHooks for a live floor frame and
// de-inert the terrain hook in one call. The remaining frame leaves (clear, scene
// walk, particles, ...) stay inert/null (their de-inert is a DISJOINT cluster owned
// elsewhere). Returns the install result.
TerrainBridgeInstall MakeLiveTerrainFrame(render::FrameState& fs,
                                          render::FrameHooks& hooks,
                                          TerrainBridgeContext* ctx);

} // namespace guild::play
