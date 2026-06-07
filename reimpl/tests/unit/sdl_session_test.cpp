#include "test.h"

// UNIT tier for play::RunSdlSession — the native interactive city session loop,
// driven BACKEND-AGNOSTIC through shim::MemoryGraphicsDevice + shim::ScriptedPlatform
// (no Vulkan/SDL needed). Asserts the loop's control flow:
//   * frames present (device.present per frame, bounded by cfg.maxFrames),
//   * a scripted LEFT-CLICK is handled (one edge),
//   * SPACE advances a day,
//   * ESC and maxFrames each terminate the loop,
//   * the trace is deterministic across reruns.
//
// RunSdlSession loads the real city (MountRealGameAssets/io::LoadWorld), so the
// asset-driven assertions are GUARDED on the real game dir (clean skip if absent).
// The control-flow contract that needs NO assets (an unmounted FS yields the
// empty early-return trace) is asserted unconditionally.
#include "play/sdl_session.h"
#include "shim_impl/disk_filesystem.h"
#include "shim_impl/mem_filesystem.h"
#include "shim_impl/memory_graphics.h"
#include "shim_impl/scripted_platform.h"
#include "sim/entity.h"
#include "io/vfs.h"

#include <cstdio>
#include <cstdlib>
#include <string>

using namespace guild;

namespace {

std::string GameDir() {
    if (const char* env = std::getenv("GUILD_GAME_DIR"))
        return env;
    return "/home/cnupt/work/reverse/reverse-guild/reimpl/europe_guild_1400_original";
}

bool RealAssetsPresent() {
    shim::DiskFileSystem fs(GameDir());
    return fs.exists("Gilde.INI") &&
           fs.exists("Resources/gamedata/Cities/AUGSBURG.cty") &&
           fs.exists("Resources/Objects.BIN");
}

play::SdlSessionConfig BaseCfg() {
    play::SdlSessionConfig cfg;
    cfg.gameDir   = GameDir();
    cfg.cityPath  = "Resources/gamedata/Cities/AUGSBURG.cty";
    cfg.fbW = 160; cfg.fbH = 120;     // small framebuffer for a fast unit render
    cfg.textured = false;
    cfg.frameCapMs = 0;               // uncapped so the test is fast
    cfg.seed = 0x4711;
    return cfg;
}

} // namespace

// No assets: the session early-returns an EMPTY trace (not mounted/loaded). This
// part needs no game dir, so it is asserted unconditionally.
TEST(SdlSessionUnit, UnmountedFsEarlyReturn) {
    shim::MemFileSystem fs;          // no Gilde.INI -> MountRealGameAssets fails
    shim::MemoryGraphicsDevice dev;
    dev.init(160, 120, 16, false);
    shim::ScriptedPlatform plat;

    play::SdlSessionConfig cfg = BaseCfg();
    cfg.gameDir = "/nonexistent-guild-dir";
    cfg.maxFrames = 4;
    play::SdlSessionTrace tr = play::RunSdlSession(fs, dev, plat, cfg);

    // With no real city the load fails -> the loop never runs: no frames, no days.
    CHECK(!tr.loaded);
    CHECK_EQ(tr.framesPresented, 0);
    CHECK_EQ(tr.daysAdvanced, 0);
    CHECK(!tr.cleanQuit);
    io::VfsShutdown();
}

// maxFrames bounds the loop: exactly N frames present, no input -> no mutation.
TEST(SdlSessionUnit, MaxFramesTerminates) {
    if (!RealAssetsPresent()) {
        std::printf("  [skip] SdlSessionUnit.MaxFramesTerminates: real game dir absent\n");
        CHECK(true);
        return;
    }
    shim::DiskFileSystem fs(GameDir());
    shim::MemoryGraphicsDevice dev;
    CHECK(dev.init(160, 120, 16, false));
    shim::ScriptedPlatform plat;      // no quit, no input

    play::SdlSessionConfig cfg = BaseCfg();
    cfg.maxFrames = 5;
    play::SdlSessionTrace tr = play::RunSdlSession(fs, dev, plat, cfg);

    CHECK(tr.mounted);
    CHECK(tr.loaded);
    CHECK(tr.liveObjects > 0);
    CHECK_EQ(tr.framesPresented, 5);  // present() per frame, bounded by maxFrames
    CHECK(!tr.cleanQuit);             // exited via maxFrames, not quit
    CHECK_EQ(tr.clicksHandled, 0);
    CHECK_EQ(tr.daysAdvanced, 0);
    CHECK_EQ(tr.hashStart, tr.hashEnd); // no input -> world unchanged
    sim::ResetEntityArrays();
    io::VfsShutdown();
}

