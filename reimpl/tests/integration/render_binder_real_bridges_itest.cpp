#include "test.h"

// INTEGRATION: render a SMALL SYNTHETIC WORLD through the REAL RenderBinder live
// frame with the real bridges ON, and assert the framebuffer gains terrain + mesh
// + HUD content the default frame lacks — composited through the REAL leaves:
//   render::RenderMainViewFrame  (the live frame entry)
//     -> renderTerrain : play::RenderTerrain (real per-tile-lit floor) -> fb
//     -> sceneWalk     : render::ProcessSceneNodeAppend (real node dispatch) + sort
//     -> flushDrawList : render::RasterizeMeshList (real software raster)
//   -> HUD sprite      : render::ShapeShowFromBank (real 2D sprite-bank blit)
//   -> render::PresentFrame -> shim::MemoryGraphicsDevice (headless capture)
// Plus a supplied synthetic Heightfield/TerrainView (SetTerrain) to prove the real
// floor leaf draws caller-provided ground. Deterministic across renders.
#include "play/render_binder.h"
#include "play/terrain_render.h"
#include "render/surface.h"
#include "render/colorformat.h"
#include "shim_impl/memory_graphics.h"

#include <cstdint>
#include <vector>

using namespace guild;
using namespace guild::play;

namespace {

std::vector<std::uint8_t> SnapFb(const render::Surface* s) {
    std::vector<std::uint8_t> out;
    if (!s || !s->pixels) return out;
    std::size_t bytes = (std::size_t)s->pitch * (std::size_t)s->height;
    out.assign(s->pixels, s->pixels + bytes);
    return out;
}

// Read a 16bpp pixel.
u16 Px16(const render::Surface* s, int x, int y) {
    const u16* p = reinterpret_cast<const u16*>(s->pixels);
    return p[(s->widthPx * y) + x];
}

LoadedWorld SmallWorld() {
    LoadedWorld w = LoadedWorld::MakeDefault();
    w.fbW = 80; w.fbH = 64;
    return w;
}

} // namespace

// --- the real-bridge frame composites terrain + mesh + HUD the default lacks ---
TEST(RenderBinderRealBridgesItest, RealFrameHasTerrainMeshHud) {
    LoadedWorld w = SmallWorld();

    // Default frame (bridges off).
    shim::MemoryGraphicsDevice dDef;
    CHECK(dDef.init(w.fbW, w.fbH, 16, false));
    RenderBinder def;
    CHECK(def.load(w));
    RenderStats sDef = def.renderFrame(dDef);
    int defNonClear = def.nonClearPixels();

    // Real-bridge frame with a CALLER-SUPPLIED synthetic heightfield + view.
    Heightfield hf = Heightfield::MakeSynthetic(/*edge=*/32, /*seed=*/0x51u);
    TerrainView view = TerrainView::MakeTopDown(hf.size, hf.tileSpan, w.fbW, w.fbH);

    shim::MemoryGraphicsDevice dReal;
    CHECK(dReal.init(w.fbW, w.fbH, 16, false));
    RenderBinder real;
    real.SetRealBridges(true);
    real.SetTerrain(hf, view);
    CHECK(real.load(w));
    RenderStats sReal = real.renderFrame(dReal);

    // The default frame had NO terrain / scene-dispatch / mesh / sprite content.
    CHECK_EQ(sDef.terrainTris, 0);
    CHECK_EQ(sDef.sceneDispatched, 0);
    CHECK_EQ(sDef.meshTris, 0);
    CHECK_EQ(sDef.hudSprites, 0);

    // The real frame DID: the supplied floor drew, the node dispatched, the
    // multi-tri mesh fed it, the HUD sprite blitted.
    CHECK(sReal.realBridges);
    CHECK(sReal.terrainTiles == 64);
    CHECK(sReal.terrainTris > 0);
    CHECK(sReal.terrainPixels > 0);
    CHECK(sReal.sceneDispatched > 0);
    CHECK(sReal.meshTris > 2);
    CHECK_EQ(sReal.hudSprites, 1);

    // The composited frame is substantially more non-blank than the default.
    CHECK(real.nonClearPixels() > defNonClear);

    // The REAL floor painted the lower ground band that the default fake-quad
    // scene leaves entirely at the clear colour: count painted pixels in the
    // lower half of the frame for BOTH frames and assert the real frame has them
    // where the default has (near) none.
    render::Surface* rfb = real.framebuffer();
    render::Surface* dfb = def.framebuffer();
    CHECK(rfb != nullptr);
    CHECK(dfb != nullptr);
    if (rfb && dfb) {
        u16 clear = (u16)render::PackColor(rfb->fmt, w.clearR, w.clearG, w.clearB);
        int realLower = 0, defLower = 0;
        for (int y = w.fbH / 2; y < w.fbH; ++y)
            for (int x = 0; x < w.fbW; ++x) {
                if (Px16(rfb, x, y) != clear) ++realLower;
                if (Px16(dfb, x, y) != clear) ++defLower;
            }
        CHECK(realLower > 0);          // the real floor painted the lower band
        CHECK(realLower > defLower);   // ...where the default frame did not
    }

    // The device captured a non-blank frame.
    const std::vector<std::uint8_t>& shown = dReal.lastPresented();
    CHECK(!shown.empty());
    bool nonBlank = false;
    for (std::uint8_t b : shown) if (b) { nonBlank = true; break; }
    CHECK(nonBlank);
}

// --- the real-bridge synthetic frame is byte-deterministic ---------------------
TEST(RenderBinderRealBridgesItest, RealFrameDeterministic) {
    LoadedWorld w = SmallWorld();
    Heightfield hf = Heightfield::MakeSynthetic(32, 0x51u);
    TerrainView view = TerrainView::MakeTopDown(hf.size, hf.tileSpan, w.fbW, w.fbH);

    shim::MemoryGraphicsDevice d1, d2;
    CHECK(d1.init(w.fbW, w.fbH, 16, false));
    CHECK(d2.init(w.fbW, w.fbH, 16, false));

    RenderBinder a, b;
    a.SetRealBridges(true); a.SetTerrain(hf, view);
    b.SetRealBridges(true); b.SetTerrain(hf, view);
    CHECK(a.load(w));
    CHECK(b.load(w));
    a.renderFrame(d1);
    b.renderFrame(d2);

    std::vector<std::uint8_t> fa = SnapFb(a.framebuffer());
    std::vector<std::uint8_t> fb = SnapFb(b.framebuffer());
    CHECK(!fa.empty());
    CHECK(fa.size() == fb.size());
    CHECK(fa == fb);
    CHECK(d1.lastPresented() == d2.lastPresented());

    // Re-rendering the SAME binder is stable.
    a.renderFrame(d1);
    std::vector<std::uint8_t> fa2 = SnapFb(a.framebuffer());
    CHECK(fa == fa2);
}
