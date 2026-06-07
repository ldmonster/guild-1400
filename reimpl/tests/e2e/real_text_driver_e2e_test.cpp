// Real-asset e2e: full run of the text-DB driver over the REAL
// europe_guild_1400_original bytes. MountRealGameAssets -> LoadRealTextDb of the
// REAL Resources/textbin_deutsch.BIN (~101 compiled .res members) through the
// reconstructed ArchiveMount + gui::text::BuildTextArray, then RESOLVE a set of
// string ids and assert non-empty results + the entry count.
//
// GUARDED: if the asset dir is absent, print a [skip] line and return cleanly.
// Override the dir with GUILD_GAME_DIR.
#include "tests/framework/test.h"

#include "app/real_boot.h"
#include "app/real_text_driver.h"

#include "gui/text/textdb.h"
#include "io/vfs.h"

#include "shim_impl/disk_filesystem.h"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

using namespace guild;
using guild::gui::text::TextDb;

namespace {

std::string GameDir() {
    if (const char* env = std::getenv("GUILD_GAME_DIR")) return env;
    return "/home/cnupt/work/reverse/reverse-guild/reimpl/europe_guild_1400_original";
}

bool AssetsPresent() {
    shim::DiskFileSystem fs(GameDir());
    return fs.exists("Gilde.INI") && fs.exists("Resources/textbin_deutsch.BIN");
}

} // namespace

TEST(RealTextDriverE2E, LoadRealGermanTextDbAndResolveIds) {
    if (!AssetsPresent()) {
        std::printf("    [skip] real assets absent (set GUILD_GAME_DIR)\n");
        CHECK(true);
        return;
    }

    shim::DiskFileSystem fs(GameDir());

    // Full driver: real boot (INI + VFS + archive mounts) -> load the German text DB.
    TextDb db;
    auto res = guild::app::MountAndLoadRealTextDb(
        &fs, GameDir(), db, "Resources/textbin_deutsch.BIN");

    CHECK(res.archiveMounted);
    CHECK(res.resMembers >= 100u);
    CHECK_EQ(res.resLoaded, res.resMembers);
    CHECK(res.entryCount > 10000);
    CHECK_EQ(res.entryCount, db.Count());

    std::printf("    [real] textbin_deutsch.BIN: %zu .res members, %zu loaded, "
                "%d text entries\n",
                res.resMembers, res.resLoaded, res.entryCount);

    // Resolve a spread of real string ids by name (case-folded lookup) + by index.
    std::vector<std::string> keys = {
        "_NEV_DORTHIN+0",
        "_NEV_DANKE+0",
        "_nev_weiter+0",   // mixed case, must still resolve
    };
    auto resolved = guild::app::ResolveStringKeys(db, keys);

    int foundCount = 0;
    for (const auto& r : resolved) {
        if (r.found && !r.text.empty()) {
            ++foundCount;
            std::printf("    [real] %-18s -> [%d] tag=0x%02X \"%s\"\n",
                        r.key.c_str(), r.index, static_cast<unsigned>(r.tag),
                        r.text.c_str());
        }
    }
    // All three known keys resolve to non-empty strings.
    CHECK_EQ(foundCount, 3);
    CHECK(resolved[0].found);
    CHECK_EQ(resolved[0].index, 6062);          // first entry of Text_N_Nachrichten.res
    CHECK(!resolved[0].text.empty());
    CHECK_EQ(resolved[0].tag, 0xFFu);
    CHECK_EQ(resolved[1].index, 6063);
    CHECK(resolved[2].found);                   // case-folded match

    io::VfsShutdown();
}
