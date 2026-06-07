// guild::play — the reusable interactive-app assembly. See run_interactive_app.h.
//
// Ties the reconstructed spine bring-up (PlayableApp) to the outer menu<->session
// FSM (RunInteractive) and the session bootstrap (GameApp::InitOrLoadSession via
// GameAppSessionRunner), so apps/guild_run.cpp's main() — and the three headless
// test tiers — share one assembly. The menu decision/dispatch and the session
// bootstrap are REUSED from the real siblings; this file only sequences them.
#include "play/run_interactive_app.h"

namespace guild::play {

InteractiveAppResult RunInteractiveApp(app::GameApp& game, shim::IPlatform& plat,
                                       app::MenuClickSource& clicks, int displayMode,
                                       int menuMaxFrames, int sessionMaxFrames) {
    InteractiveAppResult out;

    // Bring up the spine exactly as the binary does before its menu/session loop.
    PlayableApp appSpine(game, plat);
    if (!appSpine.init(displayMode)) {
        out.spineInited = false;
        return out;
    }
    out.spineInited = true;

    // Drive the real outer while(1): menu pass -> if a session armed, bring it up +
    // run its bounded turn loop -> back to menu, until Quit or window-close.
    out.interactive = RunInteractive(plat, clicks, GameAppSessionRunner(game),
                                     menuMaxFrames, sessionMaxFrames);

    // The verbatim 13-step shutdown.
    appSpine.shutdown();
    return out;
}

InteractiveAppResult RunInteractiveApp(app::GameApp& game, shim::IPlatform& plat,
                                       std::function<int(int x, int y)> hitTest,
                                       int displayMode, int menuMaxFrames,
                                       int sessionMaxFrames) {
    PlatformClickSource clicks(plat, std::move(hitTest));
    return RunInteractiveApp(game, plat, clicks, displayMode, menuMaxFrames,
                             sessionMaxFrames);
}

InteractiveAppResult RunInteractiveAppHeadless(app::GameApp& game, shim::IPlatform& plat,
                                               int displayMode, int maxFrames) {
    InteractiveAppResult out;
    out.headless = true;

    PlayableApp appSpine(game, plat);
    if (!appSpine.init(displayMode)) {
        out.spineInited = false;
        return out;
    }
    out.spineInited = true;

    // No menu interaction without a window: run live frames until quit / budget.
    out.fallbackFrames = appSpine.runUntilQuit(kLiveFrameMask, maxFrames);
    out.fallbackQuit = appSpine.quitRequested();

    appSpine.shutdown();
    return out;
}

} // namespace guild::play
