// tests/integration/app_menu_loop_itest.cpp — cross-module integration of the app
// top-level menu state machine against the REAL siblings:
//
//   app::MenuRunMainMenu / MenuMainDecide   (src/app/menu_loop.cpp)
//   gui::MainMenu_Dispatch                  (src/gui/main_menu.cpp)     [real]
//   app::InitOrLoadSession                  (src/app/session_init.cpp)  [real]
//   shim::NullPlatform                      (src/shim_impl/null_platform.cpp) [real]
//
// Drives the boot->play state transition the spine performs:
//   main menu  --(New Game click)-->  arm a NewGame session  -->  in-game
//   bootstrap (InitOrLoadSession runs the real world reset + turn loop).
//
// This exercises the input pump (OS click -> latched edge -> gui dispatch) and the
// outer-loop decision (menu -> kRunSession) across the real gui + app + shim
// modules, then runs the real session bootstrap and asserts the world actually
// came up (the newly-wired path ran real, not mock).
#include "app/menu_loop.h"
#include "app/session_init.h"
#include "gui/main_menu.h"
#include "tests/framework/test.h"

#include "shim_impl/null_platform.h"

using namespace guild;

namespace {

// A real-ish new-game sink: New Game starts a session (as ChooseCity->...->Commit
// would in the binary). The other buttons are inert.
struct NewGameMenuSink : gui::MainMenuCommandSink {
    bool EnterChooseCity() override { return true; } // a city was chosen -> start
    void Quit() override {}
};

// A click source that hovers New Game (slot 0) and clicks it on frame 1, so the
// pump's first frame is a settle frame and the second carries the click edge.
struct NewGameClicks : app::MenuClickSource {
    int  hoverThisFrame(int /*frame*/) override { return 0; } // hover New Game
    bool clickThisFrame(int frame) override { return frame == 1; }
};

} // namespace

TEST(AppMenuLoopITest, MenuNewGameToInGameAcrossRealSiblings) {
    // --- 1. The main menu: pump real input, dispatch through the real gui sibling.
    NewGameMenuSink sink;
    gui::MainMenu_SetCommandSink(&sink);

    shim::NullPlatform plat;
    plat.createMainWindow("itest", 320, 200, false);
    shim::MouseState m;
    m.x = 32 + 4; m.y = 10 + 4; m.left = true; // over the New Game button, pressed
    plat.setMouse(m);

    NewGameClicks clicks;
    app::MenuResult r = app::MenuRunMainMenu(plat, clicks, /*maxFrames=*/8);

    // The menu closed on the New Game click with the new-game session flag armed.
    CHECK(r.close);
    CHECK(r.item == gui::MainMenuItem::kNewGame);
    CHECK_EQ(r.sessionFlags, gui::kSessionNewGame);
    CHECK(!r.quit);
    CHECK(r.frames >= 2);            // ran the settle frame + the click frame

    // --- 2. The outer-loop decision: a session was armed -> run it.
    app::MenuMainNext next = app::MenuMainDecide(r, /*restartDisplay=*/false);
    CHECK(next == app::MenuMainNext::kRunSession);

    // --- 3. In-game: run the REAL session bootstrap for the armed flags.
    CHECK(next == app::MenuMainNext::kRunSession);
    app::SessionInitCtx ctx;
    ctx.cityName = "Augsburg";
    ctx.rngSeed  = 0x1234;
    ctx.players.push_back({/*personId=*/100, /*kind=*/6, /*isPlayer=*/true});

    app::InitOrLoadSession(static_cast<std::uint16_t>(r.sessionFlags), ctx,
                           /*framesPerSession=*/3);

    // The real new-game bootstrap ran: the world was reset, the RNG seeded, the
    // turn loop entered (i.e. the menu DID carry us into a live session).
    CHECK(ctx.mode == app::SessionMode::NewSingle);
    CHECK(ctx.worldInited);         // sim::ResetEntityArrays + CityInitParameterTable
    CHECK(ctx.rngSeeded);           // crt::Srand
    CHECK(ctx.reachedTurnLoop);     // EnterTurnLoop emitted

    // The session-state mirror reflects a fresh new-game (round counter seeded).
    app::SessionState& st = app::GameSessionState();
    CHECK_EQ(static_cast<int>(st.sessionFlags) & gui::kSessionNewGame,
             gui::kSessionNewGame);
    CHECK(st.roundCounter >= 1);

    gui::MainMenu_SetCommandSink(nullptr);
}

TEST(AppMenuLoopITest, MenuQuitEndsTheStateMachine) {
    // ESC on the menu -> quit -> the outer loop leaves the program (no session).
    gui::MainMenu_SetCommandSink(nullptr); // default sink

    shim::NullPlatform plat;
    plat.createMainWindow("itest", 320, 200, false);

    struct EscClicks : app::MenuClickSource {
        int  hoverThisFrame(int) override { return -1; }
        bool clickThisFrame(int) override { return false; }
        bool escThisFrame(int frame) override { return frame == 0; }
    } clicks;

    app::MenuResult r = app::MenuRunMainMenu(plat, clicks, /*maxFrames=*/4);
    CHECK(r.quit);
    CHECK(r.close);
    CHECK(app::MenuMainDecide(r, false) == app::MenuMainNext::kQuit);
}

TEST(AppMenuLoopITest, PumpRoutesRealPlatformClickEdge) {
    // The input pump (InputPumpAndLatch) latches the real shim mouse into the gui
    // click edge global the dispatch reads — the OS->gui input wiring.
    shim::NullPlatform plat;
    plat.createMainWindow("itest", 320, 200, false);

    shim::MouseState up; up.left = false; plat.setMouse(up);
    app::LatchedInput in;
    bool alive = app::InputPumpAndLatch(plat, in);
    CHECK(alive);
    CHECK_EQ(in.heldEdge, 0);
    CHECK_EQ(gui::g_mouseDown, 0);

    shim::MouseState down; down.left = true; down.x = 17; down.y = 42;
    plat.setMouse(down);
    app::InputPumpAndLatch(plat, in);
    CHECK_EQ(in.heldEdge, 1);
    CHECK_EQ(in.cursorX, 17);
    CHECK_EQ(in.cursorY, 42);
    CHECK_EQ(gui::g_mouseDown, 1); // mirrored into the gui input global
}
