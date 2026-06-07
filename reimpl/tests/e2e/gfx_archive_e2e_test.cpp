// e2e (GUARDED on the real gfx/gilde.gfx): load the shipped archive, decode the
// REAL menu artwork (#1773 _MENUE_BACKGROUND 800x600, #174 _BUTTON_RED, #1776
// _MAIN_MENU_RAHMEN), assert sane dims / nonzero coverage / finite colours, dump
// PPM proofs to /tmp, and confirm the decode is deterministic.  Skips cleanly
// when the asset is absent (CHECK(true); return).
#include "test.h"

#include "play/menu_assets.h"
#include "render/gfx_archive.h"
#include "shim_impl/disk_filesystem.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

using guild::render::GfxArchive;
using guild::render::DecodedShape;

namespace {

std::string GameDir() {
    if (const char* env = std::getenv("GUILD_GAME_DIR")) return env;
    return "/home/cnupt/work/reverse/reverse-guild/reimpl/europe_guild_1400_original";
}

// Count opaque pixels (A != 0).
int Opaque(const DecodedShape& s) {
    int n = 0;
    for (std::uint32_t p : s.argb) if (p & 0xFF000000u) ++n;
    return n;
}

void DumpPPM(const DecodedShape& s, const char* path) {
    std::FILE* f = std::fopen(path, "wb");
    if (!f) return;
    std::fprintf(f, "P6\n%d %d\n255\n", s.width, s.height);
    std::vector<std::uint8_t> row((std::size_t)s.width * 3);
    for (int y = 0; y < s.height; ++y) {
        for (int x = 0; x < s.width; ++x) {
            std::uint32_t p = s.argb[(std::size_t)y * s.width + x];
            row[x * 3 + 0] = (std::uint8_t)((p >> 16) & 0xFF);
            row[x * 3 + 1] = (std::uint8_t)((p >> 8) & 0xFF);
            row[x * 3 + 2] = (std::uint8_t)(p & 0xFF);
        }
        std::fwrite(row.data(), 1, row.size(), f);
    }
    std::fclose(f);
}

} // namespace

TEST(GfxArchiveReal, DecodeMenuArt) {
    guild::shim::DiskFileSystem fs(GameDir());
    if (!fs.exists("gfx/gilde.gfx")) { CHECK(true); return; }  // GUARDED skip

    GfxArchive arc;
    CHECK(arc.LoadFromFile(fs, "gfx/gilde.gfx"));
    if (!arc.ok()) { CHECK(true); return; }
    CHECK(arc.recordCount() > 1000);

    // ---- background #1773 _MENUE_BACKGROUND ----
    int bgIdx = arc.FindByName("_MENUE_BACKGROUND");
    CHECK(bgIdx >= 0);
    DecodedShape bg;
    CHECK(arc.DecodeShape(bgIdx, 0, bg));
    CHECK_EQ(bg.width, 800);
    CHECK_EQ(bg.height, 600);
    int bgOpaque = Opaque(bg);
    // The background is a near-full-frame image (rounded transparent corners).
    CHECK(bgOpaque > (int)(0.5 * bg.width * bg.height));
    std::printf("  _MENUE_BACKGROUND %dx%d opaque=%d (%.1f%%) sample@center=0x%08X\n",
                bg.width, bg.height, bgOpaque,
                100.0 * bgOpaque / (bg.width * bg.height),
                bg.argb[(std::size_t)(bg.height / 2) * bg.width + bg.width / 2]);
    DumpPPM(bg, "/tmp/guild_menu_bg.ppm");
    std::printf("  wrote /tmp/guild_menu_bg.ppm\n");

    // ---- button #174 _BUTTON_RED ----
    int btnIdx = arc.FindByName("_BUTTON_RED");
    CHECK(btnIdx >= 0);
    int shapes = arc.ShapeCount(btnIdx);
    CHECK(shapes >= 1);
    // The wide centre face is shape 2 (100x33) in the real bank.
    int faceShape = shapes > 2 ? 2 : 0;
    DecodedShape btn;
    CHECK(arc.DecodeShape(btnIdx, faceShape, btn));
    CHECK(btn.width > 0 && btn.height > 0);
    CHECK(btn.width <= 1024 && btn.height <= 1024);
    int btnOpaque = Opaque(btn);
    CHECK(btnOpaque > 0);
    std::printf("  _BUTTON_RED[%d] %dx%d opaque=%d (%.1f%%) sample@center=0x%08X\n",
                faceShape, btn.width, btn.height, btnOpaque,
                100.0 * btnOpaque / (btn.width * btn.height),
                btn.argb[(std::size_t)(btn.height / 2) * btn.width + btn.width / 2]);
    DumpPPM(btn, "/tmp/guild_button_red.ppm");
    std::printf("  wrote /tmp/guild_button_red.ppm\n");

    // ---- frame #1776 _MAIN_MENU_RAHMEN ----
    int rahIdx = arc.FindByName("_MAIN_MENU_RAHMEN");
    if (rahIdx >= 0) {
        DecodedShape rah;
        CHECK(arc.DecodeShape(rahIdx, 0, rah));
        CHECK_EQ(rah.width, 300);
        CHECK_EQ(rah.height, 320);
        CHECK(Opaque(rah) > 0);
    }

    // ---- determinism: re-decode and assert byte-identical ----
    DecodedShape bg2;
    CHECK(arc.DecodeShape(bgIdx, 0, bg2));
    CHECK(bg2.argb == bg.argb);

    // Reload from file and re-decode -> identical.
    GfxArchive arc2;
    CHECK(arc2.LoadFromFile(fs, "gfx/gilde.gfx"));
    DecodedShape bg3;
    CHECK(arc2.DecodeShape(bgIdx, 0, bg3));
    CHECK(bg3.argb == bg.argb);
}

TEST(GfxArchiveReal, MenuAssetsLoadsRealArt) {
    guild::shim::DiskFileSystem fs(GameDir());
    if (!fs.exists("gfx/gilde.gfx")) { CHECK(true); return; }  // GUARDED skip

    guild::play::MenuAssets assets;
    CHECK(assets.Load(fs));
    if (!assets.loaded()) { CHECK(true); return; }
    CHECK_EQ(assets.backgroundWidth(), 800);
    CHECK_EQ(assets.backgroundHeight(), 600);
    CHECK(assets.buttonFrameCount() >= 1);

    // The generic gfx-id sprite path resolves the button artwork.
    const guild::render::DecodedShape* sprite = assets.SpriteForGfxId(174);
    CHECK(sprite != nullptr);
    CHECK(sprite->width > 0 && sprite->height > 0);
}
