// e2e (GUARDED on the real gfx/gilde.gfx): decode the REAL _CREDITS_BACKGROUND
// (#1792, 800x600), run RunCreditsScreen for a few scroll frames over it, assert
// the background decoded + the crawl rendered moving text, and dump rendered-frame
// PPM proofs to /tmp.  Skips cleanly (CHECK(true); return) when the asset is absent.
#include "test.h"

#include "play/sdl_credits_screen.h"
#include "render/gfx_archive.h"
#include "shim_impl/memory_graphics.h"
#include "shim_impl/disk_filesystem.h"
#include "shim/IPlatform.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

using namespace guild;

namespace {

std::string GameDir() {
    if (const char* env = std::getenv("GUILD_GAME_DIR")) return env;
    return "/home/cnupt/work/reverse/reverse-guild/reimpl/europe_guild_1400_original";
}

struct NoInputPlatform : shim::IPlatform {
    int iter = 0;
    bool createMainWindow(const char*, int, int, bool) override { return true; }
    void destroyMainWindow() override {}
    bool pumpMessages() override { ++iter; return true; }
    std::uint32_t timeMs() override { return (std::uint32_t)iter * 16u; }
    void sleepMs(std::uint32_t) override {}
    void getMouse(shim::MouseState& o) override { o.x = 0; o.y = 0; o.left = false; }
    bool keyDown(int) override { return false; }
};

// Dump a 32bpp BGRA framebuffer (as MemoryGraphicsDevice stores it) as a PPM.
void DumpFramebufferPPM(const std::vector<std::uint8_t>& fb, int w, int h,
                        const char* path) {
    std::FILE* f = std::fopen(path, "wb");
    if (!f) return;
    std::fprintf(f, "P6\n%d %d\n255\n", w, h);
    std::vector<std::uint8_t> row((std::size_t)w * 3);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const std::size_t i = ((std::size_t)y * w + x) * 4;
            // The 32bpp scratch is 0xAARRGGBB stored little-endian -> bytes B,G,R,A.
            row[x * 3 + 0] = fb[i + 2]; // R
            row[x * 3 + 1] = fb[i + 1]; // G
            row[x * 3 + 2] = fb[i + 0]; // B
        }
        std::fwrite(row.data(), 1, row.size(), f);
    }
    std::fclose(f);
}

int CountNonZero(const std::vector<std::uint8_t>& fb) {
    int n = 0;
    for (std::size_t i = 0; i + 3 < fb.size(); i += 4)
        if (fb[i] || fb[i + 1] || fb[i + 2]) ++n;
    return n;
}

} // namespace

TEST(CreditsScreenReal, DecodeBackgroundAndScroll) {
    shim::DiskFileSystem fs(GameDir());
    if (!fs.exists("gfx/gilde.gfx")) { CHECK(true); return; }  // GUARDED skip

    // ---- the real _CREDITS_BACKGROUND (#1792) ----
    render::GfxArchive arc;
    CHECK(arc.LoadFromFile(fs, "gfx/gilde.gfx"));
    if (!arc.ok()) { CHECK(true); return; }
    const int idx = arc.FindByName("_CREDITS_BACKGROUND");
    CHECK(idx >= 0);
    render::DecodedShape bg;
    CHECK(arc.DecodeShapeByName("_CREDITS_BACKGROUND", 0, bg));
    CHECK_EQ(bg.width, 800);
    CHECK_EQ(bg.height, 600);
    int bgOpaque = 0;
    for (std::uint32_t p : bg.argb) if (p & 0xFF000000u) ++bgOpaque;
    CHECK(bgOpaque > 0);                       // nonzero pixels
    std::printf("  _CREDITS_BACKGROUND %dx%d opaque=%d (%.1f%%)\n",
                bg.width, bg.height, bgOpaque,
                100.0 * bgOpaque / (bg.width * bg.height));

    // ---- run a few scroll frames over the real art ----
    const int W = 400, H = 300;
    auto renderUpTo = [&](int n) {
        shim::MemoryGraphicsDevice dev;
        dev.init(W, H, 32, false);
        NoInputPlatform plat;
        play::CreditsScreenConfig cfg;
        cfg.gameDir = GameDir();
        cfg.fbW = W; cfg.fbH = H; cfg.frameCapMs = 0; cfg.maxFrames = n;
        cfg.frameTimeMetric = 10.0f;          // step 1 (advance every frame)
        play::CreditsScreenResult r = play::RunCreditsScreen(dev, plat, cfg);
        return std::make_pair(r, dev.lastPresented());
    };

    auto early = renderUpTo(3);
    auto late  = renderUpTo(60);
    CHECK(early.first.haveAssets);            // the real art was used
    CHECK(late.first.haveAssets);
    CHECK(!early.second.empty());
    CHECK(early.second.size() == late.second.size());
    // Background + crawl both draw, so the frame is heavily non-blank.
    CHECK(CountNonZero(early.second) > (W * H) / 4);
    // The crawl moved between the two frames (text shifted upward over the bg).
    CHECK(early.second != late.second);

    DumpFramebufferPPM(early.second, W, H, "/tmp/guild_credits_f3.ppm");
    DumpFramebufferPPM(late.second,  W, H, "/tmp/guild_credits_f60.ppm");
    std::printf("  wrote /tmp/guild_credits_f3.ppm and /tmp/guild_credits_f60.ppm\n");
    std::printf("  frames3 offset=%d frames60 offset=%d\n",
                early.first.scrollOffset, late.first.scrollOffset);
}
