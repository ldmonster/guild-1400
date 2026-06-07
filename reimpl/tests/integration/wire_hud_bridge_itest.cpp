// tests/integration/wire_hud_bridge_itest.cpp — INTEGRATION: render a small
// synthetic HUD through the LIVE path (play::RenderHud, which drives the inert
// HudRenderHooks::drawSprite slot for the slot-icon + map-marker artwork) with the
// bridge INSTALLED vs NOT, and assert the output differs: real sprite pixels
// (rasterised by render::ShapeShowFromBank -> ShapeBlitColored16) appear at the
// slot-icon / marker positions that were ABSENT under the inert hook.
#include "test.h"

#include "play/wire_hud_bridge.h"
#include "play/hud_render.h"
#include "gui/playerbar.h"
#include "render/types.h"
#include "render/colorformat.h"

#include <cstdint>
#include <cstring>
#include <vector>

using namespace guild;

namespace {

// 16bpp surface (the real HUD surface depth the sprite blit leaf writes).
render::Surface WrapSurface(std::vector<uint16_t>& buf, int w, int h,
                            const render::ColorFormat& fmt) {
    render::Surface s;
    std::memset(&s, 0, sizeof(s));
    s.width = w; s.height = h; s.bpp = 16;
    s.pitch = 2 * w; s.widthPx = w;
    s.pixels = reinterpret_cast<uint8_t*>(buf.data());
    s.clipX0 = 0; s.clipY0 = 0; s.clipX1 = w; s.clipY1 = h;
    s.fmt = fmt;
    return s;
}

long NonZero(const std::vector<uint16_t>& buf) {
    long n = 0; for (uint16_t v : buf) if (v) ++n; return n;
}

play::HudRenderState MakeState() {
    play::HudRenderState st;
    st.money = 1234; st.moneyRate = 1; st.gameDay = 1; st.clockTick = 0;
    st.barObjects = { {10, 1.0}, {20, 0.5} };  // two slot icons
    st.markers    = { {32.f, 32.f} };          // one marker icon
    return st;
}

} // namespace

// Inert vs bridge through the live RenderHud path: the bridge produces strictly
// MORE drawn pixels (the slot-icon + marker sprites), at the icon positions.
TEST(WireHudBridgeItest, RenderHudGainsSpritePixels) {
    const int W = 320, H = 200;
    render::ColorFormat fmt = render::Format565();
    play::HudRenderState st = MakeState();
    play::HudPalette pal;

    // --- inert: drawSprite null -> only track/fill/frame/text/marker-dot draw ---
    play::UninstallRealHudBridge();
    play::SetHudRenderHooks(play::HudRenderHooks{});
    std::vector<uint16_t> inert(W * H, 0);
    render::Surface si = WrapSurface(inert, W, H, fmt);
    play::HudRenderResult ri =
        play::RenderHud(si, st, /*barOX*/ 4, /*barOY*/ 4,
                        /*capX*/ 8, /*capY*/ 4, /*mapOX*/ 8, /*mapOY*/ 40, pal);
    CHECK_EQ(ri.barSlotsDrawn, 2);
    CHECK_EQ(ri.markersDrawn, 1);
    long inertPx = NonZero(inert);
    CHECK(inertPx > 0);   // the deterministic fill/frame/marker leaves still drew

    // --- bridge: drawSprite -> real ShapeShowFromBank blit ---------------------
    play::InstallRealHudBridge();
    std::vector<uint16_t> real(W * H, 0);
    render::Surface sr = WrapSurface(real, W, H, fmt);
    play::HudRenderResult rr =
        play::RenderHud(sr, st, 4, 4, 8, 4, 8, 40, pal);
    CHECK_EQ(rr.barSlotsDrawn, 2);
    CHECK_EQ(rr.markersDrawn, 1);
    long realPx = NonZero(real);

    // The bridge draws STRICTLY MORE pixels: each slot icon (8x8) + the marker
    // icon (8x8) is now rasterised by the real blit leaf.
    CHECK(realPx > inertPx);

    // Sprite pixels land at a slot-icon position. Slot 0 icon is at
    // (barOX + spriteX, barOY + spriteY) (the same coords RenderHud passes the hook).
    gui::ResetPlayerBar();
    int slot0 = gui::PlayerBar_AssignSlot(10);
    gui::PlayerBarLayout L0 = gui::PlayerBar_SlotLayout(slot0);
    int iconX = 4 + L0.spriteX;
    int iconY = 4 + L0.spriteY;
    // Inert had nothing at the icon block; bridge has non-zero there.
    bool inertIconBlank = true, bridgeIconDrawn = false;
    for (int dy = 0; dy < 8; ++dy)
        for (int dx = 0; dx < 8; ++dx) {
            int px = iconX + dx, py = iconY + dy;
            if (px < 0 || py < 0 || px >= W || py >= H) continue;
            if (inert[py * W + px]) inertIconBlank = false;
            if (real[py * W + px])  bridgeIconDrawn = true;
        }
    CHECK(inertIconBlank);     // the inert hook drew no icon
    CHECK(bridgeIconDrawn);    // the real blit leaf drew the slot icon
}

// Determinism: two bridge-installed renders from identical inputs are identical.
TEST(WireHudBridgeItest, BridgeRenderDeterministic) {
    const int W = 160, H = 120;
    render::ColorFormat fmt = render::Format565();
    play::HudRenderState st = MakeState();
    play::HudPalette pal;

    play::InstallRealHudBridge();
    std::vector<uint16_t> a(W * H, 0), b(W * H, 0);
    render::Surface sa = WrapSurface(a, W, H, fmt);
    render::Surface sb = WrapSurface(b, W, H, fmt);
    play::RenderHud(sa, st, 4, 4, 8, 4, 8, 40, pal);
    play::RenderHud(sb, st, 4, 4, 8, 4, 8, 40, pal);
    CHECK(a == b);

    play::UninstallRealHudBridge();
}
