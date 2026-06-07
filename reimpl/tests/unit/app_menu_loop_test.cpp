// tests/unit/app_menu_loop_test.cpp — unit coverage for the app top-level menu
// state machine + input-event pump (src/app/menu_loop.cpp).
//
// Covers (1:1 against the recovered control flow):
//   * VIBE_Input_LatchMouseState @0x40dab8 — the ring-drain latch (first pending
//     slot wins; field copy into the current-input block; empty-ring clears edges).
//   * VIBE_Menu_RunMainMenu @0x529d08 dispatch body — click->transition routing via
//     the real gui::MainMenu_Dispatch sibling; ESC -> quit.
//   * VIBE_GameLogic_MainEntryAndShutdown @0x534bbc outer-loop decision.
#include "app/menu_loop.h"
#include "gui/main_menu.h"
#include "tests/framework/test.h"

using namespace guild;

namespace {

// A scripted main-menu command sink so the dispatch is deterministic.
struct ScriptedSink : gui::MainMenuCommandSink {
    bool newGameStarts = false;
    bool loadStarts = false;
    bool netStarts = false;
    int  optionsRuns = 0;
    int  quits = 0;

    bool EnterChooseCity() override { return newGameStarts; }
    bool RunLoadGame() override { return loadStarts; }
    bool ChooseNetworkMode() override { return netStarts; }
    void RunGameOptions() override { ++optionsRuns; }
    void RunGfxOptions() override { ++optionsRuns; }
    void RunSfxOptions() override { ++optionsRuns; }
    void RunCreditsScroll() override {}
    void Quit() override { ++quits; }
};

} // namespace

// ---------------------------------------------------------------------------
// VIBE_Input_LatchMouseState — the ring-drain latch.
// ---------------------------------------------------------------------------
TEST(AppMenuLoop, LatchFirstPendingSlotWins) {
    app::MouseEvent ring[app::kMouseEventSlotCount] = {};
    // Slot 0 empty, slot 1 pending with a click edge, slot 2 also pending.
    ring[1].pending = true;
    ring[1].clickEdge = 1;
    ring[1].heldEdge = 1;
    ring[1].packedX = 120;
    ring[1].packedY = 64;
    ring[2].pending = true;
    ring[2].clickEdge = 1;

    app::LatchedInput in;
    int slot = app::InputLatchMouseState(ring, app::kMouseEventSlotCount, in);

    CHECK_EQ(slot, 1);              // first pending slot wins
    CHECK_EQ(in.clickEdge, 1);
    CHECK_EQ(in.heldEdge, 1);
    CHECK_EQ(in.cursorX, 120);
    CHECK_EQ(in.cursorY, 64);
    CHECK(!ring[1].pending);        // drained slot's flag cleared
    CHECK(ring[2].pending);         // later slot left untouched
    // The gui input globals were mirrored (mock->real: the dispatch reads these).
    CHECK_EQ(gui::g_mouseClick, 1);
    CHECK_EQ(gui::g_mouseDown, 1);
}

TEST(AppMenuLoop, LatchEmptyRingClearsEdges) {
    // Pre-set the live edges, then latch an empty ring: the top-of-call clear wins.
    gui::g_mouseClick = 1;
    gui::g_mouseDown = 1;
    app::MouseEvent ring[4] = {};   // none pending
    app::LatchedInput in;
    int slot = app::InputLatchMouseState(ring, 4, in);
    CHECK_EQ(slot, -1);
    CHECK_EQ(in.clickEdge, 0);
    CHECK_EQ(in.wheelUp, 0);
    CHECK_EQ(gui::g_mouseClick, 0); // cleared
    CHECK_EQ(gui::g_mouseDown, 0);
}

TEST(AppMenuLoop, LatchCopiesWheelFields) {
    app::MouseEvent ring[2] = {};
    ring[0].pending = true;
    ring[0].wheelUp = 1;
    ring[0].wheelDown = 0;
    app::LatchedInput in;
    app::InputLatchMouseState(ring, 2, in);
    CHECK_EQ(in.wheelUp, 1);
    CHECK_EQ(in.wheelDown, 0);
}

