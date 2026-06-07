// Unit tests for play::RunCharCreateScreen — the native character-creation flow.
// Backend-agnostic: MemoryGraphicsDevice + a sequenced test platform.  No assets:
// the grids fall back to labelled rects; the hit-test still uses the REAL geometry
// (gui::Profession_ButtonX/Y) and the produced NewGameParams reflect the picks.
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

// Same scripted platform pattern as sdl_menu_test.cpp.
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

// Centre of profession cell `i` (real geometry).
SeqPlatform::Step OnProf(int i, bool left) {
    SeqPlatform::Step s;
    s.x = gui::Profession_ButtonX(i) + 10;
    s.y = gui::Profession_ButtonY(i) + 10;
    s.left = left;
    return s;
}

// Wappen grid layout used by the screen (4 cols x 80/96 step, origin 100,130).
SeqPlatform::Step OnWappen(int i, bool left) {
    SeqPlatform::Step s;
    s.x = 96 * (i % 4) + 100 + 10;
    s.y = 80 * (i / 4) + 130 + 10;
    s.left = left;
    return s;
}

SeqPlatform::Step OnConfirm(bool left) {
    SeqPlatform::Step s; s.x = 520 + 40; s.y = 520 + 16; s.left = left; return s;
}

// Build a click script.  Input at loop-iteration k reads steps[k] (render reads
// steps[k-1]); a click registers on the leftEdge (left && !prevLeft), so each
// click step must be preceded by a non-pressed step.  This interleaves a leading
// no-op + a release after every click so each click fires exactly once.
std::vector<SeqPlatform::Step> Script(std::initializer_list<SeqPlatform::Step> clicks) {
    std::vector<SeqPlatform::Step> out;
    out.push_back(SeqPlatform::Step{});      // steps[0]: render-only no-op
    for (const auto& c : clicks) {
        out.push_back(c);                    // held click (left set)
        SeqPlatform::Step rel = c; rel.left = false; rel.key = 0;
        out.push_back(rel);                  // release so the next edge re-triggers
    }
    return out;
}

play::CharCreateConfig BaseCfg() {
    play::CharCreateConfig c;
    c.fbW = 800; c.fbH = 600; c.frameCapMs = 0; c.maxFrames = 60;
    return c;
}

// The real beruf table (dword_527604): {1,2,3,4,5,6,7,11}.
const int kBeruf[8] = {1, 2, 3, 4, 5, 6, 7, 11};

} // namespace

TEST(CharCreateUnit, RendersFramesAndTracksHover) {
    shim::MemoryGraphicsDevice dev;
    CHECK(dev.init(800, 600, 32, false));
    SeqPlatform plat;
    plat.steps = { OnProf(2, false) };   // hover cell 2, never click
    auto r = play::RunCharCreateScreen(dev, plat, BaseCfg());
    CHECK(r.framesPresented > 0);
    CHECK(r.hoveredProfession == 2);
    CHECK(!r.confirmed);
    CHECK(!r.back);   // bounded out, neither confirm nor back
}

TEST(CharCreateUnit, GridHitMatchesProfessionGeometry) {
    // The hit-test must agree with the real Profession_ButtonX/Y for every cell.
    shim::MemoryGraphicsDevice dev;
    CHECK(dev.init(800, 600, 32, false));
    for (int i = 0; i < gui::kProfessionCount; ++i) {
        SeqPlatform plat;
        plat.steps = { OnProf(i, false) };
        auto r = play::RunCharCreateScreen(dev, plat, BaseCfg());
        CHECK_EQ(r.hoveredProfession, i);
    }
}

