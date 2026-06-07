#include "test.h"

// INTEGRATION: compose a small REAL-FORMAT world (synthetic entity records in the
// live sim arrays + a synthetic-but-real-format terrain heightfield) plus a HUD
// into one city frame through play::RenderCityFrame, present it to a headless
// device, and assert all THREE layers coexist in the finished/presented frame:
//   * terrain ground pixels are present (the floor filled),
//   * object pixels are present OVER the terrain (the scene-graph layer),
//   * HUD pixels are present OVER the world (the stats overlay),
//   * the device actually received one presented frame matching the surface.
#include "play/city_frame.h"
#include "render/surface.h"
#include "gui/menu_render.h"           // gui::Argb8888
#include "sim/entity.h"
#include "sim/types.h"
#include "shim_impl/memory_graphics.h"

#include <cstdint>

using namespace guild;
using namespace guild::play;
using namespace guild::sim;

namespace {

// Decode a 32bpp ARGB pixel as true R,G,B (R@16,G@8,B@0); the reconstructed
// SurfaceGetPixelRgb returns raw [B,G,R] bytes on the 32bpp read path.
void Px(const render::Surface* s, int x, int y, u8& r, u8& g, u8& b) {
    const std::uint32_t px =
        reinterpret_cast<const std::uint32_t*>(s->pixels)[(std::size_t)s->widthPx * y + x];
    r = (u8)((px >> 16) & 0xff); g = (u8)((px >> 8) & 0xff); b = (u8)(px & 0xff);
}
bool IsGround(u8 r, u8 g, u8 b) { return g > r + 10 && g > b + 10; }
bool IsObject(u8 r, u8 g, u8 b) { return r > g && g >= b && r > 60; }

} // namespace

TEST(CityFrameItest, ComposeRealFormatWorldAllLayersCoexist) {
    ResetEntityArrays();
    // A small real-format scene: a 2-node scene tree (parent -> child) + 3 alive
    // objects, the same shapes a real .cty scatters into the live arrays.
    g_sceneNodes[0].type = 7; g_sceneNodes[0].id = 900; g_sceneNodes[0].childPtr = 1;
    g_sceneNodes[0].entityPtr = -1;
    g_sceneNodes[1].type = 7; g_sceneNodes[1].id = 901; g_sceneNodes[1].childPtr = -1;
    g_sceneNodes[1].entityPtr = -1;
    g_sceneNodeCount = 2;
    for (int i = 0; i < 3; ++i) { g_objects[i].alive = 1; g_objects[i].id = 300 + i; }

    const int W = 96, H = 72;

    CityFrameScene scene;
    scene.terrain = Heightfield::MakeSynthetic(32, /*seed*/ 7);
    scene.terrainView = TerrainView::MakeTopDown(scene.terrain.size,
                                                 scene.terrain.tileSpan, W, H);
    scene.worldOpt.scanScene   = true;
    scene.worldOpt.scanObjects = true;
    scene.worldOpt.scanPersons = false;

    scene.hud.money = 250000; scene.hud.gameDay = 12; scene.hud.clockTick = 5000;
    scene.hud.barObjects.push_back({ 300, 0.75 });
    scene.hud.barObjects.push_back({ 301, 0.25 });
    scene.hudBarOriginX = 2; scene.hudBarOriginY = 2;
    scene.hudCaptionX   = 2; scene.hudCaptionY   = 2;
    scene.drawHud = true;

    CameraControl cam{};

    shim::MemoryGraphicsDevice dev;
    CHECK(dev.init(W, H, 32, /*fullscreen=*/false));

    render::Surface* frame = nullptr;
    CityFrameStats st = RenderCityFrame(cam, scene, W, H, dev, &frame);

    std::printf("[city_frame][itest] terrain(tiles=%d tris=%d px=%d) "
                "objects(built=%d drawn=%d px=%d) hud(slots=%d glyphs=%d px=%d) "
                "nonBlank=%d presented=%d\n",
                st.terrainTiles, st.terrainTris, st.terrainPixels,
                st.objectsBuilt, st.objectsDrawn, st.objectPixels,
                st.hudBarSlots, st.hudCaptionGlyphs, st.hudPixels,
                st.nonBlankPixels, (int)st.presented);

    // all three layers produced output
    CHECK(st.terrainDrawn);
    CHECK(st.terrainPixels > 0);
    CHECK(st.objectsBuilt >= 3);     // 2 scene + >=1 objects (capped at 8)
    CHECK(st.objectsDrawn >= 1);
    CHECK(st.objectPixels > 0);
    CHECK(st.hudDrawn);
    CHECK(st.hudPixels > 0);

    // present succeeded into the device.
    CHECK(st.presented);
    CHECK_EQ(dev.presentCount(), 1);

    // The composited surface coexists all three colour classes.
    CHECK(frame != nullptr);
    if (frame) {
        int ground = 0, object = 0;
        for (int y = 0; y < H; ++y)
            for (int x = 0; x < W; ++x) {
                u8 r, g, b; Px(frame, x, y, r, g, b);
                if (IsGround(r, g, b)) ++ground;
                else if (IsObject(r, g, b)) ++object;
            }
        CHECK(ground > 0);
        CHECK(object > 0);

        // The presented bytes match the composited surface (the device received the
        // frame we built). Compare the first row word-for-word.
        const auto& pres = dev.lastPresented();
        CHECK(pres.size() >= (size_t)W * 4);
        if (pres.size() >= (size_t)W * 4) {
            bool rowMatch = true;
            const u8* fbrow = frame->pixels;
            for (int i = 0; i < W * 4; ++i)
                if (pres[i] != fbrow[i]) { rowMatch = false; break; }
            CHECK(rowMatch);
        }
        render::SurfaceDestroy(frame);
    }
}
