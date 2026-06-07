// tests/unit/dialog_council_test.cpp — Wave 30 PLAY B2: REAL council-dialog LAYOUT
// unit. Loads a SYNTHETIC FRM2 form (root + inner list window) through the REAL
// parser (Form_ParseResourceFile) and asserts the office-apply widget LAYOUT the
// dialog builds: root + list window rects, one office button per eligible office, and
// that a button id maps to the correct command intent (kApplyCandidacy with the
// office's type / holder key / applicant).
#include "test.h"

#include "play/dialog_council.h"

#include <vector>

using namespace guild;
using namespace guild::play;

// The real parser builds the synthetic form's two windows; the dialog records them.
TEST(DialogCouncilUnit, RealParserBuildsRootAndListWindow) {
    std::vector<OfficeChoice> offices = { {5, 3}, {6, 4} };
    CouncilDialog d = BuildSyntheticCouncilDialog(offices, /*applicant*/31337,
                                                  /*x*/8, /*y*/56, /*w*/352, /*h*/448);
    CHECK(d.formParsed);
    CHECK(d.frm2);
    CHECK(d.formId >= 0);
    CHECK_EQ(d.windowCount, 2);            // root + list window
    CHECK(d.rootWindow >= 0);
    CHECK(d.listWindow >= 0);
    CHECK(d.rootWindow != d.listWindow);
    CHECK_EQ(d.panelX, 8);
    CHECK_EQ(d.panelY, 56);
    CHECK_EQ(d.panelW, 352);
    CHECK_EQ(d.panelH, 448);
    // The inner list window is inset inside the root (the apply panel column).
    CHECK(d.listX > d.panelX);
    CHECK(d.listY > d.panelY);

    int windowRects = 0;
    for (const auto& w : d.widgets)
        if (w.role == CouncilWidgetRole::kWindow) ++windowRects;
    CHECK_EQ(windowRects, 2);
}

// One office button is laid out per eligible office, stacked down the list window.
TEST(DialogCouncilUnit, OneButtonPerEligibleOffice) {
    std::vector<OfficeChoice> offices = { {5, 3}, {6, 4}, {7, 5} };
    CouncilDialog d = BuildSyntheticCouncilDialog(offices, 31337, 8, 56, 352, 448);

    CHECK_EQ(d.buttonCount(), 3);
    CHECK_EQ((int)d.offices.size(), 3);

    // Buttons carry the office type they apply for, in order, inside the list rect.
    int seen = 0;
    int prevY = -1;
    for (const auto& w : d.widgets) {
        if (w.role != CouncilWidgetRole::kOfficeButton) continue;
        CHECK_EQ((int)w.officeType, (int)offices[w.officeIndex].officeType);
        CHECK(w.x >= d.listX);
        CHECK(w.y > prevY);             // stacked downward
        prevY = w.y;
        ++seen;
    }
    CHECK_EQ(seen, 3);

    // A title line is present (Text_RenderRichString 0x1610).
    int titles = 0;
    for (const auto& w : d.widgets)
        if (w.role == CouncilWidgetRole::kTitle) ++titles;
    CHECK_EQ(titles, 1);
}

// A button click maps to the right command intent (office type + holder + applicant).
TEST(DialogCouncilUnit, ButtonClickMapsToCandidacyIntent) {
    std::vector<OfficeChoice> offices = { {5, 3}, {6, 4} };
    const i32 kApplicant = 4242;
    CouncilDialog d = BuildSyntheticCouncilDialog(offices, kApplicant, 8, 56, 352, 448);

    // Locate the 2nd office button and click its centre.
    const CouncilWidgetRect* btn = nullptr;
    for (const auto& w : d.widgets)
        if (w.role == CouncilWidgetRole::kOfficeButton && w.officeIndex == 1) btn = &w;
    CHECK(btn != nullptr);
    if (!btn) return;

    CouncilDialogClick c = ClickCouncilDialog(d, btn->x + btn->w / 2, btn->y + btn->h / 2);
    CHECK(c.hitButton);
    CHECK_EQ(c.officeIndex, 1);
    CHECK(c.interaction.action == CouncilAction::kApplyCandidacy);
    CHECK_EQ((int)c.interaction.officeType, 6);     // offices[1].officeType
    CHECK_EQ((int)c.interaction.holderKey, 4);      // offices[1].holderKey
    CHECK_EQ((int)c.interaction.applicantId, (int)kApplicant);

    // The intent feeds the REAL council command builder -> an opcode-68 packet.
    CouncilPacket pkt = BuildCouncilPacket(c.interaction);
    CHECK(pkt.built);
    CHECK_EQ((int)pkt.opcode, 68);
    CHECK_EQ((int)pkt.officeType, 6);
    CHECK_EQ(pkt.applicant, (i32)kApplicant);
}

// A click that lands on no button yields no candidacy intent.
TEST(DialogCouncilUnit, ClickOffButtonsYieldsNone) {
    std::vector<OfficeChoice> offices = { {5, 3} };
    CouncilDialog d = BuildSyntheticCouncilDialog(offices, 7, 8, 56, 352, 448);
    CouncilDialogClick c = ClickCouncilDialog(d, d.panelX + 1, d.panelY + 1);
    CHECK(!c.hitButton);
    CHECK(c.interaction.action == CouncilAction::kNone);
}
