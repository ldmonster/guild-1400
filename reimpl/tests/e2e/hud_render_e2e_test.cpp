// E2E tier — render a full HUD overlay into a 320x200 surface ON TOP of a painted
// world frame.  Asserts (1) the HUD overlay region is non-blank (bar + caption +
// markers drew), and (2) the world frame shows through everywhere the HUD is
// transparent (the overlay composites, it does not clear the frame) — exactly the
// original DrawUniverseAndStats overlay model.
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

} // namespace

// Full HUD over a world frame: overlay region non-blank, world shows through.
TEST(HudRenderE2E, HudCompositesOverWorldFrame) {
    const int W = 320, H = 200;
    std::vector<uint32_t> buf(W * H, 0);
    render::Surface s = WrapSurface(buf, W, H);
    play::SetHudRenderHooks(play::HudRenderHooks{});   // inert sprite hook

    // Paint a distinctive "world" base frame everywhere (the rendered 3D scene).
    const uint8_t WR = 30, WG = 70, WB = 110;
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x)
            buf[y * W + x] = render::PackColor(gui::Argb8888(), WR, WG, WB);

    // A full HUD: several owned-object slots (varied fill), money/date, markers.
    play::HudRenderState st;
    st.money = 987654; st.moneyRate = 1; st.gameDay = 12; st.clockTick = 5000;
    st.barObjects = { {1, 1.0}, {2, 0.75}, {3, 0.4}, {4, 0.1} };
    st.markers = { {10.f, 20.f}, {200.f, 60.f} };
    st.markerPanX = 0; st.markerPanY = 0; st.markerCameraOrigX = 0;

    play::HudPalette pal;
    // Player bar across the bottom; caption top-left; markers in the map region.
    play::HudRenderResult res =
        play::RenderHud(s, st, /*barOX*/ 4, /*barOY*/ 4,
                        /*capX*/ 8, /*capY*/ 4,
                        /*mapOX*/ 8, /*mapOY*/ 40, pal);

    CHECK_EQ(res.barSlotsDrawn, 4);
    CHECK_EQ(res.markersDrawn, 2);
    CHECK(res.captionGlyphs > 0);

    // (1) The HUD overlay region is non-blank: count pixels that differ from the
    // world base colour.  Bar fills, frames, caption glyphs and markers all differ.
    long overlayPx = 0;
    uint8_t r, g, b;
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x) {
            Px(buf, W, x, y, r, g, b);
            if (!(r == WR && g == WG && b == WB)) ++overlayPx;
        }
    CHECK(overlayPx > 0);

    // The bar-fill colour specifically is present (a real fill leaf ran).
    long fillPx = 0;
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x) {
            Px(buf, W, x, y, r, g, b);
            if (r == pal.barFillR && g == pal.barFillG && b == pal.barFillB) ++fillPx;
        }
    CHECK(fillPx > 0);

    // (2) The world shows through where the HUD is transparent: a region the HUD
    // never touches (far right edge, mid-height) is still the world base colour.
    Px(buf, W, W - 2, H / 2, r, g, b);
    CHECK_EQ((int)r, WR); CHECK_EQ((int)g, WG); CHECK_EQ((int)b, WB);

    // And most of the frame is still world (the HUD is a sparse overlay, not a clear).
    long worldPx = (long)(W * H) - overlayPx;
    CHECK(worldPx > (long)(W * H) / 2);   // > 50% of the frame is untouched world
}

// Determinism: two renders from identical inputs produce identical pixels.
TEST(HudRenderE2E, DeterministicAcrossRenders) {
    const int W = 320, H = 200;
    std::vector<uint32_t> a(W * H, 0), b(W * H, 0);
    render::Surface sa = WrapSurface(a, W, H);
    render::Surface sb = WrapSurface(b, W, H);
    play::SetHudRenderHooks(play::HudRenderHooks{});

    play::HudRenderState st;
    st.money = 50000; st.moneyRate = 1; st.gameDay = 4; st.clockTick = 1000;
    st.barObjects = { {7, 0.6}, {8, 0.3} };
    st.markers = { {64.f, 64.f} };

    play::RenderHud(sa, st, 4, 4, 8, 4, 8, 40);
    play::RenderHud(sb, st, 4, 4, 8, 4, 8, 40);

    bool identical = (a == b);
    CHECK(identical);
}
