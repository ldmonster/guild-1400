// e2e (GUARDED on the real gfx/gilde.gfx): play::SessionHud over the REAL
// shipped artwork archive.
//
//   * Init loads + parses the real 59.5MB gilde.gfx with the REAL loaders
//     (gui::Form_LoadFromBuffer -> 1806 records; render::GfxArchive directory).
//   * Pins the investigation finding: every data-carrying record is a 24bpp
//     (format-2) SHAPBANK; ZERO depth-1 banks ship, so render::ShapeShowFromBank
//     @0x5d861c cannot rasterize the real shapes without the unreconstructed
//     VIBE_Shape_ConvertRgbTo16 @0x5d7c0c (the named gap) — proven BEHAVIORALLY:
//     feeding the real _WIN_BORDER bank to the real leaf returns "success" but
//     paints nothing (the engine's depth-2 no-op), while the documented
//     DefaultHudSpriteBank fallback paints real pixels through the same leaf.
//   * Renders a full HUD frame over a 16bpp 565 framebuffer and asserts
//     non-trivial pixel output + determinism.
//   * Cross-checks the REAL depth-2 pixel decode (render::DecodeShapeBlob,
//     1:1 of VIBE_FrameTable_Index @0x5fbb24) still decodes the real artwork.
//
// Skips cleanly when the asset is absent (honors GUILD_GAME_DIR).
#include "test.h"

#include "play/session_hud.h"
#include "play/wire_hud_bridge.h"
#include "render/gfx_archive.h"
#include "render/sprite_scale.h"
#include "render/shape_blit.h"
#include "render/animation_decode.h"
#include "render/colorformat.h"
#include "gui/form_loader.h"
#include "shim_impl/disk_filesystem.h"

#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

using guild::u8;
using guild::u16;
using guild::u32;
using guild::play::SessionHud;
using guild::play::HudBarObject;
using guild::play::HudMarker;

namespace {

std::string GameDir() {
    if (const char* env = std::getenv("GUILD_GAME_DIR")) return env;
    return "/home/cnupt/work/reverse/guild-1400/reimpl/europe_guild_1400_original";
}

int NonZero(const std::vector<u16>& fb) {
    int n = 0;
    for (u16 p : fb) if (p) ++n;
    return n;
}

inline u16 RdU16(const u8* p) { return (u16)(p[0] | (p[1] << 8)); }
inline u32 RdU32(const u8* p) {
    return (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24);
}

} // namespace

// ---------------------------------------------------------------------------
TEST(SessionHudReal, InitLoadsRealGildeGfxAndPinsTheGap) {
    guild::shim::DiskFileSystem fs(GameDir());
    if (!fs.exists("gfx/gilde.gfx")) { CHECK(true); return; }  // GUARDED skip

    SessionHud hud;
    CHECK(hud.Init(&fs));
    CHECK(hud.gfxLoaded());

    // The shipped archive: 1806 84-byte records (the d2:fileobj table the real
    // Form_LoadFromBuffer populated), 172 of which carry a SHAPBANK blob.
    CHECK_EQ(hud.gfxObjectCount(), 1806);
    CHECK_EQ(guild::gui::g_gfxObjectCount, 1806);
    CHECK_EQ(hud.bankCount(), 172);
    CHECK_EQ((int)hud.archive().recordCount(), 1806);

    // THE FINDING: every bank is format 2 (24bpp); zero depth-1 banks ship.
    CHECK_EQ(hud.fmt2BankCount(), hud.bankCount());
    CHECK_EQ(hud.depth1BankCount(), 0);
    CHECK(!hud.realShapesUsable());
    CHECK(std::strstr(SessionHud::missingDecode(), "0x5d7c0c") != nullptr);
}

