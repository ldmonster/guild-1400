// tests/integration/interactive_itest.cpp — cross-module integration of the OUTER
// INTERACTIVE LOOP (src/play/interactive.cpp) against the REAL siblings:
//
//   play::RunInteractive / play::ModeFsm     (src/play/interactive.cpp, mode_fsm.cpp)
//   app::MenuRunMainMenu / MenuMainDecide     (src/app/menu_loop.cpp)    [real]
//   gui::MainMenu_Dispatch                    (src/gui/main_menu.cpp)     [real]
//   shim::MockInputPlatform                   (src/shim_impl/mock_input_platform.cpp)
//
// Drives the program spine the way the binary's outer while(1) does, but with a
// SCRIPTED MenuClickSource (the ClickButton pattern from tests/unit/mode_fsm_test.cpp)
// and a SessionRunner lambda that just returns N frames — so the assertion is the
// SEQUENCING: menu pass -> New Game arms a session -> the session runs -> back to the
// menu -> Quit -> the loop exits. Asserts the transition path + counts the spine
// produced from the real menu dispatch.
#include "test.h"
#include "play/interactive.h"
#include "app/menu_loop.h"
#include "gui/main_menu.h"
#include "shim_impl/mock_input_platform.h"

using namespace guild;

namespace {

// A scripted main-menu command sink so the real gui dispatch is deterministic:
// New Game succeeds (starts a session); other entry points do nothing.
struct ItestSink : gui::MainMenuCommandSink {
    int quits = 0;
    bool EnterChooseCity() override { return true; } // New Game arms a session
    void Quit() override { ++quits; }
};

// A scripted click source that drives a SEQUENCE of menu passes. Each call to
// MenuRunMainMenu restarts the per-pass frame counter at 0, so we advance our own
// "which menu pass" cursor on the first frame of each pass and emit that pass's
// scripted (hover, click). Pass 0 clicks New Game (slot 0); pass 1 clicks Quit (slot 7).
struct SequencedClicks : app::MenuClickSource {
    int   pass = 0;          // which menu pass we are scripting
    int   clickFrame = 1;    // emit the click on frame 1 of each pass
    int   hovers[2] = {0, 7};// pass 0 -> New Game, pass 1 -> Quit

    int hoverThisFrame(int f) override {
        (void)f;
        return pass < 2 ? hovers[pass] : -1;
    }
    bool clickThisFrame(int f) override {
        return pass < 2 && f == clickFrame;
    }
    // Detect the start of a fresh menu pass: frame 0 after a click already fired means
    // the previous pass closed and a new one began. We advance the cursor when frame
    // wraps back to 0 having already emitted this pass's click.
    void notePassBoundary() { ++pass; }
};

} // namespace

TEST(InteractiveItest, NewGameRunsSessionThenQuitExitsLoop) {
    ItestSink sink;
    gui::MainMenu_SetCommandSink(&sink);

    shim::MockInputPlatform plat;
    plat.createMainWindow("t", 800, 600, false);

    SequencedClicks clicks;

    // Count session runs and total frames through the injected SessionRunner. Each
    // call advances the click script to the NEXT menu pass (so the second menu pass
    // clicks Quit). The runner returns a fixed frame budget echo.
    int sessionCalls = 0;
    play::SessionRunner runner =
        [&](int flags, shim::IPlatform& p, int maxFrames) -> int {
            (void)p;
            ++sessionCalls;
            CHECK_EQ(flags, gui::kSessionNewGame); // the menu armed New Game
            clicks.notePassBoundary();             // next menu pass -> Quit
            return maxFrames;                      // "ran" maxFrames frames
        };

    play::InteractiveResult res =
        play::RunInteractive(plat, clicks, runner,
                             /*menuMaxFrames=*/8, /*sessionMaxFrames=*/5);

    // One session ran (New Game), then Quit cleanly exited the loop.
    CHECK_EQ(sessionCalls, 1);
    CHECK_EQ(res.sessions, 1);
    CHECK_EQ(res.totalSessionFrames, 5);
    CHECK(res.quitClean == true);
    CHECK_EQ(res.menuRuns, 2); // pass 0 (New Game) + pass 1 (Quit)

    // Transition path: start at the menu, arm New Game, go InGame, back to the menu,
    // then Quit.  (mode_fsm seeds the path with kMainMenu at start().)
    const std::vector<play::GameMode>& path = res.transitions;
    CHECK_EQ((int)path.size(), 5);
    if (path.size() == 5) {
        CHECK(path[0] == play::GameMode::kMainMenu);
        CHECK(path[1] == play::GameMode::kNewGame);
        CHECK(path[2] == play::GameMode::kInGame);
        CHECK(path[3] == play::GameMode::kMainMenu);
        CHECK(path[4] == play::GameMode::kQuit);
    }
    CHECK_EQ(sink.quits, 1);

    gui::MainMenu_SetCommandSink(nullptr);
}

// Window-close mid-loop ends the run UNCLEANLY (the player never chose Quit).
TEST(InteractiveItest, WindowCloseEndsLoopNotClean) {
    ItestSink sink;
    gui::MainMenu_SetCommandSink(&sink);

    shim::MockInputPlatform plat;
    plat.createMainWindow("t", 800, 600, false);
    plat.quitAfter(0); // pumpMessages() returns false immediately

    // A click source that would keep clicking New Game forever if the loop ran.
    struct AlwaysNewGame : app::MenuClickSource {
        int  hoverThisFrame(int) override { return 0; }
        bool clickThisFrame(int f) override { return f == 1; }
    } clicks;

    int sessionCalls = 0;
    play::SessionRunner runner =
        [&](int, shim::IPlatform&, int mf) -> int { ++sessionCalls; return mf; };

    play::InteractiveResult res =
        play::RunInteractive(plat, clicks, runner, 8, 5);

    // The window closed before any menu pass ran: no sessions, not a clean quit.
    CHECK(res.quitClean == false);
    CHECK_EQ(res.sessions, 0);
    CHECK_EQ(sessionCalls, 0);
    CHECK_EQ(res.menuRuns, 0);
    // Only the seeded start mode is present.
    CHECK_EQ((int)res.transitions.size(), 1);
    if (!res.transitions.empty())
        CHECK(res.transitions[0] == play::GameMode::kMainMenu);

    gui::MainMenu_SetCommandSink(nullptr);
}
