// tests/unit/mode_fsm_test.cpp — unit coverage for the GAME-MODE FSM
// (src/play/mode_fsm.cpp), PLAYABLE_PLAN P1/M1.
//
// Each transition out of MainMenu is golden-tested against the REAL reconstructed
// decision (app::MenuMainDecide): quit (highest priority), restart-display,
// run-session, and loop-back. Plus the driver's MainMenu -> sub-mode -> InGame ->
// MainMenu/Quit sequencing.
#include "play/mode_fsm.h"
#include "app/menu_loop.h"
#include "gui/main_menu.h"
#include "shim_impl/scripted_platform.h"
#include "tests/framework/test.h"

using namespace guild;

namespace {

// A scripted main-menu command sink so the real dispatch is deterministic.
struct FsmSink : gui::MainMenuCommandSink {
    bool newGameStarts = false;
    bool loadStarts = false;
    bool netStarts = false;
    int  optionsRuns = 0;
    int  quits = 0;
    bool EnterChooseCity() override { return newGameStarts; }
    bool RunLoadGame() override { return loadStarts; }
    bool ChooseNetworkMode() override { return netStarts; }
    void RunGameOptions() override { ++optionsRuns; }
    void RunCreditsScroll() override { ++optionsRuns; }
    void Quit() override { ++quits; }
};

// A click source that clicks a fixed hovered button on frame 1.
struct ClickButton : app::MenuClickSource {
    int  hover;
    int  clickFrame;
    bool esc = false;
    explicit ClickButton(int h, int cf = 1) : hover(h), clickFrame(cf) {}
    int  hoverThisFrame(int) override { return hover; }
    bool clickThisFrame(int f) override { return f == clickFrame; }
    bool escThisFrame(int f) override { return esc && f == clickFrame; }
};

// Build a MenuResult the way the real dispatch would for a given armed outcome.
app::MenuResult Armed(bool close, int flags, bool quit = false) {
    app::MenuResult r;
    r.close = close;
    r.sessionFlags = flags;
    r.quit = quit;
    return r;
}

} // namespace

// ---------------------------------------------------------------------------
// NextMode is a faithful lift of the REAL MenuMainDecide — golden-test each branch.
// ---------------------------------------------------------------------------
TEST(ModeFsm, NextModeMatchesDecideQuitHighestPriority) {
    // Quit beats restart-display AND an armed session (exactly MenuMainDecide).
    app::MenuResult r = Armed(/*close=*/true, gui::kSessionNewGame, /*quit=*/true);
    CHECK(app::MenuMainDecide(r, /*restartDisplay=*/true) == app::MenuMainNext::kQuit);
    CHECK(play::NextMode(r, /*restartDisplay=*/true) == play::GameMode::kQuit);
}

TEST(ModeFsm, NextModeMatchesDecideRestartDisplay) {
    app::MenuResult r; // no quit, nothing armed
    CHECK(app::MenuMainDecide(r, true) == app::MenuMainNext::kRestartDisplay);
    // Restart-display re-enters the menu from the game-mode point of view.
    CHECK(play::NextMode(r, /*restartDisplay=*/true) == play::GameMode::kMainMenu);
}

TEST(ModeFsm, NextModeMatchesDecideRunSession) {
    app::MenuResult r = Armed(true, gui::kSessionNewGame);
    CHECK(app::MenuMainDecide(r, false) == app::MenuMainNext::kRunSession);
    CHECK(play::NextMode(r, false) == play::GameMode::kNewGame);
}

TEST(ModeFsm, NextModeMatchesDecideLoopBack) {
    // close + no flags -> restart-menu in MenuMainDecide; an option screen ran.
    app::MenuResult r = Armed(true, 0);
    CHECK(app::MenuMainDecide(r, false) == app::MenuMainNext::kRestartMenu);
    CHECK(play::NextMode(r, false) == play::GameMode::kOptions);

    // Un-closed idle loop (no transition) -> stay at the menu.
    app::MenuResult idle;
    CHECK(app::MenuMainDecide(idle, false) == app::MenuMainNext::kRestartMenu);
    CHECK(play::NextMode(idle, false) == play::GameMode::kMainMenu);
}

// ---------------------------------------------------------------------------
// Session-flag classification (word_63C740 bits).
// ---------------------------------------------------------------------------
TEST(ModeFsm, SessionFlagClassification) {
    CHECK(play::ModeForSessionFlags(gui::kSessionNewGame)  == play::GameMode::kNewGame);
    CHECK(play::ModeForSessionFlags(gui::kSessionLoadSave) == play::GameMode::kLoadGame);
    CHECK(play::ModeForSessionFlags(gui::kSessionNetwork)  == play::GameMode::kNetwork);
    CHECK(play::ModeForSessionFlags(0)                     == play::GameMode::kMainMenu);
    // New Game wins the priority when multiple bits are set.
    CHECK(play::ModeForSessionFlags(gui::kSessionNewGame | gui::kSessionLoadSave) ==
          play::GameMode::kNewGame);
}

