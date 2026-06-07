// guild::play — the OUTER INTERACTIVE LOOP. See interactive.h.
//
// Reconstructs the outermost while(1) of VIBE_GameLogic_MainEntryAndShutdown @0x534bbc
// (run the menu; if a session armed, run it; loop back unless quit) on top of the clean
// ModeFsm + the shim OS boundary. The menu dispatch/decision are REUSED via
// ModeFsm::stepMenu (which calls the real app::MenuRunMainMenu / app::MenuMainDecide);
// the session bring-up is delegated to the injected SessionRunner so this stays free of
// the world-bootstrap dependency and runs headless.
#include "play/interactive.h"

namespace guild::play {

// ===========================================================================
// PlatformClickSource — adapt a shim::IPlatform into a menu click source.
// ===========================================================================
PlatformClickSource::PlatformClickSource(shim::IPlatform& plat,
                                         std::function<int(int x, int y)> hitTest)
    : plat_(plat), hitTest_(std::move(hitTest)) {}

int PlatformClickSource::hoverThisFrame(int /*frame*/) {
    shim::MouseState m;
    plat_.getMouse(m);
    // Resolve the hovered widget id from the cursor via the injected hit-test; with no
    // hit-test wired, nothing is ever hovered (-1), matching the menu's "no widget".
    return hitTest_ ? hitTest_(m.x, m.y) : -1;
}

bool PlatformClickSource::clickThisFrame(int /*frame*/) {
    // A click is the DOWN edge of the left button (false -> true) since the last query,
    // mirroring the original's latched click edge (dword_672228) which fires once per
    // press rather than for every held frame.
    shim::MouseState m;
    plat_.getMouse(m);
    const bool edge = m.left && !prevLeft_;
    prevLeft_ = m.left;
    return edge;
}

bool PlatformClickSource::escThisFrame(int /*frame*/) {
    // byte_67225C == 1 -> the ESC quit gate; the keyboard leaf is the shim keyDown.
    return plat_.keyDown(kVkEscape);
}

// ===========================================================================
// RunInteractive — the program spine.
// ===========================================================================
InteractiveResult RunInteractive(shim::IPlatform& plat, app::MenuClickSource& clicks,
                                 SessionRunner runSession, int menuMaxFrames,
                                 int sessionMaxFrames) {
    InteractiveResult out;

    ModeFsm fsm;
    fsm.start();

    // The outer while(1): keep running until the FSM chooses Quit, or the OS window is
    // closed (pumpMessages() == false). Each iteration drives exactly one menu pass and,
    // if it armed a session, one session run.
    while (!fsm.done()) {
        // Window-close gate: a closed window ends the program mid-loop (the original's
        // PumpMessages-returns-quit break), an UNCLEAN exit (the player did not choose
        // Quit). Probe one pump before doing menu/session work this iteration.
        if (!plat.pumpMessages())
            break;

        if (fsm.mode() == GameMode::kMainMenu) {
            // One full pass of the REAL main-menu dispatch + outer-loop decision. This
            // lands in a session sub-mode, kOptions, kMainMenu (loop back) or kQuit.
            fsm.stepMenu(plat, clicks, menuMaxFrames);

            // If the menu armed a session, bring it up and run it, then return to menu.
            if (IsSessionMode(fsm.mode())) {
                const int flags = fsm.sessionFlags();
                fsm.enterInGame();                 // session sub-mode -> kInGame
                ++out.sessions;
                const int frames = runSession(flags, plat, sessionMaxFrames);
                out.totalSessionFrames += frames;
                fsm.endSession();                  // kInGame -> kMainMenu
            }
        } else {
            // Defensive: the FSM should only ever rest in kMainMenu/kQuit between
            // iterations (sessions are entered+exited within a single iteration above).
            // If somehow parked in a transient mode, return it to the menu.
            if (IsSessionMode(fsm.mode())) {
                fsm.enterInGame();
                fsm.endSession();
            }
        }
    }

    out.menuRuns = fsm.menuRuns();
    out.transitions = fsm.transitions();
    out.quitClean = fsm.done();
    return out;
}

} // namespace guild::play