// ---------------------------------------------------------------------------
// VIBE_Menu_RunMainMenu — the click-dispatch body (via real gui sibling).
// ---------------------------------------------------------------------------
TEST(AppMenuLoop, DispatchNewGameArmsSessionAndCloses) {
    ScriptedSink sink;
    sink.newGameStarts = true;
    gui::MainMenu_SetCommandSink(&sink);

    app::LatchedInput in;
    in.clickEdge = 1;               // a click this frame
    app::MenuResult r;
    // Hover the New Game button (radio slot 0).
    bool cont = app::MenuDispatchFrame(in, /*hoverObject=*/0, /*escDown=*/false, r);

    CHECK(!cont);                   // closed -> loop exits
    CHECK(r.close);
    CHECK(r.item == gui::MainMenuItem::kNewGame);
    CHECK_EQ(r.sessionFlags, gui::kSessionNewGame);
    CHECK(!r.quit);

    gui::MainMenu_SetCommandSink(nullptr);
}

TEST(AppMenuLoop, DispatchNoClickDoesNothing) {
    ScriptedSink sink;
    gui::MainMenu_SetCommandSink(&sink);
    app::LatchedInput in;           // clickEdge == 0
    app::MenuResult r;
    bool cont = app::MenuDispatchFrame(in, /*hoverObject=*/0, /*escDown=*/false, r);
    CHECK(cont);                    // no close -> continue
    CHECK(!r.close);
    CHECK_EQ(r.sessionFlags, 0);
    gui::MainMenu_SetCommandSink(nullptr);
}

TEST(AppMenuLoop, DispatchQuitButtonSetsQuit) {
    ScriptedSink sink;
    gui::MainMenu_SetCommandSink(&sink);
    app::LatchedInput in;
    in.clickEdge = 1;
    app::MenuResult r;
    // Quit is radio slot 7.
    bool cont = app::MenuDispatchFrame(in, /*hoverObject=*/7, /*escDown=*/false, r);
    CHECK(!cont);
    CHECK(r.close);
    CHECK(r.quit);
    CHECK(r.item == gui::MainMenuItem::kQuit);
    CHECK_EQ(sink.quits, 1);
    gui::MainMenu_SetCommandSink(nullptr);
}

TEST(AppMenuLoop, DispatchEscQuits) {
    ScriptedSink sink;
    gui::MainMenu_SetCommandSink(&sink);
    app::LatchedInput in;           // no click
    app::MenuResult r;
    bool cont = app::MenuDispatchFrame(in, /*hoverObject=*/-1, /*escDown=*/true, r);
    CHECK(!cont);
    CHECK(r.close);
    CHECK(r.quit);                  // ESC -> dword_63CC48 = 1
    gui::MainMenu_SetCommandSink(nullptr);
}

TEST(AppMenuLoop, DispatchOptionsDoesNotClose) {
    ScriptedSink sink;
    gui::MainMenu_SetCommandSink(&sink);
    app::LatchedInput in;
    in.clickEdge = 1;
    app::MenuResult r;
    // Game Options is radio slot 3; it runs the option screen but does NOT close.
    bool cont = app::MenuDispatchFrame(in, /*hoverObject=*/3, /*escDown=*/false, r);
    CHECK(cont);
    CHECK(!r.close);
    CHECK_EQ(sink.optionsRuns, 1);
    CHECK(r.item == gui::MainMenuItem::kGameOptions);
    gui::MainMenu_SetCommandSink(nullptr);
}

// ---------------------------------------------------------------------------
// VIBE_GameLogic_MainEntryAndShutdown — the outer-loop decision.
// ---------------------------------------------------------------------------
TEST(AppMenuLoop, MainDecideRunSession) {
    app::MenuResult r;
    r.close = true;
    r.sessionFlags = gui::kSessionNewGame;
    CHECK(app::MenuMainDecide(r, /*restartDisplay=*/false) ==
          app::MenuMainNext::kRunSession);
}

TEST(AppMenuLoop, MainDecideQuitTakesPriority) {
    app::MenuResult r;
    r.quit = true;
    r.close = true;
    r.sessionFlags = gui::kSessionNewGame;
    // Quit beats both restart-display and run-session.
    CHECK(app::MenuMainDecide(r, /*restartDisplay=*/true) ==
          app::MenuMainNext::kQuit);
}

TEST(AppMenuLoop, MainDecideRestartDisplay) {
    app::MenuResult r; // no quit, no armed session
    CHECK(app::MenuMainDecide(r, /*restartDisplay=*/true) ==
          app::MenuMainNext::kRestartDisplay);
}

TEST(AppMenuLoop, MainDecideRestartMenuWhenNothingArmed) {
    app::MenuResult r;
    r.close = true;
    r.sessionFlags = 0;             // cancelled transition -> back to menu
    CHECK(app::MenuMainDecide(r, /*restartDisplay=*/false) ==
          app::MenuMainNext::kRestartMenu);
}
