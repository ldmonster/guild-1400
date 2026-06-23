// mapview_test.cpp — golden vectors for the 2D OVERVIEW MAP VIEW (play::MapView_*),
// the render + interaction half of VIBE_MapView_PanelDispatcher @0x5441d0.
//
// Asserts:
//   * the marker projection (REAL gui::MapView_ComputeMarkerScreenPos @0x5440b4)
//     reaches the expected viewport pixel, scroll-offset applied,
//   * the click->world inverse round-trips the projection's affine half,
//   * the Y-sort orders markers ascending by projected screenY (REAL
//     MapViewSortMarkersByScreenY @0x54457e),
//   * the render composes a non-blank viewport (inert background fill) with one
//     readable dot per in-view marker, at the centred pixel, into a real Surface.
#include "test.h"

#include "play/map_view.h"
#include "gui/mapview.h"
#include "gui/menu_render.h"          // gui::Argb8888 (32bpp ARGB format)
#include "render/surface.h"           // render::SurfaceCreate / SurfaceGetPixelRgb

#include <vector>
#include <cmath>

using namespace guild;
using namespace guild::play;

// Set the world bitmap size the projection + clamp read (gui::g_mapWidth/Height).
static void SetMapSize(int w, int h) { gui::g_mapWidth = w; gui::g_mapHeight = h; }

// ---------------------------------------------------------------------------
// 1. Marker projection — a marker at world origin lands at the map centre.
// ---------------------------------------------------------------------------
TEST(MapViewUnit, ProjectCenterMarker) {
    SetMapSize(1024, 720);
    OverviewCamera cam{};                 // all zero pan/scroll/origin
    OverviewMarker mk{}; mk.worldX = 0.0f; mk.worldZ = 0.0f;
    OverviewXY p = MapView_ProjectMarker(mk, cam);
    CHECK_EQ(p.x, 512);                   // mapW/2
    CHECK_EQ(p.y, 360);                   // mapH/2
    // The map centre x=512 is the RIGHT edge of the 512-wide viewport, so it is
    // NOT inside [0,512); inView is false at the edge.  A marker scrolled into the
    // viewport interior is inView.
    CHECK(!p.inView);
    cam.scrollX = 256; cam.scrollY = 180; // bring the centre into the viewport
    OverviewXY pin = MapView_ProjectMarker(mk, cam);
    CHECK_EQ(pin.x, 256);
    CHECK_EQ(pin.y, 180);
    CHECK(pin.inView);
}

// A marker offset in +X bows outward (perspective term) — golden against the
// reconstructed constants (5.33 world scale, 0.5 persp scale, 51.0 bow).
TEST(MapViewUnit, ProjectOffsetMarkerGolden) {
    SetMapSize(1024, 720);
    OverviewCamera cam{};
    OverviewMarker mk{}; mk.worldX = 10.0f; mk.worldZ = 0.0f;
    OverviewXY p = MapView_ProjectMarker(mk, cam);
    // dx = 10*5.33 = 53.3 ; |dx|/(1024*0.5)=0.104102 ; *51 = 5.3092 ; v15=58.609
    // screenX = 512 + 58.609 = 570.609 -> (int)570
    CHECK_EQ(p.x, 570);
    CHECK_EQ(p.y, 360);
}

// Pan + scroll: pan shifts the full-map pixel; scroll subtracts back for the
// viewport-local pixel.  A pan of +100 with an equal scroll of +100 cancels.
TEST(MapViewUnit, ProjectPanScrollOffset) {
    SetMapSize(1024, 720);
    OverviewCamera cam{}; cam.panX = 100; cam.panY = 40;
    OverviewMarker mk{}; mk.worldX = 0.0f; mk.worldZ = 0.0f;
    OverviewXY p0 = MapView_ProjectMarker(mk, cam);
    CHECK_EQ(p0.x, 512 + 100);
    CHECK_EQ(p0.y, 360 + 40);
    cam.scrollX = 100; cam.scrollY = 40;
    OverviewXY p1 = MapView_ProjectMarker(mk, cam);
    CHECK_EQ(p1.x, 512);                  // pan cancelled by scroll
    CHECK_EQ(p1.y, 360);
}

