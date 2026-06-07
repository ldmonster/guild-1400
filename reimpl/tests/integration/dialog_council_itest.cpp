// tests/integration/dialog_council_itest.cpp — Wave 30 PLAY B2: REAL council-dialog
// RENDER + scripted office-button click -> real opcode-68 command.
//
// Renders the apply-for-office dialog (parsed from a synthetic FRM2 form through the
// REAL parser, laid out with one button per eligible office) to a software surface and
// asserts the widgets DRAW (non-clear pixels land on the button rects); then a scripted
// click on an office button routes through slice_council's REAL opcode-68 candidacy
// command (correct office type / applicant) and the REAL apply
// (OfficeAssignToCandidate) bumps the folded g_officeHolders rank. Deterministic.
#include "test.h"

#include "play/dialog_council.h"
#include "render/surface.h"
#include "world/office.h"
#include "world/office_assign.h"
#include "world/law_types.h"

#include <cstring>
#include <vector>

using namespace guild;
using namespace guild::play;

namespace {

// The parchment clear colour dialog_council.cpp uses.
constexpr u8 kCR = 196, kCG = 178, kCB = 128;

// A test-side applicant resolver (models VIBE_Person_FindRecordById for the op-68
// apply). The applicant record the candidacy apply mutates (+360 / valid).
world::OfficePersonRec g_applicant;
i32 g_applicantId = -1;

world::OfficePersonRec* ApplicantFind(i32 id, void*) {
    return (id == g_applicantId) ? &g_applicant : nullptr;
}

// Seat ONE vacant electable council office into g_officeHolders[0] (the seat the
// dialog button applies for) + install the applicant resolver, exactly as the council
// slice seeds the politics state.
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

// Render the dialog: every laid-out widget draws (non-clear pixels at the rects).
TEST(DialogCouncilItest, WidgetsRenderNonClear) {
    std::vector<OfficeChoice> offices = { {5, 3}, {6, 4} };
    CouncilDialog d = BuildSyntheticCouncilDialog(offices, 31337, 8, 56, 352, 448);

    CouncilRenderStats st;
    render::Surface* surf = RenderCouncilDialog(d, 380, 520, st);
    CHECK(surf != nullptr);
    if (!surf) return;

    CHECK_EQ(st.buttonsDrawn, 2);
    CHECK(st.widgetsDrawn >= 4);            // 2 windows + title + 2 buttons
    CHECK(st.nonClearPixels > 0);

    // A pixel inside the first office button is non-clear (the button drew there).
    const CouncilWidgetRect* btn = nullptr;
    for (const auto& w : d.widgets)
        if (w.role == CouncilWidgetRole::kOfficeButton && w.officeIndex == 0) btn = &w;
    CHECK(btn != nullptr);
    if (btn) {
        u8 px[3];
        render::SurfaceGetPixelRgb(surf, btn->x + btn->w / 2, btn->y + btn->h / 2, px);
        bool nonClear = (px[0] != kCR || px[1] != kCG || px[2] != kCB);
        CHECK(nonClear);
    }
    render::SurfaceDestroy(surf);
}

// A scripted office-button click emits the real opcode-68 command (correct fields)
// and the apply bumps the folded g_officeHolders rank.
TEST(DialogCouncilItest, ConfirmClickEmitsCandidacyAndBumpsRank) {
    const u8 kHolder = 3, kOffice = 5;
    const i32 kApplicant = 5151;
    SeedPolitics(kHolder, kOffice, kApplicant);

    std::vector<OfficeChoice> offices = { {kOffice, kHolder}, {6, 4} };

    i32 rankBefore = world::g_officeHolders[0].rank;
    CHECK_EQ(rankBefore, 0);

    std::vector<u8> form = MakeSyntheticCouncilForm(8, 56, 352, 448);
    CouncilDialogResult r = RunCouncilDialog(form.data(), form.size(), offices,
                                             kApplicant, /*clickOffice=*/0);

    // The dialog rendered + the click hit a button.
    CHECK(r.render.nonClearPixels > 0);
    CHECK(r.click.hitButton);
    CHECK_EQ(r.click.officeIndex, 0);

    // The REAL opcode-68 command was built + applied.
    CHECK(r.commandBuilt);
    CHECK_EQ(r.commandOpcode, 68);
    CHECK_EQ((int)r.commandOffice, (int)kOffice);
    CHECK(r.commandApplied);

    // The folded politics field moved: the seat rank bumped, the applicant seated.
    CHECK_EQ((int)world::g_officeHolders[0].rank, rankBefore + 1);
    CHECK_EQ((int)g_applicant.office360, (int)kOffice);

    SetCouncilApplyHooks(nullptr);
}

// Deterministic: the same scripted dialog interaction reproduces the same outcome.
TEST(DialogCouncilItest, Deterministic) {
    const u8 kHolder = 2, kOffice = 7;
    const i32 kApplicant = 9090;
    std::vector<OfficeChoice> offices = { {kOffice, kHolder} };
    std::vector<u8> form = MakeSyntheticCouncilForm(8, 56, 352, 448);

    SeedPolitics(kHolder, kOffice, kApplicant);
    CouncilDialogResult a = RunCouncilDialog(form.data(), form.size(), offices,
                                             kApplicant, 0);
    int rankA = world::g_officeHolders[0].rank;

    SeedPolitics(kHolder, kOffice, kApplicant);
    CouncilDialogResult b = RunCouncilDialog(form.data(), form.size(), offices,
                                             kApplicant, 0);
    int rankB = world::g_officeHolders[0].rank;

    CHECK_EQ(a.commandApplied, b.commandApplied);
    CHECK_EQ(rankA, rankB);
    CHECK_EQ(a.render.nonClearPixels, b.render.nonClearPixels);
    CHECK_EQ(a.commandOpcode, b.commandOpcode);

    SetCouncilApplyHooks(nullptr);
}
