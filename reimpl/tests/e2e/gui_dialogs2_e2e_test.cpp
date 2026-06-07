// End-to-end flows across the second batch of game dialog/window builders.
//   1. Open a FEAST: course menu -> table -> confirm dispatches a (mocked) HoldFeast.
//   2. Open a VIOLATION report: build the fee range, confirm dispatches a (mocked) Report.
//   3. Open the FAMILY-TREE window for a synthetic family: verify the node tree + layout
//      coords, then click a child node and confirm it resolves to that person (recentre).
#include "gui/feast_dialog.h"
#include "gui/violation_dialog.h"
#include "gui/stammbaum_window.h"
#include "tests/framework/test.h"

#include <cstring>

using namespace guild::gui;

// --- mock command sinks ----------------------------------------------------
namespace {
struct E2EFeastSink : FeastCommandSink {
    int held = 0, invites = 0, lastCourse = -1, lastDrink = -1, lastGuests = 0;
    void HoldFeast(int, int c, int d, int g) override {
        ++held; lastCourse = c; lastDrink = d; lastGuests = g;
    }
    void InviteGuest(int, int) override { ++invites; }
};
struct E2EVioSink : ViolationCommandSink {
    int reports = 0, fee = 0, target = 0;
    void Report(int t, int, int f, int) override { ++reports; target = t; fee = f; }
};
} // namespace

TEST(GuiDlg2E2E, FeastInviteFlow) {
    E2EFeastSink sink; FeastDialog_SetCommandSink(&sink);

    // Screen A: pick a main course.
    FeastMenuLayout course = FeastDialog_BuildCourseMenu();
    CHECK(std::strcmp(course.form, kFormFeast) == 0);
    int chosenCourse = FeastDialog_DispatchMenu(course, course.childIds[1]);
    CHECK_EQ(chosenCourse, 1);

    // No wine cellar -> drink menu skipped, drink fixed to "none".
    FeastState s{};
    s.building = 77;
    s.course = chosenCourse;
    s.drink  = kFeastNoChoice;
    CHECK(FeastDialog_BuildDrinkMenu(s).form == nullptr);

    // Screen C: two guests at the table.
    s.guestEntities[0] = 500;
    s.guestEntities[1] = 501;
    s.guestRanks[1] = 7; // VIP guest
    FeastTableLayout table = FeastDialog_BuildTable(s);
    CHECK_EQ(table.guestCount, 2);
    CHECK(!table.confirmDisabled);

    // Confirm the feast.
    int r = FeastDialog_DispatchTable(table, s, table.confirmObj);
    CHECK_EQ(r, -3);
    CHECK_EQ(sink.held, 1);
    CHECK_EQ(sink.lastCourse, 1);
    CHECK_EQ(sink.lastGuests, 2);
    CHECK_EQ(sink.invites, 2);
}

TEST(GuiDlg2E2E, ViolationReportFlow) {
    E2EVioSink sink; ViolationDialog_SetCommandSink(&sink);

    ViolationState s{};
    s.targetEntity = 314;
    s.building = 9;
    s.wealthSelf = 500000; s.wealthTarget = 500000; // wealth 1,000,000
    s.currencyHeld = 100000; // affordability cap (above the computed fee)
    s.hasResources = true;

    ViolationLayout l = ViolationDialog_Build(s);
    CHECK(std::strcmp(l.form, kFormViolation) == 0);
    // fee0 = 1,000,000 * 0.0007843137 = 784.31 -> 784 (below cap, not clamped).
    CHECK_EQ(l.feeDefault, 784);

    // Player confirms at the default fee.
    bool ended = ViolationDialog_Dispatch(l, s, l.confirmObj, l.feeDefault);
    CHECK(ended);
    CHECK_EQ(sink.reports, 1);
    CHECK_EQ(sink.target, 314);
    CHECK_EQ(sink.fee, 784);
}

TEST(GuiDlg2E2E, FamilyTreeWindowAndRecentre) {
    // A synthetic three-generation family rendered into the tree window.
    Family fam{};
    fam.focus.entity  = 1000;
    fam.spouse.entity = 1001;
    fam.father.entity = 900;
    fam.mother.entity = 901;
    fam.children = { {1100, 0}, {1101, 0}, {1102, 0} }; // 3 children (odd)

    // Canvas 600 px wide, node sprite 50 px -> center 300, half 25.
    FamilyTreeLayout l = Stammbaum_BuildLayout(fam, /*w=*/600, /*nw=*/50, /*title=*/0);

    // Widget tree: focus + spouse + 2 parents + 3 children.
    CHECK(std::strcmp(l.form, kFormStammbaum) == 0);
    CHECK_EQ(l.center, 300);
    CHECK_EQ(l.self.entity, 1000);
    CHECK_EQ(l.self.x, 300 - (50 + 90)); // 160
    CHECK(!l.spouse.empty);
    CHECK_EQ(l.spouse.x, 300 + 90);      // 390
    CHECK_EQ((int)l.parents.size(), 2);
    CHECK_EQ(l.parents[0].x, 300 - (50 + 16)); // 234
    CHECK_EQ(l.parents[1].x, 300 + 16);        // 316
    CHECK_EQ((int)l.children.size(), 3);
    // odd children anchors: c0 = center-25 = 275; c1 = 50+275+40 = 365; c2 = 275-40-50 = 185.
    CHECK_EQ(l.children[0].x, 275);
    CHECK_EQ(l.children[1].x, 365);
    CHECK_EQ(l.children[2].x, 185);

    // Each node carries its person and a distinct clickable object.
    CHECK(l.children[0].objectId != l.children[1].objectId);
    CHECK(l.self.objectId != l.spouse.objectId);

    // A click on a child node resolves to that person (the tree would recentre on them).
    int clicked = Stammbaum_DispatchClick(l, l.children[2].objectId);
    CHECK_EQ(clicked, 1102);
    // A click on a parent resolves to that parent.
    CHECK_EQ(Stammbaum_DispatchClick(l, l.parents[1].objectId), 901);
    // A click somewhere empty resolves to nobody.
    CHECK_EQ(Stammbaum_DispatchClick(l, -999), -1);
}