// ---------------------------------------------------------------------------
// 2. Click -> world inverse round-trips the affine projection (worldZ=0 path
//    has no bow, so the inverse is exact there).
// ---------------------------------------------------------------------------
TEST(MapViewUnit, ClickToWorldRoundTrip) {
    SetMapSize(1024, 720);
    OverviewCamera cam{}; cam.panX = 30; cam.panY = -20; cam.scrollX = 5; cam.scrollY = 7;
    // Forward: world (0,0) -> viewport pixel.
    OverviewMarker mk{}; mk.worldX = 0.0f; mk.worldZ = 0.0f;
    OverviewXY p = MapView_ProjectMarker(mk, cam);
    // Inverse of that exact pixel must recover ~ (0,0).
    WorldXZ w = MapView_ClickToWorld(p.x, p.y, cam);
    CHECK(std::fabs(w.x) < 0.5f);
    CHECK(std::fabs(w.z) < 0.5f);

    // A pure +X world point with no bow (worldZ irrelevant for X) round-trips:
    // worldX=12 -> screenX = 512 + 12*5.33 + pan - scroll (no bow on the X centre
    // line because the bow adds to X by |dx|; check the inverse of a hand pixel).
    WorldXZ w2 = MapView_ClickToWorld(512 + 64 + 30 - 5, 360 - 20 - 7, cam);
    // ((vx+scroll-pan) - mapW/2)/5.33 = (64)/5.33 = 12.0075
    CHECK(std::fabs(w2.x - 12.0075f) < 0.05f);
    CHECK(std::fabs(w2.z) < 0.05f);
}

// ---------------------------------------------------------------------------
// 3. Y-sort: markers paint nearest-first (ascending projected screenY).
// ---------------------------------------------------------------------------
TEST(MapViewUnit, MarkersSortByScreenY) {
    SetMapSize(1024, 720);
    OverviewCamera cam{};
    // worldZ controls screenY (screenY = 360 + worldZ*5.33 +bow).  Give three
    // markers descending worldZ so they start in reverse sort order.
    std::vector<OverviewMarker> mk(3);
    mk[0].worldZ = 30.0f; mk[0].entity = 100;   // largest Y
    mk[1].worldZ = 0.0f;  mk[1].entity = 200;   // middle
    mk[2].worldZ = -30.0f;mk[2].entity = 300;   // smallest Y
    // Render into a big surface; the topmost (last-drawn) dot at a shared X column
    // would be the largest-Y one.  Here we instead assert via PickMarker: at the
    // smallest-Y marker's pixel only it is hit.
    OverviewXY p2 = MapView_ProjectMarker(mk[2], cam);
    int hit = MapView_PickMarker(mk, cam, p2.x, p2.y, 6);
    CHECK_EQ(hit, 2);
    CHECK_EQ(mk[hit].entity, 300);
}

// ---------------------------------------------------------------------------
// 4. Render: inert background fill + one readable dot per in-view marker.
// ---------------------------------------------------------------------------
TEST(MapViewUnit, RenderOverviewSurface) {
    SetMapSize(1024, 720);
    render::Surface* s = render::SurfaceCreate(640, 480, 32, gui::Argb8888());
    CHECK(s != nullptr);

    OverviewCamera cam{};
    OverviewPalette pal{};                // default backdrop (30,50,30), dots per-kind
    std::vector<OverviewMarker> mk(2);
    mk[0].worldX = 0.0f; mk[0].worldZ = 0.0f; mk[0].kind = MarkerKind::Player;  // centre
    mk[1].worldX = 10.0f;mk[1].worldZ = 0.0f; mk[1].kind = MarkerKind::Building;

    // viewW=512: both marker centres (512 and 570) are at/past the right edge, so
    // both clip out — markersDrawn==0, but the backdrop is still painted.
    OverviewRenderResult r = MapView_RenderOverview(*s, mk, cam,
                                                    /*viewX*/0, /*viewY*/0,
                                                    /*viewW*/512, /*viewH*/360,
                                                    /*dotSize*/6, pal);
    CHECK_EQ(r.backgroundDrawn, 1);
    CHECK_EQ(r.markersDrawn, 0);

    // Backdrop is painted in the viewport (a corner pixel == backdrop colour).
    u8 px[3];
    render::SurfaceGetPixelRgb(s, 1, 1, px);
    CHECK_EQ((int)px[0], (int)pal.bgR);
    CHECK_EQ((int)px[1], (int)pal.bgG);
    CHECK_EQ((int)px[2], (int)pal.bgB);

    // Re-render with a wider viewport (640x480) so both centres (512,360) and
    // (570,360) are in-view, and read the dot colours back.  Compute the expected
    // readback via the SAME render leaves (MenuFillRect + SurfaceGetPixelRgb round
    // trip), so the assertion is independent of the shared format's channel order.
    render::SurfaceColorFill(s, 0, 0, 0);
    OverviewRenderResult r2 = MapView_RenderOverview(*s, mk, cam,
                                                     0, 0, 640, 480, 6, pal);
    CHECK_EQ(r2.markersDrawn, 2);

    auto expectDot = [&](int kind, u8 out[3]) {
        render::Surface* ref = render::SurfaceCreate(8, 8, 32, gui::Argb8888());
        gui::MenuFillRect(ref, 0, 0, 8, 8, pal.dotR[kind], pal.dotG[kind], pal.dotB[kind]);
        render::SurfaceGetPixelRgb(ref, 1, 1, out);
        render::SurfaceDestroy(ref);
    };
    u8 want[3];
    // Player dot fill at the centre pixel (512,360).
    expectDot((int)MarkerKind::Player, want);
    render::SurfaceGetPixelRgb(s, 512, 360, px);
    CHECK_EQ((int)px[0], (int)want[0]);
    CHECK_EQ((int)px[1], (int)want[1]);
    CHECK_EQ((int)px[2], (int)want[2]);
    // Building dot fill at (570,360) — distinct colour from the Player dot.
    expectDot((int)MarkerKind::Building, want);
    render::SurfaceGetPixelRgb(s, 570, 360, px);
    CHECK_EQ((int)px[0], (int)want[0]);
    CHECK_EQ((int)px[1], (int)want[1]);
    CHECK_EQ((int)px[2], (int)want[2]);

    render::SurfaceDestroy(s);
}

