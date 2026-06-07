// Integration test for the real-asset GUI-forms driver.
//
// Wires the driver against the REAL reconstructed loader siblings over a REAL
// `.form` member: mount Resources/forms.BIN through io::ArchiveMount, extract one
// FRM2 member's bytes, and feed them to guild::app::DriveFormBuffer (which runs the
// real gui::Form_ParseResourceFile and builds the live retained-mode tables). This
// asserts the cross-module flow (ZipArchive/ArchiveMount -> FRM2 parser -> widget
// build) end to end on a single real member.
//
// GUARDED: if the real game dir is absent the test skips cleanly. Override the dir
// with GUILD_GAME_DIR.
#include "tests/framework/test.h"

#include "app/real_forms_driver.h"

#include "io/archive_mount.h"
#include "shim_impl/disk_filesystem.h"

#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

using namespace guild;

namespace {

std::string GameDir() {
    if (const char* env = std::getenv("GUILD_GAME_DIR"))
        return env;
    return "/home/cnupt/work/reverse/reverse-guild/reimpl/europe_guild_1400_original";
}

bool FormsBinPresent() {
    shim::DiskFileSystem fs(GameDir());
    return fs.exists("Resources/forms.BIN");
}

bool EndsWithForm(const std::string& n) {
    if (n.size() < 5) return false;
    const char* e = n.c_str() + n.size() - 5;
    return e[0] == '.' && (e[1] == 'f' || e[1] == 'F') && (e[2] == 'o' || e[2] == 'O') &&
           (e[3] == 'r' || e[3] == 'R') && (e[4] == 'm' || e[4] == 'M');
}

} // namespace

// Mount the real forms.BIN, extract the first FRM2 `.form` member, and drive it
// through the reconstructed parser. Assert a real widget tree was built.
TEST(RealFormsDriverItest, MountExtractDriveOneRealMember) {
    if (!FormsBinPresent()) {
        std::printf("  [skip] RealFormsDriverItest.MountExtractDriveOneRealMember: "
                    "Resources/forms.BIN absent (%s)\n", GameDir().c_str());
        return;
    }

    shim::DiskFileSystem fs(GameDir());
    io::ArchiveMount mount;
    CHECK(mount.Mount(&fs, "Resources/forms.BIN", /*caseInsensitive=*/true));
    CHECK(mount.isMounted());
    CHECK(mount.memberCount() > 0u);

    // Find the first FRM2-shaped `.form` member (byte[3]=='2') and drive it.
    bool drove = false;
    for (const auto& m : mount.members()) {
        if (!EndsWithForm(m.name))
            continue;
        std::vector<u8> bytes;
        if (!mount.OpenMember(m.name.c_str(), bytes) || bytes.size() < 4)
            continue;
        if (bytes[3] != '2')   // want a real FRM2 member
            continue;

        app::DrivenForm df = app::DriveFormBuffer(bytes.data(), bytes.size(), m.name.c_str());
        // A real FRM2 member must parse and build at least one window.
        CHECK(df.parsed);
        CHECK(df.frm2);
        CHECK(df.formId >= 1);
        CHECK(df.windowCount >= 1);
        CHECK(df.bytes == bytes.size());
        CHECK(df.rootWindows + df.childWindows == df.windowCount);
        drove = true;
        break;
    }
    CHECK(drove); // at least one FRM2 member was found + driven
}

// Cross-check the full driver entry (DriveRealForms) over the real assets: it must
// mount, parse the bulk of members, and produce non-trivial structural totals.
TEST(RealFormsDriverItest, DriveRealFormsMountsAndParses) {
    if (!FormsBinPresent()) {
        std::printf("  [skip] RealFormsDriverItest.DriveRealFormsMountsAndParses: "
                    "Resources/forms.BIN absent\n");
        return;
    }

    shim::DiskFileSystem fs(GameDir());
    app::RealFormsResult r = app::DriveRealForms(&fs, GameDir());

    CHECK(r.assetsPresent);
    CHECK(r.mounted);
    CHECK(r.formMembers > 0);
    CHECK(r.parsedForms > 0);
    CHECK(r.totalWindows > 0);
    CHECK(r.totalWidgets > 0);
    // The inert edge hooks must have fired during the bulk build.
    CHECK(r.propertyValidateCalls > 0);
}
