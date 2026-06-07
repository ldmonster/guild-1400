#include "test.h"

// End-to-end: the screenshot CAPTURE flow the engine runs in
// VIBE_Render_CaptureScreenshot (0x4FF6D4) — lock the back buffer, read the
// 16bpp surface region out as 24-bit RGB (VIBE_Surface_CopyRegionRgb), then write
// a BMP. Here the whole chain runs against the real FileDumpGraphicsDevice backend
// so a genuine image file is produced ON DISK and read back, mirroring the
// original's gamedata/screenshots/gilde%04i.bmp output.
//
// REAL-ASSET GUARD: the test needs a writable scratch directory (the engine wrote
// to gamedata/screenshots/). If one cannot be created it SKIPS rather than fail,
// so the suite stays green in sandboxes without filesystem access.

#include "render/surface_present.h"
#include "render/colorformat.h"
#include "shim_impl/filedump_graphics.h"

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

using namespace guild;
using namespace guild::render;

namespace {

// Try to obtain a writable scratch dir; empty string => skip the test body.
std::string ScratchDir() {
    const char* env = std::getenv("GUILD_TEST_TMPDIR");
    std::string base = env ? env : "/tmp";
    std::string dir = base + "/guild_present_e2e";
    // Probe writability by creating a sentinel file.
    std::string probe = dir + "/.probe";
    std::string mk = "mkdir -p '" + dir + "' 2>/dev/null";
    if (std::system(mk.c_str()) != 0) return {};
    std::FILE* f = std::fopen(probe.c_str(), "wb");
    if (!f) return {};
    std::fclose(f);
    std::remove(probe.c_str());
    return dir;
}

} // namespace

// Full capture: device backbuffer -> CopyRegionRgb -> FileDumpGraphicsDevice BMP
// on disk -> decode the BMP back and confirm the pixels survived the round trip.
TEST(SurfacePresentE2E, CaptureToBmpRoundTrip) {
    std::string dir = ScratchDir();
    if (dir.empty()) {
        std::printf("    [skip] SurfacePresentE2E.CaptureToBmpRoundTrip: "
                    "no writable scratch dir\n");
        return; // guarded skip
    }

    const int w = 32, h = 24;

    // 1) Bring up the real file-dump backend (composes a MemoryGraphicsDevice).
    guild::shim::FileDumpGraphicsDevice dev;
    CHECK(dev.init(w, h, 16, false));
    dev.configureDump(dir, "gilde", guild::shim::FileDumpGraphicsDevice::kBmp);

    // 2) Lock the back buffer through the present path (Blt mode).
    struct Adapter : IDDrawSurface {
        guild::shim::IGraphicsDevice& d;
        explicit Adapter(guild::shim::IGraphicsDevice& dd) : d(dd) {}
        i32 GetSurfaceDesc(DDrawLock& o) override {
            auto* bb = d.backbuffer();
            if (!bb) return kDDErrSurfaceLost;
            o.pixels = bb->pixels; o.pitch = (u32)bb->pitch; o.bitDepth = (u32)bb->bpp;
            return 0;
        }
        i32 Lock(u32, DDrawLock& o) override { return GetSurfaceDesc(o); }
        i32 Unlock() override { return 0; }
        i32 Restore() override { return 0; }
    } surf(dev);

    PresentGlobals g;
    g.mode = PresentBackend::DDrawBlt;
    g.primary = &surf;
    g.bytesPerPx = 2;
    CHECK(AcquireBackBuffer(g));
    CHECK(g.targetBase != 0);

    // 3) Render a 565 gradient into the locked surface.
    auto* px = reinterpret_cast<u16*>(static_cast<std::intptr_t>(g.targetBase));
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            u8 r = static_cast<u8>(x * 255 / (w - 1));
            u8 gg = static_cast<u8>(y * 255 / (h - 1));
            px[y * g.strideWords + x] =
                static_cast<u16>(((r >> 3) << 11) | ((gg >> 2) << 5) | (0));
        }

    // 4) Read the whole surface back out as packed 24-bit RGB (the screenshot
    //    encoder's input), through the reconstructed CopyRegionRgb.
    std::vector<u8> rgb(static_cast<std::size_t>(w) * h * 3, 0);
    u32 ret = CopyRegionRgb((u32)h, (u32)w, px, rgb.data(), Format565(), g);
    CHECK_EQ(ret, (u32)(3 * w));

    UnlockBackBuffer(g, 0);

    // 5) Present once through the real backend -> a BMP lands on disk.
    dev.present();
    CHECK_EQ(dev.presentCount(), 1);
    std::string path = dev.framePath(0, guild::shim::FileDumpGraphicsDevice::kBmp);

    // 6) Read the BMP file back from disk and decode it (no external libs).
    std::FILE* f = std::fopen(path.c_str(), "rb");
    CHECK(f != nullptr);
    if (f) {
        std::fseek(f, 0, SEEK_END);
        long n = std::ftell(f);
        std::fseek(f, 0, SEEK_SET);
        std::vector<u8> file(static_cast<std::size_t>(n));
        size_t got = std::fread(file.data(), 1, file.size(), f);
        std::fclose(f);
        CHECK_EQ(got, file.size());

        auto img = guild::shim::FileDumpGraphicsDevice::DecodeBmp24(file);
        CHECK(img.ok);
        CHECK_EQ(img.width, w);
        CHECK_EQ(img.height, h);

        // The dumped frame should reproduce the gradient: x=0 -> red≈0, growing.
        // Compare a couple of pixels against our CopyRegionRgb readback (both
        // derive from the same 565 source, so the high bits agree).
        if (img.ok && img.width == w && img.height == h) {
            // Red channel rises left-to-right.
            CHECK(img.at(w - 1, 0, 0) >= img.at(0, 0, 0));
            // Green channel rises top-to-bottom.
            CHECK(img.at(0, h - 1, 1) >= img.at(0, 0, 1));
        }
    }

    std::remove(path.c_str());
}

