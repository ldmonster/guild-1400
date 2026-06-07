// Real-asset e2e: mount the REAL shipped `Resources/forms.BIN` (a PKZIP archive of
// ~477 members, 323 of them `.form` screens), walk the central directory, extract
// every `.form` member with the reconstructed ZipArchive/ArchiveMount, and feed each
// to the reconstructed gfx-catalogue parser (Form_LoadFromBuffer @0x41b888) to prove
// the loader never crashes on real, varied input.
//
// FINDING (documented): the shipped `.form` members are NOT the gfx-object catalogue
// format that gilde.gfx uses. 321/323 are an "FRM2"-magic retained-mode layout
// format (a separate, deferred parser); only 2 (help.form, "ToolTip Geldsack.form")
// happen to use the small gfx-object-catalogue layout (objectCount=1). The recovered
// Form_LoadFromBuffer correctly *rejects* the FRM2 files via its objectCount>2048 cap
// and short-buffer guard (returns false, no crash) and *accepts* the 2 catalogue-shaped
// ones. This test locks in that robustness contract against the real bytes.
//
// GUARDED: if the asset folder isn't present the test passes trivially.
#include "tests/framework/test.h"

#include "gui/form_loader.h"
#include "gui/form.h"
#include "gui/window.h"
#include "gui/object.h"

#include "io/zip_archive.h"
#include "io/archive_mount.h"
#include "shim_impl/disk_filesystem.h"

#include <cstring>
#include <string>
#include <vector>

using namespace guild;
using namespace guild::gui;

// Renderer edge stub (RegisterGfxState is weak in form_loader.cpp). The catalogue-
// shaped .form members are tiny (objectCount=1) so this should not fire, but provide
// a definition for a clean link.
namespace guild::gui {
void RegisterGfxState(int /*gfxIndex*/) {}
}

static const char* kRoot =
    "/home/cnupt/work/reverse/reverse-guild/reimpl/europe_guild_1400_original";

static bool assetsPresent() {
    guild::shim::DiskFileSystem fs(kRoot);
    return fs.exists("Resources/forms.BIN");
}

static bool endsWithForm(const char* name) {
    std::size_t n = std::strlen(name);
    if (n < 5) return false;
    const char* e = name + n - 5;
    return (e[0] == '.' &&
            (e[1] == 'f' || e[1] == 'F') &&
            (e[2] == 'o' || e[2] == 'O') &&
            (e[3] == 'r' || e[3] == 'R') &&
            (e[4] == 'm' || e[4] == 'M'));
}

TEST(RealAssetsForms, MountFormsBinParseEveryFormMember) {
    if (!assetsPresent()) { CHECK(true); return; } // skipped: no assets

    guild::shim::DiskFileSystem fs(kRoot);

    io::ZipArchive z;
    CHECK(z.Open(&fs, "Resources/forms.BIN"));
    CHECK_EQ(z.numberEntry(), 477u);

    // Walk the whole central directory; for every `.form` member, extract its bytes
    // (stored or deflated, via the reconstructed InflateRaw) and feed them to the
    // recovered Form_LoadFromBuffer. Count clean parses vs clean rejections; assert
    // NOTHING crashes and the totals match the shipped archive.
    int totalMembers = 0;
    int formMembers  = 0;
    int parsedClean  = 0;     // Form_LoadFromBuffer returned true
    int rejectedClean = 0;    // returned false (e.g. FRM2: objectCount cap / short guard)
    int extractFail   = 0;
    std::vector<std::string> parsedNames;

    char nm[260];
    io::ZipFileInfo fi;
    for (int r = z.GoToFirstFile(); r == io::kZipOk; r = z.GoToNextFile()) {
        ++totalMembers;
        if (z.GetCurrentFileInfo(&fi, nm, sizeof nm) != io::kZipOk)
            continue;
        if (!endsWithForm(nm))
            continue;
        ++formMembers;

        std::vector<u8> form;
        if (!z.ExtractCurrentFile(form)) {
            ++extractFail;
            continue;
        }

        // Reset the GUI/catalogue state before each parse so one member can't leak
        // into the next, then drive the real loader. It must return cleanly either
        // way (true = catalogue-shaped, false = guarded reject) and never crash.
        ResetGuiState();
        ResetGfxObjects();
        bool ok = Form_LoadFromBuffer(form.data(), form.size(), /*initTables=*/true);
        if (ok) {
            ++parsedClean;
            parsedNames.push_back(nm);
        } else {
            ++rejectedClean;
        }
    }

    CHECK_EQ(totalMembers, 477);
    CHECK_EQ(formMembers, 323);
    CHECK_EQ(extractFail, 0);              // every member inflated cleanly
    CHECK_EQ(parsedClean + rejectedClean, 323);

    // The 2 catalogue-shaped members parse cleanly as gfx-object tables; the 321
    // FRM2 layout members are cleanly rejected by the loader's guards (no crash).
    CHECK_EQ(parsedClean, 2);
    CHECK_EQ(rejectedClean, 321);

    // The 2 that parse are help.form and "ToolTip Geldsack.form".
    bool sawHelp = false, sawTooltip = false;
    for (const auto& n : parsedNames) {
        if (n.find("help.form") != std::string::npos) sawHelp = true;
        if (n.find("ToolTip Geldsack.form") != std::string::npos) sawTooltip = true;
    }
    CHECK(sawHelp);
    CHECK(sawTooltip);
}

// Cross-check the same walk through the higher-level ArchiveMount index path
// (VIBE_Vfs_EnumerateMatchingFiles): mount, then OpenMember each indexed `.form`.
TEST(RealAssetsForms, ArchiveMountExtractsEveryFormMember) {
    if (!assetsPresent()) { CHECK(true); return; }

    guild::shim::DiskFileSystem fs(kRoot);
    io::ArchiveMount mount;
    CHECK(mount.Mount(&fs, "Resources/forms.BIN", /*caseInsensitive=*/true));
    CHECK(mount.isMounted());

    int formMembers = 0, extracted = 0;
    std::size_t totalBytes = 0;
    for (const auto& m : mount.members()) {
        if (!endsWithForm(m.name.c_str()))
            continue;
        ++formMembers;
        std::vector<u8> bytes;
        if (mount.OpenMember(m.name.c_str(), bytes)) {
            ++extracted;
            totalBytes += bytes.size();
        }
    }
    // The mount index only records non-directory members; all 323 .form members are
    // files and must extract.
    CHECK_EQ(formMembers, 323);
    CHECK_EQ(extracted, 323);
    CHECK(totalBytes > 100000u); // real inflated form payloads, sanity floor
}
