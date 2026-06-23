// Real-asset e2e for the GUI-forms driver.
//
// Drives the ENTIRE real Resources/forms.BIN through the reconstructed FRM2 parser:
// MountRealGameAssets -> the mounted forms.BIN member index -> for every `.form`
// member, extract its bytes and run gui::Form_ParseResourceFile, BUILDING the live
// retained-mode Form/Window/Widget trees, and assert the aggregate structure.
//
// This is the COMPLEMENT of tests/e2e/real_assets_forms_e2e_test.cpp: that test
// proves the gfx-OBJECT-catalogue parser (Form_LoadFromBuffer) *rejects* the FRM2
// members; THIS test proves the *correct* FRM2 parser *accepts* them and builds
// their widget trees. The shipped archive has 477 members, 323 `.form`; 321 are
// FRM2 + 2 old-layout (help.form, "ToolTip Geldsack.form"). All 323 should parse.
//
// GUARDED: skip cleanly (zero checks) if the real game dir is absent. Override the
// dir with GUILD_GAME_DIR.
#include "tests/framework/test.h"

#include "app/real_forms_driver.h"

#include "shim_impl/disk_filesystem.h"

#include <cstdio>
#include <cstdlib>
#include <string>

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

} // namespace

TEST(RealFormsDriverE2E, DriveEveryFormThroughFrm2Parser) {
    if (!FormsBinPresent()) {
        std::printf("  [skip] RealFormsDriverE2E.DriveEveryFormThroughFrm2Parser: "
                    "real game dir absent (%s)\n", GameDir().c_str());
        return; // clean skip
    }

    shim::DiskFileSystem fs(GameDir());
    app::RealFormsResult r = app::DriveRealForms(&fs, GameDir());

    CHECK(r.assetsPresent);
    CHECK(r.mounted);
    // The ArchiveMount index records only non-directory members (the raw central-dir
    // entry count is 477; 416 are files, the rest directory markers).
    CHECK_EQ(r.archiveMembers, 416u);

    // Every `.form` member must be found and parse cleanly through the FRM2 parser.
    CHECK_EQ(r.formMembers, 323);
    CHECK_EQ(r.parsedForms, 323);
    CHECK_EQ(r.failedForms, 0);
    CHECK_EQ(r.frm2Forms, 321);   // FRM2 layout
    CHECK_EQ(r.oldForms, 2);      // help.form + "ToolTip Geldsack.form"

    // The build produced real, non-trivial widget trees. Observed with the
    // BYTE-EXACT per-object strides recovered from gilde.exe 0x41c4d6 (type/aux base
    // +4*o, x/y +2*o, name +64*o, slider-range +1*o): 918 windows / 592 child links /
    // 493 widgets [sprite=404 label=13 input=5 slider=45] / 1085 object records over
    // the 323 forms. (An earlier reconstruction used a +8 type/aux stride, which
    // mis-read object type bytes and inflated the widget count to ~609; the +4 stride
    // is what the binary actually uses.)
    CHECK(r.totalWindows > 800);     // most forms have several windows
    CHECK(r.totalWidgets > 450);     // hundreds of controls across all forms (493)
    CHECK(r.totalObjectRecords > 1000);
    CHECK(r.totalChildWindows > 400);// many nested child-window links
    CHECK(r.totalSprites > 400);
    CHECK(r.totalLabels > 0);
    CHECK(r.totalInputs > 0);
    CHECK(r.totalSliders > 0);
    CHECK(r.propertyValidateCalls > 0);

    // A richest form exists with a deep window tree.
    CHECK(r.maxWindowsInOneForm >= 5);
    CHECK(r.maxWidgetsInOneForm >= 5);
    CHECK(!r.richestFormName.empty());

    std::printf("  [real] forms.BIN: members=%zu form-members=%d parsed=%d "
                "(FRM2=%d old=%d failed=%d)\n",
                r.archiveMembers, r.formMembers, r.parsedForms,
                r.frm2Forms, r.oldForms, r.failedForms);
    std::printf("  [real]   windows=%ld (child=%ld) widgets=%ld "
                "[sprite=%ld label=%ld input=%ld slider=%ld] objRecords=%ld\n",
                r.totalWindows, r.totalChildWindows, r.totalWidgets,
                r.totalSprites, r.totalLabels, r.totalInputs, r.totalSliders,
                r.totalObjectRecords);
    std::printf("  [real]   richest form: %s (windows=%d widgets=%d) "
                "edge-hooks: propValidate=%ld findText=%ld\n",
                r.richestFormName.c_str(), r.maxWindowsInOneForm,
                r.maxWidgetsInOneForm, r.propertyValidateCalls, r.findTextIndexCalls);
}
