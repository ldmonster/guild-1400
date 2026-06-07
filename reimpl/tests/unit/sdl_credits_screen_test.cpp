// Unit tests for play::RunCreditsScreen — the native credits-crawl screen.
// Backend-agnostic: MemoryGraphicsDevice + a sequenced test platform (the same
// SeqPlatform pattern as sdl_menu_test).  No real assets here (gameDir empty).
#include "test.h"
#include "play/sdl_credits_screen.h"
#include "gui/credits.h"
#include "shim_impl/memory_graphics.h"
#include "shim/IPlatform.h"
#include <vector>
#include <cstdint>

using namespace guild;

namespace {
struct SeqPlatform : shim::IPlatform {
    struct Step { int x = 0, y = 0; bool left = false; int key = 0; };
    std::vector<Step> steps;
    int iter = 0;
    int maxPumps = 100000;

    bool createMainWindow(const char*, int, int, bool) override { return true; }
    void destroyMainWindow() override {}
    bool pumpMessages() override { ++iter; return iter < maxPumps; }
    std::uint32_t timeMs() override { return (std::uint32_t)iter * 16u; }
    void sleepMs(std::uint32_t) override {}
    void getMouse(shim::MouseState& o) override {
        const Step& s = at(); o.x = s.x; o.y = s.y; o.left = s.left;
    }
    bool keyDown(int vk) override { return at().key == vk; }
    const Step& at() const {
        static Step z;
        if (steps.empty()) return z;
        int i = iter < (int)steps.size() ? iter : (int)steps.size() - 1;
        if (i < 0) i = 0;
        return steps[i];
    }
};

play::CreditsScreenConfig BaseCfg() {
    play::CreditsScreenConfig c;
    c.fbW = 320; c.fbH = 240; c.frameCapMs = 0; c.maxFrames = 8;
    return c;   // no gameDir -> inert (no art); default credit lines
}
} // namespace

// The reconstructed scroll model is what we drive: sanity-check it directly first.
TEST(CreditsScreenUnit, ScrollModelMatchesRecovery) {
    // Step ramp: <30 -> 1, [30,80) -> 2, >=80 -> 4.
    CHECK(gui::Credits_ScrollStep(10.0f) == 1);
    CHECK(gui::Credits_ScrollStep(30.0f) == 2);
    CHECK(gui::Credits_ScrollStep(79.9f) == 2);
    CHECK(gui::Credits_ScrollStep(80.0f) == 4);
    // Initial offset is -screenHeight (text starts below the window).
    CHECK(gui::Credits_InitialOffset(600) == -600);
    // Advance only on (frame % step == 0).
    CHECK(gui::Credits_AdvanceOffset(0, 0, 2) == 1);
    CHECK(gui::Credits_AdvanceOffset(0, 1, 2) == 0);
    CHECK(gui::Credits_AdvanceOffset(5, 4, 2) == 6);
}

TEST(CreditsScreenUnit, OffsetAdvancesDeterministicallyPerFrame) {
    shim::MemoryGraphicsDevice dev;
    CHECK(dev.init(320, 240, 32, false));
    SeqPlatform plat;                         // no input -> just scrolls
    auto cfg = BaseCfg();
    cfg.frameTimeMetric = 30.0f;              // step 2
    play::CreditsScreenResult r = play::RunCreditsScreen(dev, plat, cfg);
    CHECK(r.framesPresented == cfg.maxFrames);
    // Started at -240; step 2 over 8 frames advances on frames 0,2,4,6 -> +4.
    // (frame counter reaches 8; advances counted at frames 0,2,4,6.)
    CHECK(r.scrollOffset == -240 + 4);
    CHECK(!r.finished);
    CHECK(!r.back);
}