// ---------------------------------------------------------------------------
// The driver: MainMenu -> NewGame -> InGame -> MainMenu, then Quit.
// ---------------------------------------------------------------------------
TEST(ModeFsm, DriverNewGameToInGameBackToMenuThenQuit) {
    FsmSink sink;
    sink.newGameStarts = true;
    gui::MainMenu_SetCommandSink(&sink);

    play::ModeFsm fsm;
    fsm.start();
    CHECK(fsm.mode() == play::GameMode::kMainMenu);

    shim::ScriptedPlatform plat;
    plat.createMainWindow("t", 800, 600, false);
    plat.setMouse(36, 14, /*left=*/true);

    // Click New Game (radio slot 0).
    ClickButton newGame(/*hover=*/0);
    app::MenuResult r = fsm.stepMenu(plat, newGame, /*maxFrames=*/4);
    CHECK(r.close);
    CHECK(r.item == gui::MainMenuItem::kNewGame);
    CHECK_EQ(r.sessionFlags, gui::kSessionNewGame);
    CHECK(fsm.mode() == play::GameMode::kNewGame);
    CHECK_EQ(fsm.sessionFlags(), gui::kSessionNewGame);

    // The session goes live, then returns to the menu.
    fsm.enterInGame();
    CHECK(fsm.mode() == play::GameMode::kInGame);
    fsm.endSession();
    CHECK(fsm.mode() == play::GameMode::kMainMenu);

    // Now click Quit (radio slot 7).
    shim::ScriptedPlatform plat2;
    plat2.createMainWindow("t", 800, 600, false);
    plat2.setMouse(36, 315, true);
    ClickButton quit(/*hover=*/7);
    app::MenuResult rq = fsm.stepMenu(plat2, quit, /*maxFrames=*/4);
    CHECK(rq.quit);
    CHECK(fsm.mode() == play::GameMode::kQuit);
    CHECK(fsm.done());

    // The mode path is exactly the scripted lifecycle.
    const std::vector<play::GameMode>& path = fsm.transitions();
    CHECK_EQ((int)path.size(), 5);
    if (path.size() == 5) {
        CHECK(path[0] == play::GameMode::kMainMenu);
        CHECK(path[1] == play::GameMode::kNewGame);
        CHECK(path[2] == play::GameMode::kInGame);
        CHECK(path[3] == play::GameMode::kMainMenu);
        CHECK(path[4] == play::GameMode::kQuit);
    }
    CHECK_EQ(sink.quits, 1);
    CHECK_EQ(fsm.menuRuns(), 2);

    gui::MainMenu_SetCommandSink(nullptr);
}

// Options click does not arm a session -> FSM routes through kOptions, no InGame.
TEST(ModeFsm, DriverOptionsLoopsBack) {
    FsmSink sink;
    gui::MainMenu_SetCommandSink(&sink);

    play::ModeFsm fsm;
    fsm.start();
    shim::ScriptedPlatform plat;
    plat.createMainWindow("t", 800, 600, false);
    plat.setMouse(36, 143, true);

    ClickButton opts(/*hover=*/3); // Game Options
    app::MenuResult r = fsm.stepMenu(plat, opts, /*maxFrames=*/4);
    CHECK(!r.close);                       // options does NOT close the menu
    CHECK_EQ(sink.optionsRuns, 1);
    // No close, no armed session -> idle loop-back to the menu.
    CHECK(fsm.mode() == play::GameMode::kMainMenu);
    CHECK_EQ(fsm.restartCount(), 1);

    // enterInGame is a no-op when not in a session sub-mode.
    fsm.enterInGame();
    CHECK(fsm.mode() == play::GameMode::kMainMenu);

    gui::MainMenu_SetCommandSink(nullptr);
}

// Load Game arms the load-save session and routes to kLoadGame -> InGame.
TEST(ModeFsm, DriverLoadGameToInGame) {
    FsmSink sink;
    sink.loadStarts = true;
    gui::MainMenu_SetCommandSink(&sink);

    play::ModeFsm fsm;
    fsm.start();
    shim::ScriptedPlatform plat;
    plat.createMainWindow("t", 800, 600, false);
    plat.setMouse(36, 57, true);

    ClickButton load(/*hover=*/1);
    app::MenuResult r = fsm.stepMenu(plat, load, /*maxFrames=*/4);
    CHECK(r.close);
    CHECK(r.item == gui::MainMenuItem::kLoad);
    CHECK_EQ(r.sessionFlags, gui::kSessionLoadSave);
    CHECK(fsm.mode() == play::GameMode::kLoadGame);
    fsm.enterInGame();
    CHECK(fsm.mode() == play::GameMode::kInGame);

    gui::MainMenu_SetCommandSink(nullptr);
}

