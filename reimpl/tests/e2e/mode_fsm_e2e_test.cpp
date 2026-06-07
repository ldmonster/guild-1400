// tests/e2e/mode_fsm_e2e_test.cpp — PROVE THE GAME-MODE FSM RUNS (PLAYABLE_PLAN P1/M1).
//
// Drives a scripted player sequence through the FSM and the REAL reconstructed menu
// dispatch end-to-end, headless:
//
//   start (MainMenu) --click New Game--> [real MenuRunMainMenu routes the click
//   through the real gui::MainMenu_Dispatch -> arms word_63C740] --> [real
//   MenuMainDecide -> kRunSession] --> kNewGame --> enterInGame (kInGame) -->
//   endSession --> MainMenu --click Quit--> kQuit.
//
// Asserts the resulting session flags + the full mode-transition path match the
// reconstructed logic (every MainMenu exit cross-checked against the real
// MenuMainDecide oracle). Self-consistency: the same scripted inputs produce the
// identical path on a re-run (determinism, PLAYABLE_PLAN B1).
#include "play/mode_fsm.h"
#include "app/menu_loop.h"
#include "gui/main_menu.h"
#include "shim_impl/scripted_platform.h"
#include "tests/framework/test.h"

#include <vector>

using namespace guild;

namespace {

struct E2ESink : gui::MainMenuCommandSink {
    bool newGameStarts = false;
    int  quits = 0;
    bool chooseCityRan = false;
    bool EnterChooseCity() override { chooseCityRan = true; return newGameStarts; }
    void Quit() override { ++quits; }
};

struct ClickHover : app::MenuClickSource {
    int hover, clickFrame;
    ClickHover(int h, int cf) : hover(h), clickFrame(cf) {}
    int  hoverThisFrame(int) override { return hover; }
    bool clickThisFrame(int f) override { return f == clickFrame; }
};

// Run the full scripted lifecycle on a fresh FSM, returning the path + the flags the
// menu armed for the New Game click. Re-runnable for the determinism check.
struct RunOutcome {
    std::vector<play::GameMode> path;
    int  newGameFlags = 0;
    gui::MainMenuItem newGameItem = gui::MainMenuItem::kQuit;
    bool oracleRunSession = false; // real MenuMainDecide said kRunSession
    bool oracleQuit = false;       // real MenuMainDecide said kQuit
    bool chooseCityRan = false;
    int  quits = 0;
};

RunOutcome RunScript() {
    E2ESink sink;
    sink.newGameStarts = true;
    gui::MainMenu_SetCommandSink(&sink);

    play::ModeFsm fsm;
    fsm.start();

    RunOutcome out;

    // --- frame block 1: the main menu, click New Game (radio slot 0) on frame 1. ---
    shim::ScriptedPlatform menuPlat;
    menuPlat.createMainWindow("Die Gilde", 800, 600, false);
    menuPlat.setMouse(32 + 4, 10 + 4, /*left=*/true); // over the New Game button
    ClickHover newGame(/*hover=*/0, /*clickFrame=*/1);

    app::MenuResult r1 = fsm.stepMenu(menuPlat, newGame, /*maxFrames=*/8);
    out.newGameFlags = r1.sessionFlags;
    out.newGameItem  = r1.item;
    // Cross-check the FSM's transition against the real reconstructed oracle.
    out.oracleRunSession =
        (app::MenuMainDecide(r1, /*restartDisplay=*/false) ==
         app::MenuMainNext::kRunSession);

    // --- the session goes live, then returns to the menu. ---
    fsm.enterInGame();
    fsm.endSession();

    // --- frame block 2: back at the menu, click Quit (radio slot 7). ---
    shim::ScriptedPlatform quitPlat;
    quitPlat.createMainWindow("Die Gilde", 800, 600, false);
    quitPlat.setMouse(32 + 4, 311 + 4, true);
    ClickHover quit(/*hover=*/7, /*clickFrame=*/1);

    app::MenuResult r2 = fsm.stepMenu(quitPlat, quit, /*maxFrames=*/8);
    out.oracleQuit =
        (app::MenuMainDecide(r2, /*restartDisplay=*/false) == app::MenuMainNext::kQuit);

    out.path = fsm.transitions();
    out.chooseCityRan = sink.chooseCityRan;
    out.quits = sink.quits;

    gui::MainMenu_SetCommandSink(nullptr);
    return out;
}

} // namespace

TEST(ModeFsmE2E, ScriptedMenuNewGameInGameQuitRunsThroughRealDispatch) {
    RunOutcome o = RunScript();

    // The real gui dispatch ran (mock->real): New Game routed to EnterChooseCity and
    // armed the new-game session flag (word_63C740 | 1).
    CHECK(o.chooseCityRan);
    CHECK(o.newGameItem == gui::MainMenuItem::kNewGame);
    CHECK_EQ(o.newGameFlags, gui::kSessionNewGame);

    // The FSM's transitions match the REAL MenuMainDecide oracle at each menu exit.
    CHECK(o.oracleRunSession);   // New Game close -> kRunSession
    CHECK(o.oracleQuit);         // Quit close     -> kQuit
    CHECK_EQ(o.quits, 1);

    // The full mode path is exactly the scripted lifecycle.
    CHECK_EQ((int)o.path.size(), 5);
    if (o.path.size() == 5) {
        CHECK(o.path[0] == play::GameMode::kMainMenu);
        CHECK(o.path[1] == play::GameMode::kNewGame);  // armed by the real dispatch
        CHECK(o.path[2] == play::GameMode::kInGame);   // session went live
        CHECK(o.path[3] == play::GameMode::kMainMenu); // session returned to menu
        CHECK(o.path[4] == play::GameMode::kQuit);     // Quit click ended the program
    }
}

// Determinism / self-consistency (PLAYABLE_PLAN B1): same scripted inputs -> identical
// mode path and flags across runs.
TEST(ModeFsmE2E, ScriptedRunIsDeterministic) {
    RunOutcome a = RunScript();
    RunOutcome b = RunScript();

    CHECK_EQ(a.newGameFlags, b.newGameFlags);
    CHECK_EQ((int)a.path.size(), (int)b.path.size());
    bool same = a.path.size() == b.path.size();
    if (same) {
        for (size_t i = 0; i < a.path.size(); ++i)
            if (a.path[i] != b.path[i]) same = false;
    }
    CHECK(same);                 // identical transition path
    // And the path actually CHANGED state (not inert): it visited InGame and Quit.
    bool sawInGame = false, sawQuit = false;
    for (play::GameMode m : a.path) {
        if (m == play::GameMode::kInGame) sawInGame = true;
        if (m == play::GameMode::kQuit)   sawQuit = true;
    }
    CHECK(sawInGame);
    CHECK(sawQuit);
}
