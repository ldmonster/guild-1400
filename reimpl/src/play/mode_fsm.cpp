// guild::play — the GAME-MODE FSM (PLAYABLE_PLAN P1, M1). See mode_fsm.h.
//
// Drives the reconstructed top-level menu state machine (app::MenuRunMainMenu @0x529d08
// + app::MenuMainDecide @0x534bbc) and lifts its session-flag / quit / restart-display
// outputs into a clean MainMenu -> {NewGame|LoadGame|Options|Quit} -> InGame -> menu
// lifecycle. The menu DECISION logic is NOT reinvented here — every MainMenu exit is
// taken straight from the real MenuMainDecide; this module only classifies the armed
// session (word_63C740) and sequences the modes.
#include "play/mode_fsm.h"

namespace guild::play {

const char* GameModeName(GameMode m) {
    switch (m) {
        case GameMode::kMainMenu: return "MainMenu";
        case GameMode::kNewGame:  return "NewGame";
        case GameMode::kLoadGame: return "LoadGame";
        case GameMode::kNetwork:  return "Network";
        case GameMode::kOptions:  return "Options";
        case GameMode::kInGame:   return "InGame";
        case GameMode::kQuit:     return "Quit";
    }
    return "?";
}

// word_63C740 bit-classify. The original arms at most one session per menu close
// (New Game | 1, Load = 10 -> &2, Network &4); the priority below makes the partial
// mapping total and stable.
GameMode ModeForSessionFlags(int sessionFlags) {
    if (sessionFlags & gui::kSessionNewGame)  return GameMode::kNewGame;
    if (sessionFlags & gui::kSessionLoadSave) return GameMode::kLoadGame;
    if (sessionFlags & gui::kSessionNetwork)  return GameMode::kNetwork;
    return GameMode::kMainMenu; // nothing armed -> loop back to the menu
}

// Lift the REAL MenuMainDecide to the game-mode vocabulary. We delegate the whole
// quit/restart-display/run-session/loop-back priority to the reconstructed decision
// so this stays a faithful re-statement, not a second copy of the logic.
GameMode NextMode(const app::MenuResult& r, bool restartDisplay) {
    switch (app::MenuMainDecide(r, restartDisplay)) {
        case app::MenuMainNext::kQuit:
            return GameMode::kQuit;
        case app::MenuMainNext::kRestartDisplay:
            // Tear down + re-init the display, then re-enter the menu. From the
            // game-mode point of view we are back at the main menu.
            return GameMode::kMainMenu;
        case app::MenuMainNext::kRunSession:
            // A session was armed: classify word_63C740 into its sub-mode.
            return ModeForSessionFlags(r.sessionFlags);
        case app::MenuMainNext::kRestartMenu:
            // No session armed. If the menu closed (an option/credits screen ran and
            // returned) we route through kOptions on the way back; an un-closed loop
            // (idle frames) stays at the menu.
            return r.close ? GameMode::kOptions : GameMode::kMainMenu;
    }
    return GameMode::kMainMenu;
}

void ModeFsm::enter(GameMode m) {
    mode_ = m;
    transitions_.push_back(m);
}

void ModeFsm::start() {
    mode_ = GameMode::kMainMenu;
    sessionFlags_ = 0;
    restartCount_ = 0;
    menuRuns_ = 0;
    transitions_.clear();
    transitions_.push_back(GameMode::kMainMenu);
}

app::MenuResult ModeFsm::stepMenu(shim::IPlatform& plat, app::MenuClickSource& clicks,
                                  int maxFrames, bool restartDisplay) {
    if (mode_ != GameMode::kMainMenu)
        return app::MenuResult{}; // only the menu drives a step

    // Run one full pass of the REAL menu loop: pumps the real shim input each frame
    // and dispatches clicks through the real gui::MainMenu_Dispatch sibling.
    app::MenuResult r = app::MenuRunMainMenu(plat, clicks, maxFrames);
    ++menuRuns_;
    sessionFlags_ = r.sessionFlags;

    // Decide the next mode straight from the reconstructed outer-loop logic.
    GameMode next = NextMode(r, restartDisplay);
    if (next == GameMode::kMainMenu || next == GameMode::kOptions)
        ++restartCount_;
    enter(next);
    return r;
}

void ModeFsm::enterInGame() {
    if (IsSessionMode(mode_))
        enter(GameMode::kInGame);
}

void ModeFsm::endSession() {
    if (mode_ == GameMode::kInGame)
        enter(GameMode::kMainMenu);
}

} // namespace guild::play
