// tests/e2e/run_interactive_app_e2e_test.cpp — END-TO-END scripted interactive
// session, headless. Exercises the FULL program spine the way guild_run
// --interactive does, but driven by a ScriptedPlatform + scripted menu clicks and
// rendered to a MemoryGraphicsDevice — no window, no main():
//
//   PlayableApp::init (CreateMainWindow -> InitSubsystems -> InitDisplayAndPaths ->
//   InitEngineAndScriptCommands)  -->  play::RunInteractive outer FSM (real
//   MenuRunMainMenu / MenuMainDecide / gui::MainMenu_Dispatch)  -->  per armed
//   session GameAppSessionRunner -> GameApp::InitOrLoadSession -> N live RunFrameLoop
//   passes  -->  back to the menu  -->  Quit  -->  13-step Shutdown.
//
// The scenario plays TWO sessions (New Game, then New Game again) and then Quits,
// proving the menu<->session loop cycles correctly and the spine accumulates the
// live frames of every session. Also re-runs the whole scenario and asserts the
// observable result is identical (the scripted spine is deterministic).
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

struct E2ESink : gui::MainMenuCommandSink {
    int quits = 0;
    bool EnterChooseCity() override { return true; } // every New Game arms a session
    void Quit() override { ++quits; }
};

// Plays `nSessions` New-Game passes, then a Quit pass. Self-advances per pass by
// detecting MenuRunMainMenu's per-pass frame counter restarting at 0.
struct ScriptedMenu : app::MenuClickSource {
    int  nSessions = 2;
    int  pass = 0;
    int  clickFrame = 1;
    bool clickedThisPass = false;

    void maybeAdvance(int f) {
        if (f == 0 && clickedThisPass) { ++pass; clickedThisPass = false; }
    }
    int hoverThisFrame(int f) override {
        maybeAdvance(f);
        if (pass < nSessions) return 0;   // New Game (slot 0)
        if (pass == nSessions) return 7;  // Quit (slot 7)
        return -1;
    }
    bool clickThisFrame(int f) override {
        maybeAdvance(f);
        if (pass <= nSessions && f == clickFrame) { clickedThisPass = true; return true; }
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

// One full scripted interactive run; returns the observable result + the spine's
// accumulated live-frame count for cross-run determinism comparison.
struct RunOutcome {
    play::InteractiveAppResult res;
    int spineFrames = 0;
    int quits = 0;
};

RunOutcome PlayScenario(int nSessions, int sessionFrames) {
    E2ESink sink;
    gui::MainMenu_SetCommandSink(&sink);

    Rig r;
    r.plat.quitAfterPumps(-1); // window open for the whole scripted run

    ScriptedMenu clicks;
    clicks.nSessions = nSessions;

    RunOutcome o;
    o.res = play::RunInteractiveApp(r.app, r.plat, clicks, /*displayMode=*/1,
                                    /*menuMaxFrames=*/8, /*sessionMaxFrames=*/sessionFrames);
    o.spineFrames = r.sub.frameCount();
    o.quits = sink.quits;

    gui::MainMenu_SetCommandSink(nullptr);
    return o;
}

} // namespace

TEST(RunInteractiveAppE2E, TwoSessionsThenQuitFullLifecycle) {
    const int kSessions = 2, kFrames = 4;
    RunOutcome o = PlayScenario(kSessions, kFrames);

    // The whole spine came up and ended on a clean Quit.
    CHECK(o.res.spineInited == true);
    CHECK(o.res.interactive.quitClean == true);

    // Two sessions ran; each drove `kFrames` live frames through the real spine.
    CHECK_EQ(o.res.interactive.sessions, kSessions);
    CHECK_EQ(o.res.interactive.totalSessionFrames, kSessions * kFrames);
    CHECK_EQ(o.spineFrames, kSessions * kFrames);
    CHECK_EQ(o.quits, 1);
    // menu passes: one per session + the final Quit pass.
    CHECK_EQ(o.res.interactive.menuRuns, kSessions + 1);

    // Transition path: kMainMenu, then per session (NewGame, InGame, MainMenu), then Quit.
    const std::vector<play::GameMode>& path = o.res.interactive.transitions;
    CHECK_EQ((int)path.size(), 1 + kSessions * 3 + 1);
    if (path.size() == (size_t)(1 + kSessions * 3 + 1)) {
        CHECK(path.front() == play::GameMode::kMainMenu);
        for (int s = 0; s < kSessions; ++s) {
            CHECK(path[1 + s * 3 + 0] == play::GameMode::kNewGame);
            CHECK(path[1 + s * 3 + 1] == play::GameMode::kInGame);
            CHECK(path[1 + s * 3 + 2] == play::GameMode::kMainMenu);
        }
        CHECK(path.back() == play::GameMode::kQuit);
    }
}

TEST(RunInteractiveAppE2E, ScriptedRunIsDeterministicAcrossReruns) {
    RunOutcome a = PlayScenario(2, 4);
    RunOutcome b = PlayScenario(2, 4);

    // Same script + same fresh state => identical observable spine behavior.
    CHECK_EQ(a.res.interactive.sessions, b.res.interactive.sessions);
    CHECK_EQ(a.res.interactive.menuRuns, b.res.interactive.menuRuns);
    CHECK_EQ(a.res.interactive.totalSessionFrames, b.res.interactive.totalSessionFrames);
    CHECK_EQ(a.spineFrames, b.spineFrames);
    CHECK(a.res.interactive.quitClean == b.res.interactive.quitClean);
    CHECK_EQ((int)a.res.interactive.transitions.size(),
             (int)b.res.interactive.transitions.size());
    if (a.res.interactive.transitions.size() == b.res.interactive.transitions.size()) {
        bool same = true;
        for (size_t i = 0; i < a.res.interactive.transitions.size(); ++i)
            if (a.res.interactive.transitions[i] != b.res.interactive.transitions[i])
                same = false;
        CHECK(same == true);
    }
}
