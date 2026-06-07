// Unit tests for play::RunSdlMenu — the native main-menu screen.
// Backend-agnostic: MemoryGraphicsDevice + a sequenced test platform.
#include "test.h"
#include "play/sdl_menu.h"
#include "gui/main_menu.h"
#include "shim_impl/memory_graphics.h"
#include "shim/IPlatform.h"
#include <vector>
#include <cstdint>

using namespace guild;

namespace {
// A platform whose mouse/keys are scripted per pump (one advance per pumpMessages).
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
        const Step& s = at();
        o.x = s.x; o.y = s.y; o.left = s.left;
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

// Cursor at the center of main-menu button `index`'s hit row.
SeqPlatform::Step OnMenuButton(int index, bool left) {
    SeqPlatform::Step s;
    s.x = gui::kMainMenuButtonX + 20;
    s.y = gui::MainMenu_ButtonY(index) + 10;
    s.left = left;
    return s;
}

play::SdlMenuConfig BaseCfg() {
    play::SdlMenuConfig c;
    c.fbW = 320; c.fbH = 240; c.frameCapMs = 0; c.maxFrames = 40;
    return c;
}
} // namespace

TEST(SdlMenuUnit, RendersFramesAndTracksHover) {
    shim::MemoryGraphicsDevice dev;
    CHECK(dev.init(320, 240, 32, false));
    SeqPlatform plat;
    // Hover New Game (index 0), never click.
    plat.steps = { OnMenuButton(0, false) };
    auto cfg = BaseCfg();
    play::SdlMenuResult r = play::RunSdlMenu(dev, plat, cfg);
    CHECK(r.framesPresented > 0);
    CHECK(r.hoveredItem == 0);
    // No click -> bounded out -> default quit.
    CHECK(r.choice == play::SdlMenuChoice::kQuit);
}

TEST(SdlMenuUnit, QuitButtonReturnsQuit) {
    shim::MemoryGraphicsDevice dev;
    CHECK(dev.init(320, 240, 32, false));
    SeqPlatform plat;
    const int quitIdx = (int)gui::MainMenuItem::kQuit;   // 7
    plat.steps = { OnMenuButton(quitIdx, true) };   // press-and-hold on Quit
    auto cfg = BaseCfg();
    play::SdlMenuResult r = play::RunSdlMenu(dev, plat, cfg);
    CHECK(r.choice == play::SdlMenuChoice::kQuit);
    CHECK(!r.quitByWindow);
}

TEST(SdlMenuUnit, EscReturnsQuit) {
    shim::MemoryGraphicsDevice dev;
    CHECK(dev.init(320, 240, 32, false));
    SeqPlatform plat;
    SeqPlatform::Step s; s.key = 0x1B; // ESC held
    plat.steps = { s };
    auto cfg = BaseCfg();
    play::SdlMenuResult r = play::RunSdlMenu(dev, plat, cfg);
    CHECK(r.choice == play::SdlMenuChoice::kQuit);
    CHECK(r.quitByEsc);
}

TEST(SdlMenuUnit, WindowCloseReturnsQuit) {
    shim::MemoryGraphicsDevice dev;
    CHECK(dev.init(320, 240, 32, false));
    SeqPlatform plat;
    plat.maxPumps = 3;            // window "closes" after 3 pumps
    plat.steps = { OnMenuButton(0, false) };
    auto cfg = BaseCfg();
    cfg.maxFrames = -1;           // rely on the window close to end it
    play::SdlMenuResult r = play::RunSdlMenu(dev, plat, cfg);
    CHECK(r.quitByWindow);
    CHECK(r.choice == play::SdlMenuChoice::kQuit);
}

TEST(SdlMenuUnit, NewGameButtonReturnsNewGame) {
    shim::MemoryGraphicsDevice dev;
    CHECK(dev.init(320, 240, 32, false));
    SeqPlatform plat;
    plat.steps = { OnMenuButton((int)gui::MainMenuItem::kNewGame, true) };
    auto cfg = BaseCfg();
    play::SdlMenuResult r = play::RunSdlMenu(dev, plat, cfg);
    // The caller (guild_run) then runs ChooseCity + character creation.
    CHECK(r.choice == play::SdlMenuChoice::kNewGame);
}

TEST(SdlMenuUnit, OptionsButtonsReturnOptionsWithPage) {
    struct Case { gui::MainMenuItem item; int page; };
    const Case cases[] = {
        { gui::MainMenuItem::kGameOptions, 0 },
        { gui::MainMenuItem::kGfxOptions,  1 },
        { gui::MainMenuItem::kSfxOptions,  2 },
    };
    for (const auto& c : cases) {
        shim::MemoryGraphicsDevice dev; CHECK(dev.init(320, 240, 32, false));
        SeqPlatform plat; plat.steps = { OnMenuButton((int)c.item, true) };
        auto cfg = BaseCfg();
        play::SdlMenuResult r = play::RunSdlMenu(dev, plat, cfg);
        CHECK(r.choice == play::SdlMenuChoice::kOptions);
        CHECK(r.optionsPage == c.page);
    }
}

TEST(SdlMenuUnit, CreditsButtonReturnsCredits) {
    shim::MemoryGraphicsDevice dev;
    CHECK(dev.init(320, 240, 32, false));
    SeqPlatform plat;
    plat.steps = { OnMenuButton((int)gui::MainMenuItem::kCredits, true) };
    auto cfg = BaseCfg();
    play::SdlMenuResult r = play::RunSdlMenu(dev, plat, cfg);
    CHECK(r.choice == play::SdlMenuChoice::kCredits);
}

TEST(SdlMenuUnit, LoadButtonReturnsLoad) {
    shim::MemoryGraphicsDevice dev;
    CHECK(dev.init(320, 240, 32, false));
    SeqPlatform plat;
    plat.steps = { OnMenuButton((int)gui::MainMenuItem::kLoad, true) };
    auto cfg = BaseCfg();
    play::SdlMenuResult r = play::RunSdlMenu(dev, plat, cfg);
    CHECK(r.choice == play::SdlMenuChoice::kLoadGame);
}
