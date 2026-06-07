// tests/integration/wire_terrain_bridge_itest.cpp — P6 terrain on the LIVE path.
//
// Renders a small synthetic terrain through the WHOLE live frame entry
// (render::RenderMainViewFrame -> RenderUniverseFrame -> BeginUniverseFrame, the
// real reconstructed orchestration) with the terrain bridge installed vs NOT, and
// asserts the output differs: terrain pixels appear that were absent on the inert
// frame, and the live walk actually reached the real floor leaf.
#include "test.h"

#include "play/wire_terrain_bridge.h"
#include "play/terrain_render.h"
#include "render/frame.h"
#include "render/surface.h"

#include <cstring>

using namespace guild;

namespace {

render::Surface* MakeFb(int W, int H, u8 bg) {
    render::Surface* fb = render::SurfaceCreate(W, H, 8);
    if (fb) std::memset(fb->pixels, bg, (size_t)fb->pitch * fb->height);
    return fb;
}

int NonBg(const render::Surface* fb, u8 bg) {
    if (!fb || !fb->pixels) return 0;
    int n = 0;
    for (int y = 0; y < fb->height; ++y) {
        const u8* row = fb->pixels + (size_t)y * fb->widthPx;
        for (int x = 0; x < fb->width; ++x) if (row[x] != bg) ++n;
    }
    return n;
}

// Run the FULL live frame entry (RenderMainViewFrame) over a synthetic floor with
// the given install mode. Returns the rendered framebuffer (caller frees).
render::Surface* RunLiveFrame(bool deInert, int W, int H,
                              play::TerrainBridgeContext& ctx, int* outNonBg) {
    render::Surface* fb = MakeFb(W, H, /*bg=*/0);
    if (!fb) { if (outNonBg) *outNonBg = -1; return nullptr; }

    ctx.fb = fb;
    ctx.hf = play::Heightfield::MakeSynthetic(/*edge=*/40, /*seed=*/123);
    ctx.view = play::TerrainView::MakeTopDown(ctx.hf.size, ctx.hf.tileSpan, W, H);
    ctx.background = 0;

    render::FrameState fs{};
    render::FrameHooks hooks{};
    if (deInert)
        play::InstallRealTerrainBridge(fs, hooks, &ctx);
    else
        play::InstallInertTerrainBridge(fs, hooks, &ctx);

    // The REAL live frame entry: gate -> (clear) -> RenderUniverseFrame ->
    // BeginUniverseFrame (which invokes hooks.renderTerrain when hasTerrain).
    render::RenderMainViewFrame(fs, hooks);

    if (outNonBg) *outNonBg = NonBg(fb, 0);
    return fb;
}

} // namespace

TEST(WireTerrainBridgeITest, LiveFrameDiffersWithBridge) {
    const int W = 160, H = 120;

    play::TerrainBridgeContext inertCtx{};
    int inertNB = -2;
    render::Surface* inertFb = RunLiveFrame(/*deInert=*/false, W, H, inertCtx, &inertNB);
    CHECK(inertFb != nullptr);

    play::TerrainBridgeContext realCtx{};
    int realNB = -2;
    render::Surface* realFb = RunLiveFrame(/*deInert=*/true, W, H, realCtx, &realNB);
    CHECK(realFb != nullptr);

    // Both frames went through the same live entry; only the inert-vs-real terrain
    // hook differs. The live walk reached the leaf in BOTH (drawCount bumped).
    CHECK(inertCtx.drawCount == 1);
    CHECK(realCtx.drawCount == 1);
    CHECK(inertCtx.drewReal == false);
    CHECK(realCtx.drewReal == true);

    // The output DIFFERS: inert is blank, real has the tessellated ground.
    CHECK_EQ(inertNB, 0);
    CHECK(realNB > 0);
    CHECK(realNB != inertNB);
    CHECK(realCtx.lastStats.tilesDrawn == 64);
    CHECK(realCtx.lastStats.trisDrawn > 0);

    // Per-pixel: at least one pixel painted on the real frame that is blank on inert.
    if (inertFb && realFb) {
        int newlyPainted = 0;
        for (int y = 0; y < H; ++y) {
            const u8* ri = inertFb->pixels + (size_t)y * inertFb->widthPx;
            const u8* rr = realFb->pixels  + (size_t)y * realFb->widthPx;
            for (int x = 0; x < W; ++x)
                if (ri[x] == 0 && rr[x] != 0) ++newlyPainted;
        }
        CHECK(newlyPainted > (W * H) / 2);
    }

    render::SurfaceDestroy(inertFb);
    render::SurfaceDestroy(realFb);
}

// Determinism: the de-inerted live frame is byte-identical across two runs.
TEST(WireTerrainBridgeITest, LiveFrameDeterministic) {
    const int W = 96, H = 96;
    play::TerrainBridgeContext c1{}, c2{};
    int nb1 = 0, nb2 = 0;
    render::Surface* fb1 = RunLiveFrame(true, W, H, c1, &nb1);
    render::Surface* fb2 = RunLiveFrame(true, W, H, c2, &nb2);
    CHECK(fb1 != nullptr);
    CHECK(fb2 != nullptr);
    CHECK_EQ(nb1, nb2);
    if (fb1 && fb2) {
        bool identical = true;
        for (int y = 0; y < H && identical; ++y) {
            const u8* r1 = fb1->pixels + (size_t)y * fb1->widthPx;
            const u8* r2 = fb2->pixels + (size_t)y * fb2->widthPx;
            for (int x = 0; x < W; ++x)
                if (r1[x] != r2[x]) { identical = false; break; }
        }
        CHECK(identical);
    }
    render::SurfaceDestroy(fb1);
    render::SurfaceDestroy(fb2);
}
