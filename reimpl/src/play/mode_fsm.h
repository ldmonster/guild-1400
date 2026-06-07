#pragma once
// guild::play — the GAME-MODE finite-state machine (PLAYABLE_PLAN P1, M1).
//
// This is the top-level "what is the game doing right now" controller that sits
// ABOVE the reconstructed main-menu dispatch. It drives the real menu state machine
//   guild::app::MenuRunMainMenu  @0x529d08  (the click-dispatch frame loop) and
//   guild::app::MenuMainDecide   @0x534bbc  (the menu<->session outer-loop decision)
// and turns their reconstructed outputs — the session flags (word_63C740), the quit
// flag (dword_63CC48) and the restart-display signal (dword_63CC38) — into a clean
// game-mode lifecycle:
//
//      +----------------------------------------------------+
//      |                                                    v
//   MainMenu --click--> { NewGame | LoadGame | Options | Quit }
//      ^                      |          |        |        |
//      |                      +----+-----+        |        v
//      |                           v              |      Quit (terminal)
//      |                        InGame            |
//      |  (session ends)           |              |
//      +---------------------------+              |
//      ^                                          |
//      +------------ (Options closes) ------------+
//
// The FSM does NOT reinvent the menu decision logic: every transition out of
// MainMenu is decided by the REAL MenuMainDecide (quit highest priority, then
// restart-display, then run-session, else loop back) plus the REAL MenuRunMainMenu
// dispatch (which routes the click through the real gui::MainMenu_Dispatch sibling
// and arms word_63C740 / dword_63CC48). This module only:
//   * classifies an armed session (word_63C740) into NewGame/LoadGame/Network, and
//   * sequences MainMenu -> sub-mode -> InGame -> MainMenu/Quit.
//
// ODR: defines no globals owned elsewhere. Reuses (extern) app::MenuRunMainMenu /
// app::MenuMainDecide / app::MenuResult and the gui session-flag constants.

#include "app/menu_loop.h"
#include "gui/main_menu.h"
#include "shim/IPlatform.h"

#include <cstdint>
#include <vector>

namespace guild::play {

// The top-level game mode. The session sub-modes (NewGame/LoadGame/Network) are the
// transient "we just armed a session of kind X" states that the FSM passes through
// on the way into InGame; they mirror word_63C740's bits.
enum class GameMode {
    kMainMenu,   // the startup menu is up (MenuRunMainMenu running)
    kNewGame,    // word_63C740 & kSessionNewGame  — new single/campaign armed
    kLoadGame,   // word_63C740 & kSessionLoadSave — a savegame was picked
    kNetwork,    // word_63C740 & kSessionNetwork  — a network session armed
    kOptions,    // an option/credits screen ran (no session) -> back to menu
    kInGame,     // a session is live (InitOrLoadSession ran the turn loop)
    kQuit,       // dword_63CC48 — leave the program (terminal)
};

const char* GameModeName(GameMode m);

// Map an armed session-flags word (word_63C740) to the session sub-mode it selects.
// New Game (|1) wins over Load (&2) wins over Network (&4) — the original arms at
// most one per close, but the priority makes the mapping total. Returns kMainMenu
// when nothing is armed (a cancelled transition loops back to the menu).
GameMode ModeForSessionFlags(int sessionFlags);

// ---------------------------------------------------------------------------
// One outer-loop decision, lifted to the game-mode vocabulary.
// ---------------------------------------------------------------------------
// Given a MenuResult (the reconstructed close/quit/session-flag bookkeeping) and the
// restart-display signal, return the NEXT game mode. This is a thin, faithful lift
// of the REAL app::MenuMainDecide:
//   MenuMainNext::kQuit            -> kQuit
//   MenuMainNext::kRestartDisplay  -> kMainMenu (re-init display, then re-enter menu)
//   MenuMainNext::kRestartMenu     -> kMainMenu  (or kOptions if an option screen ran)
//   MenuMainNext::kRunSession      -> the session sub-mode (NewGame/LoadGame/Network)
GameMode NextMode(const app::MenuResult& r, bool restartDisplay);

// Whether `m` is a session sub-mode (an armed-session state that leads into InGame).
inline bool IsSessionMode(GameMode m) {
    return m == GameMode::kNewGame || m == GameMode::kLoadGame ||
           m == GameMode::kNetwork;
}

// ---------------------------------------------------------------------------
// The driver. Wraps the real menu state machine into a steppable game-mode FSM so a
// scripted (or live) input sequence can be run frame-by-frame and asserted against
// the reconstructed dispatch.
// ---------------------------------------------------------------------------
//
// Lifecycle, one full play cycle:
//   start()                  — enter kMainMenu.
//   stepMenu(plat, clicks)   — run ONE MenuRunMainMenu pass (real dispatch + real
//                              decision), advance the mode, return the MenuResult.
//                              From kMainMenu this lands in a session sub-mode,
//                              kOptions, kMainMenu (loop back) or kQuit.
//   enterInGame()            — a session sub-mode -> kInGame (the session is live).
//   endSession()            — kInGame -> kMainMenu (the session returned to the menu).
//
// `transitions()` is the ordered history of modes entered (for golden-testing the
// FSM path); `sessionFlags()` is the last armed word_63C740; `restartCount()` counts
// the menu loop-backs (restart-menu / option-screen returns).
class ModeFsm {
public:
    void start();

    // Run one menu pass and transition. Returns the reconstructed MenuResult so the
    // caller can assert the real session flags / quit / item. Only valid in
    // kMainMenu; in any other mode it is a no-op and returns a default MenuResult.
    app::MenuResult stepMenu(shim::IPlatform& plat, app::MenuClickSource& clicks,
                             int maxFrames, bool restartDisplay = false);

    // A session sub-mode (NewGame/LoadGame/Network) -> InGame. The actual world
    // bring-up (InitOrLoadSession) is the caller's job; this records the mode edge.
    // No-op unless currently in a session sub-mode.
    void enterInGame();

    // InGame -> back to MainMenu (the session returned to the menu). No-op unless
    // currently InGame.
    void endSession();

    GameMode mode() const { return mode_; }
    bool done() const { return mode_ == GameMode::kQuit; }
    int  sessionFlags() const { return sessionFlags_; }
    int  restartCount() const { return restartCount_; }
    int  menuRuns() const { return menuRuns_; }

    // The ordered list of modes the FSM has entered (start() seeds it with kMainMenu).
    const std::vector<GameMode>& transitions() const { return transitions_; }

private:
    void enter(GameMode m);

    GameMode mode_ = GameMode::kMainMenu;
    int sessionFlags_ = 0;
    int restartCount_ = 0;
    int menuRuns_ = 0;
    std::vector<GameMode> transitions_;
};

} // namespace guild::play
