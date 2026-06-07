// Integration tests for play::RunCityScreen — the CHOOSECITY screen renders a
// non-blank framed list with real city names (DrawText pixels at the rows), and
// a pick returns the correct cityPath; deterministic rerun.
#include "test.h"
#include "play/sdl_city_screen.h"
#include "shim_impl/memory_graphics.h"
#include "shim/IPlatform.h"
#include <vector>
#include <cstdint>

using namespace guild;

namespace {
struct SeqPlatform : shim::IPlatform {
    struct Step { int x = 0, y = 0; bool left = false; int key = 0; std::uint32_t t = 0; };
    std::vector<Step> steps; int iter = 0, maxPumps = 100000;
    bool createMainWindow(const char*, int, int, bool) override { return true; }
    void destroyMainWindow() override {}
    bool pumpMessages() override { ++iter; return iter < maxPumps; }
    std::uint32_t timeMs() override { return at().t; }
    void sleepMs(std::uint32_t) override {}
    void getMouse(shim::MouseState& o) override { const Step& s = at(); o.x = s.x; o.y = s.y; o.left = s.left; }
    bool keyDown(int vk) override { return at().key == vk; }
    const Step& at() const { static Step z; if (steps.empty()) return z;
        int i = iter < (int)steps.size() ? iter : (int)steps.size() - 1; return steps[i < 0 ? 0 : i]; }
};
SeqPlatform::Step OnCityRow(int i, bool left) {
    SeqPlatform::Step s; s.x = 220 + 16 + 20; s.y = (70 + 56) + i * (40 + 8) + 20; s.left = left; return s;
}
int ActionY(int n) { int nn = n > 0 ? n : 1; return (70 + 56) + (nn - 1) * (40 + 8) + 40 + 24; }
SeqPlatform::Step OnConfirm(int n, bool left) {
    SeqPlatform::Step s; s.x = 220 + 24 + 20; s.y = ActionY(n) + 18; s.left = left; return s;
}
int NonBlank(const std::vector<std::uint8_t>& fb) {
    int n = 0; const auto* p = reinterpret_cast<const std::uint32_t*>(fb.data());
    for (std::size_t i = 0; i < fb.size() / 4; ++i) if ((p[i] & 0x00FFFFFFu) != 0) ++n;
    return n;
}
// Count non-zero pixels within a horizontal band over a city row (text + face).
int RowPixels(const std::vector<std::uint8_t>& fb, int W, int rowIndex) {
    const int ry = (70 + 56) + rowIndex * (40 + 8);
    const auto* p = reinterpret_cast<const std::uint32_t*>(fb.data());
    int n = 0;
    for (int y = ry; y < ry + 40; ++y)
        for (int x = 220 + 16; x < 220 + 16 + (360 - 32); ++x)
            if ((p[(std::size_t)y * W + x] & 0x00FFFFFFu) != 0) ++n;
    return n;
}
play::CityScreenConfig Cfg3() {
    play::CityScreenConfig c;
    c.fbW = 800; c.fbH = 600; c.frameCapMs = 0; c.maxFrames = 60;
    c.cities = { {"AUGSBURG", "Resources/gamedata/Cities/AUGSBURG.cty"},
                 {"BERLIN",   "Resources/gamedata/Cities/BERLIN.cty"},
                 {"DRESDEN",  "Resources/gamedata/Cities/DRESDEN.cty"} };
    return c;
}
} // namespace

TEST(SdlCityScreenItest, FramedListIsNonBlankWithRowText) {
    shim::MemoryGraphicsDevice dev;
    CHECK(dev.init(800, 600, 32, false));
    SeqPlatform plat;
    plat.steps = { OnCityRow(0, false) };
    auto cfg = Cfg3(); cfg.maxFrames = 3;
    play::CityScreenResult r = play::RunCityScreen(dev, plat, cfg);
    CHECK(r.framesPresented == 3);
    const auto& fb = dev.lastPresented();
    CHECK(NonBlank(fb) > 1000);
    // Each of the three city rows carries painted pixels (face + name text).
    CHECK(RowPixels(fb, 800, 0) > 200);
    CHECK(RowPixels(fb, 800, 1) > 200);
    CHECK(RowPixels(fb, 800, 2) > 200);
}

TEST(SdlCityScreenItest, PickCityOneReturnsCorrectPath) {
    shim::MemoryGraphicsDevice dev;
    CHECK(dev.init(800, 600, 32, false));
    SeqPlatform plat;
    const int n = 3;
    plat.steps = {
        OnCityRow(1, false), OnCityRow(1, true), OnCityRow(1, false),
        OnConfirm(n, false), OnConfirm(n, true), OnConfirm(n, false),
    };
    play::CityScreenResult r = play::RunCityScreen(dev, plat, Cfg3());
    CHECK(r.confirmed);
    CHECK(r.cityIndex == 1);
    CHECK(r.cityName == "BERLIN");
    CHECK(r.cityPath == "Resources/gamedata/Cities/BERLIN.cty");
}

TEST(SdlCityScreenItest, DeterministicRerun) {
    auto run = []() {
        shim::MemoryGraphicsDevice dev; dev.init(800, 600, 32, false);
        SeqPlatform plat;
        plat.steps = {
            OnCityRow(2, false), OnCityRow(2, true), OnCityRow(2, false),
            OnConfirm(3, false), OnConfirm(3, true), OnConfirm(3, false),
        };
        return play::RunCityScreen(dev, plat, Cfg3());
    };
    auto a = run(); auto b = run();
    CHECK(a.confirmed && b.confirmed);
    CHECK(a.cityPath == b.cityPath);
    CHECK(a.cityPath == "Resources/gamedata/Cities/DRESDEN.cty");
    CHECK(a.framesPresented == b.framesPresented);
}
