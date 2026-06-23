// Unit tests for play::RunCityScreen — the native CHOOSECITY screen.
// Backend-agnostic: MemoryGraphicsDevice + a sequenced test platform.
#include "test.h"
#include "play/sdl_city_screen.h"
#include "shim_impl/memory_graphics.h"
#include "shim/IPlatform.h"
#include <vector>
#include <string>
#include <cstdint>

using namespace guild;

namespace {
// A platform whose mouse/keys are scripted per pump (one advance per pumpMessages).
struct SeqPlatform : shim::IPlatform {
    struct Step { int x = 0, y = 0; bool left = false; int key = 0; std::uint32_t t = 0; };
    std::vector<Step> steps;
    int iter = 0, maxPumps = 100000;
    bool createMainWindow(const char*, int, int, bool) override { return true; }
    void destroyMainWindow() override {}
    bool pumpMessages() override { ++iter; return iter < maxPumps; }
    std::uint32_t timeMs() override { return at().t; }
    void sleepMs(std::uint32_t) override {}
    void getMouse(shim::MouseState& o) override { const Step& s = at(); o.x = s.x; o.y = s.y; o.left = s.left; }
    bool keyDown(int vk) override { return at().key == vk; }
    const Step& at() const {
        static Step z;
        if (steps.empty()) return z;
        int i = iter < (int)steps.size() ? iter : (int)steps.size() - 1;
        return steps[i < 0 ? 0 : i];
    }
};

// Layout mirrors sdl_city_screen.cpp: panel x=220,y=70,w=360; rowY0 = 70+56,
// rowH=40, gap=8.  Centre of city row i:
SeqPlatform::Step OnCityRow(int i, bool left, std::uint32_t t = 0) {
    SeqPlatform::Step s;
    s.x = 220 + 16 + 20;
    s.y = (70 + 56) + i * (40 + 8) + 20;
    s.left = left; s.t = t; return s;
}
int ActionY(int n) { int nn = n > 0 ? n : 1; return (70 + 56) + (nn - 1) * (40 + 8) + 40 + 24; }
SeqPlatform::Step OnConfirm(int n, bool left) {
    SeqPlatform::Step s; s.x = 220 + 24 + 20; s.y = ActionY(n) + 18; s.left = left; return s;
}

play::CityScreenConfig Cfg3() {
    play::CityScreenConfig c;
    c.fbW = 800; c.fbH = 600; c.frameCapMs = 0; c.maxFrames = 60;
    c.cities = { {"AUGSBURG", "Cities/AUGSBURG.cty"},
                 {"BERLIN",   "Cities/BERLIN.cty"},
                 {"DRESDEN",  "Cities/DRESDEN.cty"} };
    return c;
}
} // namespace

TEST(SdlCityScreenUnit, ClickRowThenConfirmReturnsCity) {
    shim::MemoryGraphicsDevice dev;
    CHECK(dev.init(800, 600, 32, false));
    SeqPlatform plat;
    const int n = 3;
    // Select BERLIN (row 1), then click Confirm.
    plat.steps = {
        OnCityRow(1, false),    // iter0 render
        OnCityRow(1, true),     // edge -> select row 1
        OnCityRow(1, false),
        OnConfirm(n, false),    // move to confirm
        OnConfirm(n, true),     // edge -> confirm
        OnConfirm(n, false),
    };
    play::CityScreenResult r = play::RunCityScreen(dev, plat, Cfg3());
    CHECK(r.confirmed);
    CHECK(!r.back);
    CHECK(r.cityIndex == 1);
    CHECK(r.cityName == "BERLIN");
    CHECK(r.cityPath == "Cities/BERLIN.cty");
}

TEST(SdlCityScreenUnit, DoubleClickConfirms) {
    shim::MemoryGraphicsDevice dev;
    CHECK(dev.init(800, 600, 32, false));
    SeqPlatform plat;
    // Two clicks on row 2 within the double-click window -> confirm.
    plat.steps = {
        OnCityRow(2, false, 100),
        OnCityRow(2, true,  100),   // first click selects
        OnCityRow(2, false, 200),
        OnCityRow(2, true,  300),   // second click within 400ms -> confirm
        OnCityRow(2, false, 300),
    };
    play::CityScreenResult r = play::RunCityScreen(dev, plat, Cfg3());
    CHECK(r.confirmed);
    CHECK(r.cityIndex == 2);
    CHECK(r.cityName == "DRESDEN");
}

TEST(SdlCityScreenUnit, EscReturnsBack) {
    shim::MemoryGraphicsDevice dev;
    CHECK(dev.init(800, 600, 32, false));
    SeqPlatform plat;
    SeqPlatform::Step s; s.key = 0x1B; // ESC held
    plat.steps = { s };
    play::CityScreenResult r = play::RunCityScreen(dev, plat, Cfg3());
    CHECK(r.back);
    CHECK(r.backByEsc);
    CHECK(!r.confirmed);
    CHECK(r.cityIndex == -1);
}

