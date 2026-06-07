// tests/e2e/app_menu_loop_e2e_test.cpp — GUARDED real-asset boot of the top-level
// menu->ingame path.
//
// Boots the app spine on a REAL "Die Gilde — Europe 1400" install (mount the real
// Gilde.INI + Resources/*.BIN + gilde.gfx), then drives the TOP-LEVEL menu state
// machine the binary runs between engine bring-up and the session:
//
//   real-asset spine boot  -->  main menu (real gui dispatch + real shim input
//   pump)  --(New Game click)-->  outer-loop decision (kRunSession)  -->  the real
//   InitOrLoadSession bootstrap on the real city seed.
//
// Asserts a NEWLY-WIRED hook ran REAL all the way through: the menu's click was
// routed by the real gui::MainMenu_Dispatch sibling (mock->real), and that armed
// the session that the real new-game bootstrap brought up (world reset + turn
// loop) — i.e. the menu carried us into a live in-game session on real assets.
//
// GUARDED: clean skip (zero checks) if the real game dir is absent. Override with
// GUILD_GAME_DIR.
#include "app/menu_loop.h"
#include "app/real_boot.h"
#include "app/session_init.h"
#include "gui/main_menu.h"
#include "io/vfs.h"
#include "tests/framework/test.h"

#include "shim_impl/disk_filesystem.h"
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

bool RealAssetsPresent() {
    shim::DiskFileSystem fs(GameDir());
    return fs.exists("Gilde.INI") && fs.exists("gfx/gilde.gfx") &&
           fs.exists("Resources/forms.BIN") &&
           fs.exists("Resources/gamedata/Cities/AUGSBURG.cty");
}

// The menu's New Game button: ChooseCity succeeds (a real city was selected). The
// sink records that the REAL gui dispatch invoked it (mock->real proof).
struct RealNewGameSink : gui::MainMenuCommandSink {
    bool chooseCityRan = false;
    bool EnterChooseCity() override { chooseCityRan = true; return true; }
};

struct NewGameClicks : app::MenuClickSource {
    int  hoverThisFrame(int) override { return 0; }            // hover New Game
    bool clickThisFrame(int frame) override { return frame == 1; }
};

} // namespace

TEST(AppMenuLoopE2E, RealAssetBootThenMenuNewGameToInGame) {
    if (!RealAssetsPresent()) {
        std::printf("  [skip] AppMenuLoopE2E.RealAssetBootThenMenuNewGameToInGame: "
                    "real game dir absent (%s)\n", GameDir().c_str());
        return; // clean skip
    }

    // --- 1. Boot the real assets through the reconstructed loaders: parse the real
    //        Gilde.INI, bind the VFS to the real install, mount Resources/*.BIN.
    //        This is the real spine pre-menu file-layer bring-up (real bytes).
    shim::DiskFileSystem fs(GameDir());
    app::RealGameAssets assets =
        app::MountRealGameAssets(&fs, GameDir(), "Gilde.INI", {},
                                 /*caseInsensitive=*/false);
    CHECK(assets.iniLoaded);
    CHECK(assets.vfsBound);
    int mounted = 0;
    for (const auto& a : assets.archives) if (a.mounted) ++mounted;
    CHECK(mounted == static_cast<int>(app::DefaultResourceArchives().size()));
    CHECK(assets.totalMembers() > 1000);       // thousands of indexed members
    // The configured start city seed resolves on disk through the real path build.
    const std::string startCity = assets.stadt; // the real [General] Stadt
    CHECK(!startCity.empty());
    CHECK(fs.exists(app::RealCityPath(startCity).c_str()));

    // --- 2. The main menu: pump the real shim input, dispatch through the REAL
    //        gui sibling. The New Game click closes the menu with the new-game flag.
    RealNewGameSink sink;
    gui::MainMenu_SetCommandSink(&sink);

    shim::NullPlatform plat;
    plat.createMainWindow("Die Gilde", 800, 600, false);
    shim::MouseState m;
    m.x = 32 + 4; m.y = 10 + 4; m.left = true; // over the New Game button, pressed
    plat.setMouse(m);

    NewGameClicks clicks;
    app::MenuResult r = app::MenuRunMainMenu(plat, clicks, /*maxFrames=*/8);

    // The newly-wired hook ran REAL: the real gui dispatch invoked EnterChooseCity.
    CHECK(sink.chooseCityRan);
    CHECK(r.close);
    CHECK(r.item == gui::MainMenuItem::kNewGame);
    CHECK_EQ(r.sessionFlags, gui::kSessionNewGame);

    // --- 3. The outer-loop decision carries us into a session.
    app::MenuMainNext next = app::MenuMainDecide(r, /*restartDisplay=*/false);
    CHECK(next == app::MenuMainNext::kRunSession);

    // --- 4. In-game: the REAL new-game bootstrap on the real start city.
    app::SessionInitCtx ctx;
    ctx.cityName = startCity;
    ctx.rngSeed  = 0xC0FFEE;
    ctx.players.push_back({/*personId=*/1, /*kind=*/6, /*isPlayer=*/true});
    app::InitOrLoadSession(static_cast<std::uint16_t>(r.sessionFlags), ctx,
                           /*framesPerSession=*/4);

    CHECK(ctx.mode == app::SessionMode::NewSingle);
    CHECK(ctx.worldInited);     // the real world reset ran for the real city
    CHECK(ctx.rngSeeded);
    CHECK(ctx.reachedTurnLoop); // the menu carried us into the live turn loop

    gui::MainMenu_SetCommandSink(nullptr);
    io::VfsShutdown();          // unbind the process-global VFS bound by the mount
}
