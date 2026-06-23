// tests/integration/full_session_itest.cpp — the M5 FULL SESSION on a small
// real-FORMAT world (synthetic entity records in the live sim arrays driven through
// the REAL render pipeline; no shipped assets). Drives the REAL spine end to end:
//   boot the REAL menu (ModeFsm + MenuRunMainMenu dispatch) -> New Game arms a
//   session -> InGame -> the REAL WorldRenderer renders interactive frames over the
//   live g_objects -> a REAL P5 conquer order mutates the world -> a REAL game-day ->
//   more real frames (the visible frame CHANGES) -> a REAL ESC quit exits cleanly.
//
// Asserts the menu->play transition, that the real render produced non-clear frames,
// that the action changed the world hash AND the visible frame, a clean quit, and a
// deterministic byte-identical trace on rerun. Every step is a REAL play sibling.
#include "test.h"

#include "play/full_session.h"
#include "play/mode_fsm.h"
#include "gui/main_menu.h"

#include <cstdint>
#include <cstdio>

using namespace guild;
using namespace guild::play;

namespace {

SessionScript ItestScript() {
    SessionScript s;
    s.menuButton = 0;               // New Game
    s.framesBeforeAction = 4;
    s.framesAfterAction  = 3;
    s.actionMode = CursorMode::kConquer;   // enqueues + applies (folded mutation)
    s.fbW = 96; s.fbH = 72;
    s.econSeed = 0xCAFE;
    return s;
}

} // namespace

// ---------------------------------------------------------------------------
// Full session over a small real-format world: menu->play, real frames, action
// changes the world + the visible frame, clean quit.
// ---------------------------------------------------------------------------
TEST(FullSessionItest, MenuToPlayRealFramesActionChangesFrameCleanQuit) {
    // 10 alive objects (> the renderer's 8-object cap, so despawning one still leaves
    // the frame full but with a DIFFERENT set -> the frame changes).
    SessionTrace t = RunFullSessionSynthetic(/*worldSeed=*/0xA11CE, /*persons=*/2,
                                             /*objects=*/10, ItestScript());

    std::printf("[fs-itest] booted=%d menuToPlay=%d inGame=%d cleanQuit=%d | "
                "frames=%d (pre=%d post=%d) | beforeNonClear=%d afterNonClear=%d | "
                "beforeObj=%d afterObj=%d\n",
                (int)t.bootedMenu, (int)t.menuToPlay, (int)t.enteredInGame,
                (int)t.cleanQuit, t.framesRendered, t.preActionFrames,
                t.postActionFrames, t.frameBeforeNonClear, t.frameAfterNonClear,
                t.frameBeforeObjects, t.frameAfterObjects);
    std::printf("[fs-itest] hashBefore=%llu hashAfterAction=%llu hashAfterDay=%llu "
                "kind=%d target=%d econPasses=%d\n",
                (unsigned long long)t.hashBeforeAction,
                (unsigned long long)t.hashAfterAction,
                (unsigned long long)t.hashAfterDay,
                t.actionKind, t.actionTarget, t.economyPasses);

    // -- the menu->play spine transition --
    CHECK(t.bootedMenu);
    CHECK(t.menuToPlay);
    CHECK(t.sessionMode == GameMode::kNewGame);
    CHECK(t.enteredInGame);

    // -- the REAL render produced frames with non-clear pixels --
    CHECK_EQ(t.preActionFrames, 4);
    CHECK_EQ(t.postActionFrames, 3);
    CHECK(t.frameBeforeNonClear > 0);
    CHECK(t.frameAfterNonClear > 0);
    CHECK(t.frameBeforeObjects > 0);
    CHECK(t.frameAfterObjects > 0);

    // -- the action changed the world AND the visible frame --
    CHECK(t.actionIssued);
    CHECK(t.actionChangedWorld());
    // The city visibly changed: compare frame CONTENT (FNV over the backbuffer).
    // Since the wave-3 level-shaded untextured fill, the nonClear COUNT can be
    // identical across a same-coverage layout change (the despawned object's
    // quad moves another object into its 8-slot place), so the count alone is
    // no longer a sufficient witness.
    CHECK(t.frameBeforeHash != 0 && t.frameAfterHash != 0);
    CHECK(t.frameBeforeHash != t.frameAfterHash);          // the city visibly changed
    CHECK(t.economyPasses > 0);

    // -- clean quit (the REAL MenuMainDecide kQuit -> ModeFsm done) --
    CHECK(t.cleanQuit);

    // -- the central M5 invariant --
    CHECK(t.ok());
}

// ---------------------------------------------------------------------------
// Determinism: a full rerun reproduces an identical trace (hashes + frame stats +
// FSM path).
// ---------------------------------------------------------------------------
TEST(FullSessionItest, FullRunIsDeterministic) {
    SessionScript s = ItestScript();
    SessionTrace a = RunFullSessionSynthetic(0x2024, 2, 10, s);
    SessionTrace b = RunFullSessionSynthetic(0x2024, 2, 10, s);

    CHECK_EQ(a.hashBeforeAction, b.hashBeforeAction);
    CHECK_EQ(a.hashAfterAction,  b.hashAfterAction);
    CHECK_EQ(a.hashAfterDay,     b.hashAfterDay);
    CHECK_EQ(a.frameBeforeNonClear, b.frameBeforeNonClear);
    CHECK_EQ(a.frameAfterNonClear,  b.frameAfterNonClear);
    CHECK_EQ(a.frameBeforeObjects,  b.frameBeforeObjects);
    CHECK_EQ(a.frameAfterObjects,   b.frameAfterObjects);
    CHECK_EQ(a.actionTarget,     b.actionTarget);
    CHECK_EQ((int)a.cleanQuit,   (int)b.cleanQuit);
    bool pathSame = (a.fsmPath == b.fsmPath);
    CHECK(pathSame);

    std::printf("[fs-itest] deterministic rerun: hashAfterDay %llu == %llu\n",
                (unsigned long long)a.hashAfterDay,
                (unsigned long long)b.hashAfterDay);
}