TEST(SdlCityScreenUnit, HoverIsTracked) {
    shim::MemoryGraphicsDevice dev;
    CHECK(dev.init(800, 600, 32, false));
    SeqPlatform plat;
    plat.steps = { OnCityRow(2, false) };   // hover DRESDEN, never click
    auto cfg = Cfg3(); cfg.maxFrames = 4;
    play::CityScreenResult r = play::RunCityScreen(dev, plat, cfg);
    CHECK(r.hoveredItem == 2);
    CHECK(r.framesPresented == 4);
    // No click/ESC -> bounded out -> neither confirmed nor back.
    CHECK(!r.confirmed);
    CHECK(!r.back);
}

TEST(SdlCityScreenUnit, ConfirmWithoutSelectionDoesNothing) {
    shim::MemoryGraphicsDevice dev;
    CHECK(dev.init(800, 600, 32, false));
    SeqPlatform plat;
    const int n = 3;
    plat.steps = { OnConfirm(n, false), OnConfirm(n, true), OnConfirm(n, false) };
    auto cfg = Cfg3(); cfg.maxFrames = 8;
    play::CityScreenResult r = play::RunCityScreen(dev, plat, cfg);
    CHECK(!r.confirmed);   // nothing selected -> Confirm is inert
}

TEST(SdlCityScreenUnit, EmptyListHandled) {
    shim::MemoryGraphicsDevice dev;
    CHECK(dev.init(800, 600, 32, false));
    SeqPlatform plat;
    SeqPlatform::Step s; s.key = 0x1B; plat.steps = { s };
    play::CityScreenConfig cfg;
    cfg.fbW = 800; cfg.fbH = 600; cfg.frameCapMs = 0; cfg.maxFrames = 5;
    // No cities at all.
    play::CityScreenResult r = play::RunCityScreen(dev, plat, cfg);
    CHECK(r.back);
    CHECK(!r.confirmed);
}

// ---- HARDENING (wave-12): boundary city counts + overlong names ----

// A single city is the 1-element edge of the list / action-row math (n-1==0).
TEST(SdlCityScreenUnit, SingleCityConfirms) {
    shim::MemoryGraphicsDevice dev; CHECK(dev.init(800, 600, 32, false));
    SeqPlatform plat;
    const int n = 1;
    plat.steps = { OnCityRow(0, false), OnCityRow(0, true), OnCityRow(0, false),
                   OnConfirm(n, false), OnConfirm(n, true), OnConfirm(n, false) };
    play::CityScreenConfig cfg; cfg.fbW = 800; cfg.fbH = 600; cfg.maxFrames = 30;
    cfg.cities = { {"AUGSBURG", "Cities/AUGSBURG.cty"} };
    play::CityScreenResult r = play::RunCityScreen(dev, plat, cfg);
    CHECK(r.confirmed);
    CHECK_EQ(r.cityIndex, 0);
}

// Many cities (far more rows than fit the panel) must render + hit-test without
// indexing past cfg.cities; rows beyond the framebuffer are clipped by DrawGlyph.
TEST(SdlCityScreenUnit, ManyCitiesNoOob) {
    shim::MemoryGraphicsDevice dev; CHECK(dev.init(400, 300, 32, false));
    SeqPlatform plat;
    SeqPlatform::Step esc; esc.key = 0x1B;
    plat.steps = { esc };
    play::CityScreenConfig cfg; cfg.fbW = 400; cfg.fbH = 300; cfg.maxFrames = 4;
    for (int i = 0; i < 64; ++i)
        cfg.cities.push_back({ "CITY_NUMBER_" + std::to_string(i),
                               "Cities/C" + std::to_string(i) + ".cty" });
    play::CityScreenResult r = play::RunCityScreen(dev, plat, cfg);
    CHECK(r.back);
    CHECK(r.framesPresented > 0);
}

// Overlong city names on a tiny framebuffer: the row label must not write past
// the scratch (right-edge band). ASAN guards any overrun.
TEST(SdlCityScreenUnit, OverlongNameTinyBufferNoOob) {
    shim::MemoryGraphicsDevice dev; CHECK(dev.init(64, 48, 32, false));
    SeqPlatform plat;
    SeqPlatform::Step esc; esc.key = 0x1B;
    plat.steps = { esc };
    play::CityScreenConfig cfg; cfg.fbW = 64; cfg.fbH = 48; cfg.maxFrames = 4;
    cfg.cities = { { std::string(300, 'Z'), "Cities/Z.cty" },
                   { "A REASONABLY LONG CITY NAME THAT OVERRUNS", "Cities/B.cty" } };
    play::CityScreenResult r = play::RunCityScreen(dev, plat, cfg);
    CHECK(r.back);
    CHECK(r.framesPresented > 0);
}

TEST(SdlCityScreenUnit, Deterministic) {
    auto run = []() {
        shim::MemoryGraphicsDevice dev; dev.init(800, 600, 32, false);
        SeqPlatform plat;
        plat.steps = {
            OnCityRow(0, false), OnCityRow(0, true), OnCityRow(0, false),
            OnConfirm(3, false), OnConfirm(3, true), OnConfirm(3, false),
        };
        return play::RunCityScreen(dev, plat, Cfg3());
    };
    auto a = run(); auto b = run();
    CHECK(a.confirmed && b.confirmed);
    CHECK(a.cityName == b.cityName);
    CHECK(a.cityPath == b.cityPath);
    CHECK(a.framesPresented == b.framesPresented);
}
