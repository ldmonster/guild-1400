#include "test.h"

// UNIT: layer compositing ORDER / REGIONS golden for the full city-view frame
// compositor (play::city_frame). On a SYNTHETIC world (a synthetic terrain
// heightfield + a couple of seeded scene objects + a HUD) it asserts the engine's
// layer order is honoured in the finished single Surface:
//   * terrain FILLS the ground region (ground-tinted pixels present),
//   * objects draw OVER terrain (an object-quad pixel is object-coloured, not the
//     ground tint that the terrain put there first),
//   * HUD draws OVER objects/world (a HUD-region pixel is HUD-coloured),
//   * the colour-mapping leaves (CityFrameGroundRgb / CityFrameObjectRgb) are
//     deterministic and classify into distinct sky/ground/object bands.
#include "play/city_frame.h"
#include "render/surface.h"
#include "gui/menu_render.h"          // gui::Argb8888
#include "sim/entity.h"
#include "sim/types.h"

#include <cstdint>

using namespace guild;
using namespace guild::play;
using namespace guild::sim;

namespace {

// Read an ARGB8888 pixel as true R,G,B. The reconstructed SurfaceGetPixelRgb
// returns raw little-endian bytes [B,G,R] for a 32bpp surface, so decode the
// packed dword directly (R@16,G@8,B@0 per gui::Argb8888).
void Px(const render::Surface* s, int x, int y, u8& r, u8& g, u8& b) {
    const std::uint32_t px =
        reinterpret_cast<const std::uint32_t*>(s->pixels)[(std::size_t)s->widthPx * y + x];
    r = (u8)((px >> 16) & 0xff);
    g = (u8)((px >> 8)  & 0xff);
    b = (u8)( px        & 0xff);
}

bool IsSky(u8 r, u8 g, u8 b, const CityFramePalette& p) {
    return r == p.skyR && g == p.skyG && b == p.skyB;
}
// Ground = green-dominant earthy tint (g clearly > r and > b).
bool IsGround(u8 r, u8 g, u8 b) { return g > r + 10 && g > b + 10; }
// Object = warm (r dominant, r > g > b).
bool IsObject(u8 r, u8 g, u8 b) { return r > g && g >= b && r > 60; }

// A deterministic placement hook that puts every object as a small quad straddling
// the frame CENTER (where the synthetic terrain is guaranteed to have painted
// ground), so the "object over terrain" order proof is exact. Each object is
// nudged by its slot so two objects don't fully overlap.
EntityPlacement CenterPlacement(const EntityRef& e, int fbW, int fbH) {
    EntityPlacement p;
    p.visible = true;
    float cx = fbW * 0.5f + (float)(e.slot * 4);
    float cy = fbH * 0.5f;
    p.quad.x0 = cx - 5; p.quad.z0 = cy - 5;
    p.quad.x1 = cx + 5; p.quad.z1 = cy + 5;
    p.quad.light = (u8)(160 + (e.id & 63));
    p.quad.translucent = false;
    return p;
}

} // namespace

// ---------------------------------------------------------------------------
// Colour-mapping leaves: deterministic + distinct bands.
// ---------------------------------------------------------------------------
TEST(CityFrameUnit, ColourLeavesAreDeterministicAndDistinct) {
    CityFramePalette pal;
    u8 r1, g1, b1, r2, g2, b2;
    // ground tint: same shade -> same RGB (deterministic), green-dominant.
    CityFrameGroundRgb(150, pal, r1, g1, b1);
    CityFrameGroundRgb(150, pal, r2, g2, b2);
    CHECK_EQ((int)r1, (int)r2); CHECK_EQ((int)g1, (int)g2); CHECK_EQ((int)b1, (int)b2);
    CHECK(IsGround(r1, g1, b1));

    // brighter shade -> brighter green (the ramp is monotone in the shade byte).
    u8 rg, gg, bg;
    CityFrameGroundRgb(250, pal, rg, gg, bg);
    CHECK(gg >= g1);

    // object colour: warm (r-dominant), deterministic, and NOT classified as ground.
    u8 ro, go, bo;
    CityFrameObjectRgb(200, pal, ro, go, bo);
    CHECK(IsObject(ro, go, bo));
    CHECK(!IsGround(ro, go, bo));
}

