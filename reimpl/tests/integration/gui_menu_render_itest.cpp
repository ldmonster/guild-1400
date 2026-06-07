// Integration tests for guild::gui menu rasterization wired against the REAL
// reconstructed rasterizer/text leaves (render/surface.cpp gfx.c siblings +
// render/text_raster.cpp text.c font) over a small widget tree.
//
// Where the unit tier asserts golden pixels of the deterministic fill path, this
// tier confirms the full reconstructed paint — MenuFillRect -> real
// render::SurfaceDrawHLine, render::SurfaceDrawRectOutline (0x4242d4), and
// render::DrawText/DrawGlyph (0x434E18 / 0x434D0C, the 5x7 GUI font) — produces a
// non-blank frame with the expected structural pixels (backdrop / border / glyph)
// over a built Window+Widget tree.
#include "test.h"

#include "gui/menu_render.h"
#include "render/colorformat.h"

#include <vector>
#include <cstdint>

using namespace guild;

namespace {

void Px(const std::vector<uint32_t>& buf, int w, int x, int y,
        uint8_t& r, uint8_t& g, uint8_t& b) {
    render::UnpackColor(gui::Argb8888(), buf[y * w + x], r, g, b);
}

long CountColor(const std::vector<uint32_t>& buf, int w, int h,
                uint8_t tr, uint8_t tg, uint8_t tb) {
    long n = 0;
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            uint8_t r, g, b; Px(buf, w, x, y, r, g, b);
            if (r == tr && g == tg && b == tb) ++n;
        }
    return n;
}

} // namespace

// Render a small 3-button window tree through the real leaves; assert non-blank
// + a known structural pixel + that the real outline/text leaves actually fired.
TEST(GuiMenuRenderItest, RealLeavesPaintWidgetTree) {
    const int W = 160, H = 120;
    std::vector<uint32_t> buf(W * H, 0);
    auto t = gui::MenuRenderTarget::Wrap(buf.data(), W, H);
    gui::SetMenuRenderHooks(gui::MenuRenderHooks{});   // inert sprite hook

    gui::MenuPalette pal;
    gui::Window win;  // origin (0,0)
    std::vector<gui::Widget> wgts(3);
    for (int i = 0; i < 3; ++i) {
        wgts[i].inUse() = 1;
        wgts[i].type()  = gui::kTypeLabel;
        wgts[i].x() = 10;
        wgts[i].y() = 8 + i * 24;
        wgts[i].w() = 100;
        wgts[i].h() = 18;
        wgts[i].id() = 174;
    }

    int drawn = gui::RenderMenuForm(t, /*bg*/ 0, &win, wgts.data(), 3, pal);
    CHECK_EQ(drawn, 3);

    // Non-blank: every pixel covered at least by the backdrop fill.
    long nonzero = 0;
    for (auto p : buf) if (p) ++nonzero;
    CHECK(nonzero > 0);

    // Known structural pixels.
    uint8_t r, g, b;
    Px(buf, W, 50, 14, r, g, b);  // inside button 0 face
    CHECK_EQ((int)r, 96); CHECK_EQ((int)g, 96); CHECK_EQ((int)b, 160);

    // The REAL render::SurfaceDrawRectOutline produced border pixels (edge colour).
    long border = CountColor(buf, W, H, pal.edgeR, pal.edgeG, pal.edgeB);
    CHECK(border > 0);
}

// Drive the label glyph path through the REAL render::DrawText/DrawGlyph leaf by
// rendering the canonical main-menu column and confirming white text pixels land
// inside the first button band (the 5x7 font actually rasterized captions).
TEST(GuiMenuRenderItest, RealTextLeafDrawsCaptions) {
    const int W = 200, H = 360;
    std::vector<uint32_t> buf(W * H, 0);
    auto t = gui::MenuRenderTarget::Wrap(buf.data(), W, H);
    gui::SetMenuRenderHooks(gui::MenuRenderHooks{});

    int n = gui::RenderMainMenu(t);
    CHECK_EQ(n, 8);

    // First button is at x=32, y=10, 96x18. Its caption is white text drawn by the
    // real font leaf. Count white pixels in that band.
    long white = 0;
    uint8_t r, g, b;
    for (int y = 10; y < 28; ++y)
        for (int x = 32; x < 128; ++x) {
            Px(buf, W, x, y, r, g, b);
            if (r == 255 && g == 255 && b == 255) ++white;
        }
    CHECK(white > 0);   // glyph bits were written by DrawGlyph
}

// The installable sprite hook must be honoured: when it claims to draw, the
// reconstructed fill/outline is suppressed for that widget (matches the original
// where the real sprite blit replaces the placeholder).
TEST(GuiMenuRenderItest, SpriteHookSuppressesPlaceholder) {
    const int W = 64, H = 48;
    std::vector<uint32_t> buf(W * H, 0);
    auto t = gui::MenuRenderTarget::Wrap(buf.data(), W, H);

    static int calls = 0; calls = 0;
    gui::MenuRenderHooks h{};
    h.drawSprite = [](render::Surface* s, int x, int y, int gfx, void*) -> bool {
        (void)s; (void)x; (void)y; (void)gfx; ++calls; return true; // claims it drew
    };
    gui::SetMenuRenderHooks(h);

    gui::Window win;
    std::vector<gui::Widget> wgts(1);
    wgts[0].inUse() = 1; wgts[0].type() = gui::kTypeLabel;
    wgts[0].x() = 8; wgts[0].y() = 6; wgts[0].w() = 40; wgts[0].h() = 16;
    wgts[0].id() = 174;

    // bg<0 to skip the backdrop fill so only the button is in play.
    int drawn = gui::RenderMenuForm(t, /*bg*/ -1, &win, wgts.data(), 1);
    CHECK_EQ(drawn, 1);
    CHECK(calls >= 1);   // hook was invoked for the button

    // Because the hook claimed the draw, no placeholder face/border was painted:
    // the button-face colour is absent.
    long face = CountColor(buf, W, H, 96, 96, 160);
    CHECK_EQ(face, 0);

    gui::SetMenuRenderHooks(gui::MenuRenderHooks{});  // restore inert
}
