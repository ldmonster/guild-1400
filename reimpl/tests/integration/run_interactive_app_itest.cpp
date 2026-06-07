// tests/integration/run_interactive_app_itest.cpp — INTEGRATION of the reusable
// interactive-app assembly (src/play/run_interactive_app.cpp) against the REAL
// siblings it ties together:
//
//   play::RunInteractiveApp / GameAppSessionRunner (src/play/run_interactive_app.cpp)
//   play::RunInteractive / ModeFsm                  (src/play/interactive.cpp, mode_fsm.cpp)
//   app::MenuRunMainMenu / MenuMainDecide           (src/app/menu_loop.cpp)        [real]
//   gui::MainMenu_Dispatch                          (src/gui/main_menu.cpp)        [real]
//   app::GameApp::InitOrLoadSession                 (src/app/app_init.cpp)         [real spine]
//   shim::ScriptedPlatform + MemoryGraphicsDevice   (headless rig)
//
// Drives the program spine the way guild_run --interactive does — but headless:
// a scripted MenuClickSource clicks New Game (pass 0) then Quit (pass 1); the
// SessionRunner is the REAL GameAppSessionRunner (it calls InitOrLoadSession on a
// real, inited GameApp). Asserts the SEQUENCING: menu -> New Game -> live session
// -> menu -> Quit, the session ran budget frames through the spine, and the run
// ended on a clean Quit.
#include "test.h"

#include "play/run_interactive_app.h"
#include "app/wiring.h"
#include "app/menu_loop.h"
#include "config/ini.h"
#include "gui/main_menu.h"

#include "shim_impl/scripted_platform.h"
#include "shim_impl/memory_graphics.h"
#include "shim_impl/null_audio.h"
#include "shim_impl/mem_filesystem.h"
#include "shim_impl/loopback_socket.h"

using namespace guild;
using guild::app::RealSubsystems;

namespace {

// New Game arms a session; Quit is counted. (Same shape as the interactive itest.)
struct ItestSink : gui::MainMenuCommandSink {
    int quits = 0;
    bool EnterChooseCity() override { return true; }
    void Quit() override { ++quits; }
};

// A self-advancing scripted click source: it does NOT rely on the SessionRunner to
// bump the pass (the real GameAppSessionRunner can't), so it detects each new menu
// pass itself — MenuRunMainMenu restarts its per-pass frame counter at 0, so a
// hoverThisFrame(0) seen AFTER we already emitted this pass's click means the prior
// pass closed and a new one began. Pass 0 -> New Game (slot 0); pass 1 -> Quit (slot 7).
struct SequencedClicks : app::MenuClickSource {
    int  pass = 0;
    int  clickFrame = 1;
    int  hovers[2] = {0, 7};
    bool clickedThisPass = false;

    void maybeAdvance(int f) {
        if (f == 0 && clickedThisPass) { ++pass; clickedThisPass = false; }
    }
    int hoverThisFrame(int f) override {
        maybeAdvance(f);
        return pass < 2 ? hovers[pass] : -1;
    }
    bool clickThisFrame(int f) override {
        maybeAdvance(f);
        if (pass < 2 && f == clickFrame) { clickedThisPass = true; return true; }
        return false;
    }
};

struct Rig {
    shim::ScriptedPlatform     plat;
    shim::MemoryGraphicsDevice gfx;
    shim::NullAudioDevice      audio;
    shim::MemFileSystem        fs;
    decltype(shim::LoopbackSocket::makePair()) sock = shim::LoopbackSocket::makePair();
    config::IniFile            ini;
    RealSubsystems             sub{&plat, &gfx, &audio, &fs, sock.first.get(), &ini};
    app::GameApp               app{plat, gfx, audio, sub};
};

} // namespace

TEST(RunInteractiveAppItest, NewGameRunsLiveSessionThenQuitExits) {
    ItestSink sink;
    gui::MainMenu_SetCommandSink(&sink);

    Rig r;
    // Window stays open across the whole scripted run (never window-closed).
    r.plat.quitAfterPumps(-1);

    SequencedClicks clicks;

    play::InteractiveAppResult res =
        play::RunInteractiveApp(r.app, r.plat, clicks, /*displayMode=*/1,
                                /*menuMaxFrames=*/8, /*sessionMaxFrames=*/5);

    // The spine came up and the run ended on a clean Quit.
    CHECK(res.spineInited == true);
    CHECK(res.interactive.quitClean == true);

    // Exactly one session was brought up (New Game), and it ran the 5-frame budget
    // through the REAL spine (InitOrLoadSession -> 5x RunFrameLoop).
    CHECK_EQ(res.interactive.sessions, 1);
    CHECK_EQ(res.interactive.totalSessionFrames, 5);
    CHECK_EQ(r.sub.frameCount(), 5);
    CHECK_EQ(res.interactive.menuRuns, 2); // pass 0 (New Game) + pass 1 (Quit)
    CHECK_EQ(sink.quits, 1);

    // Transition path: menu -> NewGame -> InGame -> menu -> Quit.
    const std::vector<play::GameMode>& path = res.interactive.transitions;
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

// Window closed before any menu pass: the assembly ends UNCLEAN (no Quit chosen),
// no session ran — but the spine still inited + shut down cleanly.
TEST(RunInteractiveAppItest, WindowCloseEndsRunUnclean) {
    ItestSink sink;
    gui::MainMenu_SetCommandSink(&sink);

    Rig r;
    r.plat.quitAfterPumps(0); // pumpMessages() returns false immediately

    struct AlwaysNewGame : app::MenuClickSource {
        int  hoverThisFrame(int) override { return 0; }
        bool clickThisFrame(int f) override { return f == 1; }
    } clicks;

    play::InteractiveAppResult res =
        play::RunInteractiveApp(r.app, r.plat, clicks, 1, 8, 5);

    CHECK(res.spineInited == true);
    CHECK(res.interactive.quitClean == false);
    CHECK_EQ(res.interactive.sessions, 0);
    CHECK_EQ(res.interactive.menuRuns, 0);
    CHECK_EQ(r.sub.frameCount(), 0);

    gui::MainMenu_SetCommandSink(nullptr);
}
