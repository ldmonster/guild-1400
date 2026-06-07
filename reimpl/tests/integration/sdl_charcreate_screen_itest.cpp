// Integration tests for play::RunCharCreateScreen — render the profession/wappen
// grids into a MemoryGraphicsDevice (asset-less: fallback cells), assert the frame
// is non-blank where the grid cells sit, drive a scripted selection, and confirm a
// deterministic rerun.  No real assets required (the GUARDED real-sprite path is
// the e2e test).
#include "test.h"
#include "play/sdl_charcreate_screen.h"
#include "gui/newgame_setup.h"
#include "shim_impl/memory_graphics.h"
#include "shim/IPlatform.h"
#include <vector>
#include <cstdint>
#include <initializer_list>

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
    void getMouse(shim::MouseState& o) override { const Step& s = at(); o.x = s.x; o.y = s.y; o.left = s.left; }
    bool keyDown(int vk) override { return at().key == vk; }
    const Step& at() const {
        static Step z; if (steps.empty()) return z;
        int i = iter < (int)steps.size() ? iter : (int)steps.size() - 1;
        if (i < 0) i = 0;
        return steps[i];
    }
};

SeqPlatform::Step OnProf(int i, bool left) {
    SeqPlatform::Step s; s.x = gui::Profession_ButtonX(i) + 10; s.y = gui::Profession_ButtonY(i) + 10; s.left = left; return s;
}
SeqPlatform::Step OnWappen(int i, bool left) {
    SeqPlatform::Step s; s.x = 96 * (i % 4) + 100 + 10; s.y = 80 * (i / 4) + 130 + 10; s.left = left; return s;
}
SeqPlatform::Step OnConfirm(bool left) { SeqPlatform::Step s; s.x = 560; s.y = 536; s.left = left; return s; }

// Interleave a leading no-op + a release after each click (input at loop-iter k
// reads steps[k]; a click fires on the leftEdge, so each click needs a preceding
// non-pressed step).
std::vector<SeqPlatform::Step> Script(std::initializer_list<SeqPlatform::Step> clicks) {
    std::vector<SeqPlatform::Step> out;
    out.push_back(SeqPlatform::Step{});
    for (const auto& c : clicks) {
        out.push_back(c);
        SeqPlatform::Step rel = c; rel.left = false; rel.key = 0;
        out.push_back(rel);
    }
    return out;
}

play::CharCreateConfig BaseCfg() {
    play::CharCreateConfig c; c.fbW = 800; c.fbH = 600; c.frameCapMs = 0; c.maxFrames = 60; return c;
}

// Count non-zero (drawn) pixels in the presented 32bpp framebuffer.
int NonBlank(const std::vector<std::uint8_t>& fb) {
    int n = 0;
    const auto* p = reinterpret_cast<const std::uint32_t*>(fb.data());
    const std::size_t count = fb.size() / 4;
    for (std::size_t i = 0; i < count; ++i) if (p[i] & 0x00FFFFFFu) ++n;
    return n;
}

const int kBeruf[8] = {1, 2, 3, 4, 5, 6, 7, 11};

} // namespace

TEST(CharCreateItest, ProfessionGridDrawsCells) {
    shim::MemoryGraphicsDevice dev;
    CHECK(dev.init(800, 600, 32, false));
    SeqPlatform plat;
    plat.steps = { OnProf(0, false) };       // hover, render only
    auto cfg = BaseCfg(); cfg.maxFrames = 1; // one frame
    auto r = play::RunCharCreateScreen(dev, plat, cfg);
    CHECK_EQ(r.framesPresented, 1);
    // Asset-less fallback: rect cells + labels -> a substantial non-blank area.
    CHECK(NonBlank(dev.lastPresented()) > 2000);
    CHECK(!r.usedRealSprites);   // no assets configured
}

TEST(CharCreateItest, ScriptedSelectionYieldsParams) {
    shim::MemoryGraphicsDevice dev;
    CHECK(dev.init(800, 600, 32, false));
    SeqPlatform plat;
    plat.steps = Script({ OnProf(5, true), OnWappen(1, true), OnConfirm(true) });
    auto r = play::RunCharCreateScreen(dev, plat, BaseCfg());
    CHECK(r.confirmed);
    CHECK_EQ(r.params.profession, kBeruf[5]);   // beruf 6
    CHECK_EQ(r.params.professionVariant, kBeruf[5]); // identity variant sink
    CHECK_EQ(r.params.wappen, 1);
    CHECK(r.params.started);
    CHECK_EQ(r.phaseReached, 1);
}

TEST(CharCreateItest, WappenGridRendersInPhaseTwo) {
    shim::MemoryGraphicsDevice dev;
    CHECK(dev.init(800, 600, 32, false));
    SeqPlatform plat;
    // Click prof (loop-iter 1 -> phase=wappen), then the script clamps to a release
    // over the wappen grid, so the remaining frames render the wappen phase.
    plat.steps = Script({ OnProf(2, true) });
    auto cfg = BaseCfg(); cfg.maxFrames = 6;
    auto r = play::RunCharCreateScreen(dev, plat, cfg);
    CHECK_EQ(r.phaseReached, 1);
    CHECK(NonBlank(dev.lastPresented()) > 2000);  // wappen cells + confirm drawn
}

TEST(CharCreateItest, DeterministicRerun) {
    auto run = [] {
        shim::MemoryGraphicsDevice d; d.init(800, 600, 32, false);
        SeqPlatform p; p.steps = Script({ OnProf(3, true), OnWappen(7, true), OnConfirm(true) });
        auto r = play::RunCharCreateScreen(d, p, BaseCfg());
        return std::make_pair(r, d.lastPresented());
    };
    auto a = run(); auto b = run();
    CHECK_EQ(a.first.params.profession, b.first.params.profession);
    CHECK_EQ(a.first.params.wappen, b.first.params.wappen);
    CHECK_EQ(a.first.framesPresented, b.first.framesPresented);
    CHECK(a.second == b.second);    // pixel-identical
}
