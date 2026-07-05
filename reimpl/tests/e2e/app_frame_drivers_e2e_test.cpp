// End-to-end: the app per-frame outer-loop drivers driving the REAL spine, plus
// the SIXTH wave of newly-wired hooks (cameraCombatScroll, soundWaveInitSineTables)
// exercised through the full lifecycle.
//
// Part (a) — synthetic headless: build GameApp on RealSubsystems + the headless
// shims, run the init lifecycle, then drive frames through the translated outer-
// loop drivers (RunFrameLoopWrapper / RunPauseLoop / RunEndRoundScreen) and assert
// the spine ran the right masks; drive a combat-mode frame (mask kCombatScroll +
// kGameObjects) and assert cameraCombatScroll reached the real ResolveCombatScroll
// core; assert soundWaveInitSineTables built the real 48-entry sine table (0x30 @0x52879f).
//
// Part (b) — GUARDED real-asset: boot the spine on a real "Die Gilde — Europe
// 1400" install (RunHeadlessRealAssets) and assert the newly-wired
// soundWaveInitSineTables hook ran the REAL path over the real boot (it fires in
// InitEngineAndScriptCommands on every boot). Skips cleanly (zero checks) if
// europe_guild_1400_original/Resources/forms.BIN is absent. Override with
// GUILD_GAME_DIR.
#include "app/frame_drivers.h"
#include "app/wiring.h"
#include "config/ini.h"
#include "shim_impl/disk_filesystem.h"
#include "shim_impl/loopback_socket.h"
#include "shim_impl/mem_filesystem.h"
#include "shim_impl/memory_graphics.h"
#include "shim_impl/null_audio.h"
#include "shim_impl/null_platform.h"
#include "test.h"

#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>

using namespace guild;
using guild::app::RealSubsystems;

namespace {

std::string GameDir() {
    if (const char* env = std::getenv("GUILD_GAME_DIR"))
        return env;
    return "/home/cnupt/work/reverse/reverse-guild/reimpl/europe_guild_1400_original";
}

bool RealAssetsPresent() {
    shim::DiskFileSystem fs(GameDir());
    return fs.exists("Gilde.INI") && fs.exists("gfx/gilde.gfx") &&
           fs.exists("Resources/forms.BIN") &&
           fs.exists("Resources/gamedata/Cities/AUGSBURG.cty");
}

} // namespace

// ---------------------------------------------------------------------------
// Part (a): synthetic headless — drivers + the two newly-wired hooks.
// ---------------------------------------------------------------------------
TEST(AppFrameDriversE2E, DriversAndSixthWaveHooksRealPath) {
    shim::NullPlatform plat;
    shim::MemoryGraphicsDevice gfx;
    shim::NullAudioDevice audio;
    shim::MemFileSystem fs;
    auto sockPair = shim::LoopbackSocket::makePair();
    config::IniFile ini;

    RealSubsystems sub(&plat, &gfx, &audio, &fs, sockPair.first.get(), &ini);
    app::GameApp appObj(plat, gfx, audio, sub);

    // ---- init lifecycle (soundWaveInitSineTables fires here) ---------------
    CHECK(appObj.CreateMainWindow(1));
    CHECK(appObj.InitSubsystemsAndMovieDll());
    CHECK(appObj.InitDisplayAndPaths(1));
    CHECK(appObj.InitEngineAndScriptCommands());

    // soundWaveInitSineTables ran the REAL d3sndw builder. The table length is
    // 0x30 = 48 — the boot call VIBE_SoundWave_InitSineTables(0x30u) at gilde.exe
    // 0x52879f. [Pin updated from 256 with that binary evidence.]
    CHECK(sub.firedReal("soundWaveInitSineTables"));
    CHECK_EQ(sub.sineTableCount(), 48);

    // ---- outer-loop drivers (translated VIBE_GameLogic_* loops) ------------
    // The main session loop runs the 0x67FFF feature set every tick.
    int fwFrames = app::RunFrameLoopWrapper(appObj, /*maxFrames=*/4);
    CHECK_EQ(fwFrames, 4);
    CHECK_EQ(appObj.lastFeatureMask(), 0x67FFFu);

    // The pause loop derives its mask from the last published frame mask:
    // (dword_11BC2D0 | 0x100000) & ~0x2000 (gilde.exe 0x56e7e7/0x56e7f2/0x56e805).
    // The wrapper just published 0x67FFF, so paused frames run 0x165FFF.
    // [Pin updated from 0u — the original mask is NOT zero; evidence @0x56e7e0.]
    int probe = 0;
    int pauseFrames = app::RunPauseLoop(
        appObj, [&]() { return ++probe >= 3 ? app::kUnpauseKeyScancode : 0; }, 20);
    CHECK_EQ(pauseFrames, 3);
    CHECK_EQ(appObj.lastFeatureMask(),
             (0x67FFFu | app::mask::kInputSuppress) & ~app::mask::kOptionsAndPanels);

    // The end-round screen runs the full mask + raises advance on a posted action.
    bool advance = false;
    int erFrames = app::RunEndRoundScreen(
        appObj, [&]() { return -1; /* never posts */ }, /*maxFrames=*/3, &advance);
    CHECK_EQ(erFrames, 3);
    CHECK(!advance);

    const int framesAfterDrivers = sub.frameCount();
    CHECK_EQ(framesAfterDrivers, 4 + 3 + 3);

    // ---- a combat-mode frame: exercises cameraCombatScroll via the spine -----
    // kGameObjects gates the HUD/present block where kCombatScroll runs the
    // combat-scroll step. Drive one such frame through RunFrameLoop.
    const std::uint32_t combatMask = app::mask::kGameObjects | app::mask::kCombatScroll;
    appObj.RunFrameLoop(combatMask);
    CHECK(sub.firedReal("cameraCombatScroll"));
    // Headless edge vectors are the settled {0,0,0} block: scrollX=-1 -> right edge
    // SETTLED (code 1); scrollY=1 -> bottom edge SETTLED (code 1).
    CHECK_EQ(sub.combatScrollRightCode(), 1);
    CHECK_EQ(sub.combatScrollBottomCode(), 1);

    // ---- 13-step shutdown (no crash) ---------------------------------------
    appObj.Shutdown();
    CHECK(sub.firedReal("tdGameShutdownSubsystems"));
}

// ---------------------------------------------------------------------------
// Part (b): GUARDED real-asset boot — soundWaveInitSineTables real over real boot.
// ---------------------------------------------------------------------------
TEST(AppFrameDriversE2E, RealAssetBootRunsSineTableInitReal) {
    if (!RealAssetsPresent()) {
        std::printf("  [skip] AppFrameDriversE2E.RealAssetBootRunsSineTableInitReal:"
                    " real game dir absent (%s)\n", GameDir().c_str());
        return; // clean skip-pass
    }

    const int kFrames = 5;
    app::RealHeadlessResult r =
        app::RunHeadlessRealAssets(GameDir(), kFrames, /*displayMode=*/1);

    CHECK(r.assetsPresent);
    CHECK(r.iniLoaded);
    // The full real-asset lifecycle ran through the spine and exited cleanly.
    CHECK(r.base.frameCount == kFrames);
    CHECK(r.base.exitCode == 0);
    // The newly-wired soundWaveInitSineTables hook ran the REAL d3sndw builder over
    // the real boot — the 256-entry sine table was built in InitEngineAndScript-
    // Commands, on the real-asset path.
    CHECK(r.base.sineTableCount == 48); // 0x30 @0x52879f
}
