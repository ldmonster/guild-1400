// Integration tests for play::RunSdlMenu — menu renders non-blank, and the
// two-phase New Game -> city pick returns the chosen .cty.
#include "test.h"
#include "play/sdl_menu.h"
#include "gui/main_menu.h"
#include "shim_impl/memory_graphics.h"
#include "shim/IPlatform.h"
#include <vector>
#include <cstdint>

using namespace guild;

namespace {
struct SeqPlatform : shim::IPlatform {
    struct Step { int x = 0, y = 0; bool left = false; int key = 0; };
    std::vector<Step> steps;
    int iter = 0, maxPumps = 100000;
    bool createMainWindow(const char*, int, int, bool) override { return true; }
    void destroyMainWindow() override {}
    bool pumpMessages() override { ++iter; return iter < maxPumps; }
    std::uint32_t timeMs() override { return (std::uint32_t)iter * 16u; }
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
SeqPlatform::Step OnMenuButton(int index, bool left) {
    SeqPlatform::Step s; s.x = gui::kMainMenuButtonX + 20;
    s.y = gui::MainMenu_ButtonY(index) + 10; s.left = left; return s;
}
int NonBlank(const std::vector<std::uint8_t>& fb) {
    int n = 0; const auto* p = reinterpret_cast<const std::uint32_t*>(fb.data());
    for (std::size_t i = 0; i < fb.size() / 4; ++i) if ((p[i] & 0x00FFFFFFu) != 0) ++n;
    return n;
}
} // namespace

TEST(SdlMenuItest, MenuFrameIsNonBlank) {
    shim::MemoryGraphicsDevice dev;
    CHECK(dev.init(320, 240, 32, false));
    SeqPlatform plat;
    plat.steps = { OnMenuButton(0, false) };
    play::SdlMenuConfig cfg; cfg.fbW = 320; cfg.fbH = 240; cfg.frameCapMs = 0; cfg.maxFrames = 3;
    play::SdlMenuResult r = play::RunSdlMenu(dev, plat, cfg);
    CHECK(r.framesPresented == 3);
    // The presented menu frame draws the 8-button column -> non-blank pixels.
    CHECK(NonBlank(dev.lastPresented()) > 500);
}

TEST(SdlMenuItest, EachButtonReturnsItsChoice) {
    struct Case { gui::MainMenuItem item; play::SdlMenuChoice choice; int page; };
    const Case cases[] = {
        { gui::MainMenuItem::kNewGame,     play::SdlMenuChoice::kNewGame,  -1 },
        { gui::MainMenuItem::kLoad,        play::SdlMenuChoice::kLoadGame, -1 },
        { gui::MainMenuItem::kGameOptions, play::SdlMenuChoice::kOptions,   0 },
        { gui::MainMenuItem::kGfxOptions,  play::SdlMenuChoice::kOptions,   1 },
        { gui::MainMenuItem::kSfxOptions,  play::SdlMenuChoice::kOptions,   2 },
        { gui::MainMenuItem::kCredits,     play::SdlMenuChoice::kCredits,  -1 },
        { gui::MainMenuItem::kQuit,        play::SdlMenuChoice::kQuit,     -1 },
    };
    for (const auto& c : cases) {
        shim::MemoryGraphicsDevice dev; CHECK(dev.init(320, 240, 32, false));
        SeqPlatform plat; plat.steps = { OnMenuButton((int)c.item, true) };
        play::SdlMenuConfig cfg; cfg.fbW = 320; cfg.fbH = 240; cfg.frameCapMs = 0; cfg.maxFrames = 30;
        play::SdlMenuResult r = play::RunSdlMenu(dev, plat, cfg);
        CHECK(r.choice == c.choice);
        if (c.page >= 0) CHECK(r.optionsPage == c.page);
    }
}

TEST(SdlMenuItest, Deterministic) {
    auto run = []() {
        shim::MemoryGraphicsDevice dev; dev.init(320, 240, 32, false);
        SeqPlatform plat; plat.steps = { OnMenuButton((int)gui::MainMenuItem::kQuit, true) };
        play::SdlMenuConfig cfg; cfg.fbW = 320; cfg.fbH = 240; cfg.frameCapMs = 0; cfg.maxFrames = 20;
        return play::RunSdlMenu(dev, plat, cfg).choice;
    };
    CHECK(run() == play::SdlMenuChoice::kQuit);
    CHECK(run() == play::SdlMenuChoice::kQuit);
}
