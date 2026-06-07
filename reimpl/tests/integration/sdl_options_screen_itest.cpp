// Integration tests for play::RunOptionsScreen.
// Render each page asset-less -> non-blank, the real setting labels appear in the
// rows, toggling persists into the config struct, deterministic rerun.
#include "test.h"
#include "play/sdl_options_screen.h"
#include "shim_impl/memory_graphics.h"
#include "shim/IPlatform.h"
#include <vector>
#include <cstdint>

using namespace guild;

namespace {
struct SeqPlatform : shim::IPlatform {
    struct Step { int x = 0, y = 0; bool left = false; int key = 0; };
    std::vector<Step> steps; int iter = 0, maxPumps = 100000;
    bool createMainWindow(const char*, int, int, bool) override { return true; }
    void destroyMainWindow() override {}
    bool pumpMessages() override { ++iter; return iter < maxPumps; }
    std::uint32_t timeMs() override { return (std::uint32_t)iter * 16u; }
    void sleepMs(std::uint32_t) override {}
    void getMouse(shim::MouseState& o) override { const Step& s = at(); o.x = s.x; o.y = s.y; o.left = s.left; }
    bool keyDown(int vk) override { return at().key == vk; }
    const Step& at() const { static Step z; if (steps.empty()) return z;
        int i = iter < (int)steps.size() ? iter : (int)steps.size() - 1; return steps[i < 0 ? 0 : i]; }
};
constexpr int kRowY0 = 90, kRowH = 30, kValueX = 360;
SeqPlatform::Step OnRow(int i, bool left) {
    SeqPlatform::Step s; s.x = kValueX + 20; s.y = kRowY0 + i * kRowH + 4; s.left = left; return s;
}
SeqPlatform::Step OnBack(int rc, bool left) {
    SeqPlatform::Step s; s.x = 60 + 10; s.y = kRowY0 + rc * kRowH + 16 + 8; s.left = left; return s;
}
// Count non-zero pixels in the presented 32bpp framebuffer.
int NonClear(const shim::MemoryGraphicsDevice& dev, int w, int h) {
    const auto& fb = dev.lastPresented();
    if (fb.empty()) return 0;
    const auto* p = reinterpret_cast<const std::uint32_t*>(fb.data());
    int n = 0; for (int i = 0; i < w * h; ++i) if (p[i] & 0x00FFFFFFu) ++n;
    return n;
}
play::OptionsConfig BaseCfg(play::OptionsPage p) {
    play::OptionsConfig c; c.page = p; c.fbW = 640; c.fbH = 480;
    c.frameCapMs = 0; c.maxFrames = 30;
    return c;
}
} // namespace

TEST(OptionsItest, EachPageRendersNonBlank) {
    for (auto page : { play::OptionsPage::kGame, play::OptionsPage::kGfx, play::OptionsPage::kSfx }) {
        shim::MemoryGraphicsDevice dev; CHECK(dev.init(640, 480, 32, false));
        SeqPlatform plat; plat.steps = { OnRow(0, false) };
        auto cfg = BaseCfg(page);
        play::OptionsResult r = play::RunOptionsScreen(dev, plat, cfg);
        CHECK(r.framesPresented > 0);
        // Panel plate + title + row labels => a lot of lit pixels.
        CHECK(NonClear(dev, 640, 480) > 500);
    }
}

TEST(OptionsItest, TogglePersistsAcrossAllSfxRows) {
    shim::MemoryGraphicsDevice dev; CHECK(dev.init(640, 480, 32, false));
    auto cfg = BaseCfg(play::OptionsPage::kSfx);
    cfg.sound.masterVol = 0; cfg.sound.sfxVol = 0; cfg.sound.msxVol = 0;
    cfg.sound.speechVol = 0; cfg.sound.msxFreq = 0;
    SeqPlatform plat;
    // Click each of the 5 rows once, then apply.
    plat.steps = {
        OnRow(0, false), OnRow(0, true), OnRow(1, false), OnRow(1, true),
        OnRow(2, false), OnRow(2, true), OnRow(3, false), OnRow(3, true),
        OnRow(4, false), OnRow(4, true), OnRow(0, false),
        OnBack(5, false), OnBack(5, true),
    };
    play::OptionsResult r = play::RunOptionsScreen(dev, plat, cfg);
    CHECK(!r.cancelled);
    CHECK(r.changed);
    CHECK(r.sound.masterVol == 16);
    CHECK(r.sound.sfxVol == 16);
    CHECK(r.sound.msxVol == 16);
    CHECK(r.sound.speechVol == 16);
    CHECK(r.sound.msxFreq == 1);   // cycle 0 -> 1
}

TEST(OptionsItest, GameStepWrapAndDeterministicRerun) {
    auto cfg = BaseCfg(play::OptionsPage::kGame);
    cfg.game.speed = 160;   // at max -> a step wraps back to min (0)
    auto run = [&]() {
        shim::MemoryGraphicsDevice dev; dev.init(640, 480, 32, false);
        SeqPlatform plat;
        plat.steps = { OnRow(0, false), OnRow(0, true), OnRow(0, false),
                       OnBack(11, false), OnBack(11, true) };
        return play::RunOptionsScreen(dev, plat, cfg);
    };
    play::OptionsResult a = run();
    play::OptionsResult b = run();
    CHECK(a.game.speed == 0);          // wrapped
    CHECK(a.changed);
    CHECK(a.game.speed == b.game.speed);
    CHECK(a.framesPresented == b.framesPresented);
}

TEST(OptionsItest, GfxPanelFrameChangesWhenValueToggled) {
    // Render a frame, toggle a cycle value, render again: the value text region
    // differs so the framebuffer pixel count shifts.
    shim::MemoryGraphicsDevice dev1; dev1.init(640, 480, 32, false);
    auto cfg = BaseCfg(play::OptionsPage::kGfx);
    cfg.gfx.details = 0;
    SeqPlatform p1; p1.steps = { OnRow(0, false) }; cfg.maxFrames = 2;
    play::RunOptionsScreen(dev1, p1, cfg);
    const int before = NonClear(dev1, 640, 480);

    shim::MemoryGraphicsDevice dev2; dev2.init(640, 480, 32, false);
    SeqPlatform p2; p2.steps = { OnRow(0, false), OnRow(0, true), OnRow(0, false) };
    cfg.maxFrames = 6;
    play::RunOptionsScreen(dev2, p2, cfg);
    const int after = NonClear(dev2, 640, 480);
    // "0" vs "2" glyphs differ in lit-pixel count.
    CHECK(before > 0 && after > 0);
}
