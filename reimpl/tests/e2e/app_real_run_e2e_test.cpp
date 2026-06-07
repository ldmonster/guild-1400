// tests/e2e/app_real_run_e2e_test.cpp — GUARDED real-asset full-lifecycle e2e.
//
// Runs the ENTIRE app spine lifecycle (CreateMainWindow -> InitSubsystems ->
// InitDisplayAndPaths -> InitEngineAndScriptCommands -> N session frames ->
// verbatim 13-step shutdown) against a REAL "Die Gilde — Europe 1400" install,
// with the file-layer hooks consuming the REAL loaders over REAL bytes (no
// synthetic in-memory stand-ins). Asserts that, all through the spine:
//
//   * the real Gilde.INI was read (Stadt=Augsburg),
//   * the Resources/*.BIN archives mounted (thousands of members),
//   * gfx/gilde.gfx loaded (1806 objects),
//   * data/A_Geb.dat (72) + A_Obj.dat (731) loaded,
//   * the configured AUGSBURG.cty city seed loaded,
//   * N frames ran and the lifecycle exited 0 (clean shutdown).
//
// GUARDED: skip cleanly (zero checks) if the real game dir is absent. Override
// with GUILD_GAME_DIR.
#include "tests/framework/test.h"

#include "app/wiring.h"

#include "shim_impl/disk_filesystem.h"

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
    return fs.exists("Gilde.INI") && fs.exists("gfx/gilde.gfx") &&
           fs.exists("Resources/forms.BIN") &&
           fs.exists("Resources/gamedata/Cities/AUGSBURG.cty");
}

} // namespace

TEST(AppRealRun, FullLifecycleExit0) {
    if (!RealAssetsPresent()) {
        std::printf("  [skip] AppRealRun.FullLifecycleExit0: real game dir absent "
                    "(%s)\n", GameDir().c_str());
        return; // clean skip
    }

    const int kFrames = 5;
    app::RealHeadlessResult r =
        app::RunHeadlessRealAssets(GameDir(), kFrames, /*displayMode=*/1);

    // Assets resolved + real INI parsed through the spine's mount.
    CHECK(r.assetsPresent);
    CHECK(r.iniLoaded);
    CHECK(r.stadt == "Augsburg");

    // Archives mounted with thousands of indexed members.
    CHECK(r.archivesMounted == app::DefaultResourceArchives().size());
    CHECK(r.totalMembers > 1000);

    // gilde.gfx -> 1806 real objects (the load-bearing gfx count).
    CHECK(r.gfxObjectCount == 1806);

    // A_Geb.dat / A_Obj.dat record counts.
    CHECK(r.buildingTypeCount == 72);
    CHECK(r.sceneTypeCount == 731);

    // The configured city seed loaded through the bound VFS (gunzip + loader).
    CHECK(r.cityLoaded);
    CHECK(r.cityName == "Augsburg");

    // The lifecycle ran N frames and exited cleanly through the spine.
    CHECK(r.base.frameCount == kFrames);
    CHECK(r.base.presentCount > 0);
    // memoryTracker/vfs are torn down by the 13-step shutdown, so the post-run
    // snapshot reports them released (false) — a clean shutdown, not a leak.
    CHECK(!r.base.memoryTrackerInited);
    CHECK(!r.base.vfsInited);
    CHECK(r.base.exitCode == 0);         // clean shutdown
}
