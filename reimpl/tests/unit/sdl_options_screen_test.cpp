// Unit tests for play::RunOptionsScreen — the native Game/Gfx/Sfx options pages.
// Backend-agnostic: MemoryGraphicsDevice + a sequenced test platform.
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

// Geometry mirror (must track sdl_options_screen.cpp's constants).
constexpr int kRowY0 = 90, kRowH = 30, kValueX = 360;
SeqPlatform::Step OnRow(int i, bool left) {
    SeqPlatform::Step s; s.x = kValueX + 20; s.y = kRowY0 + i * kRowH + 4; s.left = left; return s;
}
SeqPlatform::Step OnBack(int rowCount, bool left) {
    SeqPlatform::Step s; s.x = 60 + 10; s.y = kRowY0 + rowCount * kRowH + 16 + 8; s.left = left; return s;
}

play::OptionsConfig BaseCfg(play::OptionsPage p) {
    play::OptionsConfig c; c.page = p;
    c.fbW = 640; c.fbH = 480; c.frameCapMs = 0; c.maxFrames = 60;
    return c;
}
} // namespace

TEST(OptionsUnit, RowsForEachPageMatchRealKeys) {
    config::GfxSettings g; config::SoundSettings s; config::GameSettings m;
    auto gfx = play::OptionsRowsFor(play::OptionsPage::kGfx, g, s, m);
    auto sfx = play::OptionsRowsFor(play::OptionsPage::kSfx, g, s, m);
    auto game = play::OptionsRowsFor(play::OptionsPage::kGame, g, s, m);
    CHECK(gfx.size() == 10);
    CHECK(sfx.size() == 5);
    CHECK(game.size() == 11);
    // The first Sfx row is the real master_vol key (a kStep volume 0..127).
    CHECK(std::string(sfx[0].key) == "master_vol");
    CHECK(sfx[0].kind == play::OptionKind::kStep);
    CHECK(sfx[0].maxV == 127);
    // The Gfx resolution row is the real cur_res cycle.
    CHECK(std::string(gfx[9].key) == "cur_res");
    CHECK(gfx[9].kind == play::OptionKind::kCycle);
}

TEST(OptionsUnit, SfxToggleFlipsMasterVolumeAndApplies) {
    shim::MemoryGraphicsDevice dev; CHECK(dev.init(640, 480, 32, false));
    SeqPlatform plat;
    auto cfg = BaseCfg(play::OptionsPage::kSfx);
    cfg.sound.masterVol = 0;  // start at min
    // Click master_vol once (row 0 -> +16), then Back to apply.
    plat.steps = { OnRow(0, false), OnRow(0, true), OnRow(0, false),
                   OnBack(5, false), OnBack(5, true) };
    play::OptionsResult r = play::RunOptionsScreen(dev, plat, cfg);
    CHECK(r.back);
    CHECK(!r.cancelled);
    CHECK(r.changed);
    CHECK(r.sound.masterVol == 16);   // 0 -> +16 step
    CHECK(r.lastToggledRow == 0);
}

TEST(OptionsUnit, GameToggleFlipsBoolean) {
    shim::MemoryGraphicsDevice dev; CHECK(dev.init(640, 480, 32, false));
    SeqPlatform plat;
    auto cfg = BaseCfg(play::OptionsPage::kGame);
    cfg.game.invertMouse = 0;    // row 4 = invert_mouse toggle
    plat.steps = { OnRow(4, false), OnRow(4, true), OnRow(4, false),
                   OnBack(11, false), OnBack(11, true) };
    play::OptionsResult r = play::RunOptionsScreen(dev, plat, cfg);
    CHECK(!r.cancelled);
    CHECK(r.changed);
    CHECK(r.game.invertMouse == 1);   // toggled on
}

TEST(OptionsUnit, GfxResolutionCycleUpdatesDerivedSize) {
    shim::MemoryGraphicsDevice dev; CHECK(dev.init(640, 480, 32, false));
    SeqPlatform plat;
    auto cfg = BaseCfg(play::OptionsPage::kGfx);
    cfg.gfx.curRes = 0;  // 800x600
    plat.steps = { OnRow(9, false), OnRow(9, true), OnRow(9, false),
                   OnBack(10, false), OnBack(10, true) };
    play::OptionsResult r = play::RunOptionsScreen(dev, plat, cfg);
    CHECK(!r.cancelled);
    CHECK(r.gfx.curRes == 1);          // cycled to idx 1
    // Derived size mirrors the real (interleaved height,width) res table @0x63D70C:
    // idx1 = {1024, 768} -> height=1024, width=768 (see config::ResolutionForIndex).
    int ew = 0, eh = 0; config::ResolutionForIndex(1, &ew, &eh);
    CHECK(r.gfx.resWidth == ew);
    CHECK(r.gfx.resHeight == eh);
    CHECK(ew == 768 && eh == 1024);
}

TEST(OptionsUnit, EscCancelsAndDiscardsChanges) {
    shim::MemoryGraphicsDevice dev; CHECK(dev.init(640, 480, 32, false));
    SeqPlatform plat;
    auto cfg = BaseCfg(play::OptionsPage::kSfx);
    cfg.sound.masterVol = 0;
    SeqPlatform::Step esc; esc.key = 0x1B;
    // Toggle then ESC: the edit is made but NOT applied to res.sound.
    plat.steps = { OnRow(0, false), OnRow(0, true), OnRow(0, false), esc };
    play::OptionsResult r = play::RunOptionsScreen(dev, plat, cfg);
    CHECK(r.cancelled);
    CHECK(r.quitByEsc);
    CHECK(r.sound.masterVol == 0);     // discarded — config unchanged
}

TEST(OptionsUnit, BackWithoutEditsReturnsUnchanged) {
    shim::MemoryGraphicsDevice dev; CHECK(dev.init(640, 480, 32, false));
    SeqPlatform plat;
    auto cfg = BaseCfg(play::OptionsPage::kGame);
    plat.steps = { OnBack(11, false), OnBack(11, true) };
    play::OptionsResult r = play::RunOptionsScreen(dev, plat, cfg);
    CHECK(!r.cancelled);
    CHECK(!r.changed);
}

TEST(OptionsUnit, RendersFramesAndIsDeterministic) {
    shim::MemoryGraphicsDevice dev; CHECK(dev.init(640, 480, 32, false));
    SeqPlatform plat; plat.steps = { OnRow(0, false) };  // hover, never click
    auto cfg = BaseCfg(play::OptionsPage::kGfx);
    play::OptionsResult a = play::RunOptionsScreen(dev, plat, cfg);
    shim::MemoryGraphicsDevice dev2; CHECK(dev2.init(640, 480, 32, false));
    SeqPlatform plat2; plat2.steps = { OnRow(0, false) };
    play::OptionsResult b = play::RunOptionsScreen(dev2, plat2, cfg);
    CHECK(a.framesPresented > 0);
    CHECK(a.framesPresented == b.framesPresented);
    CHECK(a.changed == b.changed);
}
