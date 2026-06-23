// Integration tier — render the HUD over a base world surface via the REAL
// reconstructed draw leaves (gui::MenuFillRect -> render::SurfaceDrawHLine,
// render::SurfaceDrawRectOutline, render::DrawText/DrawGlyph the 5x7 GUI font),
// and assert known pixels (bar track / fill / frame) plus that the money/date
// caption actually rasterized glyph pixels.
#include "test.h"

#include "play/hud_render.h"
#include "gui/playerbar.h"
#include "render/colorformat.h"

#include <vector>
#include <cstdint>
#include <cstring>

using namespace guild;

namespace {

render::Surface WrapSurface(std::vector<uint32_t>& buf, int w, int h) {
    render::Surface s;
    std::memset(&s, 0, sizeof(s));
    s.width = w; s.height = h; s.bpp = 32;
    s.pitch = 4 * w; s.widthPx = w;
    s.pixels = reinterpret_cast<uint8_t*>(buf.data());
    s.clipX0 = 0; s.clipY0 = 0; s.clipX1 = w; s.clipY1 = h;
    s.fmt = gui::Argb8888();
    return s;
}

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

// Player bar slots painted through the real fill/outline leaves: track + fill +
// frame land where PlayerBar_SlotLayout places them, with the fill width = ratio.
TEST(HudRenderItest, BarFillRealLeaves) {
    const int W = 320, H = 200;
    std::vector<uint32_t> buf(W * H, 0);
    render::Surface s = WrapSurface(buf, W, H);
    play::SetHudRenderHooks(play::HudRenderHooks{});   // inert sprite hook

    play::HudRenderState st;
    st.money = 1234; st.moneyRate = 1; st.gameDay = 1; st.clockTick = 0;
    st.barObjects = { {10, 1.0}, {20, 0.5} };          // two slots: full + half fill

    play::HudPalette pal;
    play::HudRenderResult res =
        play::RenderHud(s, st, /*barOX*/ 0, /*barOY*/ 0,
                        /*capX*/ 200, /*capY*/ 4, /*mapOX*/ 0, /*mapOY*/ 0, pal);

    CHECK_EQ(res.barSlotsDrawn, 2);
    CHECK(res.barFillRows > 0);

    // Slot 0 sub-window at (subWinX, subWinY) = (6, 63), w=15 x h=80 (AddChildWindow
    // a1=x=6, a2=y=63, a3=w=15, a4=h=80 @0x4b19a5), fully filled.
    gui::PlayerBarLayout L0 = gui::PlayerBar_SlotLayout(0);
    uint8_t r, g, b;
    Px(buf, W, L0.subWinX + 2, L0.subWinY + 2, r, g, b); // inside slot 0 fill
    CHECK_EQ((int)r, pal.barFillR); CHECK_EQ((int)g, pal.barFillG); CHECK_EQ((int)b, pal.barFillB);

    // Slot 1 at row pitch 78: sub-window y = 78 + 63 = 141; half-filled. With w=15 the
    // fill is (50*15)/100 = 7px wide. A pixel near the left is fill; a pixel past the
    // fill (still inside the 15px-wide bar) is track (empty).
    gui::PlayerBarLayout L1 = gui::PlayerBar_SlotLayout(1);
    Px(buf, W, L1.subWinX + 2, L1.subWinY + 2, r, g, b);  // left -> fill
    CHECK_EQ((int)r, pal.barFillR); CHECK_EQ((int)g, pal.barFillG); CHECK_EQ((int)b, pal.barFillB);
    Px(buf, W, L1.subWinX + 12, L1.subWinY + 2, r, g, b); // right (x=12, > 7 fill) -> track
    CHECK_EQ((int)r, pal.barTrackR); CHECK_EQ((int)g, pal.barTrackG); CHECK_EQ((int)b, pal.barTrackB);

    // The real SurfaceDrawRectOutline produced frame pixels.
    long frame = CountColor(buf, W, H, pal.barFrameR, pal.barFrameG, pal.barFrameB);
    CHECK(frame > 0);
}

// Money/date caption rasterized via the real text leaf: caption-colour glyph
// pixels land in the caption band.
TEST(HudRenderItest, CaptionTextRealLeaf) {
    const int W = 320, H = 200;
    std::vector<uint32_t> buf(W * H, 0);
    render::Surface s = WrapSurface(buf, W, H);
    play::SetHudRenderHooks(play::HudRenderHooks{});

    play::HudRenderState st;
    st.money = 12345; st.moneyRate = 1; st.gameDay = 5; st.clockTick = 0;

    play::HudPalette pal;
    const int capX = 100, capY = 20;
    play::HudRenderResult res =
        play::RenderHud(s, st, 0, 0, capX, capY, 0, 0, pal);

    CHECK(res.captionGlyphs > 0);   // chars submitted to the real DrawText leaf

    // The 5x7 font stamps caption-colour pixels in the two-line caption band
    // (money line at capY, date line at capY+9).  Count them.
    long textPx = 0;
    uint8_t r, g, b;
    for (int y = capY; y < capY + 9 + 7; ++y)
        for (int x = capX; x < capX + 120 && x < W; ++x) {
            Px(buf, W, x, y, r, g, b);
            if (r == pal.textR && g == pal.textG && b == pal.textB) ++textPx;
        }
    CHECK(textPx > 0);   // the money + date strings actually drew glyph bits
}
