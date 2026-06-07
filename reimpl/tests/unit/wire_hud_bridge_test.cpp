// tests/unit/wire_hud_bridge_test.cpp — UNIT: prove InstallRealHudBridge swaps the
// inert HudRenderHooks::drawSprite slot (paints nothing) for the REAL reconstructed
// 2D sprite-bank blit leaf render::ShapeShowFromBank (0x5d861c) -> ShapeBlitColored16
// (0x5d7164), observed as a behaviour change on a synthetic 16bpp surface.
#include "test.h"

#include "play/wire_hud_bridge.h"
#include "play/hud_render.h"
#include "render/types.h"
#include "render/colorformat.h"

#include <cstdint>
#include <cstring>
#include <vector>

using namespace guild;

namespace {

// Count non-zero (drawn) pixels in a 16bpp buffer.
long NonZero(const std::vector<uint16_t>& buf) {
    long n = 0;
    for (uint16_t v : buf) if (v) ++n;
    return n;
}

} // namespace

// --- the hook table itself: inert default vs installed bridge ----------------
TEST(WireHudBridge, InstallSwapsDrawSpriteHook) {
    play::UninstallRealHudBridge();
    CHECK(!play::RealHudBridgeInstalled());

    // Inert default: drawSprite slot is null (paints nothing).
    play::SetHudRenderHooks(play::HudRenderHooks{});
    CHECK(play::GetHudRenderHooks().drawSprite == nullptr);

    // Install the bridge: the slot is now the real blit trampoline.
    play::InstallRealHudBridge();
    CHECK(play::RealHudBridgeInstalled());
    CHECK(play::GetHudRenderHooks().drawSprite != nullptr);

    // Uninstall reverts to inert.
    play::UninstallRealHudBridge();
    CHECK(!play::RealHudBridgeInstalled());
    CHECK(play::GetHudRenderHooks().drawSprite == nullptr);
}

// --- inert hook draws nothing; bridge hook draws the real sprite -------------
TEST(WireHudBridge, BridgeDrawsSpriteInertDoesNot) {
    const int W = 32, H = 32;
    render::ColorFormat fmt = render::Format565();

    // Build a 16bpp surface (the real engine HUD surface is 16bpp).
    auto MakeSurf = [&](std::vector<uint16_t>& buf) {
        render::Surface s;
        std::memset(&s, 0, sizeof(s));
        s.width = W; s.height = H; s.bpp = 16;
        s.pitch = 2 * W; s.widthPx = W;
        s.pixels = reinterpret_cast<uint8_t*>(buf.data());
        s.clipX0 = 0; s.clipY0 = 0; s.clipX1 = W; s.clipY1 = H;
        s.fmt = fmt;
        return s;
    };

    // INERT: call the (null) drawSprite hook directly -> no pixels.
    play::UninstallRealHudBridge();
    play::SetHudRenderHooks(play::HudRenderHooks{});
    {
        std::vector<uint16_t> buf(W * H, 0);
        render::Surface s = MakeSurf(buf);
        const auto& hk = play::GetHudRenderHooks();
        if (hk.drawSprite)
            hk.drawSprite(&s, 4, 4, /*gfx*/ 1403, hk.userData);
        CHECK_EQ(NonZero(buf), 0L);   // inert: nothing drawn
    }

    // REAL: install the bridge and call the now-real hook -> the 8x8 sprite block.
    play::InstallRealHudBridge();
    {
        std::vector<uint16_t> buf(W * H, 0);
        render::Surface s = MakeSurf(buf);
        const auto& hk = play::GetHudRenderHooks();
        CHECK(hk.drawSprite != nullptr);
        bool drawn = hk.drawSprite(&s, 4, 4, /*gfx*/ 1403, hk.userData);
        CHECK(drawn);
        long nz = NonZero(buf);
        CHECK(nz > 0);                 // the real blit leaf rasterised opaque pixels
        // The default bank is an 8x8 solid shape -> 64 opaque pixels recoloured.
        CHECK_EQ(nz, 64L);
    }

    play::UninstallRealHudBridge();
}

// --- the bridge accessor drives the SAME real leaf as the installed hook -----
TEST(WireHudBridge, BlitHudSpriteIsTheRealLeaf) {
    const int W = 24, H = 24;
    std::vector<uint16_t> buf(W * H, 0);
    render::ColorFormat fmt = render::Format565();

    // Out-of-range shape index -> the real leaf rejects it (no pixels).
    int rvBad = play::BlitHudSprite(2, 2, /*shapeIndex*/ 5, buf.data(), W, fmt);
    CHECK_EQ(rvBad, 0);
    CHECK_EQ(NonZero(buf), 0L);

    // Valid index 0 -> the real BlitColored16 path draws the 8x8 block.
    int rvOk = play::BlitHudSprite(2, 2, /*shapeIndex*/ 0, buf.data(), W, fmt);
    CHECK_EQ(rvOk, 1);
    CHECK_EQ(NonZero(buf), 64L);

    // The block sits at (2,2)..(9,9): a pixel inside is non-zero, one outside is 0.
    CHECK(buf[5 * W + 5] != 0);
    CHECK_EQ((int)buf[0], 0);

    // The default bank is real-format and non-empty.
    std::size_t sz = 0;
    const u8* bank = play::DefaultHudSpriteBank(&sz);
    CHECK(bank != nullptr);
    CHECK(sz > 0x80);
}
