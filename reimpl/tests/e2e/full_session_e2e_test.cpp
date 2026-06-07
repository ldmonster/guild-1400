// tests/e2e/full_session_e2e_test.cpp — GUARDED real-asset M5 FULL SESSION on
// AUGSBURG. The end-to-end proof the game is PLAYABLE, driving the REAL spine:
//
//   boot the REAL menu (ModeFsm + MenuRunMainMenu dispatch) -> New Game arms a
//   session -> InGame -> LOAD real AUGSBURG into the live world -> ~5 interactive
//   frames rendering the REAL city (real WorldRenderer over the live g_objects, BMPs
//   dumped) -> a REAL P5 conquer order mutates a live object -> a REAL game-day ->
//   more real frames (the frame VISIBLY changes) -> a REAL ESC quit exits cleanly.
//
// Asserts it boots the real menu, transitions to play, loads the real city, renders
// real (non-clear) frames, the action changes the world AND the visible frame, and
// quits cleanly — byte-identical trace on rerun. Skips when AUGSBURG.cty is absent
// (honors GUILD_GAME_DIR).
#include "test.h"

#include "play/full_session.h"
#include "play/mode_fsm.h"
#include "gui/main_menu.h"
#include "io/vfs.h"
#include "shim_impl/disk_filesystem.h"
#include "shim_impl/filedump_graphics.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

using namespace guild;
using namespace guild::play;

namespace {

std::string GameDir() {
    if (const char* env = std::getenv("GUILD_GAME_DIR"))
        return env;
    return "/home/cnupt/work/reverse/reverse-guild/reimpl/europe_guild_1400_original";
}

bool RealAssetsPresent() {
    shim::DiskFileSystem fs(GameDir());
    return fs.exists("Gilde.INI") &&
           fs.exists("Resources/gamedata/Cities/AUGSBURG.cty");
}

SessionScript AugsburgScript() {
    SessionScript s;
    s.menuButton = 0;                 // New Game
    s.menuMaxFrames = 4;
    s.framesBeforeAction = 5;         // ~5 real-rendered frames before the action
    s.framesAfterAction  = 3;
    s.actionMode = CursorMode::kConquer;   // WARE conquer (kind 6): enqueues + applies
    s.fbW = 128; s.fbH = 96;
    s.econSeed = 0xA065B;
    return s;
}

// Run the full session once, dumping the last pre/post frames with `prefix`.
SessionTrace RunOnce(const char* prefix, std::vector<std::uint8_t>& lastFrameBytes) {
    SessionScript s = AugsburgScript();
    shim::DiskFileSystem fs(GameDir());
    shim::FileDumpGraphicsDevice dev;
    dev.init(s.fbW, s.fbH, 16, false);
    dev.configureDump("/tmp", prefix, shim::FileDumpGraphicsDevice::kBmp);

    SessionTrace t = RunFullSession(&fs, GameDir(), "Augsburg", s, &dev);
    lastFrameBytes = dev.lastPresented();
    return t;
}

} // namespace

// ---------------------------------------------------------------------------
// The full M5 session on AUGSBURG: boots, renders the real city, the action
// changes the world + visible frame, quits cleanly, deterministic on rerun.
// ---------------------------------------------------------------------------
TEST(FullSessionE2E, AugsburgFullScriptedSessionPlayableAndDeterministic) {
    if (!RealAssetsPresent()) {
        std::printf("  [skip] FullSessionE2E: real game dir absent (%s)\n",
                    GameDir().c_str());
        CHECK(true);
        return;
    }
    std::printf("[fs-e2e] asset dir: %s\n", GameDir().c_str());

    std::vector<std::uint8_t> lastA;
    SessionTrace t = RunOnce("guild_fs_e2e", lastA);

    std::printf("[fs-e2e] booted=%d menuToPlay=%d mode=%d inGame=%d loaded=%d "
                "persons=%u objects=%u cleanQuit=%d\n",
                (int)t.bootedMenu, (int)t.menuToPlay, (int)t.sessionMode,
                (int)t.enteredInGame, (int)t.loaded, t.personCount, t.objectCount,
                (int)t.cleanQuit);
    std::printf("[fs-e2e] frames=%d (pre=%d post=%d) | beforeNonClear=%d "
                "afterNonClear=%d | beforeObj=%d afterObj=%d\n",
                t.framesRendered, t.preActionFrames, t.postActionFrames,
                t.frameBeforeNonClear, t.frameAfterNonClear,
                t.frameBeforeObjects, t.frameAfterObjects);
    std::printf("[fs-e2e] action issued=%d enqueued=%d kind=%d target=%d econPasses=%d\n",
                (int)t.actionIssued, (int)t.actionEnqueued, t.actionKind,
                t.actionTarget, t.economyPasses);
    std::printf("[fs-e2e] hashBefore=%llu hashAfterAction=%llu hashAfterDay=%llu\n",
                (unsigned long long)t.hashBeforeAction,
                (unsigned long long)t.hashAfterAction,
                (unsigned long long)t.hashAfterDay);
    std::printf("[fs-e2e] last pre-action BMP -> %s\n", t.frameBeforePath.c_str());
    std::printf("[fs-e2e] last post-action BMP -> %s\n", t.frameAfterPath.c_str());

    // -- it boots the real menu + transitions to play --
    CHECK(t.bootedMenu);
    CHECK(t.menuToPlay);
    CHECK(t.sessionMode == GameMode::kNewGame);
    CHECK(t.enteredInGame);

    // -- it loaded the real city (shipped AUGSBURG seed) --
    CHECK(t.loaded);
    if (!t.loaded) { io::VfsShutdown(); return; }
    CHECK_EQ(t.personCount, (u32)1);
    CHECK_EQ(t.objectCount, (u32)55);

    // -- it rendered the REAL city (non-clear pixels) --
    CHECK_EQ(t.preActionFrames, 5);
    CHECK_EQ(t.postActionFrames, 3);
    CHECK(t.frameBeforeNonClear > 0);
    CHECK(t.frameAfterNonClear > 0);
    CHECK(t.frameBeforeObjects > 0);
    CHECK(t.frameAfterObjects > 0);

    // -- the REAL P5 action changed the world AND the visible frame --
    CHECK(t.actionIssued);
    CHECK(t.actionChangedWorld());
    CHECK(t.frameBeforeNonClear != t.frameAfterNonClear);  // the conquered building gone
    CHECK(t.economyPasses > 0);

    // -- it quit cleanly --
    CHECK(t.cleanQuit);

    // -- the central M5 invariant --
    CHECK(t.ok());

    // Determinism: a full rerun reproduces an identical trace + identical last frame.
    std::vector<std::uint8_t> lastB;
    SessionTrace t2 = RunOnce("guild_fs_e2e2", lastB);
    std::printf("[fs-e2e] rerun hashAfterDay=%llu (was %llu) lastFrame %zu==%zu bytes\n",
                (unsigned long long)t2.hashAfterDay,
                (unsigned long long)t.hashAfterDay, lastA.size(), lastB.size());
    CHECK_EQ(t.hashBeforeAction, t2.hashBeforeAction);
    CHECK_EQ(t.hashAfterAction,  t2.hashAfterAction);
    CHECK_EQ(t.hashAfterDay,     t2.hashAfterDay);
    CHECK_EQ(t.frameBeforeNonClear, t2.frameBeforeNonClear);
    CHECK_EQ(t.frameAfterNonClear,  t2.frameAfterNonClear);
    bool pathSame = (t.fsmPath == t2.fsmPath);
    CHECK(pathSame);
    CHECK(lastA == lastB);   // byte-identical final frame across reruns

    io::VfsShutdown();
}
