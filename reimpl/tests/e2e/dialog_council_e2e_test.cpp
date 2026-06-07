// tests/e2e/dialog_council_e2e_test.cpp — GUARDED Wave 30 PLAY B2: the REAL council
// apply-for-office dialog on REAL assets. Loads the REAL shipped office form
// (Resources/forms.BIN -> Locations/Rathaus/amtsbewerbung.form) through the REAL
// parser, lays out + renders the dialog (real window count, non-clear px), seats a
// vacant council seat, then clicks an office button -> the REAL opcode-68 candidacy
// command -> observable world effect (g_officeHolders rank bump), deterministic.
//
// Skips cleanly when the shipped assets are absent (honors GUILD_GAME_DIR).
#include "test.h"

#include "app/real_boot.h"
#include "io/archive_mount.h"
#include "io/vfs.h"
#include "play/dialog_council.h"
#include "render/surface.h"
#include "shim_impl/disk_filesystem.h"
#include "world/office.h"
#include "world/office_assign.h"
#include "world/law_types.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

using namespace guild;
using namespace guild::play;

namespace {

const char* kCouncilFormMember = "Locations/Rathaus/amtsbewerbung.form";

std::string GameDir() {
    if (const char* env = std::getenv("GUILD_GAME_DIR")) return env;
    return "/home/cnupt/work/reverse/reverse-guild/reimpl/europe_guild_1400_original";
}

bool RealAssetsPresent() {
    shim::DiskFileSystem fs(GameDir());
    return fs.exists("Resources/forms.BIN") && fs.exists("Gilde.INI");
}

bool ReadCouncilForm(shim::IFileSystem& fs, std::vector<u8>& out) {
    app::RealGameAssets a =
        app::MountRealGameAssets(&fs, GameDir(), "Gilde.INI",
                                 {"Resources/forms.BIN"}, /*caseInsensitive=*/true);
    bool ok = false;
    if (a.vfsBound) {
        io::ArchiveMount* m = a.archiveForMember(kCouncilFormMember);
        if (m) ok = m->OpenMember(kCouncilFormMember, out) && !out.empty();
    }
    io::VfsShutdown();
    return ok;
}

world::OfficePersonRec g_applicant;
i32 g_applicantId = -1;
world::OfficePersonRec* ApplicantFind(i32 id, void*) {
    return (id == g_applicantId) ? &g_applicant : nullptr;
}

void SeedPolitics(u8 holderKey, u8 officeType, i32 applicantId) {
    std::memset(world::g_officeHolders, 0, sizeof(world::g_officeHolders));
    world::OfficeHolder& s = world::g_officeHolders[0];
    s.holder = holderKey; s.city = -1; s.type = officeType; s.rank = 0;
    s.state = 3; s.secondary = -1;
    g_applicant = world::OfficePersonRec{};
    g_applicant.ownerId = applicantId; g_applicant.office360 = 0; g_applicant.valid = true;
    g_applicantId = applicantId;
    static CouncilApplyHooks hooks;
    hooks.find = &ApplicantFind; hooks.ctx = nullptr;
    SetCouncilApplyHooks(&hooks);
}

} // namespace

TEST(DialogCouncilE2E, RealFormApplyForOfficeBumpsRankDeterministic) {
    if (!RealAssetsPresent()) {
        std::printf("[council-dialog-e2e] forms.BIN absent — skipping (set GUILD_GAME_DIR)\n");
        CHECK(true);
        return;
    }

    std::vector<u8> formBytes;
    {
        shim::DiskFileSystem fs(GameDir());
        if (!ReadCouncilForm(fs, formBytes)) {
            std::printf("[council-dialog-e2e] could not read %s — skipping\n",
                        kCouncilFormMember);
            CHECK(true);
            return;
        }
    }

    // The REAL form parses (FRM2).
    CouncilDialog probe = BuildCouncilDialog(formBytes.data(), formBytes.size(),
                                             {}, /*applicant*/0, kCouncilFormMember);
    std::printf("[council-dialog-e2e] form '%s' parsed=%d frm2=%d windows=%d "
                "root=(%d,%d %dx%d) list=(%d,%d %dx%d)\n",
                kCouncilFormMember, (int)probe.formParsed, (int)probe.frm2,
                probe.windowCount, probe.panelX, probe.panelY, probe.panelW,
                probe.panelH, probe.listX, probe.listY, probe.listW, probe.listH);
    CHECK(probe.formParsed);
    CHECK(probe.frm2);
    CHECK_EQ(probe.windowCount, 2);          // the real amtsbewerbung.form: 2 windows
    CHECK(probe.rootWindow >= 0);
    CHECK(probe.listWindow >= 0);

    const u8 kHolder = 3, kOffice = 5;
    const i32 kApplicant = 31337;
    std::vector<OfficeChoice> offices = { {kOffice, kHolder}, {6, 4}, {7, 5} };

    auto runOnce = [&](CouncilDialogResult& out, int& rankBefore, int& rankAfter,
                       int& cand) {
        SeedPolitics(kHolder, kOffice, kApplicant);
        rankBefore = world::g_officeHolders[0].rank;
        out = RunCouncilDialog(formBytes.data(), formBytes.size(), offices,
                               kApplicant, /*clickOffice=*/0, kCouncilFormMember);
        rankAfter = world::g_officeHolders[0].rank;
        cand = g_applicant.office360;
    };

    CouncilDialogResult a; int rb = 0, ra = 0, ca = 0;
    runOnce(a, rb, ra, ca);

    std::printf("[council-dialog-e2e] offices=%d buttonsDrawn=%d nonClear=%d "
                "opcode=%d applied=%d rank %d->%d cand=%d\n",
                (int)a.dialog.offices.size(), a.render.buttonsDrawn,
                a.render.nonClearPixels, a.commandOpcode, (int)a.commandApplied,
                rb, ra, ca);

    // The real form rendered widgets (one button per eligible office) + non-clear px.
    CHECK_EQ((int)a.dialog.offices.size(), 3);
    CHECK_EQ(a.dialog.buttonCount(), 3);
    CHECK_EQ(a.render.buttonsDrawn, 3);
    CHECK(a.render.nonClearPixels > 0);

    // The click hit the office button + emitted the REAL opcode-68 command.
    CHECK(a.click.hitButton);
    CHECK(a.commandBuilt);
    CHECK_EQ(a.commandOpcode, 68);
    CHECK_EQ((int)a.commandOffice, (int)kOffice);
    CHECK(a.commandApplied);

    // Observable world effect: the folded g_officeHolders rank bumped + seated.
    CHECK_EQ(rb, 0);
    CHECK_EQ(ra, 1);
    CHECK_EQ(ca, (int)kOffice);

    // Deterministic: a full rerun reproduces the same render + world effect.
    CouncilDialogResult b; int rb2 = 0, ra2 = 0, ca2 = 0;
    runOnce(b, rb2, ra2, ca2);
    CHECK_EQ(ra2, ra);
    CHECK_EQ(ca2, ca);
    CHECK_EQ(b.render.nonClearPixels, a.render.nonClearPixels);
    CHECK_EQ(b.commandOpcode, a.commandOpcode);

    SetCouncilApplyHooks(nullptr);
    std::printf("[council-dialog-e2e] determinism OK; nonClear=%d rank=%d\n",
                a.render.nonClearPixels, ra);
}
