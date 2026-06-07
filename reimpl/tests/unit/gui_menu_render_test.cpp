// Unit tests for guild::gui menu rasterization (the VISIBLE half of
// VIBE_Menu_RunMainMenu @0x529d08): render a SYNTHETIC 2-widget menu (a filled
// background rect + a single button rect) into a 64x48 32bpp buffer and assert
// EXACT pixels at known coordinates (golden).
//
// This tier exercises the reconstructed deterministic draw leaf MenuFillRect
// (gilde.exe 0x423c70 VIBE_Surface_ColorFillRect, software path) and the real
// render::SurfaceDrawHLine sibling it routes through — with the sprite hook left
// inert so the asset-less fill+outline path is what we measure.
#include "test.h"

#include "gui/menu_render.h"
#include "render/colorformat.h"

#include <vector>
#include <cstdint>

using namespace guild;

namespace {

// Decode one pixel of a 32bpp ARGB8888 buffer back to an RGB triple via the REAL
// render::UnpackColor (0x434f7c), so the golden assertion is format-honest.
void Px(const std::vector<uint32_t>& buf, int w, int x, int y,
        uint8_t& r, uint8_t& g, uint8_t& b) {
    render::UnpackColor(gui::Argb8888(), buf[y * w + x], r, g, b);
}

} // namespace

// ---- MenuFillRect golden: clamp + fill via the real HLine span -----------
TEST(GuiMenuRenderUnit, FillRectExactPixels) {
    const int W = 64, H = 48;
    std::vector<uint32_t> buf(W * H, 0xDEAD);
    auto t = gui::MenuRenderTarget::Wrap(buf.data(), W, H);

    // Fill a 10x6 rect at (5,4) with (12,34,56).
    int rows = gui::MenuFillRect(&t.surf, 5, 4, 10, 6, 12, 34, 56);
    CHECK_EQ(rows, 6);

    uint8_t r = 0, g = 0, b = 0;
    // Inside the rect: exact colour.
    Px(buf, W, 5, 4, r, g, b);  CHECK_EQ((int)r, 12); CHECK_EQ((int)g, 34); CHECK_EQ((int)b, 56);
    Px(buf, W, 14, 9, r, g, b); CHECK_EQ((int)r, 12); CHECK_EQ((int)g, 34); CHECK_EQ((int)b, 56);
    // Just outside the right/bottom edge: untouched (0xDEAD raw).
    CHECK_EQ(buf[4 * W + 15], 0xDEADu);   // (15,4) -> x past x+w
    CHECK_EQ(buf[10 * W + 5], 0xDEADu);   // (5,10) -> y past y+h
    CHECK_EQ(buf[3 * W + 5], 0xDEADu);    // (5,3) -> above
}

// MenuFillRect must clamp a rect that overhangs the surface bounds (the original
// clamps x+w to width-1 / y+h to height-1, rejects x>=width).
TEST(GuiMenuRenderUnit, FillRectClampsToBounds) {
    const int W = 64, H = 48;
    std::vector<uint32_t> buf(W * H, 0);
    auto t = gui::MenuRenderTarget::Wrap(buf.data(), W, H);

    // Overhang both edges: origin (60,45), 20x20 -> clamped to (60..64, 45..48).
    int rows = gui::MenuFillRect(&t.surf, 60, 45, 20, 20, 1, 2, 3);
    CHECK(rows > 0);
    uint8_t r = 0, g = 0, b = 0;
    Px(buf, W, 63, 47, r, g, b); CHECK_EQ((int)r, 1); CHECK_EQ((int)g, 2); CHECK_EQ((int)b, 3);
    // No out-of-bounds write corrupted the last legal pixel's neighbours: the
    // pixel at (0,0) stays background.
    CHECK_EQ(buf[0], 0u);

    // Fully off-surface -> no rows.
    CHECK_EQ(gui::MenuFillRect(&t.surf, 100, 100, 5, 5, 9, 9, 9), 0);
}

// ---- Synthetic 2-widget menu: background fill + one button rect -----------
TEST(GuiMenuRenderUnit, SyntheticTwoWidgetGolden) {
    const int W = 64, H = 48;
    std::vector<uint32_t> buf(W * H, 0);
    auto t = gui::MenuRenderTarget::Wrap(buf.data(), W, H);

    // Ensure the inert sprite hook (no asset draw) so the reconstructed
    // fill+outline path is what paints.
    gui::SetMenuRenderHooks(gui::MenuRenderHooks{});

    gui::MenuPalette pal;  // defaults: bg(24,28,64) btn(96,96,160) edge(200,200,220)

    // Build a 2-widget tree: a backing window + one button widget at (8,6) 40x16.
    gui::Window win;        // origin (0,0)
    std::vector<gui::Widget> wgts(1);
    wgts[0].inUse() = 1;
    wgts[0].type()  = gui::kTypeLabel;   // a label-flagged sprite button
    wgts[0].x() = 8; wgts[0].y() = 6; wgts[0].w() = 40; wgts[0].h() = 16;
    wgts[0].id() = 174;                  // gfx id

    int drawn = gui::RenderMenuForm(t, /*bg gfx*/ 0, &win, wgts.data(),
                                    (int)wgts.size(), pal);
    CHECK_EQ(drawn, 1);

    uint8_t r = 0, g = 0, b = 0;
    // Background pixel (corner) == backdrop fill colour.
    Px(buf, W, 0, 0, r, g, b);   CHECK_EQ((int)r, 24); CHECK_EQ((int)g, 28); CHECK_EQ((int)b, 64);
    // Button face interior (well inside the 40x16 rect, away from the border).
    Px(buf, W, 20, 12, r, g, b); CHECK_EQ((int)r, 96); CHECK_EQ((int)g, 96); CHECK_EQ((int)b, 160);
    // Button border at the top-left corner of the button.
    Px(buf, W, 8, 6, r, g, b);   CHECK_EQ((int)r, 200); CHECK_EQ((int)g, 200); CHECK_EQ((int)b, 220);
    // A pixel outside the button stays backdrop.
    Px(buf, W, 60, 40, r, g, b); CHECK_EQ((int)r, 24); CHECK_EQ((int)g, 28); CHECK_EQ((int)b, 64);
}
