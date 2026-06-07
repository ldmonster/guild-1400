// End-to-end test for the visible-output graphics backend: drive several frames
// of changing content through FileDumpGraphicsDevice, dump them to a temp dir in
// BOTH BMP and PPM, then decode each dumped file and verify it matches the
// expected image. Exercises 8bpp palette expansion and a palette change between
// frames, plus the headless NullPlatform timing/input alongside.
#include "shim_impl/filedump_graphics.h"
#include "shim_impl/null_platform.h"
#include "tests/framework/test.h"

// Optional: the SDL2 window backend is only built/tested when GUILD_HAVE_SDL2 is
// defined. The default build never requires SDL2.
#ifdef GUILD_HAVE_SDL2
#include "shim_impl/sdl2_backend.h"
#include <cstdlib>
#endif

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

using namespace guild::shim;

namespace {

std::string makeTempDir() {
    char templ[] = "/tmp/guild_filedump_e2e_XXXXXX";
    char* d = mkdtemp(templ);
    return d ? std::string(d) : std::string();
}

std::vector<std::uint8_t> readFile(const std::string& path) {
    std::vector<std::uint8_t> data;
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return data;
    std::fseek(f, 0, SEEK_END);
    long n = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    if (n > 0) { data.resize(static_cast<std::size_t>(n));
        data.resize(std::fread(data.data(), 1, data.size(), f)); }
    std::fclose(f);
    return data;
}

// Reference: index at pixel (x,y) for frame f — a moving diagonal stripe.
std::uint8_t expectedIndex(int f, int x, int y, int w) {
    return static_cast<std::uint8_t>((x + y + f * 7) % w + 1);
}

} // namespace

TEST(ShimFileDumpE2E, ChangingFramesDecodeToExpected) {
    std::string dir = makeTempDir();
    CHECK(!dir.empty());

    const int W = 16, H = 12, FRAMES = 5;

    // Headless platform for realistic timing alongside the dump device.
    NullPlatform plat;
    CHECK(plat.createMainWindow("filedump-e2e", W, H, false));
    std::uint32_t t0 = plat.timeMs();

    FileDumpGraphicsDevice gfx;
    gfx.configureDump(dir, "scene", FileDumpGraphicsDevice::kBoth);
    CHECK(gfx.init(W, H, 8, false));

    // Initial palette: index i -> a deterministic RGB.
    auto buildPalette = [](int phase, std::uint32_t* pal) {
        for (int i = 0; i < 256; ++i) {
            std::uint8_t r = static_cast<std::uint8_t>((i * 3 + phase) & 0xFF);
            std::uint8_t g = static_cast<std::uint8_t>((i * 5 + phase * 2) & 0xFF);
            std::uint8_t b = static_cast<std::uint8_t>((i * 7 + phase * 3) & 0xFF);
            pal[i] = 0xFF000000u | (static_cast<std::uint32_t>(r) << 16) |
                     (static_cast<std::uint32_t>(g) << 8) | b;
        }
    };
    std::uint32_t pal[256];
    buildPalette(0, pal);
    gfx.setPalette(pal);

    Surface* s = gfx.backbuffer();
    CHECK(s != nullptr);
    auto* px = static_cast<std::uint8_t*>(s->pixels);

    for (int f = 0; f < FRAMES; ++f) {
        // Change palette halfway through to exercise palette-aware dumping.
        if (f == 3) { buildPalette(64, pal); gfx.setPalette(pal); }

        for (int y = 0; y < H; ++y)
            for (int x = 0; x < W; ++x)
                px[static_cast<std::size_t>(y) * s->pitch + x] = expectedIndex(f, x, y, W);

        gfx.present();
        if (plat.pumpMessages()) plat.sleepMs(0);
    }
    CHECK_EQ(gfx.presentCount(), FRAMES);
    CHECK(plat.timeMs() >= t0);

    // Verify each dumped frame in both formats decodes to the expected image,
    // reconstructing the palette that was active when that frame was presented.
    for (int f = 0; f < FRAMES; ++f) {
        std::uint32_t framePal[256];
        buildPalette(f >= 3 ? 64 : 0, framePal);

        auto bmp = FileDumpGraphicsDevice::DecodeBmp24(
            readFile(gfx.framePath(f, FileDumpGraphicsDevice::kBmp)));
        auto ppm = FileDumpGraphicsDevice::DecodePpm(
            readFile(gfx.framePath(f, FileDumpGraphicsDevice::kPpm)));
        CHECK(bmp.ok);
        CHECK(ppm.ok);
        CHECK_EQ(bmp.width, W);
        CHECK_EQ(bmp.height, H);
        CHECK_EQ(ppm.width, W);
        CHECK_EQ(ppm.height, H);

        for (int y = 0; y < H; ++y)
            for (int x = 0; x < W; ++x) {
                std::uint8_t idx = expectedIndex(f, x, y, W);
                std::uint32_t argb = framePal[idx];
                std::uint8_t r = (argb >> 16) & 0xFF;
                std::uint8_t g = (argb >> 8) & 0xFF;
                std::uint8_t b = argb & 0xFF;
                CHECK_EQ(bmp.at(x, y, 0), r);
                CHECK_EQ(bmp.at(x, y, 1), g);
                CHECK_EQ(bmp.at(x, y, 2), b);
                // BMP and PPM must agree pixel-for-pixel.
                CHECK_EQ(ppm.at(x, y, 0), r);
                CHECK_EQ(ppm.at(x, y, 1), g);
                CHECK_EQ(ppm.at(x, y, 2), b);
            }
    }
}

#ifdef GUILD_HAVE_SDL2
// Smoke test for the SDL2 backends, run headlessly via the dummy video driver so
// it needs no display. Verifies init/present/timing/input plumbing works.
TEST(ShimFileDumpE2E, Sdl2BackendSmoke) {
    setenv("SDL_VIDEODRIVER", "dummy", 1);

    Sdl2Platform plat;
    CHECK(plat.createMainWindow("sdl2-smoke", 32, 24, false));
    std::uint32_t t0 = plat.timeMs();

    Sdl2GraphicsDevice gfx;
    CHECK(gfx.init(32, 24, 32, false));
    Surface* s = gfx.backbuffer();
    CHECK(s != nullptr);
    auto* px = static_cast<std::uint8_t*>(s->pixels);
    for (int f = 0; f < 3; ++f) {
        for (int i = 0; i < s->width * s->height; ++i) {
            std::uint8_t* p = px + i * 4;
            p[0] = static_cast<std::uint8_t>(f * 10); p[1] = 0; p[2] = 0; p[3] = 0xFF;
        }
        gfx.present();          // must not crash
        plat.pumpMessages();    // must not crash
    }
    plat.sleepMs(1);
    CHECK(plat.timeMs() >= t0);
    CHECK(!plat.keyDown('A')); // no key pressed in dummy driver

    gfx.shutdown();
    plat.destroyMainWindow();
}
#endif // GUILD_HAVE_SDL2
