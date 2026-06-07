// tests/e2e/audio_tick_e2e_test.cpp — boots the full app spine through its init
// lifecycle and drives broad-mask frames, asserting the newly-wired per-frame
// AUDIO TICK ran real across the audio siblings.
//
// The spine's dayCycleAndOutdoorMusic hook now drives app::AudioTick (the audio
// block of VIBE_GameLogic_RunFrameLoop): the real 3D positional pool update, the
// speech queue, the outdoor-music state machine, and the mixer voice recycle,
// plus the market-ambience SFX trigger (Start/StopMarketLoop). The audio tick is
// gated by the kDayCycleMusic feature bit, so the frames run a broad mask that
// enables it (the same way the live interactive frame does).
//
// Two arms:
//   1. SYNTHETIC: the headless backends (always runs).
//   2. REAL-ASSET (GUARDED): if a real "Die Gilde" install is present, boot the
//      spine over the real disk fs + parsed INI and assert the same audio hook
//      ran real on the real-asset path. Skips cleanly when assets are absent.
#include "app/wiring.h"
#include "app/real_boot.h"
#include "config/ini.h"

#include "shim_impl/disk_filesystem.h"
#include "shim_impl/loopback_socket.h"
#include "shim_impl/mem_filesystem.h"
#include "shim_impl/memory_graphics.h"
#include "shim_impl/null_audio.h"
#include "shim_impl/null_platform.h"

#include "tests/framework/test.h"

#include <cstdlib>
#include <string>

using namespace guild;
using guild::app::RealSubsystems;

namespace {

std::string GameDir() {
    if (const char* env = std::getenv("GUILD_GAME_DIR"))
        return env;
    return "/home/cnupt/work/reverse/reverse-guild/reimpl/europe_guild_1400_original";
}

bool RealAssetsPresent(shim::IFileSystem& fs) {
    return fs.exists("Gilde.INI") && fs.exists("gfx/gilde.gfx") &&
           fs.exists("Resources/forms.BIN") &&
           fs.exists("Resources/gamedata/Cities/AUGSBURG.cty");
}

// A broad live-game mask that enables the audio-tick block (kDayCycleMusic) on
// top of the world-render + sim + input steps — the same bits a normal
// interactive single-player frame runs under.
constexpr std::uint32_t kBroadAudioMask =
    app::mask::kWidgetMouse | app::mask::kHudMouse | app::mask::kGameObjects |
    app::mask::kRenderWorld | app::mask::kScripts | app::mask::kNetworkCommand |
    app::mask::kDayCycleMusic | app::mask::kHudLabels | app::mask::kInputCommandPoll;

// Drive the init lifecycle + N broad-mask frames + shutdown, asserting the audio
// tick ran real. Shared by both arms.
void DriveAndAssertAudioTick(app::GameApp& appObj, RealSubsystems& sub,
                             const std::string& exeDir) {
    CHECK(appObj.CreateMainWindow(1));
    CHECK(appObj.InitSubsystemsAndMovieDll());
    CHECK(appObj.InitDisplayAndPaths(1));
    CHECK(appObj.InitEngineAndScriptCommands());

    // The sound lib came up during InitEngineAndScriptCommands -> the audio-enable
    // gates and the seeded market sample are in place.
    CHECK(sub.soundInited());
    CHECK(sub.firedReal("soundLibInit"));
    CHECK(sub.firedReal("soundLoadSampleBank"));  // newly REAL: seeds the market sample
    CHECK(sub.firedReal("soundInitMusicThread"));

    (void)exeDir;
    for (int i = 0; i < 8; ++i)
        appObj.RunFrameLoop(kBroadAudioMask);

    CHECK_EQ(appObj.lastFeatureMask(), kBroadAudioMask);

    // The newly-wired audio tick reached real code each frame.
    CHECK(sub.firedReal("dayCycleAndOutdoorMusic"));
    CHECK(sub.audioTicks() > 0);
    CHECK(sub.audioSound3dRan());   // VIBE_Sound3d_UpdateAll ran (real 3D pool)
    CHECK(sub.audioVoicesRan());    // VIBE_Sound_UpdateVoices recycle ran (real pool)

    // The market-ambience SFX trigger fired real (a looping 3D entry was created).
    CHECK(sub.marketLoopStarted());
    CHECK(sub.marketLoopActive());

    // Shutdown: the 13-step teardown stops the market loop (detach before the pool
    // teardown in tdGameShutdownSubsystems).
    appObj.Shutdown();
    CHECK(!sub.marketLoopActive());
    CHECK(sub.firedReal("tdGameShutdownSubsystems"));
}

} // namespace

// SYNTHETIC: always runs.
TEST(AudioTickE2E, SpineDrivesAudioTickReal) {
    shim::NullPlatform plat;
    shim::MemoryGraphicsDevice gfx;
    shim::NullAudioDevice audioDev;
    shim::MemFileSystem fs;
    auto pair = shim::LoopbackSocket::makePair();
    config::IniFile ini;

    RealSubsystems sub(&plat, &gfx, &audioDev, &fs, pair.first.get(), &ini);
    app::GameApp appObj(plat, gfx, audioDev, sub);

    DriveAndAssertAudioTick(appObj, sub, "\\project\\");
}

// REAL-ASSET (GUARDED): boot over a real install; clean-skip if absent.
TEST(AudioTickE2E, RealAssetSpineDrivesAudioTickReal) {
    const std::string dir = GameDir();
    shim::DiskFileSystem fs(dir);
    if (!RealAssetsPresent(fs)) {
        std::printf("  [skip] AudioTickE2E.RealAssetSpineDrivesAudioTickReal: "
                    "real game dir absent (%s)\n", dir.c_str());
        return; // clean skip — no checks recorded
    }

    app::RealGameAssets assets =
        app::MountRealGameAssets(&fs, dir, "Gilde.INI", {}, /*caseInsensitive=*/false);
    CHECK(assets.iniLoaded);

    shim::NullPlatform plat;
    shim::MemoryGraphicsDevice gfx;
    shim::NullAudioDevice audioDev;
    auto pair = shim::LoopbackSocket::makePair();

    RealSubsystems sub(&plat, &gfx, &audioDev, &fs, pair.first.get(), &assets.ini);
    sub.BindRealAssets(&assets, dir);
    app::GameApp appObj(plat, gfx, audioDev, sub);

    DriveAndAssertAudioTick(appObj, sub, dir);
}
