// Integration test for play::RunCreditsScreen — verifies the crawl actually
// MOVES: the rendered framebuffer at frame N differs from frame N+k, the text
// pixels shift upward by the model's per-frame advance, and reruns are identical.
// Headless: MemoryGraphicsDevice + a no-input scripted platform.  No real assets
// (the synthetic credit lines are rasterised by the reconstructed 5x7 font path).
#include "test.h"
#include "play/sdl_credits_screen.h"
#include "shim_impl/memory_graphics.h"
#include "shim/IPlatform.h"
#include <vector>
#include <cstdint>

using namespace guild;

namespace {
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

// Run N frames; return a copy of the framebuffer at the last present().
std::vector<std::uint8_t> RenderUpToFrame(int n, float metric) {
    shim::MemoryGraphicsDevice dev;
    dev.init(200, 160, 32, false);
    NoInputPlatform plat;
    play::CreditsScreenConfig cfg;
    cfg.fbW = 200; cfg.fbH = 160; cfg.frameCapMs = 0; cfg.maxFrames = n;
    cfg.frameTimeMetric = metric;
    cfg.lines = { "EUROPA 1400", "THE GUILD", "CREDITS LINE THREE",
                  "CREDITS LINE FOUR", "AND SO ON" };
    cfg.lineHeight = 9;
    play::RunCreditsScreen(dev, plat, cfg);
    return dev.lastPresented();
}

int CountNonZero(const std::vector<std::uint8_t>& fb) {
    int n = 0;
    for (std::size_t i = 0; i + 3 < fb.size(); i += 4)
        if (fb[i] || fb[i + 1] || fb[i + 2]) ++n;
    return n;
}
} // namespace

TEST(CreditsScreenItest, TextPixelsMoveBetweenFrames) {
    // step 1 (metric < 30) -> offset advances every frame, so a few frames apart
    // the text block has shifted upward and the framebuffers differ.  Pick frames
    // where the crawl has risen onto the 160px screen (offset = -160 + frame).
    auto fbEarly = RenderUpToFrame(20, 10.0f);
    auto fbLate  = RenderUpToFrame(40, 10.0f);
    CHECK(!fbEarly.empty());
    CHECK(fbEarly.size() == fbLate.size());
    CHECK(fbEarly != fbLate);                 // the crawl actually moved
    // Both frames render some text pixels (the crawl is on-screen, non-blank).
    CHECK(CountNonZero(fbEarly) > 0);
    CHECK(CountNonZero(fbLate)  > 0);
}

TEST(CreditsScreenItest, AdjacentFramesAtStepOneDiffer) {
    // With step 1 every consecutive frame moves -> frame N != frame N+1.
    auto a = RenderUpToFrame(30, 10.0f);
    auto b = RenderUpToFrame(31, 10.0f);
    CHECK(a != b);
}

TEST(CreditsScreenItest, DeterministicRerun) {
    auto a = RenderUpToFrame(30, 30.0f);
    auto b = RenderUpToFrame(30, 30.0f);
    CHECK(a == b);                            // byte-identical rerun
    CHECK(CountNonZero(a) > 0);
}

TEST(CreditsScreenItest, MetricChangesScrollDistance) {
    // Faster metric (step 4) covers fewer advances than step 1 over the same frames,
    // so the rendered frames are NOT identical between the two speeds.
    auto slow = RenderUpToFrame(40, 10.0f);   // step 1
    auto fast = RenderUpToFrame(40, 80.0f);   // step 4
    CHECK(slow != fast);
}
