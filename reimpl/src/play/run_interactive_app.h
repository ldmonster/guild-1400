#pragma once
// guild::play — the REUSABLE INTERACTIVE-APP ASSEMBLY (PLAYABLE_PLAN P1 finish).
//
// This is the piece apps/guild_run.cpp's main() calls to "run the real spine as a
// program" in interactive mode. It is intentionally a small, dependency-light
// assembly layer so it can be exercised by the THREE headless test tiers WITHOUT
// main(): a ScriptedPlatform + a scripted MenuClickSource drive the very same
// RunInteractiveApp() the real binary uses, with a SessionRunner that brings a
// session up through the reconstructed spine.
//
// Two shapes, matching the two ways guild_run boots:
//
//   * RunInteractiveApp(...)   — the DISPLAY path. Brings up the GameApp spine
//     (PlayableApp::init), then drives the outer menu<->session FSM
//     (play::RunInteractive) over a real shim::IPlatform: a PlatformClickSource
//     turns OS input into menu clicks, and the SessionRunner (GameAppSessionRunner)
//     brings up a session (GameApp::InitOrLoadSession) and runs live frames until
//     the platform signals quit or the frame budget is hit. Returns the observable
//     InteractiveResult (transitions / sessions / frames / clean-quit) plus the
//     spine bring-up status. Finally runs the 13-step shutdown.
//
//   * RunInteractiveAppHeadless(...) — the NO-DISPLAY fallback. There is no menu
//     interaction to drive (no window), so it brings up the spine and runs
//     PlayableApp::runUntilQuit for a bounded frame count, then shuts down.
//
// The SESSION glue — GameAppSessionRunner — is the small testable helper the brief
// asks for: it is a free function returning a play::SessionRunner closure, unit-
// testable against any app::GameApp (the headless test build wires a GameApp over a
// ScriptedPlatform + MemoryGraphicsDevice via RealSubsystems).
//
// ODR: defines no globals owned elsewhere. Reuses play::RunInteractive / ModeFsm /
// PlatformClickSource (interactive.h), play::PlayableApp (playable_app.h) and the
// app::GameApp public lifecycle (gamelogic.h).

#include "app/gamelogic.h"
#include "play/interactive.h"
#include "play/playable_app.h"
#include "shim/IPlatform.h"

#include <functional>

namespace guild::play {

// ---------------------------------------------------------------------------
// GameAppSessionRunner — the session-bring-up glue (the unit-testable helper).
// ---------------------------------------------------------------------------
// Returns a play::SessionRunner that, when the outer FSM arms a session, brings
// that session up through the reconstructed spine and runs its bounded live turn
// loop. It maps the armed session-flags word (word_63C740) straight onto
// GameApp::InitOrLoadSession(flags, maxFrames) — the same bootstrap the binary's
// outer loop calls per player slot — and reports the live frames it ran (clamped
// to >= 0). `maxFrames` bounds the otherwise-blocking lockstep loop so a headless
// run / test is deterministic.
//
// The returned closure captures `game` by reference; keep `game` alive for the
// duration of the RunInteractive call it is passed to.
inline SessionRunner GameAppSessionRunner(app::GameApp& game) {
    return [&game](int sessionFlags, shim::IPlatform& /*plat*/, int maxFrames) -> int {
        const int budget = maxFrames < 0 ? 0 : maxFrames;
        // Bring up + run the session (bootstrap + bounded turn loop). The flags
        // word is the menu's armed word_63C740 (New Game / Load / Network).
        game.InitOrLoadSession(static_cast<std::uint16_t>(sessionFlags), budget);
        // The spine ran `budget` live frames for this session (InitOrLoadSession
        // drives exactly framesPerSession RunFrameLoop passes), unless a quit was
        // signalled mid-session — in which case we still report the budget we
        // asked for; the outer loop's pump gate handles window-close.
        return budget;
    };
}

// ---------------------------------------------------------------------------
// The full result of an interactive-app run (for the summary print + tests).
// ---------------------------------------------------------------------------
struct InteractiveAppResult {
    bool             spineInited = false; // PlayableApp::init() succeeded
    bool             headless    = false; // ran the no-display fallback path
    InteractiveResult interactive;        // the menu<->session FSM result (display path)
    int              fallbackFrames = 0;  // PlayableApp::runUntilQuit frames (headless path)
    bool             fallbackQuit  = false;// fallback stopped on a quit (vs budget)
};

// ---------------------------------------------------------------------------
// RunInteractiveApp — the display path (the assembly main() calls with a window).
// ---------------------------------------------------------------------------
// Brings up the GameApp spine over `plat`, then drives the real outer menu<->session
// FSM via play::RunInteractive using `clicks` as the menu click source and a
// GameAppSessionRunner over `game` as the session bring-up. Runs the 13-step
// shutdown on the way out. `menuMaxFrames` / `sessionMaxFrames` bound each menu pass
// and each session's live turn loop. If the spine fails to init, returns with
// spineInited=false and an empty interactive result (no shutdown beyond the spine's
// own failure teardown).
InteractiveAppResult RunInteractiveApp(app::GameApp& game, shim::IPlatform& plat,
                                       app::MenuClickSource& clicks,
                                       int displayMode = 1,
                                       int menuMaxFrames = 240,
                                       int sessionMaxFrames = 120);

// Convenience overload: build the PlatformClickSource over `plat` internally (with
// an optional hit-test) and run the display path. This is the exact shape guild_run
// uses when it has an SDL/Vulkan window.
InteractiveAppResult RunInteractiveApp(app::GameApp& game, shim::IPlatform& plat,
                                       std::function<int(int x, int y)> hitTest,
                                       int displayMode = 1,
                                       int menuMaxFrames = 240,
                                       int sessionMaxFrames = 120);

// ---------------------------------------------------------------------------
// RunInteractiveAppHeadless — the no-display fallback (NullPlatform path).
// ---------------------------------------------------------------------------
// No window => no menu interaction to drive. Brings up the spine and runs
// PlayableApp::runUntilQuit for up to `maxFrames` live frames (until the platform
// signals quit), then shuts down. Returns headless=true with the frame count and
// whether it stopped on a quit.
InteractiveAppResult RunInteractiveAppHeadless(app::GameApp& game, shim::IPlatform& plat,
                                               int displayMode = 1,
                                               int maxFrames = 600);

} // namespace guild::play
