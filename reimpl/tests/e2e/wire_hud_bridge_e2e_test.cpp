// tests/e2e/wire_hud_bridge_e2e_test.cpp — GUARDED real-asset E2E for the
// HUD / sprite / 2D-blit bridge. Mounts the REAL game install (AUGSBURG) via
// app::MountRealGameAssets — exercising the real boot/VFS front half — then renders
// an in-game HUD frame (the bottom player-bar slot icons + the map-marker artwork +
// the money/date caption) ON TOP of a painted world frame, with the bridge
// INSTALLED. The inert HudRenderHooks::drawSprite slot draws no sprite; the bridge
// routes it to the REAL reconstructed 2D sprite-bank blit leaf render::ShapeShow
// FromBank (0x5d861c) -> ShapeBlitColored16 (0x5d7164). We assert real HUD/sprite
// pixels that were ABSENT under the inert hook now appear, and report the count.
//
// GUARDED: skips cleanly (zero checks) when the real game dir is absent.
#include "test.h"

#include "play/wire_hud_bridge.h"
#include "play/hud_render.h"
#include "gui/playerbar.h"
#include "app/real_boot.h"
#include "io/vfs.h"
#include "shim_impl/disk_filesystem.h"
#include "render/types.h"
#include "render/colorformat.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

using namespace guild;

namespace {

std::string GameDir() {
    if (const char* env = std::getenv("GUILD_GAME_DIR"))
        return env;
    return "/home/cnupt/work/reverse/reverse-guild/reimpl/europe_guild_1400_original";
}

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

long CountDiff(const std::vector<uint16_t>& a, const std::vector<uint16_t>& b) {
    long n = 0;
    for (std::size_t i = 0; i < a.size(); ++i) if (a[i] != b[i]) ++n;
    return n;
}

} // namespace

TEST(WireHudBridgeE2E, RealAugsburgHudSpritePixels) {
    const std::string dir = GameDir();
    shim::DiskFileSystem fs(dir);
    if (!fs.exists("Resources/Objects.BIN")) {
        std::printf("  [skip] WireHudBridgeE2E: real game dir absent (%s)\n",
                    dir.c_str());
        return;
    }

    // --- real boot front half: parse Gilde.INI + bind VFS + mount Resources/*.BIN
    app::RealGameAssets assets = app::MountRealGameAssets(&fs, dir);
    CHECK(assets.vfsBound);
    CHECK(assets.totalMembers() > 0);
    std::printf("  [info] real assets: %zu archive members, start city '%s'\n",
                assets.totalMembers(), assets.stadt.c_str());

    // --- an in-game HUD frame: several owned-object slot icons + markers + caption
    const int W = 320, H = 200;
    render::ColorFormat fmt = render::Format565();
    play::HudRenderState st;
    st.money = 987654; st.moneyRate = 1; st.gameDay = 12; st.clockTick = 5000;
    st.barObjects = { {1, 1.0}, {2, 0.75}, {3, 0.4}, {4, 0.1} };  // 4 slot icons
    st.markers    = { {64.f, 64.f}, {180.f, 90.f} };              // 2 marker icons
    play::HudPalette pal;

    // Paint a distinctive 16bpp "world" frame everywhere (the rendered 3D scene).
    const uint16_t kWorld = 0x2A4B;
    auto PaintWorld = [&](std::vector<uint16_t>& b){ std::fill(b.begin(), b.end(), kWorld); };

    // --- inert hook: no sprite blit -------------------------------------------
    play::UninstallRealHudBridge();
    play::SetHudRenderHooks(play::HudRenderHooks{});
    std::vector<uint16_t> inert(W * H, 0);
    PaintWorld(inert);
    render::Surface si = WrapSurface(inert, W, H, fmt);
    play::HudRenderResult ri =
        play::RenderHud(si, st, /*barOX*/ 4, /*barOY*/ 4,
                        /*capX*/ 8, /*capY*/ 4, /*mapOX*/ 8, /*mapOY*/ 40, pal);
    CHECK_EQ(ri.barSlotsDrawn, 4);
    CHECK_EQ(ri.markersDrawn, 2);

    // --- bridge installed: the real 2D blit leaf draws the slot/marker sprites --
    play::InstallRealHudBridge();
    std::vector<uint16_t> real(W * H, 0);
    PaintWorld(real);
    render::Surface sr = WrapSurface(real, W, H, fmt);
    play::HudRenderResult rr =
        play::RenderHud(sr, st, 4, 4, 8, 4, 8, 40, pal);
    CHECK_EQ(rr.barSlotsDrawn, 4);
    CHECK_EQ(rr.markersDrawn, 2);

    // The bridge frame differs from the inert frame ONLY where sprite pixels were
    // added (the fill/frame/caption/marker-dot leaves are identical between runs).
    long spritePx = CountDiff(inert, real);
    CHECK(spritePx > 0);
    std::printf("  [info] real HUD sprite pixels added by bridge: %ld "
                "(8x8 icons that pre-clip inside the 320x200 frame; rows past the "
                "strip / off-map markers are clipped out)\n", spritePx);

    // Sprite pixels land at the first slot's icon block (the coords RenderHud passes
    // the hook), and that block was blank under the inert hook.
    gui::ResetPlayerBar();
    int slot0 = gui::PlayerBar_AssignSlot(1);
    gui::PlayerBarLayout L0 = gui::PlayerBar_SlotLayout(slot0);
    int iconX = 4 + L0.spriteX, iconY = 4 + L0.spriteY;
    bool inertBlank = true, bridgeDrawn = false;
    for (int dy = 0; dy < 8; ++dy)
        for (int dx = 0; dx < 8; ++dx) {
            int px = iconX + dx, py = iconY + dy;
            if (px < 0 || py < 0 || px >= W || py >= H) continue;
            if (inert[py * W + px] != kWorld) inertBlank = false;
            if (real[py * W + px]  != kWorld) bridgeDrawn = true;
        }
    CHECK(inertBlank);    // inert hook: world shows through the icon block
    CHECK(bridgeDrawn);   // real blit leaf: the slot icon is rasterised

    // Determinism: a rerun with the bridge installed is byte-identical.
    std::vector<uint16_t> real2(W * H, 0);
    PaintWorld(real2);
    render::Surface sr2 = WrapSurface(real2, W, H, fmt);
    play::RenderHud(sr2, st, 4, 4, 8, 4, 8, 40, pal);
    CHECK(real == real2);

    play::UninstallRealHudBridge();
    io::VfsShutdown();
}