TEST(CreditsScreenUnit, FasterMetricScrollsFaster) {
    shim::MemoryGraphicsDevice dev;
    CHECK(dev.init(320, 240, 32, false));
    SeqPlatform slowP, fastP;
    auto slow = BaseCfg(); slow.frameTimeMetric = 10.0f;   // step 1
    auto fast = BaseCfg(); fast.frameTimeMetric = 80.0f;   // step 4
    play::CreditsScreenResult rs = play::RunCreditsScreen(dev, slowP, slow);
    play::CreditsScreenResult rf = play::RunCreditsScreen(dev, fastP, fast);
    // step 1 advances every frame (8); step 4 advances on frames 0,4 (2).
    CHECK(rs.scrollOffset == -240 + 8);
    CHECK(rf.scrollOffset == -240 + 2);
}

TEST(CreditsScreenUnit, EscReturnsEarly) {
    shim::MemoryGraphicsDevice dev;
    CHECK(dev.init(320, 240, 32, false));
    SeqPlatform plat;
    SeqPlatform::Step s; s.key = 0x1B;        // ESC held
    plat.steps = { s };
    auto cfg = BaseCfg(); cfg.maxFrames = -1; // rely on ESC to end it
    play::CreditsScreenResult r = play::RunCreditsScreen(dev, plat, cfg);
    CHECK(r.back);
    CHECK(r.quitByEsc);
    CHECK(!r.finished);
    CHECK(r.framesPresented == 1);            // one frame then ESC
}

TEST(CreditsScreenUnit, ClickReturnsEarly) {
    shim::MemoryGraphicsDevice dev;
    CHECK(dev.init(320, 240, 32, false));
    SeqPlatform plat;
    // Frame 0: button up (seed prevLeft=false); frame 1+: pressed -> edge.
    SeqPlatform::Step up; up.left = false;
    SeqPlatform::Step down; down.left = true;
    plat.steps = { up, down };
    auto cfg = BaseCfg(); cfg.maxFrames = -1;
    play::CreditsScreenResult r = play::RunCreditsScreen(dev, plat, cfg);
    CHECK(r.back);
    CHECK(r.quitByClick);
    CHECK(!r.quitByEsc);
}

TEST(CreditsScreenUnit, WindowCloseReturns) {
    shim::MemoryGraphicsDevice dev;
    CHECK(dev.init(320, 240, 32, false));
    SeqPlatform plat;
    plat.maxPumps = 3;                        // window closes after 3 pumps
    auto cfg = BaseCfg(); cfg.maxFrames = -1;
    play::CreditsScreenResult r = play::RunCreditsScreen(dev, plat, cfg);
    CHECK(r.quitByWindow);
    CHECK(r.back);
}

TEST(CreditsScreenUnit, ReachingEndFinishes) {
    shim::MemoryGraphicsDevice dev;
    CHECK(dev.init(64, 64, 32, false));
    SeqPlatform plat;
    auto cfg = BaseCfg();
    cfg.fbW = 64; cfg.fbH = 64;               // small screen -> short initial offset
    cfg.lines = { "A", "B" };                 // tiny text -> finishes quickly
    cfg.lineHeight = 4;
    cfg.frameTimeMetric = 80.0f;              // step 4 (fast)
    cfg.maxFrames = 100000;                   // let it run to completion
    play::CreditsScreenResult r = play::RunCreditsScreen(dev, plat, cfg);
    CHECK(r.finished);
    CHECK(!r.back);
    // Completion predicate: textBottom(600) < textHeight(2*4=8)*1.5 + offset.
    CHECK(gui::Credits_ScrollComplete(600, 8, 1.5, r.scrollOffset));
}

TEST(CreditsScreenUnit, Deterministic) {
    shim::MemoryGraphicsDevice dev;
    CHECK(dev.init(320, 240, 32, false));
    SeqPlatform p1, p2;
    auto cfg = BaseCfg();
    play::CreditsScreenResult a = play::RunCreditsScreen(dev, p1, cfg);
    play::CreditsScreenResult b = play::RunCreditsScreen(dev, p2, cfg);
    CHECK(a.scrollOffset == b.scrollOffset);
    CHECK(a.framesPresented == b.framesPresented);
    CHECK(a.finished == b.finished);
}

TEST(CreditsScreenUnit, DefaultCreditLinesNonEmpty) {
    CHECK(!play::DefaultCreditLines().empty());
}
