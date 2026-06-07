// End-to-end test for the VISIBLE main menu (gilde.exe 0x529d08
// VIBE_Menu_RunMainMenu): build a menu window tree, render it into a 320x200
// 32bpp framebuffer through the real reconstructed rasterizer/text leaves, and
// assert a MEANINGFUL fraction of pixels are non-background — i.e. the menu
// actually drew (buttons + borders + captions over the backdrop), not just a
// blank/clear frame.
#include "test.h"

#include "gui/menu_render.h"
#include "gui/main_menu.h"
#include "render/colorformat.h"

#include <vector>
#include <cstdint>

using namespace guild;

namespace {
void Px(const std::vector<uint32_t>& buf, int w, int x, int y,
        uint8_t& r, uint8_t& g, uint8_t& b) {
    render::UnpackColor(gui::Argb8888(), buf[y * w + x], r, g, b);
}
} // namespace

// Full 320x200 main-menu frame: non-blank, and a meaningful fraction of pixels
// differ from the backdrop colour (the eight gfx-174 buttons + their captions).
TEST(GuiMenuRenderE2E, MainMenuFrameIsDrawn) {
    const int W = 320, H = 200;
    std::vector<uint32_t> buf(W * H, 0);
    auto t = gui::MenuRenderTarget::Wrap(buf.data(), W, H);
    gui::SetMenuRenderHooks(gui::MenuRenderHooks{});   // inert sprite hook

    gui::MenuPalette pal;
    int buttons = gui::RenderMainMenu(t, pal);
    CHECK_EQ(buttons, gui::kMainMenuButtonCount);   // all 8 main-menu buttons

    // The whole frame is non-blank (backdrop fill covers it).
    long nonzero = 0;
    for (auto p : buf) if (p) ++nonzero;
    CHECK(nonzero > 0);

    // A meaningful fraction is NON-BACKGROUND: button faces, borders, and white
    // text differ from the backdrop (24,28,64). The 8 buttons are 96x18 each at
    // x=32 -> ~ 8*96*18 = 13824 px of non-backdrop face/border/text >= ~21% of a
    // 320x200 frame's columns the menu occupies; assert at least 5% to be robust.
    long nonBg = 0;
    uint8_t r, g, b;
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x) {
            Px(buf, W, x, y, r, g, b);
            if (!(r == pal.bgR && g == pal.bgG && b == pal.bgB)) ++nonBg;
        }
    double frac = (double)nonBg / (double)(W * H);
    CHECK(frac > 0.05);

    // Spot-check: button 0 face present, and the backdrop survives in a region
    // the buttons do not cover (right half of the screen).
    Px(buf, W, 40, gui::kMainMenuButtons[0].y + 9, r, g, b);
    CHECK_EQ((int)r, 96); CHECK_EQ((int)g, 96); CHECK_EQ((int)b, 160);
    Px(buf, W, 300, 100, r, g, b);
    CHECK_EQ((int)r, pal.bgR); CHECK_EQ((int)g, pal.bgG); CHECK_EQ((int)b, pal.bgB);
}

// E2E via the generic form-render entry over a built Window+Widget tree (the
// retained-mode path the original walks), asserting the same non-blank contract.
TEST(GuiMenuRenderE2E, WindowTreeFrameNonBlank) {
    const int W = 320, H = 200;
    std::vector<uint32_t> buf(W * H, 0);
    auto t = gui::MenuRenderTarget::Wrap(buf.data(), W, H);
    gui::SetMenuRenderHooks(gui::MenuRenderHooks{});

    // Build the eight-button column as Widget records (x=32 + the y-table).
    gui::Window win;  // origin (0,0)
    std::vector<gui::Widget> wgts(gui::kMainMenuButtonCount);
    for (int i = 0; i < gui::kMainMenuButtonCount; ++i) {
        wgts[i].inUse() = 1;
        wgts[i].type()  = gui::kTypeLabel;
        wgts[i].x() = gui::kMainMenuButtonX;
        wgts[i].y() = gui::kMainMenuButtons[i].y;
        wgts[i].w() = 96;
        wgts[i].h() = 18;
        wgts[i].id() = gui::kMainMenuButtonSprite;
    }

    int drawn = gui::RenderMenuForm(t, /*bg*/ 0, &win, wgts.data(),
                                    gui::kMainMenuButtonCount);
    CHECK_EQ(drawn, gui::kMainMenuButtonCount);

    long nonBg = 0;
    uint8_t r, g, b;
    gui::MenuPalette pal;
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x) {
            Px(buf, W, x, y, r, g, b);
            if (!(r == pal.bgR && g == pal.bgG && b == pal.bgB)) ++nonBg;
        }
    CHECK((double)nonBg / (double)(W * H) > 0.05);
}