TEST(CharCreateUnit, PickProfessionAndWappenThenConfirm) {
    shim::MemoryGraphicsDevice dev;
    CHECK(dev.init(800, 600, 32, false));
    SeqPlatform plat;
    // click prof 4 -> phase=wappen; click wappen 3; confirm.
    plat.steps = Script({ OnProf(4, true), OnWappen(3, true), OnConfirm(true) });
    auto cfg = BaseCfg();
    cfg.defaultName = "Heinrich";
    auto r = play::RunCharCreateScreen(dev, plat, cfg);
    CHECK(r.confirmed);
    CHECK(!r.back);
    CHECK_EQ(r.phaseReached, 1);
    // params reflect the real beruf byte + the wappen index + the defaulted name.
    CHECK_EQ(r.params.profession, kBeruf[4]);       // beruf 5
    CHECK_EQ(r.params.wappen, 3);
    CHECK(r.params.started);                          // NewGame_Commit ran
    CHECK(r.params.firstName == "Heinrich");
    CHECK_EQ(r.params.gender, 0);
}

TEST(CharCreateUnit, EscFromProfessionBacksOut) {
    shim::MemoryGraphicsDevice dev;
    CHECK(dev.init(800, 600, 32, false));
    SeqPlatform plat;
    SeqPlatform::Step esc; esc.key = 0x1B;
    plat.steps = { esc };
    auto r = play::RunCharCreateScreen(dev, plat, BaseCfg());
    CHECK(r.back);
    CHECK(!r.confirmed);
    CHECK(!r.params.started);
}

TEST(CharCreateUnit, EscFromWappenReturnsToProfession) {
    shim::MemoryGraphicsDevice dev;
    CHECK(dev.init(800, 600, 32, false));
    SeqPlatform plat;
    SeqPlatform::Step esc; esc.key = 0x1B;
    // click prof 1 -> wappen; ESC -> back to profession; re-pick prof 0 -> wappen;
    // pick wappen 2; confirm.
    plat.steps = Script({ OnProf(1, true), esc, OnProf(0, true),
                          OnWappen(2, true), OnConfirm(true) });
    auto r = play::RunCharCreateScreen(dev, plat, BaseCfg());
    CHECK(r.confirmed);
    CHECK_EQ(r.params.profession, kBeruf[0]);   // re-picked profession 0
    CHECK_EQ(r.params.wappen, 2);
}

TEST(CharCreateUnit, WindowCloseBacksOut) {
    shim::MemoryGraphicsDevice dev;
    CHECK(dev.init(800, 600, 32, false));
    SeqPlatform plat;
    plat.maxPumps = 3;             // window "closes" after 3 pumps
    plat.steps = { OnProf(0, false) };
    auto cfg = BaseCfg(); cfg.maxFrames = -1;
    auto r = play::RunCharCreateScreen(dev, plat, cfg);
    CHECK(r.quitByWindow);
    CHECK(r.back);
}

TEST(CharCreateUnit, ConfirmWithoutProfessionIgnored) {
    // Clicking confirm with no profession picked must not commit.  Without a
    // profession we never leave phase 0, so a confirm-area click does nothing.
    shim::MemoryGraphicsDevice dev;
    CHECK(dev.init(800, 600, 32, false));
    SeqPlatform plat;
    plat.steps = { OnConfirm(true) };   // confirm-area click in phase 0 = a miss
    auto r = play::RunCharCreateScreen(dev, plat, BaseCfg());
    CHECK(!r.confirmed);
    CHECK(!r.params.started);
}

TEST(CharCreateUnit, Deterministic) {
    shim::MemoryGraphicsDevice dev;
    CHECK(dev.init(800, 600, 32, false));
    auto run = [] {
        shim::MemoryGraphicsDevice d; d.init(800, 600, 32, false);
        SeqPlatform p;
        p.steps = Script({ OnProf(6, true), OnWappen(5, true), OnConfirm(true) });
        return play::RunCharCreateScreen(d, p, BaseCfg());
    };
    auto a = run(); auto b = run();
    CHECK_EQ(a.params.profession, b.params.profession);
    CHECK_EQ(a.params.wappen, b.params.wappen);
    CHECK_EQ(a.framesPresented, b.framesPresented);
    CHECK_EQ(a.confirmed, b.confirmed ? 1 : 0);
}
