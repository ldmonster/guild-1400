// End-to-end: boot the FULL app spine (init -> session frames -> 13-step shutdown)
// through GameApp on RealSubsystems + headless shims, with the REAL per-frame
// draw-list pipeline wired (sceneWalk + flushDrawList FrameHooks, mock->real this
// pass). A world frame now BUILDS + PROJECTS + CULLS + SORTS + RASTERIZES a real
// draw list across the render siblings and presents it. We run a bounded session,
// assert the real render hook ran and the rasterizer drew real triangles, then
// dump the resulting software framebuffer to a BMP so the rendered frame is a real
// artifact.
//
// GUARDED: the heavy full-spine boot only runs when GUILD_RUN_RENDER_E2E=1 is set
// (mirrors the real-asset-guarded e2e pattern). Without it the test no-ops with a
// trivial pass so the suite stays green in environments that skip the full boot.
#include "app/wiring.h"

#include "config/ini.h"
#include "shim_impl/loopback_socket.h"
#include "shim_impl/mem_filesystem.h"
#include "shim_impl/memory_graphics.h"
#include "shim_impl/null_audio.h"
#include "shim_impl/null_platform.h"
#include "tests/framework/test.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <vector>

using namespace guild;
using guild::app::RealSubsystems;
using guild::app::GameApp;

namespace {

// Write a 16bpp (565) framebuffer out as a 24bpp BMP (so it is universally viewable
// and the artifact is self-describing). Returns true on success.
bool DumpBmp565(const char* path, const std::uint16_t* px, int w, int h, int pitchBytes) {
    if (!px || w <= 0 || h <= 0)
        return false;
    const int rowBytes = (w * 3 + 3) & ~3;
    const int imgBytes = rowBytes * h;
    const int fileBytes = 54 + imgBytes;
    std::vector<std::uint8_t> buf(fileBytes, 0);
    auto put16 = [&](int o, std::uint16_t v) { buf[o] = v & 0xFF; buf[o + 1] = v >> 8; };
    auto put32 = [&](int o, std::uint32_t v) {
        buf[o] = v & 0xFF; buf[o + 1] = (v >> 8) & 0xFF;
        buf[o + 2] = (v >> 16) & 0xFF; buf[o + 3] = (v >> 24) & 0xFF;
    };
    buf[0] = 'B'; buf[1] = 'M';
    put32(2, fileBytes); put32(10, 54);
    put32(14, 40); put32(18, w); put32(22, h);
    put16(26, 1); put16(28, 24); put32(34, imgBytes);
    const int pitchPx = pitchBytes / 2;
    for (int y = 0; y < h; ++y) {
        const std::uint16_t* row = px + (std::size_t)(h - 1 - y) * pitchPx; // BMP bottom-up
        std::uint8_t* dst = buf.data() + 54 + (std::size_t)y * rowBytes;
        for (int x = 0; x < w; ++x) {
            std::uint16_t c = row[x];
            std::uint8_t r = (std::uint8_t)(((c >> 11) & 0x1F) << 3);
            std::uint8_t g = (std::uint8_t)(((c >> 5) & 0x3F) << 2);
            std::uint8_t b = (std::uint8_t)((c & 0x1F) << 3);
            dst[x * 3 + 0] = b; dst[x * 3 + 1] = g; dst[x * 3 + 2] = r;
        }
    }
    std::FILE* f = std::fopen(path, "wb");
    if (!f) return false;
    std::fwrite(buf.data(), 1, buf.size(), f);
    std::fclose(f);
    return true;
}

} // namespace

TEST(AppRenderFrameE2E, BootRenderPresentDumpBmp) {
    if (!std::getenv("GUILD_RUN_RENDER_E2E")) {
        CHECK(true); // guarded skip
        return;
    }

    shim::NullPlatform plat;
    shim::MemoryGraphicsDevice gfx;
    shim::NullAudioDevice audioDev;
    shim::MemFileSystem fs;
    auto pair = shim::LoopbackSocket::makePair();
    config::IniFile ini;

    RealSubsystems sub(&plat, &gfx, &audioDev, &fs, pair.first.get(), &ini);
    GameApp app(plat, gfx, audioDev, sub);

    // Full lifecycle: CreateMainWindow -> init -> N session frames -> shutdown.
    int exitCode = app.Run("\\project\\", /*displayMode=*/1, /*showIntro=*/false,
                           /*networkClient=*/false, /*framesPerSession=*/4);
    CHECK_EQ(exitCode, 0);

    // The real world-render hook ran for real (not a stub).
    CHECK(sub.firedReal("renderMainViewFrame"));
    // The real draw-list pipeline appended + rasterized real triangles.
    CHECK(sub.drawListPolys() >= 1);
    CHECK(sub.rasterTriangles() >= 1);

    // Dump the rendered framebuffer as a BMP artifact + assert visible pixels.
    int w = 0, hh = 0, pitch = 0;
    const void* px = sub.frameBufferPixels(&w, &hh, &pitch);
    CHECK(px != nullptr);
    if (px) {
        const std::uint16_t* p16 = reinterpret_cast<const std::uint16_t*>(px);
        int total = (pitch / 2) * hh;
        int nonZero = 0;
        for (int i = 0; i < total; ++i)
            if (p16[i] != 0) ++nonZero;
        CHECK(nonZero >= 1);            // the rasterizer wrote visible pixels
        bool dumped = DumpBmp565("/tmp/guild_render_frame.bmp", p16, w, hh, pitch);
        CHECK(dumped);
        std::printf("    [e2e] dumped %dx%d frame (%d nonzero px) to "
                    "/tmp/guild_render_frame.bmp\n", w, hh, nonZero);
    }
}
