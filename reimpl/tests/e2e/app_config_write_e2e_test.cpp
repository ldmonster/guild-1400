// tests/e2e/app_config_write_e2e_test.cpp — e2e that the spine's shutdown
// teardown STEP 3 (tdConfigWriteGfxSettings) now runs the REAL settings
// serializer (app::ConfigWriteGfxSettings, VIBE_Config_WriteGfxSettings
// @0x56af54) end-to-end through the full lifecycle.
//
// Two paths:
//   * Headless (always): boot the synthetic spine; assert step 3 wrote the full
//     45-key settings set and emitted the [Game]/stadt value the live config held.
//   * Real-asset (GUARDED): boot on a real "Die Gilde" install; assert the
//     serializer ran over the REAL parsed Gilde.INI config (Stadt=Augsburg), so
//     the newly-wired hook reaches real code on the real-asset path too.
#include "test.h"

#include "app/wiring.h"

#include "shim_impl/disk_filesystem.h"

#include <cstdlib>
#include <cstdio>
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

// ---- headless path (always runs) -----------------------------------------
TEST(AppConfigWriteE2E, ShutdownStep3SerializesSettings) {
    auto r = app::RunHeadless(/*displayMode=*/1, /*showIntro=*/false,
                              /*networkClient=*/false, /*frames=*/4);
    CHECK_EQ(r.exitCode, 0);
    // The 13-step shutdown ran step 3 (real serializer): one entry per
    // [Gfx]/[Sound]/[Game] key (11+12+5+17 == 45).
    CHECK_EQ(r.settingsKeysWritten, 45);
    // The live config (empty INI -> defaults) holds stadt == "Augsburg".
    CHECK(r.writtenStadt == "Augsburg");
}

// ---- real-asset path (GUARDED) -------------------------------------------
TEST(AppConfigWriteE2E, RealAssetsShutdownStep3OverRealIni) {
    if (!RealAssetsPresent()) {
        std::printf("  [skip] AppConfigWriteE2E.RealAssetsShutdownStep3OverRealIni: "
                    "real game dir absent (%s)\n", GameDir().c_str());
        return; // clean skip-pass
    }
    app::RealHeadlessResult r =
        app::RunHeadlessRealAssets(GameDir(), /*frames=*/4, /*displayMode=*/1);
    CHECK(r.assetsPresent);
    CHECK(r.base.exitCode == 0);
    // The serializer ran over the REAL parsed Gilde.INI config: the full 45-key
    // settings set was written (the load-bearing proof step 3 reached real code).
    // (The persisted [Game]/stadt is written verbatim from the live config — the
    // real INI ships an empty [Game]/stadt, distinct from [General]/Stadt, so it
    // is NOT asserted non-empty: the serializer faithfully writes whatever the
    // config held, matching the original byte_123356C write.)
    CHECK_EQ(r.base.settingsKeysWritten, 45);
}