// ESC quits from the menu (byte_67225C==1 -> dword_63CC48).
TEST(ModeFsm, DriverEscQuits) {
    FsmSink sink;
    gui::MainMenu_SetCommandSink(&sink);

    play::ModeFsm fsm;
    fsm.start();
    shim::ScriptedPlatform plat;
    plat.createMainWindow("t", 800, 600, false);

    ClickButton esc(/*hover=*/-1, /*clickFrame=*/0);
    esc.esc = true;
    app::MenuResult r = fsm.stepMenu(plat, esc, /*maxFrames=*/4);
    CHECK(r.quit);
    CHECK(fsm.mode() == play::GameMode::kQuit);
    CHECK(fsm.done());

    gui::MainMenu_SetCommandSink(nullptr);
}

// stepMenu is a no-op outside MainMenu (guards the FSM contract).
TEST(ModeFsm, StepIsNoOpOutsideMenu) {
    FsmSink sink;
    sink.newGameStarts = true;
    gui::MainMenu_SetCommandSink(&sink);

    play::ModeFsm fsm;
    fsm.start();
    shim::ScriptedPlatform plat;
    plat.createMainWindow("t", 800, 600, false);
    plat.setMouse(36, 14, true);
    ClickButton newGame(0);
    fsm.stepMenu(plat, newGame, 4);
    CHECK(fsm.mode() == play::GameMode::kNewGame);

    int runsBefore = fsm.menuRuns();
    // In kNewGame, a step does nothing.
    app::MenuResult r = fsm.stepMenu(plat, newGame, 4);
    CHECK(!r.close);
    CHECK_EQ(fsm.menuRuns(), runsBefore);
    CHECK(fsm.mode() == play::GameMode::kNewGame);

    gui::MainMenu_SetCommandSink(nullptr);
}

// ---- HARDENING (wave-12): degenerate / bad transitions ----

// ModeForSessionFlags is total: any flag word — including unknown high bits and
// 0 — maps to a defined GameMode (priority NewGame > Load > Network > MainMenu).
TEST(ModeFsm, ModeForSessionFlagsIsTotal) {
    CHECK(play::ModeForSessionFlags(0) == play::GameMode::kMainMenu);
    CHECK(play::ModeForSessionFlags(gui::kSessionNewGame) == play::GameMode::kNewGame);
    CHECK(play::ModeForSessionFlags(gui::kSessionLoadSave) == play::GameMode::kLoadGame);
    CHECK(play::ModeForSessionFlags(gui::kSessionNetwork) == play::GameMode::kNetwork);
    // Unknown high bits alone -> nothing armed -> back to the menu (no UB).
    CHECK(play::ModeForSessionFlags(0x40000000) == play::GameMode::kMainMenu);
    CHECK(play::ModeForSessionFlags(-1) == play::GameMode::kNewGame);   // all bits set
    // New Game wins when several bits are set at once.
    CHECK(play::ModeForSessionFlags(gui::kSessionNewGame | gui::kSessionLoadSave |
                                    gui::kSessionNetwork) == play::GameMode::kNewGame);
}

// GameModeName never returns null and handles an out-of-enum value gracefully.
TEST(ModeFsm, GameModeNameNeverNull) {
    CHECK(play::GameModeName(play::GameMode::kMainMenu) != nullptr);
    CHECK(play::GameModeName(play::GameMode::kQuit) != nullptr);
    CHECK(play::GameModeName(static_cast<play::GameMode>(999)) != nullptr);
}

// The lifecycle edge methods are no-ops outside their valid source state — calling
// them in the wrong mode must not corrupt the transition history or change mode.
TEST(ModeFsm, LifecycleEdgesNoOpInWrongState) {
    play::ModeFsm fsm;
    fsm.start();
    CHECK(fsm.mode() == play::GameMode::kMainMenu);
    const std::size_t n0 = fsm.transitions().size();
    // enterInGame only fires from a session sub-mode; endSession only from InGame.
    fsm.enterInGame();                 // in MainMenu -> no-op
    fsm.endSession();                  // in MainMenu -> no-op
    CHECK(fsm.mode() == play::GameMode::kMainMenu);
    CHECK_EQ(fsm.transitions().size(), n0);
    // endSession when InGame is the valid edge; from a session sub-mode it is a no-op.
    // (We can't enter a sub-mode without the dispatch, so just re-assert idempotence.)
    fsm.endSession();
    CHECK(fsm.mode() == play::GameMode::kMainMenu);
    CHECK_EQ(fsm.transitions().size(), n0);
}
