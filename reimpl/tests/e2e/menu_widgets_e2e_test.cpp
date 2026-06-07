// e2e (GUARDED on the real gfx/gilde.gfx): draw a REAL _BUTTON_RED 3-slice button
// at width 200 and a REAL _MAIN_MENU_RAHMEN window frame into a 32bpp ARGB buffer,
// assert nonzero real pixels + the correct extent (caps 12px, centre stretched),
// and dump PPM proofs to /tmp.  Skips cleanly when the asset is absent.
#include "test.h"

#include "render/gfx_archive.h"
#include "render/menu_widgets.h"
#include "shim_impl/disk_filesystem.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

using guild::render::GfxArchive;
using guild::render::DrawThreeSliceButton;
using guild::render::DrawWindowFrame;
using guild::render::ButtonSliceInfo;
using guild::render::FrameDrawInfo;
using guild::u32;

namespace {

std::string GameDir() {
    if (const char* env = std::getenv("GUILD_GAME_DIR")) return env;
    return "/home/cnupt/work/reverse/reverse-guild/reimpl/europe_guild_1400_original";
}

int Opaque(const std::vector<u32>& fb) {
    int n = 0;
    for (u32 p : fb) if (p & 0xFF000000u) ++n;
    return n;
}

void DumpPPM(const std::vector<u32>& fb, int W, int H, const char* path) {
    std::FILE* f = std::fopen(path, "wb");
    if (!f) return;
    std::fprintf(f, "P6\n%d %d\n255\n", W, H);
    std::vector<std::uint8_t> row((std::size_t)W * 3);
    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            u32 p = fb[(std::size_t)y * W + x];
            row[x * 3 + 0] = (std::uint8_t)((p >> 16) & 0xFF);
            row[x * 3 + 1] = (std::uint8_t)((p >> 8) & 0xFF);
            row[x * 3 + 2] = (std::uint8_t)(p & 0xFF);
        }
        std::fwrite(row.data(), 1, row.size(), f);
    }
    std::fclose(f);
}

} // namespace

TEST(MenuWidgetsReal, ButtonAndFrame) {
    guild::shim::DiskFileSystem fs(GameDir());
    if (!fs.exists("gfx/gilde.gfx")) { CHECK(true); return; }  // GUARDED skip

    GfxArchive arc;
    CHECK(arc.LoadFromFile(fs, "gfx/gilde.gfx"));
    if (!arc.ok()) { CHECK(true); return; }

    const int btn = arc.FindByName("_BUTTON_RED");
    const int frame = arc.FindByName("_MAIN_MENU_RAHMEN");
    CHECK(btn >= 0);
    CHECK(frame >= 0);
    if (btn < 0 || frame < 0) return;

    // Confirm the real 6-shape / single-panel layout we composed from.
    CHECK_EQ(arc.ShapeCount(btn), 6);
    CHECK(arc.ShapeCount(frame) >= 1);

    const int W = 320, H = 240;
    std::vector<u32> fb((std::size_t)W * H, 0u);

    // ---- real window frame (_MAIN_MENU_RAHMEN) ----
    FrameDrawInfo fi =
        DrawWindowFrame(fb.data(), W, H, 20, 20, 280, 200, arc, frame);
    CHECK(fi.real);
    CHECK(fi.thickness >= 1);
    std::printf("  _MAIN_MENU_RAHMEN frame real=%d thickness=%d tiled=%d\n",
                fi.real, fi.thickness, fi.tiled);

    // ---- real 3-slice button at width 200 ----
    const int bx = 60, by = 100, bw = 200;
    ButtonSliceInfo bi =
        DrawThreeSliceButton(fb.data(), W, H, bx, by, bw, arc, btn, false);
    CHECK(bi.real);
    CHECK_EQ(bi.capLeft, 12);                  // real _BUTTON_RED cap width
    CHECK_EQ(bi.capRight, 12);
    CHECK_EQ(bi.center, bw - 24);              // 176 stretched centre
    CHECK_EQ(bi.height, 33);
    std::printf("  _BUTTON_RED button real=%d capL=%d centre=%d capR=%d h=%d\n",
                bi.real, bi.capLeft, bi.center, bi.capRight, bi.height);

    // Pressed-state button below it (distinct down-state slices).
    ButtonSliceInfo bip =
        DrawThreeSliceButton(fb.data(), W, H, bx, by + 40, bw, arc, btn, true);
    CHECK(bip.real);
    CHECK_EQ(bip.center, bw - 24);

    const int op = Opaque(fb);
    CHECK(op > 0);
    // The button row (200x33 x2) + frame ring should be a substantial fill.
    CHECK(op > bw);  // at least more than one button row of pixels
    std::printf("  composed opaque pixels = %d / %d\n", op, W * H);

    DumpPPM(fb, W, H, "/tmp/guild_menu_widgets.ppm");
    std::printf("  wrote /tmp/guild_menu_widgets.ppm\n");

    // Determinism: two identical button+frame renders into fresh buffers match.
    std::vector<u32> a((std::size_t)W * H, 0u), b((std::size_t)W * H, 0u);
    DrawWindowFrame(a.data(), W, H, 20, 20, 280, 200, arc, frame);
    DrawThreeSliceButton(a.data(), W, H, bx, by, bw, arc, btn, false);
    DrawWindowFrame(b.data(), W, H, 20, 20, 280, 200, arc, frame);
    DrawThreeSliceButton(b.data(), W, H, bx, by, bw, arc, btn, false);
    CHECK(a == b);
}