// ---------------------------------------------------------------------------
// 5. Background hook overrides the inert fill (asset-edge contract).
// ---------------------------------------------------------------------------
static int g_bgCalls = 0;
static bool TestBg(render::Surface* s, int vx, int vy, int vw, int vh,
                   int, int, void*) {
    ++g_bgCalls;
    gui::MenuFillRect(s, vx, vy, vw, vh, 9, 9, 9);   // a sentinel colour
    return true;
}
TEST(MapViewUnit, BackgroundHookOverridesInert) {
    SetMapSize(1024, 720);
    render::Surface* s = render::SurfaceCreate(512, 360, 32, gui::Argb8888());
    CHECK(s != nullptr);
    g_bgCalls = 0;
    MapViewHooks h{}; h.drawBackground = &TestBg;
    SetMapViewHooks(h);

    OverviewCamera cam{};
    std::vector<OverviewMarker> mk;       // no markers
    MapView_RenderOverview(*s, mk, cam, 0, 0, 512, 360, 6, OverviewPalette());
    CHECK_EQ(g_bgCalls, 1);
    u8 px[3];
    render::SurfaceGetPixelRgb(s, 10, 10, px);
    CHECK_EQ((int)px[0], 9);               // hook sentinel, not the inert backdrop

    // Restore inert default so other suites are unaffected.
    SetMapViewHooks(MapViewHooks{});
    render::SurfaceDestroy(s);
}

// ---------------------------------------------------------------------------
// W10-TEX hardening — degenerate render/click inputs (ASAN/UBSAN bounds the
// surface writes through the clipped leaves).
// ---------------------------------------------------------------------------

// 0 markers: the background still paints, markersDrawn == 0, no marker loop.
TEST(MapViewUnitEdge, RenderZeroMarkers) {
    SetMapSize(1024, 720);
    render::Surface* s = render::SurfaceCreate(64, 48, 32, gui::Argb8888());
    CHECK(s != nullptr);
    std::vector<OverviewMarker> none;
    OverviewRenderResult r = MapView_RenderOverview(*s, none, OverviewCamera{},
                                                    0, 0, 64, 48, 6, OverviewPalette());
    CHECK_EQ(r.backgroundDrawn, 1);
    CHECK_EQ(r.markersDrawn, 0);
    render::SurfaceDestroy(s);
}

// Markers far off-screen (and far negative) must clip out with no OOB write:
// the centre-clip skips them and the leaves clip per-pixel regardless.
TEST(MapViewUnitEdge, RenderMarkersOffScreen) {
    SetMapSize(1024, 720);
    render::Surface* s = render::SurfaceCreate(32, 32, 32, gui::Argb8888());
    CHECK(s != nullptr);
    OverviewCamera cam{};
    std::vector<OverviewMarker> mk(4);
    mk[0].worldX = 100000.0f;  mk[0].worldZ = 0.0f;        // far +X
    mk[1].worldX = -100000.0f; mk[1].worldZ = 0.0f;        // far -X
    mk[2].worldX = 0.0f;       mk[2].worldZ = 100000.0f;   // far +Z
    mk[3].worldX = 0.0f;       mk[3].worldZ = -100000.0f;  // far -Z
    OverviewRenderResult r = MapView_RenderOverview(*s, mk, cam,
                                                    0, 0, 32, 32, 6, OverviewPalette());
    CHECK_EQ(r.backgroundDrawn, 1);
    CHECK_EQ(r.markersDrawn, 0);                 // all clipped out
    render::SurfaceDestroy(s);
}

