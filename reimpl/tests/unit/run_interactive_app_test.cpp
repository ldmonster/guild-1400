// tests/unit/run_interactive_app_test.cpp — UNIT coverage of the SESSION-RUNNER
// GLUE in src/play/run_interactive_app.h (play::GameAppSessionRunner): the small
// testable helper apps/guild_run.cpp uses to bring a session up through the real
// spine. No main(), no window — a GameApp is built over the headless shims
// (ScriptedPlatform + MemoryGraphicsDevice + RealSubsystems) and the runner closure
// is invoked DIRECTLY, asserting it (a) maps the armed session-flags word onto
// GameApp::InitOrLoadSession, (b) runs exactly the requested live-frame budget
// through the spine's RunFrameLoop, and (c) reports that budget back to the caller.
#include "test.h"

#include "play/run_interactive_app.h"
#include "app/wiring.h"
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

// Build a fully-inited GameApp over headless shims (the same rig the app_wiring
// e2e tests use). Returns by-init: the caller owns the backing objects.
struct HeadlessApp {
    shim::ScriptedPlatform   plat;
    shim::MemoryGraphicsDevice gfx;
    shim::NullAudioDevice    audio;
    shim::MemFileSystem      fs;
    decltype(shim::LoopbackSocket::makePair()) sock = shim::LoopbackSocket::makePair();
    config::IniFile          ini;
    RealSubsystems           sub{&plat, &gfx, &audio, &fs, sock.first.get(), &ini};
    app::GameApp             app{plat, gfx, audio, sub};

    bool bringUp() {
        return app.CreateMainWindow(1) && app.InitSubsystemsAndMovieDll() &&
               app.InitDisplayAndPaths(1) && app.InitEngineAndScriptCommands();
    }
};

} // namespace

TEST(RunInteractiveAppUnit, SessionRunnerRunsBudgetFramesThroughSpine) {
    HeadlessApp h;
    CHECK(h.bringUp());

    const int before = h.sub.frameCount();

    play::SessionRunner runner = play::GameAppSessionRunner(h.app);
    // Arm New Game and ask for an 8-frame bounded turn loop.
    const int ran = runner(gui::kSessionNewGame, h.plat, /*maxFrames=*/8);

    // The runner reports the budget it ran (the spine drives framesPerSession frames).
    CHECK_EQ(ran, 8);
    // The spine actually advanced 8 live frames (InitOrLoadSession -> 8x RunFrameLoop).
    CHECK_EQ(h.sub.frameCount() - before, 8);

    h.app.Shutdown();
}

TEST(RunInteractiveAppUnit, SessionRunnerClampsNegativeBudgetToZero) {
    HeadlessApp h;
    CHECK(h.bringUp());

    const int before = h.sub.frameCount();
    play::SessionRunner runner = play::GameAppSessionRunner(h.app);
    // A negative (unbounded) budget is clamped to 0 frames for a deterministic test
    // run — the runner must never spin forever and reports 0.
    const int ran = runner(gui::kSessionNewGame, h.plat, /*maxFrames=*/-1);

    CHECK_EQ(ran, 0);
    CHECK_EQ(h.sub.frameCount() - before, 0);

    h.app.Shutdown();
}

TEST(RunInteractiveAppUnit, HeadlessFallbackRunsBoundedLiveFramesThenShutsDown) {
    HeadlessApp h;
    // Never quit via window-close: let the frame budget bound it.
    h.plat.quitAfterPumps(-1);

    play::InteractiveAppResult r =
        play::RunInteractiveAppHeadless(h.app, h.plat, /*displayMode=*/1,
                                        /*maxFrames=*/6);

    CHECK(r.headless == true);
    CHECK(r.spineInited == true);
    CHECK_EQ(r.fallbackFrames, 6);
    // It stopped on the budget, not a quit (no window-close was scripted).
    CHECK(r.fallbackQuit == false);
    CHECK_EQ(h.sub.frameCount(), 6);
}
