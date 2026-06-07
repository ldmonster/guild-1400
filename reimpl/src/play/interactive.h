#pragma once
// guild::play — the OUTER INTERACTIVE LOOP (PLAYABLE_PLAN, the "run as a program"
// spine). This is the top-level driver that makes the engine behave like a running
// application rather than a fixed-frame test harness: it owns the ModeFsm
// (src/play/mode_fsm.h), pumps a real shim::IPlatform every iteration, runs the REAL
// menu dispatch (app::MenuRunMainMenu) for each menu pass, and — when the menu arms a
// session — brings up and runs one session through an injected SessionRunner, then
// returns to the menu. It stops when the FSM reaches Quit (the player chose Quit / ESC)
// OR the platform window is closed (pumpMessages() returns false).
//
// This module reconstructs the outermost while(1) shape of
//   VIBE_GameLogic_MainEntryAndShutdown @0x534bbc
// (menu -> if a session armed run it -> loop back unless quit), lifted onto the clean
// game-mode FSM and the shim OS boundary. The menu DECISION and dispatch are NOT
// reinvented here — they come straight from app::MenuRunMainMenu / app::MenuMainDecide
// via ModeFsm::stepMenu. The session bring-up is the caller's job (the SessionRunner),
// so the loop stays free of the world-bootstrap dependency and is testable headless.
//
// ODR: defines no globals owned elsewhere. Reuses app::MenuClickSource, play::ModeFsm
// and shim::IPlatform.

#include "app/menu_loop.h"
#include "play/mode_fsm.h"
#include "shim/IPlatform.h"

#include <functional>
#include <vector>

namespace guild::play {

// The observable result of one full interactive run (for golden-testing the program
// spine): how many menu passes ran, how many sessions were brought up, the total
// session frames the SessionRunner reported, the ordered game-mode path, and whether
// the run ended on a clean Quit (vs. the window being closed underneath it).
struct InteractiveResult {
    int menuRuns = 0;                    // ModeFsm::menuRuns() at exit (menu passes run)
    int sessions = 0;                    // sessions brought up + run (enterInGame count)
    int totalSessionFrames = 0;          // sum of frames each SessionRunner reported
    std::vector<GameMode> transitions;   // the full ModeFsm path (start..exit)
    bool quitClean = false;              // exited because the FSM chose Quit (not window close)
};

// Brings up and runs ONE session for the armed session flags, returns the number of
// frames it ran. The platform is provided so a real session can pump/poll input; the
// frame budget bounds the otherwise-blocking turn loop. (In tests this is just a lambda
// that returns N; in the real app it wraps app::InitOrLoadSession + the turn loop.)
using SessionRunner =
    std::function<int(int sessionFlags, shim::IPlatform& plat, int maxFrames)>;

// ===========================================================================
// MenuClickSource backed by a real shim::IPlatform.
// ---------------------------------------------------------------------------
// Adapts the OS input boundary into the menu loop's per-frame click source:
//   * hoverThisFrame: read getMouse() and resolve the hovered widget id via an
//     injectable hit-test fn hitTest(x,y) (default returns -1 == nothing hovered).
//   * clickThisFrame: a left-button DOWN edge (false->true) since the last query.
//   * escThisFrame:   keyDown(VK_ESCAPE == 27).
// ===========================================================================
inline constexpr int kVkEscape = 27; // VK_ESCAPE (the original's ESC scancode gate)

class PlatformClickSource : public app::MenuClickSource {
public:
    explicit PlatformClickSource(shim::IPlatform& plat,
                                 std::function<int(int x, int y)> hitTest = {});

    int  hoverThisFrame(int frame) override;
    bool clickThisFrame(int frame) override;
    bool escThisFrame(int frame) override;

private:
    shim::IPlatform&                  plat_;
    std::function<int(int x, int y)>  hitTest_;
    bool                              prevLeft_ = false; // left-button edge tracker
};

// ===========================================================================
// The outer interactive loop.
// ---------------------------------------------------------------------------
// Drives a fresh ModeFsm: each iteration, while in MainMenu, run one menu pass
// (stepMenu, bounded by menuMaxFrames). If that armed a session sub-mode, enter
// InGame, run the session via runSession (bounded by sessionMaxFrames), then end the
// session (back to the menu). Stop when the FSM is done() (Quit chosen) OR the platform
// pumpMessages() reports the window was closed. Records the transition path + counts.
InteractiveResult RunInteractive(shim::IPlatform& plat, app::MenuClickSource& clicks,
                                 SessionRunner runSession, int menuMaxFrames = 240,
                                 int sessionMaxFrames = 120);

} // namespace guild::play