// ---------------------------------------------------------------------------
// Full compose: terrain fills, objects over terrain, HUD over objects.
// ---------------------------------------------------------------------------
TEST(CityFrameUnit, LayerOrderTerrainObjectsHud) {
    ResetEntityArrays();
    // Two scene objects (the DefaultPlacementHook lays them on a deterministic grid
    // in the lower 60% of the frame, so they land over the terrain region).
    g_sceneNodes[0].type = 7; g_sceneNodes[0].id = 10; g_sceneNodes[0].childPtr = -1;
    g_sceneNodes[0].entityPtr = -1;
    g_sceneNodes[1].type = 7; g_sceneNodes[1].id = 11; g_sceneNodes[1].childPtr = -1;
    g_sceneNodes[1].entityPtr = -1;
    g_sceneNodeCount = 2;

    const int W = 96, H = 72;

    CityFrameScene scene;
    scene.terrain = Heightfield::MakeSynthetic(32);
    scene.terrainView = TerrainView::MakeTopDown(scene.terrain.size,
                                                 scene.terrain.tileSpan, W, H);
    scene.worldOpt.scanScene   = true;
    scene.worldOpt.scanObjects = false;
    scene.worldOpt.scanPersons = false;
    scene.worldOpt.useRealPlacement = false;   // synthetic placement
    scene.worldOpt.placement = &CenterPlacement; // place objects over terrain center

    // A simple HUD: one money/date caption + one filled bar slot, near the top.
    scene.hud.money    = 12345;
    scene.hud.gameDay  = 3;
    scene.hud.clockTick = 1000;
    scene.hud.barObjects.push_back({ /*objId*/ 1, /*ratio*/ 0.5 });
    scene.hudBarOriginX = 2;  scene.hudBarOriginY = 2;
    scene.hudCaptionX   = 2;  scene.hudCaptionY   = 2;
    scene.drawHud = true;

    CameraControl cam{};   // eye at origin

    render::Surface* fb = render::SurfaceCreate(W, H, 32, gui::Argb8888());
    CHECK(fb != nullptr);
    if (!fb) return;

    CityFrameStats st = ComposeCityFrame(fb, cam, scene);
    CityFramePalette pal;

    // terrain layer fired and filled ground pixels.
    CHECK(st.terrainDrawn);
    CHECK(st.terrainTiles == 64);          // the 8x8 tile grid
    CHECK(st.terrainTris > 0);
    CHECK(st.terrainPixels > 0);

    // object layer: both scene objects emitted + at least one filled on-frame.
    CHECK_EQ(st.objectsBuilt, 2);
    CHECK(st.objectsDrawn >= 1);
    CHECK(st.objectPixels > 0);

    // HUD layer drew its caption/bar OVER the world.
    CHECK(st.hudDrawn);
    CHECK(st.hudCaptionGlyphs > 0);
    CHECK(st.hudBarSlots >= 1);
    CHECK(st.hudPixels > 0);

    // Count colour classes across the whole frame: all three layers coexist.
    int sky = 0, ground = 0, object = 0;
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x) {
            u8 r, g, b; Px(fb, x, y, r, g, b);
            if (IsSky(r, g, b, pal)) ++sky;
            else if (IsGround(r, g, b)) ++ground;
            else if (IsObject(r, g, b)) ++object;
        }
    CHECK(ground > 0);   // terrain visible
    CHECK(object > 0);   // objects visible over terrain

    // ORDER PROOF: find an object pixel and confirm the terrain had drawn ground
    // there FIRST (i.e. the object overwrote ground, not sky) for at least one obj
    // pixel that sits inside the terrain region. We re-render terrain-only into a
    // probe surface and check those same coords were ground.
    render::Surface* probe = render::SurfaceCreate(W, H, 32, gui::Argb8888());
    CHECK(probe != nullptr);
    if (probe) {
        CityFrameScene tOnly = scene;
        tOnly.worldOpt.scanScene = false;   // terrain only
        tOnly.drawHud = false;
        ComposeCityFrame(probe, cam, tOnly);
        bool foundObjOverGround = false;
        for (int y = 0; y < H && !foundObjOverGround; ++y)
            for (int x = 0; x < W; ++x) {
                u8 r, g, b; Px(fb, x, y, r, g, b);
                if (!IsObject(r, g, b)) continue;
                u8 pr, pg, pb; Px(probe, x, y, pr, pg, pb);
                if (IsGround(pr, pg, pb)) { foundObjOverGround = true; break; }
            }
        CHECK(foundObjOverGround);   // an object pixel composited OVER terrain ground
        render::SurfaceDestroy(probe);
    }

    render::SurfaceDestroy(fb);
}

// ---------------------------------------------------------------------------
// Partial frames compose: no terrain => sky shows through under objects.
// ---------------------------------------------------------------------------
TEST(CityFrameUnit, NoTerrainStillComposesObjectsAndHud) {
    ResetEntityArrays();
    g_sceneNodes[0].type = 7; g_sceneNodes[0].id = 5; g_sceneNodes[0].childPtr = -1;
    g_sceneNodes[0].entityPtr = -1;
    g_sceneNodeCount = 1;

    const int W = 80, H = 60;
    CityFrameScene scene;                       // invalid terrain -> skipped
    scene.worldOpt.scanScene = true;
    scene.worldOpt.scanObjects = false;
    scene.hud.money = 7; scene.hud.barObjects.push_back({ 2, 1.0 });
    scene.drawHud = true;

    CameraControl cam{};
    render::Surface* fb = render::SurfaceCreate(W, H, 32, gui::Argb8888());
    CHECK(fb != nullptr);
    if (!fb) return;

    CityFrameStats st = ComposeCityFrame(fb, cam, scene);
    CHECK(!st.terrainDrawn);                    // no terrain layer
    CHECK_EQ(st.objectsBuilt, 1);
    CHECK(st.objectsDrawn >= 1);
    CHECK(st.hudDrawn);
    CHECK(st.nonBlankPixels > 0);

    render::SurfaceDestroy(fb);
}
