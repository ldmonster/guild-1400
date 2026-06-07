// tests/unit/full_session_test.cpp — UNIT coverage of the M5 FULL SESSION
// INTEGRATOR (src/play/full_session.cpp) on a SYNTHETIC live world (no assets, no
// window). Drives the REAL menu spine (play::ModeFsm + app::MenuRunMainMenu dispatch
// + app::MenuMainDecide quit decision) through a scripted New-Game click into a
// session, seeds a small world, runs render frames + a P5 action + a game-day, then
// a scripted ESC quit. Asserts:
//   * the FSM transitions are in order (MainMenu -> NewGame -> InGame -> MainMenu ->
//     Quit), the menu->play edge fired, and quit terminated the loop cleanly,
//   * the action mutated the world (hash before != after),
//   * the whole run is DETERMINISTIC (byte-identical trace on rerun).
#include "test.h"

#include "play/full_session.h"
#include "play/mode_fsm.h"
#include "gui/main_menu.h"

#include <cstdint>
#include <cstdio>

using namespace guild;
using namespace guild::play;

namespace {

SessionScript SynthScript() {
    SessionScript s;
    s.menuButton = 0;              // New Game
    s.framesBeforeAction = 3;
    s.framesAfterAction  = 2;
    s.actionMode = CursorMode::kConquer;   // a folded record mutation
    s.fbW = 64; s.fbH = 48;
    s.econSeed = 0x1234;
    return s;
}

} // namespace

// ---------------------------------------------------------------------------
// The FSM path is exactly the scripted M5 lifecycle, in order.
// ---------------------------------------------------------------------------
TEST(FullSessionUnit, FsmTransitionsInOrderThroughMenuPlayQuit) {
    SessionTrace t = RunFullSessionSynthetic(/*worldSeed=*/0xBEEF, /*persons=*/2,
                                             /*objects=*/6, SynthScript());

    CHECK(t.bootedMenu);
    CHECK(t.menuToPlay);
    CHECK(t.sessionMode == GameMode::kNewGame);
    CHECK_EQ(t.sessionFlags, gui::kSessionNewGame);
    CHECK(t.enteredInGame);
    CHECK(t.cleanQuit);

    // The full ordered mode path: MainMenu -> NewGame -> InGame -> MainMenu -> Quit.
    const std::vector<GameMode>& p = t.fsmPath;
    std::printf("[fs-unit] fsm path size=%zu menuRuns=%d cleanQuit=%d\n",
                p.size(), t.menuRuns, (int)t.cleanQuit);
    CHECK_EQ((int)p.size(), 5);
    if (p.size() == 5) {
        CHECK(p[0] == GameMode::kMainMenu);
        CHECK(p[1] == GameMode::kNewGame);
        CHECK(p[2] == GameMode::kInGame);
        CHECK(p[3] == GameMode::kMainMenu);
        CHECK(p[4] == GameMode::kQuit);
    }
    CHECK_EQ(t.menuRuns, 2);     // the New-Game pass + the ESC-quit pass
}

// ---------------------------------------------------------------------------
// The scripted action mutates the world; frames render; the day ran.
// ---------------------------------------------------------------------------
TEST(FullSessionUnit, ActionMutatesWorldFramesRenderDayRuns) {
    SessionTrace t = RunFullSessionSynthetic(0xBEEF, 2, 6, SynthScript());

    // Real-rendered interactive frames ran (3 before + 2 after).
    CHECK_EQ(t.preActionFrames, 3);
    CHECK_EQ(t.postActionFrames, 2);
    CHECK_EQ(t.framesRendered, 5);

    // The action issued an order onto a live object.
    CHECK(t.actionIssued);
    CHECK(t.actionTarget != 0);

    // The action + day changed the world; the hash before the action is non-zero.
    CHECK(t.hashBeforeAction != 0u);
    CHECK(t.actionChangedWorld());

    // The game-day ran the real economy passes.
    CHECK(t.economyPasses > 0);

    std::printf("[fs-unit] hashBefore=%llu hashAfterAction=%llu hashAfterDay=%llu "
                "kind=%d target=%d econPasses=%d\n",
                (unsigned long long)t.hashBeforeAction,
                (unsigned long long)t.hashAfterAction,
                (unsigned long long)t.hashAfterDay,
                t.actionKind, t.actionTarget, t.economyPasses);
}

// ---------------------------------------------------------------------------
// Quit terminates the loop at the right frame: the FSM is done() after ESC.
// ---------------------------------------------------------------------------
TEST(FullSessionUnit, QuitTerminatesLoopCleanly) {
    SessionTrace t = RunFullSessionSynthetic(0x01, 1, 4, SynthScript());
    CHECK(t.cleanQuit);
    // The terminal mode is Quit (the last transition).
    CHECK(!t.fsmPath.empty());
    if (!t.fsmPath.empty())
        CHECK(t.fsmPath.back() == GameMode::kQuit);
}

// ---------------------------------------------------------------------------
// Determinism: the same (seed, world, script) reproduce a byte-identical trace.
// ---------------------------------------------------------------------------
TEST(FullSessionUnit, DeterministicTraceOnRerun) {
    SessionScript s = SynthScript();
    SessionTrace a = RunFullSessionSynthetic(0xC0FFEE, 3, 7, s);
    SessionTrace b = RunFullSessionSynthetic(0xC0FFEE, 3, 7, s);

    CHECK_EQ(a.hashBeforeAction, b.hashBeforeAction);
    CHECK_EQ(a.hashAfterAction,  b.hashAfterAction);
    CHECK_EQ(a.hashAfterDay,     b.hashAfterDay);
    CHECK_EQ(a.actionKind,       b.actionKind);
    CHECK_EQ(a.actionTarget,     b.actionTarget);
    CHECK_EQ(a.framesRendered,   b.framesRendered);
    CHECK_EQ((int)a.cleanQuit,   (int)b.cleanQuit);
    CHECK_EQ((int)a.fsmPath.size(), (int)b.fsmPath.size());
    bool pathSame = (a.fsmPath == b.fsmPath);
    CHECK(pathSame);
}

// ---------------------------------------------------------------------------
// A Load-Game click arms the load-save session (the other menu->play path).
// ---------------------------------------------------------------------------
TEST(FullSessionUnit, LoadGameButtonArmsLoadSession) {
    SessionScript s = SynthScript();
    s.menuButton = 1;     // Load
    SessionTrace t = RunFullSessionSynthetic(0x55, 1, 5, s);
    CHECK(t.menuToPlay);
    CHECK(t.sessionMode == GameMode::kLoadGame);
    CHECK_EQ(t.sessionFlags, gui::kSessionLoadSave);
    CHECK(t.enteredInGame);
    CHECK(t.cleanQuit);
}
