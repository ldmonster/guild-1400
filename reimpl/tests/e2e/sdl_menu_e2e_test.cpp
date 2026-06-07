// E2E test for play::RunSdlMenu with the REAL shipped city list.
// GUARDED: skips cleanly when the game dir is absent.
#include "test.h"
#include "play/sdl_menu.h"
#include "gui/main_menu.h"
#include "shim_impl/memory_graphics.h"
#include "shim/IPlatform.h"
#include <vector>
#include <string>
#include <cstdint>
#include <cstdlib>
#include <algorithm>
#include <cctype>
#include <filesystem>

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
SeqPlatform::Step OnMenuButton(int index, bool left) {
    SeqPlatform::Step s; s.x = gui::kMainMenuButtonX + 20;
    s.y = gui::MainMenu_ButtonY(index) + 10; s.left = left; return s;
}
std::string GameDir() {
    if (const char* e = std::getenv("GUILD_GAME_DIR")) return e;
    return "europe_guild_1400_original";
}
} // namespace

TEST(SdlMenuE2E, RealMenuRendersAndDispatches) {
    namespace fsx = std::filesystem;
    std::error_code ec;
    const fsx::path gfx = fsx::path(GameDir()) / "gfx" / "gilde.gfx";
    if (!fsx::exists(gfx, ec)) {
        std::printf("[ SKIP ] real game dir not found (%s); set GUILD_GAME_DIR.\n", gfx.string().c_str());
        CHECK(true); return;
    }
    play::SdlMenuConfig cfg; cfg.fbW = 800; cfg.fbH = 600; cfg.frameCapMs = 0; cfg.maxFrames = 80;
    cfg.gameDir = GameDir();

    // New Game on the real-art menu returns kNewGame (the caller then runs ChooseCity).
    {
        shim::MemoryGraphicsDevice dev; CHECK(dev.init(800, 600, 32, false));
        SeqPlatform plat; plat.steps = { OnMenuButton((int)gui::MainMenuItem::kNewGame, true) };
        play::SdlMenuResult r = play::RunSdlMenu(dev, plat, cfg);
        CHECK(r.choice == play::SdlMenuChoice::kNewGame);
        CHECK(r.framesPresented > 0);
    }
    // Gfx-Options returns kOptions page 1 over the real background.
    {
        shim::MemoryGraphicsDevice dev; CHECK(dev.init(800, 600, 32, false));
        SeqPlatform plat; plat.steps = { OnMenuButton((int)gui::MainMenuItem::kGfxOptions, true) };
        play::SdlMenuResult r = play::RunSdlMenu(dev, plat, cfg);
        CHECK(r.choice == play::SdlMenuChoice::kOptions);
        CHECK(r.optionsPage == 1);
    }
    // Quit works on the real-size screen.
    {
        shim::MemoryGraphicsDevice dev; CHECK(dev.init(800, 600, 32, false));
        SeqPlatform p2; p2.steps = { OnMenuButton((int)gui::MainMenuItem::kQuit, true) };
        play::SdlMenuResult rq = play::RunSdlMenu(dev, p2, cfg);
        CHECK(rq.choice == play::SdlMenuChoice::kQuit);
    }
}
