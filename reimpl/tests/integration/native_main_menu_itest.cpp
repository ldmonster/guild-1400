// Integration: play::RunNativeMainMenu drives the REAL gui::Menu_RunMainMenu on a
// headless device + scripted platform — SDL present -> RunFrameLoop, mouse hit-test ->
// click/hover, sub-screens -> native sdl_* screens. Backend-agnostic.
#include "test.h"
#include "play/native_main_menu.h"
#include "gui/main_menu.h"
#include "shim_impl/memory_graphics.h"
#include "shim_impl/mem_filesystem.h"
#include "shim/IPlatform.h"
#include <vector>
#include <cstdint>

using namespace guild;

namespace {
// A platform that scripts mouse/keys per pump, and "closes the window" after maxPumps.
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
std::vector<std::pair<std::string,std::string>> Cities() {
    return { {"AUGSBURG","Resources/gamedata/Cities/AUGSBURG.cty"},
             {"BERLIN","Resources/gamedata/Cities/BERLIN.cty"} };
}
} // namespace

TEST(NativeMainMenuItest, WindowCloseEndsMenuWithQuit) {
    shim::MemoryGraphicsDevice dev; CHECK(dev.init(320, 240, 32, false));
    shim::MemFileSystem fs;
    SeqPlatform plat; plat.maxPumps = 4; plat.steps = { OnMenuButton(0, false) };
    play::NativeMenuResult r = play::RunNativeMainMenu(dev, plat, fs, /*gameDir=*/"", Cities(),
                                                       320, 240, /*frameCapMs=*/0, /*maxFrames=*/-1);
    CHECK(r.action == play::NativeMenuResult::kQuit);
    CHECK(r.quitByWindow);
    CHECK(r.framesPresented > 0);   // it rendered + presented real frames
}

TEST(NativeMainMenuItest, QuitButtonEndsMenu) {
    shim::MemoryGraphicsDevice dev; CHECK(dev.init(320, 240, 32, false));
    shim::MemFileSystem fs;
    SeqPlatform plat;
    // Click Quit on a couple of frames; the menu's Quit dispatch arms close -> loop ends.
    const int q = (int)gui::MainMenuItem::kQuit;
    plat.steps = { OnMenuButton(q, false), OnMenuButton(q, true), OnMenuButton(q, false) };
    plat.maxPumps = 40;
    play::NativeMenuResult r = play::RunNativeMainMenu(dev, plat, fs, /*gameDir=*/"", Cities(),
                                                       320, 240, 0, -1);
    CHECK(r.action == play::NativeMenuResult::kQuit);
    CHECK(!r.quitByWindow);   // ended via the armed close, not the window
}

TEST(NativeMainMenuItest, RendersFramesEachLoop) {
    shim::MemoryGraphicsDevice dev; CHECK(dev.init(320, 240, 32, false));
    shim::MemFileSystem fs;
    SeqPlatform plat; plat.maxPumps = 6; plat.steps = { OnMenuButton(0, false) };
    play::NativeMenuResult r = play::RunNativeMainMenu(dev, plat, fs, "", Cities(), 320, 240, 0, -1);
    // ~5 presented frames before the window closes; deterministic.
    CHECK(r.framesPresented >= 4);
    CHECK(dev.presentCount() >= 4);
}
