// =============================================================================
// guild::play — terrain/water live-frame bridge implementation. See header.
//
// gilde.exe 0x5B3900 — VIBE_Render_BeginUniverseFrame (the live frame walk) calls
//   VIBE_Floor_RenderTerrain(dword_64A028, a2)  // 0x5BF22C, only if dword_64A028
// We mirror that call signature with render::FrameHooks.renderTerrain(terrain, a2)
// and route it into the REAL reconstructed floor leaf cluster (play::RenderTerrain).
// =============================================================================
#include "play/wire_terrain_bridge.h"

namespace guild::play {

// ---------------------------------------------------------------------------
// Inert default: the faithful no-op terrain hook (== the engine's null hook).
// Draws nothing; only bumps the invocation counter so a test can prove the LIVE
// walk reached the leaf even in the inert state.
// ---------------------------------------------------------------------------
void InertRenderTerrain(void* terrainCtx, char /*frameFlags*/) {
    if (auto* ctx = static_cast<TerrainBridgeContext*>(terrainCtx)) {
        ++ctx->drawCount;
        ctx->drewReal = false;
        // no rasterization — the floor stays at the background byte.
    }
}

// ---------------------------------------------------------------------------
// The REAL terrain hook: drives play::RenderTerrain (which itself runs
// SelectTileMeshLod / TileSubdivCount / BuildTileVertex / RasterizeTexturedTriangle,
// the reconstructed leaves of VIBE_Floor_RenderTerrain @0x5BF22C) into the context's
// framebuffer. This is what the inert hook is de-inerted to.
// ---------------------------------------------------------------------------
namespace {
void RealRenderTerrain(void* terrainCtx, char /*frameFlags*/) {
    auto* ctx = static_cast<TerrainBridgeContext*>(terrainCtx);
    if (!ctx || !ctx->fb || !ctx->hf.valid())
        return;
    // The engine clears the floor surface inside the frame bracket before the floor
    // draw; we clear to the context background so the non-blank delta is the ground.
    ctx->lastStats = play::RenderTerrain(ctx->fb, ctx->hf, ctx->view, ctx->background);
    ++ctx->drawCount;
    ctx->drewReal = true;
}
} // namespace

// ---------------------------------------------------------------------------
void InstallInertTerrainBridge(render::FrameState& fs, render::FrameHooks& hooks,
                               TerrainBridgeContext* ctx) {
    fs.engineOn   = true;
    fs.hasWorld   = true;       // a live world this frame -> BeginUniverseFrame walks
    fs.hasTerrain = true;       // dword_64A028 != 0 -> the floor branch is taken
    hooks.terrain       = ctx;
    hooks.renderTerrain = &InertRenderTerrain;
}

TerrainBridgeInstall InstallRealTerrainBridge(render::FrameState& fs,
                                              render::FrameHooks& hooks,
                                              TerrainBridgeContext* ctx) {
    TerrainBridgeInstall r{};
    // record the prior (possibly inert/null) state for the swap assertion.
    r.previousHook = reinterpret_cast<void*>(hooks.renderTerrain);
    r.previousCtx  = hooks.terrain;
    r.wasInert     = (hooks.renderTerrain == nullptr) ||
                     (hooks.renderTerrain == &InertRenderTerrain) ||
                     (fs.hasTerrain == false);

    // DE-INERT: point the live floor hook at the REAL leaf, with a live context.
    fs.engineOn   = true;
    fs.hasWorld   = true;
    fs.hasTerrain = true;       // dword_64A028 != 0 -> RenderTerrain branch runs
    hooks.terrain       = ctx;
    hooks.renderTerrain = &RealRenderTerrain;

    r.installed = (hooks.renderTerrain == &RealRenderTerrain) && fs.hasTerrain;
    return r;
}

TerrainBridgeInstall MakeLiveTerrainFrame(render::FrameState& fs,
                                          render::FrameHooks& hooks,
                                          TerrainBridgeContext* ctx) {
    fs    = render::FrameState{};
    hooks = render::FrameHooks{};   // all other leaves stay inert (null) — disjoint.
    return InstallRealTerrainBridge(fs, hooks, ctx);
}

} // namespace guild::play
