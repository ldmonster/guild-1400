// tests/unit/app_real_wiring_test.cpp — GUARDED real-asset spine-wiring unit test.
//
// Asserts that, with a REAL "Die Gilde — Europe 1400" install mounted and the
// RealSubsystems flipped into real-asset mode (BindRealAssets), the spine's
// file-layer hooks consume the REAL loaders over REAL bytes — NOT the synthetic
// in-memory stand-ins:
//
//   * configReadGfxAndSound reads the parsed real Gilde.INI (Stadt=Augsburg,
//     [Sound] master_vol=127, [Game] difficulty=2),
//   * the VFS is bound + the Resources/*.BIN archives mounted (thousands of
//     members),
//   * guiLoadGfxFile loads gfx/gilde.gfx -> 1806 real gfx objects,
//   * worldLoadBuildingAndObjectData reads data/A_Geb.dat (72) + A_Obj.dat (731)
//     and loads the configured <Stadt>.cty seed.
//
// This drives the hooks DIRECTLY (subsystem-level), proving each upgraded hook is
// real before the e2e test runs the whole lifecycle.
//
// GUARDED: the real game directory is not in the repo. If absent the test records
// ZERO checks and returns (clean skip). Override with GUILD_GAME_DIR.
#include "tests/framework/test.h"

#include "app/real_boot.h"
#include "app/wiring.h"

#include "config/ini.h"

#include "io/vfs.h"

#include "shim_impl/disk_filesystem.h"
#include "shim_impl/loopback_socket.h"
#include "shim_impl/memory_graphics.h"
#include "shim_impl/null_audio.h"
#include "shim_impl/null_platform.h"

#include <cstdlib>
#include <string>

using namespace guild;

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

} // namespace

// City-path builder is install-independent — always exercisable.
TEST(AppRealWiring, CityPathUppercases) {
    CHECK(app::RealCityPath("Augsburg") ==
          "Resources/gamedata/Cities/AUGSBURG.cty");
    CHECK(app::RealCityPath("Köln").find("Cities/") != std::string::npos);
    CHECK(app::RealCityPath("").empty());
}

TEST(AppRealWiring, HooksConsumeRealLoaders) {
    const std::string dir = GameDir();
    shim::DiskFileSystem fs(dir);
    if (!RealAssetsPresent(fs)) {
        std::printf("  [skip] AppRealWiring.HooksConsumeRealLoaders: real game dir "
                    "absent (%s)\n", dir.c_str());
        return; // clean skip
    }

    // Mount the real assets (INI parse + VFS bind + archive mounts).
    app::RealGameAssets assets =
        app::MountRealGameAssets(&fs, dir, "Gilde.INI", {}, /*caseInsensitive=*/false);
    CHECK(assets.iniLoaded);
    CHECK(assets.stadt == "Augsburg");
    CHECK(assets.vfsBound);
    CHECK(assets.totalMembers() > 1000);

    // Build a RealSubsystems fed the REAL parsed INI + disk fs, in real-asset mode.
    shim::NullPlatform plat;
    shim::MemoryGraphicsDevice gfx;
    shim::NullAudioDevice audioDev;
    auto pair = shim::LoopbackSocket::makePair();
    app::RealSubsystems sub(&plat, &gfx, &audioDev, &fs, pair.first.get(), &assets.ini);
    sub.BindRealAssets(&assets, dir);
    CHECK(sub.realAssetsMode());

    // configReadGfxAndSound -> the REAL Gilde.INI values (not reconstructed defaults).
    sub.configReadGfxAndSound();
    CHECK(sub.firedReal("configReadGfxAndSound"));
    // [Sound] master_vol from the real file (user-mutable — the in-game options
    // screen persists into it — so compare against an independent parse, not a
    // literal) and [Game] difficulty (=2 in the shipped file, not the default 1).
    {
        config::IniFile ini;
        CHECK(ini.loadFile(dir + "/Gilde.INI"));
        CHECK((int)sub.sound().masterVol == ini.getInt("Sound", "master_vol", -1));
        CHECK(ini.getInt("Sound", "master_vol", -1) >= 0);
    }
    CHECK(sub.game().difficulty == 2);     // [Game] difficulty (not the default 1)

    // vfsInit -> binds the disk VFS (so the gfx/.cty loose-file opens resolve).
    sub.vfsInit("\\project\\gfx\\");
    CHECK(sub.firedReal("vfsInit"));
    CHECK(sub.vfsInited());

    // guiLoadGfxFile -> gui::Form_LoadFromFile("gfx/gilde.gfx") = 1806 real objects.
    bool gfxOk = sub.guiLoadGfxFile("gilde.gfx");
    CHECK(gfxOk);
    CHECK(sub.firedReal("guiLoadGfxFile"));
    CHECK(sub.gfxObjectCount() == 1806);

    // worldLoadBuildingAndObjectData -> A_Geb.dat (72) + A_Obj.dat (731) + the
    // configured <Stadt>.cty seed.
    sub.worldLoadBuildingAndObjectData("\\project\\game\\data\\");
    CHECK(sub.firedReal("worldLoadBuildingAndObjectData"));
    CHECK(sub.buildingTypeCount() == 72);
    CHECK(sub.sceneTypeCount() == 731);
    CHECK(sub.cityLoaded());
    CHECK(sub.cityName() == "Augsburg");

    io::VfsShutdown();
}
