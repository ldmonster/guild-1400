// tests/unit/wire_terrain_bridge_test.cpp — P6 de-inert the live terrain hook.
//
// Proves InstallRealTerrainBridge swaps render::FrameHooks.renderTerrain from the
// inert default (draws nothing) to the REAL play::RenderTerrain leaf, and that the
// behavior on a synthetic heightfield input changes (blank -> real ground pixels).
#include "test.h"

#include "play/wire_terrain_bridge.h"
#include "play/terrain_render.h"
#include "render/frame.h"
#include "render/surface.h"

#include <cstring>

using namespace guild;

namespace {

// Build a small synthetic floor + a top-down view + an 8bpp framebuffer cleared to
// the background byte. Returns the framebuffer (caller frees) and fills the ctx.
render::Surface* MakeFloorFrame(play::TerrainBridgeContext& ctx, int W, int H,
                                u8 background) {
    ctx.hf = play::Heightfield::MakeSynthetic(/*edge=*/32, /*seed=*/7);
    ctx.view = play::TerrainView::MakeTopDown(ctx.hf.size, ctx.hf.tileSpan, W, H);
    ctx.background = background;
    render::Surface* fb = render::SurfaceCreate(W, H, 8);
    if (fb) std::memset(fb->pixels, background, (size_t)fb->pitch * fb->height);
    ctx.fb = fb;
    return fb;
}

int NonBackground(const render::Surface* fb, u8 background) {
    if (!fb || !fb->pixels) return 0;
    int n = 0;
    for (int y = 0; y < fb->height; ++y) {
        const u8* row = fb->pixels + (size_t)y * fb->widthPx;
        for (int x = 0; x < fb->width; ++x)
            if (row[x] != background) ++n;
    }
    return n;
}

} // namespace

// The swap: inert default -> real leaf, observed on the hook pointer + frame flag.
TEST(WireTerrainBridge, SwapsInertHookToRealLeaf) {
    play::TerrainBridgeContext ctx{};
    render::Surface* fb = MakeFloorFrame(ctx, 96, 72, /*background=*/0);
    CHECK(fb != nullptr);

    render::FrameState fs{};
    render::FrameHooks hooks{};

    // The engine ships the terrain hook inert (null) — the live floor draw no-ops.
    CHECK(hooks.renderTerrain == nullptr);
    CHECK(hooks.terrain == nullptr);
    CHECK(fs.hasTerrain == false);

    // Start from the explicit inert bridge (the "before" baseline).
    play::InstallInertTerrainBridge(fs, hooks, &ctx);
    CHECK(hooks.renderTerrain == &play::InertRenderTerrain);
    CHECK(fs.hasTerrain == true);   // the floor branch IS taken now...

    // ...but the inert hook draws nothing: run the live walk, floor stays blank.
    render::BeginUniverseFrame(fs, hooks, /*a2=*/1);
    CHECK(ctx.drawCount == 1);            // the live walk reached the leaf
    CHECK(ctx.drewReal == false);         // ...but it was the inert no-op
    CHECK_EQ(NonBackground(fb, 0), 0);    // floor is blank

    // DE-INERT: point the hook at the REAL leaf.
    play::TerrainBridgeInstall ins = play::InstallRealTerrainBridge(fs, hooks, &ctx);
    CHECK(ins.installed == true);
    CHECK(ins.wasInert == true);                              // swapped FROM inert
    CHECK(ins.previousHook == (void*)&play::InertRenderTerrain);
    CHECK(hooks.renderTerrain != &play::InertRenderTerrain);  // no longer inert
    CHECK(hooks.renderTerrain != nullptr);
    CHECK(fs.hasTerrain == true);

    render::SurfaceDestroy(fb);
}

// The behavior change: same synthetic input, inert frame is blank, real frame paints
// the tessellated ground (a large non-background fraction + a real shade range).
TEST(WireTerrainBridge, RealLeafPaintsGroundInertDoesNot) {
    const int W = 128, H = 96;

    // --- INERT path ---
    play::TerrainBridgeContext inertCtx{};
    render::Surface* inertFb = MakeFloorFrame(inertCtx, W, H, /*background=*/0);
    CHECK(inertFb != nullptr);
    render::FrameState fsI{}; render::FrameHooks hkI{};
    play::InstallInertTerrainBridge(fsI, hkI, &inertCtx);
    render::BeginUniverseFrame(fsI, hkI, 1);
    int inertNB = NonBackground(inertFb, 0);

    // --- REAL path (de-inerted) ---
    play::TerrainBridgeContext realCtx{};
    render::Surface* realFb = MakeFloorFrame(realCtx, W, H, /*background=*/0);
    CHECK(realFb != nullptr);
    render::FrameState fsR{}; render::FrameHooks hkR{};
    play::InstallRealTerrainBridge(fsR, hkR, &realCtx);
    render::BeginUniverseFrame(fsR, hkR, 1);
    int realNB = NonBackground(realFb, 0);

    // Inert draws nothing; the real leaf paints a large ground fraction.
    CHECK_EQ(inertNB, 0);
    CHECK(realCtx.drewReal == true);
    CHECK(realCtx.lastStats.tilesDrawn == 64);
    CHECK(realCtx.lastStats.quadsBuilt > 0);
    CHECK(realCtx.lastStats.trisDrawn > 0);
    CHECK(realNB > (W * H) / 2);          // > 50% of the frame painted
    CHECK(realNB > inertNB);              // strictly more than the inert frame
    CHECK(realCtx.lastStats.maxShade > realCtx.lastStats.minShade); // real lighting

    render::SurfaceDestroy(inertFb);
    render::SurfaceDestroy(realFb);
}

// MakeLiveTerrainFrame builds a clean live frame with ONLY the terrain hook de-inert
// (the other leaves stay null — disjoint cluster).
TEST(WireTerrainBridge, MakeLiveFrameLeavesOtherHooksInert) {
    play::TerrainBridgeContext ctx{};
    render::Surface* fb = MakeFloorFrame(ctx, 64, 64, /*background=*/0);
    CHECK(fb != nullptr);

    render::FrameState fs{}; render::FrameHooks hooks{};
    play::TerrainBridgeInstall ins = play::MakeLiveTerrainFrame(fs, hooks, &ctx);
    CHECK(ins.installed == true);

    // Terrain hook wired; every OTHER frame leaf is still inert (null).
    CHECK(hooks.renderTerrain != nullptr);
    CHECK(hooks.clearRect == nullptr);
    CHECK(hooks.clearViewport == nullptr);
    CHECK(hooks.sceneWalk == nullptr);
    CHECK(hooks.renderParticles == nullptr);
    CHECK(hooks.buildMirrors == nullptr);
    CHECK(hooks.flushDrawList == nullptr);

    render::BeginUniverseFrame(fs, hooks, 1);
    CHECK(ctx.drewReal == true);
    CHECK(NonBackground(fb, 0) > 0);

    render::SurfaceDestroy(fb);
}
