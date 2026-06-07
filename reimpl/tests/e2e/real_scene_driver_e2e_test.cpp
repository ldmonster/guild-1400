// End-to-end validation of the real scene/script DRIVER (src/app/real_scene_driver)
// over the REAL shipped "Die Gilde — Europe 1400" archives. GUARDED: if the asset
// dir is absent every test skips cleanly (zero checks) so the suite stays green.
// Override the dir with GUILD_GAME_DIR.
//
// The full path under test:
//   MountRealGameAssets(fs, gameDir)             — real INI + bound VFS + mounted
//                                                   Resources/*.BIN archives,
//   DriveScriptsFromAssets(assets, fs, Scripts.BIN)
//     -> reuse the mounted ArchiveMount's central-dir index,
//     -> for every .esc member: read bytes, REGISTER the host command set, parse
//        through sim::LoadScriptFromSource (real strip/compile),
//     -> STEP each parsed script through sim::StepAllActive (the 128-slot context
//        table stepper) over a deterministic ScriptHost.
//
// Asserts the real archive resolves, the real .esc members parse, and the VM
// advances without error; reports the real counts observed.
#include "test.h"

#include "app/real_scene_driver.h"
#include "app/real_boot.h"
#include "io/vfs.h"
#include "shim_impl/disk_filesystem.h"

#include <cstdio>
#include <cstdlib>
#include <string>

using namespace guild;

namespace {
std::string GameDir() {
    if (const char* env = std::getenv("GUILD_GAME_DIR")) return env;
    return "/home/cnupt/work/reverse/reverse-guild/reimpl/europe_guild_1400_original";
}
bool ScriptsPresent() {
    shim::DiskFileSystem fs(GameDir());
    return fs.exists("Gilde.INI") && fs.exists("Resources/Scripts.BIN");
}
} // namespace

// Full run: mount the real assets, drive every .esc through the real loaders +
// stepper, report the counts.
TEST(RealSceneDriverE2E, DriveAllRealScriptsThroughMountedAssets) {
    if (!ScriptsPresent()) {
        std::printf("  [skip] RealSceneDriverE2E.DriveAllRealScriptsThroughMountedAssets: "
                    "real game dir absent (%s)\n", GameDir().c_str());
        return;
    }
    app::SetRealSceneDriverHooks(nullptr);   // inert command bodies (return 1)

    shim::DiskFileSystem fs(GameDir());
    app::RealGameAssets assets =
        app::MountRealGameAssets(&fs, GameDir());
    CHECK(assets.iniLoaded);
    CHECK(assets.vfsBound);

    app::RealSceneDriverResult r = app::DriveScriptsFromAssets(
        assets, &fs, "Resources/Scripts.BIN", /*maxScripts=*/0, /*stepTicks=*/3);

    std::printf("  [real] Scripts.BIN: members=%d esc=%d parsed=%d withMain=%d "
                "commandsRegistered=%d steps=%d parseErrors=%d firstEsc=%s\n",
                r.membersSeen, r.escMembers, r.scriptsParsed, r.scriptsWithMain,
                r.commandsRegistered, r.stepsExecuted, r.parseErrors,
                r.firstEsc.c_str());

    CHECK(r.assetsPresent);
    CHECK(r.escMembers > 300);             // ~349 .esc shipped
    CHECK(!r.firstEsc.empty());
    CHECK_EQ(r.scriptsParsed, r.escMembers);   // every real .esc compiles
    CHECK_EQ(r.parseErrors, 0);
    CHECK(r.scriptsWithMain > r.escMembers * 9 / 10);   // overwhelming majority
    // Every main-bearing script stepped its context once through StepAllActive.
    CHECK_EQ(r.stepsExecuted, r.scriptsWithMain);
    CHECK(r.commandsRegistered > 0);       // the VM invoked real registered cmds

    io::VfsShutdown();
}

// Sanity: the direct-archive path yields the same parse/step outcome as the
// mounted path (the driver doesn't depend on the mount being present).
TEST(RealSceneDriverE2E, DirectArchivePathMatchesMounted) {
    if (!ScriptsPresent()) {
        std::printf("  [skip] RealSceneDriverE2E.DirectArchivePathMatchesMounted\n");
        return;
    }
    app::SetRealSceneDriverHooks(nullptr);
    shim::DiskFileSystem fs(GameDir());

    app::RealSceneDriverResult r = app::DriveScriptsFromArchive(
        &fs, "Resources/Scripts.BIN", /*maxScripts=*/0, /*stepTicks=*/3);

    CHECK(r.assetsPresent);
    CHECK_EQ(r.membersSeen, 413);
    CHECK(r.escMembers > 300);
    CHECK_EQ(r.scriptsParsed, r.escMembers);
    CHECK_EQ(r.parseErrors, 0);
    CHECK(r.stepsExecuted >= 1);
}