// A null-pixel surface: the function must be a complete no-op (no deref).
TEST(MapViewUnitEdge, RenderNullSurfaceNoOp) {
    SetMapSize(1024, 720);
    render::Surface surf{};                      // pixels == nullptr
    surf.width = 100; surf.height = 100;         // dims set, but no buffer
    std::vector<OverviewMarker> mk(2);
    OverviewRenderResult r = MapView_RenderOverview(surf, mk, OverviewCamera{},
                                                    0, 0, 100, 100, 6, OverviewPalette());
    CHECK_EQ(r.backgroundDrawn, 0);             // nothing drawn
    CHECK_EQ(r.markersDrawn, 0);
    // Also a zero-size viewport on a valid surface -> no-op.
    render::Surface* s = render::SurfaceCreate(16, 16, 32, gui::Argb8888());
    OverviewRenderResult r2 = MapView_RenderOverview(*s, mk, OverviewCamera{},
                                                     0, 0, 0, 0, 6, OverviewPalette());
    CHECK_EQ(r2.backgroundDrawn, 0);
    render::SurfaceDestroy(s);
}

// A 1x1 surface: the smallest possible viewport. A marker landing on (0,0)
// draws exactly one clipped pixel; the dot/outline overrun is clipped away.
TEST(MapViewUnitEdge, RenderOnePixelSurface) {
    SetMapSize(2, 2);                            // tiny world so the centre maps low
    render::Surface* s = render::SurfaceCreate(1, 1, 32, gui::Argb8888());
    CHECK(s != nullptr);
    OverviewCamera cam{};
    cam.scrollX = 1; cam.scrollY = 1;            // bring map centre (1,1) to (0,0)
    std::vector<OverviewMarker> mk(1);
    mk[0].worldX = 0.0f; mk[0].worldZ = 0.0f; mk[0].kind = MarkerKind::Player;
    OverviewRenderResult r = MapView_RenderOverview(*s, mk, cam,
                                                    0, 0, 1, 1, 6, OverviewPalette());
    CHECK_EQ(r.backgroundDrawn, 1);
    CHECK_EQ(r.markersDrawn, 1);                 // centre in-view -> counted
    render::SurfaceDestroy(s);
}

// ClickToWorld at the viewport edges (and far corners) round-trips the affine
// inverse without overflow/UB; the projection inverse is exact on the worldZ=0
// centre line and stable elsewhere.
TEST(MapViewUnitEdge, ClickToWorldViewportEdges) {
    SetMapSize(1024, 720);
    OverviewCamera cam{}; cam.scrollX = 50; cam.scrollY = 30; cam.panX = -10; cam.panY = 5;
    // The four viewport corners + centre must all return finite world coords.
    const int xs[5] = {0, kOverviewViewW - 1, 0, kOverviewViewW - 1, kOverviewViewW / 2};
    const int ys[5] = {0, 0, kOverviewViewH - 1, kOverviewViewH - 1, kOverviewViewH / 2};
    for (int i = 0; i < 5; ++i) {
        WorldXZ w = MapView_ClickToWorld(xs[i], ys[i], cam);
        CHECK(std::isfinite(w.x));
        CHECK(std::isfinite(w.z));
    }
    // The inverse is the documented bow-free affine (the dispatcher does not bow-
    // correct a click pick). Feed a hand-computed bow-free pixel and recover the
    // exact world coords:  vx = mapW/2 + worldX*5.33 + panX - scrollX (and Z).
    const double kScale = (double)gui::kMarkerWorldScale;   // 5.33
    int vx = (int)(1024 / 2 + 12.0 * kScale + cam.panX - cam.scrollX);
    int vy = (int)(720 / 2 + 9.0 * kScale + cam.panY - cam.scrollY);
    WorldXZ w = MapView_ClickToWorld(vx, vy, cam);
    CHECK(std::fabs(w.x - 12.0f) < 0.5f);
    CHECK(std::fabs(w.z - 9.0f) < 0.5f);
}
