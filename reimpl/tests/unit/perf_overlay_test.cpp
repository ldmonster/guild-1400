// unit: the in-game performance overlay — FPS/frametime math, detail cycling, and a
// render smoke test (the overlay actually paints into the chosen corner of a surface).
#include "test.h"
#include "render/perf_overlay.h"
#include "render/surface.h"
#include "render/types.h"

#include <cstdint>

using namespace guild;

TEST(PerfOverlay, FpsFromFrameIntervals) {
    render::PerfOverlay o;
    // 16 ms per frame -> ~62.5 FPS. Feed a steady cadence.
    for (int i = 0; i <= 60; ++i) o.Frame((std::uint32_t)(i * 16));
    CHECK(o.Samples() == 60);
    CHECK(o.FrameMs() > 15.5f && o.FrameMs() < 16.5f);
    CHECK(o.Fps() > 60.0f && o.Fps() < 65.0f);
}

TEST(PerfOverlay, FpsAt50) {
    render::PerfOverlay o;
    for (int i = 0; i <= 80; ++i) o.Frame((std::uint32_t)(i * 20));  // 20 ms -> 50 FPS
    CHECK(o.FrameMs() > 19.5f && o.FrameMs() < 20.5f);
    CHECK(o.Fps() > 48.0f && o.Fps() < 52.0f);
}

TEST(PerfOverlay, ZeroIntervalStaysFinite) {
    render::PerfOverlay o;
    for (int i = 0; i < 10; ++i) o.Frame(1000u);   // same timestamp every frame
    CHECK(o.Fps() > 0.0f);                          // clamped, not inf/NaN
    CHECK(o.Fps() <= 1000.0f);
}

TEST(PerfOverlay, CycleDetail) {
    render::PerfOverlay o;
    CHECK(!o.Config().enabled);
    o.CycleDetail(); CHECK(o.Config().enabled); CHECK_EQ(o.Config().detail, 1); // off -> 1
    o.CycleDetail(); CHECK_EQ(o.Config().detail, 2);
    o.CycleDetail(); CHECK_EQ(o.Config().detail, 3);
    o.CycleDetail(); CHECK(!o.Config().enabled);                                 // 3 -> off
}

namespace {
// Count non-black pixels in a 32bpp region [x0,x1) x [y0,y1).
int NonBlack(render::Surface* s, int x0, int y0, int x1, int y1) {
    int n = 0;
    for (int y = y0; y < y1; ++y) {
        auto* row = reinterpret_cast<std::uint32_t*>(
            static_cast<std::uint8_t*>(s->pixels) + (std::size_t)y * s->pitch);
        for (int x = x0; x < x1; ++x) if ((row[x] & 0x00FFFFFFu) != 0) ++n;
    }
    return n;
}
} // namespace

TEST(PerfOverlay, DrawsIntoChosenCorner) {
    const int W = 240, H = 160;
    render::Surface* fb = render::SurfaceCreate(W, H, 32);
    CHECK(fb != nullptr);
    render::SurfaceColorFill(fb, 0, 0, 0);

    render::PerfOverlay o;
    render::PerfOverlayConfig c; c.enabled = true; c.detail = 3; c.corner = render::PerfCorner::TopLeft;
    o.Configure(c);
    for (int i = 0; i <= 30; ++i) o.Frame((std::uint32_t)(i * 16));
    o.Draw(fb);

    // The overlay painted the top-left corner; the bottom-right stays black.
    const int tl = NonBlack(fb, 0, 0, 110, 60);
    const int br = NonBlack(fb, W - 60, H - 40, W, H);
    CHECK(tl > 50);          // box + text + graph drew
    CHECK_EQ(br, 0);

    // Disabled -> no-op (clear, draw, still clear).
    render::SurfaceColorFill(fb, 0, 0, 0);
    o.SetEnabled(false);
    o.Draw(fb);
    CHECK_EQ(NonBlack(fb, 0, 0, W, H), 0);

    render::SurfaceDestroy(fb);
}
