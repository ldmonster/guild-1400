// tests/e2e/app_real_boot_e2e_test.cpp — GUARDED real-asset boot e2e.
//
// Exercises app::MountRealGameAssets over a REAL "Die Gilde — Europe 1400"
// install: read Gilde.INI through the reconstructed INI parser, bind the VFS,
// mount the Resources/*.BIN archives, open a known member by name, then load the
// real city AUGSBURG.cty (transparent gunzip + the save/city header loader)
// through the VFS and assert sane values.
//
// GUARDED: the real game directory is not part of the repo. If it is absent the
// test records ZERO checks and returns (a clean skip) so CI without the assets
// still passes. Point it elsewhere with the GUILD_GAME_DIR env var.
#include "tests/framework/test.h"

#include "app/real_boot.h"

#include "io/vfs.h"
#include "io/gamestate.h"

#include "shim_impl/disk_filesystem.h"

#include <cstdlib>
#include <string>
#include <vector>

using namespace guild;

namespace {

// Resolve the real game directory: GUILD_GAME_DIR overrides the in-repo default.
std::string GameDir() {
    if (const char* env = std::getenv("GUILD_GAME_DIR"))
        return env;
    return "/home/cnupt/work/reverse/reverse-guild/reimpl/europe_guild_1400_original";
}

// Present iff the four key real assets resolve under `fs`.
bool RealAssetsPresent(shim::IFileSystem& fs) {
    return fs.exists("Gilde.INI") && fs.exists("gfx/gilde.gfx") &&
           fs.exists("Resources/forms.BIN") &&
           fs.exists("Resources/gamedata/Cities/AUGSBURG.cty");
}

} // namespace

TEST(AppRealBoot, MountAndLoad) {
    const std::string dir = GameDir();
    shim::DiskFileSystem fs(dir);

    if (!RealAssetsPresent(fs)) {
        std::printf("  [skip] AppRealBoot.MountAndLoad: real game dir absent (%s)\n",
                    dir.c_str());
        return; // clean skip — no checks recorded
    }

    // ---- mount the real assets through the reusable helper -----------------
    app::RealGameAssets a =
        app::MountRealGameAssets(&fs, dir, "Gilde.INI", {}, /*caseInsensitive=*/false);

    // INI parsed: the [General] start city is Augsburg; [Game] difficulty came
    // from the file (=2, not the reconstructed default of 1).
    CHECK(a.iniLoaded);
    CHECK(a.stadt == "Augsburg");
    CHECK(!a.gfxPath.empty());           // [General] GfxPath present
    CHECK(a.sound.masterVol == 127);     // [Sound] master_vol from the real file

    // VFS bound, archives mounted.
    CHECK(a.vfsBound);
    CHECK(a.archives.size() == app::DefaultResourceArchives().size());
    std::size_t mounted = 0;
    for (const auto& m : a.archives)
        if (m.mounted) ++mounted;
    CHECK(mounted == a.archives.size());      // every Resources/*.BIN mounted
    CHECK(a.totalMembers() > 1000);           // thousands of indexed members

    // forms.BIN has the documented member count (416 non-directory members).
    {
        bool foundForms = false;
        for (const auto& m : a.archives) {
            if (m.name == "Resources/forms.BIN") {
                foundForms = true;
                CHECK(m.mounted);
                CHECK(m.memberCount == 416);
            }
        }
        CHECK(foundForms);
    }

    // ---- open a known member by name through the mounts --------------------
    {
        const char* member = "BAUEN/GEB_BAUEN.FORM";
        io::ArchiveMount* m = a.archiveForMember(member);
        CHECK(m != nullptr);
        if (m) {
            std::vector<u8> bytes;
            CHECK(m->OpenMember(member, bytes));
            CHECK(bytes.size() > 0);
        }
    }

    // ---- load the real city through the VFS (gunzip + header loader) -------
    {
        io::GameState city{};
        city.relink.assign(io::kRelinkBytes, 0);
        bool ok = io::LoadGameState("Resources/gamedata/Cities/AUGSBURG.cty",
                                    city, /*load=*/nullptr);
        CHECK(ok);
        if (ok) {
            // The .cty is a SaveHeader-format city seed: version in range, the
            // city name is "Augsburg", and the wealth is a sane positive value.
            CHECK(city.header.magic >= io::kSaveVersionLoadMin);
            CHECK(city.header.magic <= io::kSaveVersionLoadMax);
            CHECK(std::string(city.header.name) == "Augsburg");
            CHECK(city.header.season < 12);    // a month/season-ish byte
            CHECK(city.header.wealth > 0);
        }
    }

    io::VfsShutdown();
}