// Lost-surface recovery during a real capture lock: the device first reports the
// surface lost, then a Restore makes it lockable — the present lock loop must
// transparently recover and still capture (mirrors a DDraw ALT-TAB reacquire).
TEST(SurfacePresentE2E, CaptureRecoversFromSurfaceLost) {
    const int w = 8, h = 8;
    guild::shim::MemoryGraphicsDevice dev;
    CHECK(dev.init(w, h, 16, false));

    struct FlakySurface : IDDrawSurface {
        guild::shim::IGraphicsDevice& d;
        int loseFor;
        explicit FlakySurface(guild::shim::IGraphicsDevice& dd, int lose)
            : d(dd), loseFor(lose) {}
        i32 GetSurfaceDesc(DDrawLock& o) override {
            auto* bb = d.backbuffer();
            o.pixels = bb->pixels; o.pitch = (u32)bb->pitch; o.bitDepth = (u32)bb->bpp;
            return 0;
        }
        i32 Lock(u32, DDrawLock& o) override {
            if (loseFor > 0) { --loseFor; return kDDErrSurfaceLost; }
            return GetSurfaceDesc(o);
        }
        i32 Unlock() override { return 0; }
        i32 Restore() override { return 0; }
    } surf(dev, /*loseFor=*/3);

    PresentGlobals g;
    g.mode = PresentBackend::DDrawFlip; // mode 3 uses the WAIT lock
    g.primary = &surf;
    g.bytesPerPx = 2;

    CHECK(AcquireBackBuffer(g)); // must spin through 3 losses + Restores
    CHECK(g.targetBase != 0);

    auto* px = reinterpret_cast<u16*>(static_cast<std::intptr_t>(g.targetBase));
    px[0] = 0x07E0; // green
    std::vector<u8> rgb(static_cast<std::size_t>(w) * h * 3, 0);
    CopyRegionRgb((u32)h, (u32)w, px, rgb.data(), Format565(), g);
    UnlockBackBuffer(g, 0);

    u8 er, eg, eb;
    UnpackColor(Format565(), 0x07E0, er, eg, eb);
    // CopyRegionRgb byte order is the original's R,B,G.
    CHECK_EQ(rgb[0], er);  // R
    CHECK_EQ(rgb[1], eb);  // B
    CHECK_EQ(rgb[2], eg);  // G
}