// ---------------------------------------------------------------------------
// Behavioral proof of the gap: the REAL _WIN_BORDER bank fed to the REAL
// ShapeShowFromBank leaf is the engine's depth-2 no-op (success, zero pixels);
// the documented DefaultHudSpriteBank fallback paints through the same leaf.
TEST(SessionHudReal, RealDepth2BankIsANoOpInTheBlitter) {
    guild::shim::DiskFileSystem fs(GameDir());
    if (!fs.exists("gfx/gilde.gfx")) { CHECK(true); return; }  // GUARDED skip

    // Pull the raw _WIN_BORDER SHAPBANK bytes straight from the file.
    guild::shim::IFile* f = fs.open("gfx/gilde.gfx", "rb");
    CHECK(f != nullptr);
    if (!f) return;
    std::vector<u8> head(4 + 84);          // count + record 0
    f->seek(0, 0);
    CHECK_EQ(f->read(head.data(), head.size()), head.size());
    const u32 off  = RdU32(head.data() + 4 + 48);
    const u32 size = RdU32(head.data() + 4 + 56);
    CHECK(off > 0);
    CHECK(size > 0x45);
    std::vector<u8> bank(size);
    f->seek((long)off, 0);
    CHECK_EQ(f->read(bank.data(), bank.size()), bank.size());
    fs.close(f);

    CHECK(std::memcmp(bank.data(), "SHAPBANK", 8) == 0);
    CHECK_EQ((int)bank[52], 2);                       // 24bpp bank format
    const u32 shp0 = RdU32(bank.data() + 0x45);
    CHECK_EQ((int)bank[shp0 + 12], 2);                // first shape depth 2

    // Feed the REAL bank to the REAL leaf: depth-2 -> "no-op success".
    std::vector<u16> px(64 * 64, 0);
    guild::render::ColorBlitTarget16 dst;
    dst.widthPx = 64;
    dst.pixels  = px.data();
    guild::render::FrameBlitState st;
    const int rv = guild::render::ShapeShowFromBank(
        4, 4, bank.data(), 0, dst, guild::render::Format565(), st);
    CHECK_EQ(rv, 1);                                  // leaf reports success
    CHECK_EQ(NonZero(px), 0);                         // ...but paints NOTHING

    // The fallback bank paints real pixels through the very same leaf chain.
    guild::play::InstallRealHudBridge();
    std::vector<u16> px2(64 * 64, 0);
    const int rv2 = guild::play::BlitHudSprite(4, 4, 0, px2.data(), 64,
                                               guild::render::Format565());
    CHECK_EQ(rv2, 1);
    CHECK(NonZero(px2) > 0);
}

// ---------------------------------------------------------------------------
// The reconstructed depth-2 pixel decode still reads the REAL artwork (so the
// real pixels ARE recoverable — only the depth-1 bank CONVERSION is missing).
TEST(SessionHudReal, ReconstructedDepth2DecodeReadsRealShapes) {
    guild::shim::DiskFileSystem fs(GameDir());
    if (!fs.exists("gfx/gilde.gfx")) { CHECK(true); return; }  // GUARDED skip

    SessionHud hud;
    CHECK(hud.Init(&fs));

    guild::render::DecodedShape sh;
    CHECK(hud.archive().DecodeShapeByName("_WIN_BORDER", 0, sh));
    CHECK_EQ(sh.width, 8);
    CHECK_EQ(sh.height, 8);
    CHECK(sh.opaque > 0);
}

// ---------------------------------------------------------------------------
TEST(SessionHudReal, RenderFullHudOverRealAssetsIsNonTrivialAndDeterministic) {
    guild::shim::DiskFileSystem fs(GameDir());
    if (!fs.exists("gfx/gilde.gfx")) { CHECK(true); return; }  // GUARDED skip

    SessionHud hud;
    CHECK(hud.Init(&fs));

    const int W = 640, H = 480, P = W * 2;
    guild::sim::GameTime clk{};
    clk.day = 23; clk.hour = 14; clk.minute = 5; clk.second = 0;
    HudBarObject bars[4] = {{100, 0.2}, {101, 0.6}, {102, 0.9}, {103, 1.0}};
    HudMarker mks[2] = {{12.0f, -8.0f}, {0.0f, 0.0f}};

    SessionHud::Inputs in;
    in.money = 250000;
    in.clock = &clk;
    in.clockTick = 120;
    in.selectedId = 314;
    in.selectedName = "GERICHT";
    in.barObjects = bars;
    in.barObjectCount = 4;
    in.markers = mks;
    in.markerCount = 2;

    std::vector<u16> a((size_t)W * H, 0), b((size_t)W * H, 0);
    hud.Render(a.data(), W, H, P, in);
    hud.Render(b.data(), W, H, P, in);

    const guild::play::SessionHudResult& r = hud.lastResult();
    CHECK_EQ(r.barSlotsDrawn, 4);
    CHECK(r.captionGlyphs > 0);
    CHECK(r.statusGlyphs > 0);
    CHECK(r.markersDrawn >= 1);
    CHECK(r.spriteBlits > 0);

    CHECK(NonZero(a) > 500);                          // non-trivial HUD output
    CHECK(std::memcmp(a.data(), b.data(), a.size() * 2) == 0);  // deterministic
}
