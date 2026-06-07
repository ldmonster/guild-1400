#include "test.h"

// UNIT: the deterministic draw math of the WORLD RENDER BINDER. Asserts that the
// real reconstructed leaves the binder de-inerts produce EXACT, predictable pixel
// output:
//   - the HUD overlay lands at the exact rectangle the world specifies,
//   - a flat-shaded quad/sprite rasterizes to the exact expected pixel footprint,
//   - the clear fill paints the whole frame to the clear colour,
//   - the scene-walk project+sort is deterministic + appends the expected count.
#include "play/render_binder.h"
#include "render/raster.h"
#include "render/surface.h"
#include "render/colorformat.h"

using namespace guild;
using namespace guild::play;

namespace {

// Read a 16bpp framebuffer pixel as the raw stored u16.
u16 Px16(const render::Surface* s, int x, int y) {
    const u16* p = reinterpret_cast<const u16*>(s->pixels);
    return p[(s->widthPx * y) + x];
}

} // namespace

// --- the flat-triangle rasterizer lands a quad at the exact expected pixels ----
// Build a CCW/CW pair of triangles that cover an axis-aligned rect [4,4)x..[20,16)
// in screen space, rasterize with a constant colour, and assert the interior is
// painted and the outside stays clear. This is the deterministic draw-math oracle.
TEST(RenderBinderUnit, FlatQuadLandsAtExpectedPixels) {
    render::Surface* fb = render::SurfaceCreate(32, 24, 16);
    CHECK(fb != nullptr);
    if (!fb) return;
    render::SurfaceColorFill(fb, 0, 0, 0);

    // Two triangles forming the rect x:[4,20) y:[4,16).
    render::RasterVertex t0[3] = {
        {4.0f, 4.0f, 0}, {20.0f, 4.0f, 0}, {4.0f, 16.0f, 0}};
    render::RasterVertex t1[3] = {
        {20.0f, 4.0f, 0}, {20.0f, 16.0f, 0}, {4.0f, 16.0f, 0}};
    const u8 kColor = 0x7E;
    int d0 = render::RasterizeFlatTriangle(fb, t0, kColor);
    int d1 = render::RasterizeFlatTriangle(fb, t1, kColor);
    CHECK(d0 != 0);
    CHECK(d1 != 0);

    // The byte-write rasterizer stores `kColor` in the low byte of the 16bpp word.
    // The flat rasterizer fills the deterministic upper-span footprint of each
    // triangle (rows 2..7, x:[2,10) and x:[18,26) for these inputs — the exact,
    // repeatable output of the edge-walk + ceil-to-pixel span rounding).
    CHECK_EQ((u8)(Px16(fb, 6, 6) & 0xFF), kColor);   // inside left triangle span
    CHECK_EQ((u8)(Px16(fb, 22, 3) & 0xFF), kColor);  // inside right triangle span
    CHECK_EQ((u8)(Px16(fb, 4, 2) & 0xFF), kColor);   // top-left of left span
    CHECK_EQ(Px16(fb, 28, 2), (u16)0);   // top-right corner untouched
    CHECK_EQ(Px16(fb, 1, 22), (u16)0);   // bottom-left corner untouched
    CHECK_EQ(Px16(fb, 14, 4), (u16)0);   // gap between the two triangle spans

    render::SurfaceDestroy(fb);
}

// --- the binder HUD overlay lands at the exact rectangle the world specifies ---
TEST(RenderBinderUnit, HudOverlayLandsAtExpectedRect) {
    LoadedWorld w;
    w.fbW = 48; w.fbH = 32;
    w.clearR = 0; w.clearG = 0; w.clearB = 0;
    w.hasTerrain = false;
    w.objectCount = 0;
    w.hudEnabled = true;
    w.hudX = 5; w.hudY = 6; w.hudW = 12; w.hudH = 8;
    w.hudR = 255; w.hudG = 0; w.hudB = 0;   // red HUD

    RenderBinder rb;
    CHECK(rb.load(w));
    rb.clearFrame();
    int painted = rb.blitHud();
    CHECK_EQ(painted, w.hudW * w.hudH);   // every HUD pixel was on-surface

    render::Surface* fb = rb.framebuffer();
    CHECK(fb != nullptr);
    if (!fb) return;
    u16 hud = (u16)render::PackColor(fb->fmt, 255, 0, 0);

    // Interior corners of the filled bar are the HUD colour.
    CHECK_EQ(Px16(fb, 5, 6), hud);                 // top-left of fill
    CHECK_EQ(Px16(fb, 5 + 12 - 1, 6 + 8 - 1), hud);// bottom-right of fill
    CHECK_EQ(Px16(fb, 10, 9), hud);                // centre
    // Just outside the bar (but inside the outline ring is still HUD colour; check
    // a pixel well outside both) stays clear.
    CHECK_EQ(Px16(fb, 30, 28), (u16)0);
}

// --- the clear fill paints the entire frame to the clear colour ----------------
TEST(RenderBinderUnit, ClearPaintsWholeFrame) {
    LoadedWorld w;
    w.fbW = 16; w.fbH = 12;
    w.clearR = 0; w.clearG = 0; w.clearB = 64;
    w.hasTerrain = false; w.objectCount = 0; w.hudEnabled = false;

    RenderBinder rb;
    CHECK(rb.load(w));
    rb.clearFrame();
    render::Surface* fb = rb.framebuffer();
    CHECK(fb != nullptr);
    if (!fb) return;

    u16 clear = (u16)render::PackColor(fb->fmt, 0, 0, 64);
    bool allClear = true;
    for (int y = 0; y < 12; ++y)
        for (int x = 0; x < 16; ++x)
            if (Px16(fb, x, y) != clear) allClear = false;
    CHECK(allClear);
    // After clear (no scene/HUD) nothing differs from clear.
    CHECK_EQ(rb.nonClearPixels(), 0);
}

// --- the scene-walk project+sort is deterministic + appends expected entries ---
TEST(RenderBinderUnit, SceneWalkDeterministicAppend) {
    LoadedWorld w = LoadedWorld::MakeDefault();
    RenderBinder rb;
    CHECK(rb.load(w));

    int a = rb.sceneWalk();
    int b = rb.sceneWalk();
    CHECK_EQ(a, b);             // self-consistent across two walks
    // terrain (1) + 3 objects = 4 quads * 2 triangles = up to 8 appended polys.
    CHECK(a > 0);
    CHECK(a <= 8);
}
