// tests/e2e/interactive_e2e_test.cpp — full-loop end-to-end of the OUTER INTERACTIVE
// LOOP over the scriptable headless platform.
//
// Runs the program spine the binary runs (play::RunInteractive) end-to-end through the
// REAL input pump and REAL menu dispatch, driven entirely by scripted OS input on a
// shim::MockInputPlatform and a PlatformClickSource (the real IPlatform-backed click
// source) with a cursor->button hit-test:
//
//   boot -> main menu -> (cursor over New Game + click) arm a NewGame session ->
//   session runs (SessionRunner) -> back to the main menu -> (cursor over Quit + click)
//   -> the loop exits cleanly.
//
// Asserts quitClean and the full transition list — i.e. the scripted mouse really
// carried the program from the menu, through a session, back to the menu and out via
// Quit, all through the reconstructed dispatch (mock OS edges -> latched click ->
// gui::MainMenu_Dispatch -> ModeFsm).
#include "test.h"
#include "play/interactive.h"
#include "app/menu_loop.h"
#include "gui/main_menu.h"
#include "shim_impl/mock_input_platform.h"

using namespace guild;

namespace {

// The New Game button succeeds (a city was chosen); Quit records the request.
struct E2ESink : gui::MainMenuCommandSink {
    int chooseCityRan = 0;
    int quits = 0;
    bool EnterChooseCity() override { ++chooseCityRan; return true; }
    void Quit() override { ++quits; }
};

// Cursor -> radio-slot hit-test using the REAL main-menu button layout (x = 32, the
// per-button y table from gui/main_menu.h). A 40px-tall band per button.
int MenuHitTest(int x, int y) {
    if (x < gui::kMainMenuButtonX || x > gui::kMainMenuButtonX + 64)
        return -1;
    for (int i = 0; i < gui::kMainMenuButtonCount; ++i) {
        const int by = gui::MainMenu_ButtonY(i);
        if (y >= by && y < by + 40)
            return i;
    }
    return -1;
}

} // namespace

TEST(InteractiveE2E, ScriptedMouseNewGameThenQuit) {
    E2ESink sink;
    gui::MainMenu_SetCommandSink(&sink);

    shim::MockInputPlatform plat;
    plat.createMainWindow("Die Gilde", 800, 600, false);

    // The PlatformClickSource resolves hover from the live cursor via the real layout
    // hit-test, and a click from the left-button DOWN edge.
    play::PlatformClickSource clicks(plat, &MenuHitTest);

    // Park the cursor over the New Game button (slot 0, y=10) for the FIRST menu pass.
    // The session runner, when it runs, re-aims the cursor at Quit (slot 7, y=311) for
    // the SECOND menu pass and releases the button so a fresh DOWN edge can fire.
    plat.setMouse(gui::kMainMenuButtonX + 4, gui::MainMenu_ButtonY(0) + 4, /*left=*/false);

    int sessionCalls = 0;
    play::SessionRunner runner =
        [&](int flags, shim::IPlatform& p, int maxFrames) -> int {
            (void)p;
            ++sessionCalls;
            CHECK_EQ(flags, gui::kSessionNewGame);
            // Aim at Quit for the next menu pass; button released so the next press is
            // a fresh DOWN edge. The release must be OBSERVED by the click source on a
            // menu frame before the press, so the down-edge fires (the click source is
            // only sampled during a menu pass, not during the outer pump or the
            // session). Pressing 3 frames out leaves a clean up-frame at the top of the
            // next menu pass for the edge tracker to reset on.
            plat.setMouse(gui::kMainMenuButtonX + 4,
                          gui::MainMenu_ButtonY(7) + 4, /*left=*/false);
            plat.queueClickAt(plat.frame() + 3, gui::kMainMenuButtonX + 4,
                              gui::MainMenu_ButtonY(7) + 4);
            return maxFrames;
        };

    // Queue the New Game press a couple of frames into the FIRST menu pass.
    plat.queueClickAt(2, gui::kMainMenuButtonX + 4, gui::MainMenu_ButtonY(0) + 4);

    play::InteractiveResult res =
        play::RunInteractive(plat, clicks, runner,
                             /*menuMaxFrames=*/16, /*sessionMaxFrames=*/3);

    // The scripted mouse carried us: New Game session ran, then Quit ended it cleanly.
    CHECK_EQ(sink.chooseCityRan, 1);
    CHECK_EQ(sessionCalls, 1);
    CHECK_EQ(sink.quits, 1);
    CHECK(res.quitClean == true);
    CHECK_EQ(res.sessions, 1);
    CHECK_EQ(res.totalSessionFrames, 3);

    // Full transition list: menu -> NewGame -> InGame -> menu -> Quit.
    const std::vector<play::GameMode>& path = res.transitions;
    CHECK_EQ((int)path.size(), 5);
    if (path.size() == 5) {
        CHECK(path[0] == play::GameMode::kMainMenu);
        CHECK(path[1] == play::GameMode::kNewGame);
        CHECK(path[2] == play::GameMode::kInGame);
        CHECK(path[3] == play::GameMode::kMainMenu);
        CHECK(path[4] == play::GameMode::kQuit);
    }

    gui::MainMenu_SetCommandSink(nullptr);
}