// ESC terminates with quitByEsc + cleanQuit.
TEST(SdlSessionUnit, EscQuits) {
    if (!RealAssetsPresent()) {
        std::printf("  [skip] SdlSessionUnit.EscQuits: real game dir absent\n");
        CHECK(true);
        return;
    }
    shim::DiskFileSystem fs(GameDir());
    shim::MemoryGraphicsDevice dev;
    CHECK(dev.init(160, 120, 16, false));
    shim::ScriptedPlatform plat;
    plat.pressKey(0x1B);              // VK_ESCAPE held

    play::SdlSessionConfig cfg = BaseCfg();
    cfg.maxFrames = 100;             // ESC must end it well before this
    play::SdlSessionTrace tr = play::RunSdlSession(fs, dev, plat, cfg);

    CHECK(tr.quitByEsc);
    CHECK(tr.cleanQuit);
    CHECK_EQ(tr.framesPresented, 1); // frame 0 presents, then ESC checked -> break
    sim::ResetEntityArrays();
    io::VfsShutdown();
}

// SPACE advances exactly one game-day (one edge), and that mutates the world hash.
TEST(SdlSessionUnit, SpaceAdvancesDay) {
    if (!RealAssetsPresent()) {
        std::printf("  [skip] SdlSessionUnit.SpaceAdvancesDay: real game dir absent\n");
        CHECK(true);
        return;
    }
    shim::DiskFileSystem fs(GameDir());
    shim::MemoryGraphicsDevice dev;
    CHECK(dev.init(160, 120, 16, false));
    shim::ScriptedPlatform plat;
    plat.pressKey(0x20);             // VK_SPACE held (level-state -> one edge)

    play::SdlSessionConfig cfg = BaseCfg();
    cfg.maxFrames = 4;
    cfg.advanceDayOnSpace = true;
    play::SdlSessionTrace tr = play::RunSdlSession(fs, dev, plat, cfg);

    CHECK_EQ(tr.framesPresented, 4);
    CHECK_EQ(tr.daysAdvanced, 1);    // one SPACE edge -> exactly one day
    CHECK(tr.hashStart != tr.hashEnd); // a game-day mutated the world
    sim::ResetEntityArrays();
    io::VfsShutdown();
}

// The same scripted session twice yields an IDENTICAL trace (deterministic).
TEST(SdlSessionUnit, DeterministicTrace) {
    if (!RealAssetsPresent()) {
        std::printf("  [skip] SdlSessionUnit.DeterministicTrace: real game dir absent\n");
        CHECK(true);
        return;
    }
    auto run = []() {
        shim::DiskFileSystem fs(GameDir());
        shim::MemoryGraphicsDevice dev;
        dev.init(160, 120, 16, false);
        shim::ScriptedPlatform plat;
        plat.pressKey(0x20);          // one game-day
        play::SdlSessionConfig cfg = BaseCfg();
        cfg.maxFrames = 3;
        play::SdlSessionTrace tr = play::RunSdlSession(fs, dev, plat, cfg);
        sim::ResetEntityArrays();
        io::VfsShutdown();
        return tr;
    };
    play::SdlSessionTrace a = run();
    play::SdlSessionTrace b = run();
    CHECK_EQ(a.framesPresented, b.framesPresented);
    CHECK_EQ(a.daysAdvanced, b.daysAdvanced);
    CHECK_EQ(a.hashStart, b.hashStart);
    CHECK_EQ(a.hashEnd, b.hashEnd);
}
