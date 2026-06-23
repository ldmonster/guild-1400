// amt_recon_office_window_test.cpp — golden-vector unit tests for the pure rules of
// the political-office (Amt) window cluster (gilde.exe 0x556c40 / 0x5575c8 / 0x555eb4
// / 0x556ba0 / 0x480cb4 / 0x5546a0). Self-contained.
#include "tests/framework/test.h"
#include "world/amt_recon_office_window.h"

using namespace guild;
using namespace guild::world;

// --- static tables -----------------------------------------------------------
TEST(AmtReconWindow, RoleTableBytes) {
    CHECK_EQ(kAmtCandidateRoleCount, 8);   // dword_5526AC
    CHECK_EQ(kAmtRoleTableRows, 5);
    CHECK_EQ(kAmtRoleTable[0].collectorAddr, 0x554e34u);
    CHECK_EQ(kAmtRoleTable[0].rendererAddr,  0x555f8cu);
    CHECK_EQ(kAmtRoleTable[0].listIndex,     1);
    CHECK_EQ(kAmtRoleTable[4].rendererAddr,  0x5564acu);
    CHECK_EQ(kAmtRoleTable[4].listIndex,     5);
    // listIndex is 1..5 in order.
    for (int i = 0; i < kAmtRoleTableRows; ++i)
        CHECK_EQ(kAmtRoleTable[i].listIndex, i + 1);
}

TEST(AmtReconWindow, OverviewSlotTableBytes) {
    const u8 expect[8] = {0x00, 0x06, 0x04, 0x05, 0x01, 0x02, 0x03, 0x07};
    for (int i = 0; i < 8; ++i)
        CHECK_EQ(kAmtOverviewSlotOfficeType[i], expect[i]);
}

// --- PrepareCandidatePage -----------------------------------------------------
TEST(AmtReconWindow, PreparePageNoScroll) {
    auto r = OfficePrepareCandidatePage(2, -1);
    CHECK(!r.builtScroll);
    CHECK_EQ(r.returnValue, 0);
    CHECK_EQ(r.primaryWin, 2);
}
TEST(AmtReconWindow, PreparePageWithScroll) {
    auto r = OfficePrepareCandidatePage(2, 4);
    CHECK(r.builtScroll);
    CHECK_EQ(r.returnValue, 0);
    CHECK_EQ(r.scrollWin, 4);
}

// --- ApplyCandidateRatingBars -------------------------------------------------
TEST(AmtReconWindow, RatingBarBothNobilityPredicate) {
    CHECK(RatingBarBothNobility(6, 7));
    CHECK(RatingBarBothNobility(7, 6));
    CHECK(RatingBarBothNobility(6, 6));
    CHECK(!RatingBarBothNobility(6, 5)); // entry not in {6,7}
    CHECK(!RatingBarBothNobility(5, 7)); // head not in {6,7}
    CHECK(!RatingBarBothNobility(1, 2));
}
TEST(AmtReconWindow, RatingBarRowMode) {
    CHECK(RatingBarRowMode(false, true, 6, 7) == RatingBarMode::Skip);
    CHECK(RatingBarRowMode(true, false, 6, 7) == RatingBarMode::Skip);
    CHECK(RatingBarRowMode(true, true, 6, 7) == RatingBarMode::MaxValue);
    CHECK(RatingBarRowMode(true, true, 6, 5) == RatingBarMode::Favorability);
    CHECK(RatingBarRowMode(true, true, 1, 2) == RatingBarMode::Favorability);
}

// --- index clamp --------------------------------------------------------------
TEST(AmtReconWindow, ClampStartIndex) {
    // requested in range overrides current.
    CHECK_EQ(AmtClampStartIndex(3, 5, 8), 5);
    // requested -1 (the OpenOfficeWindow default) keeps current.
    CHECK_EQ(AmtClampStartIndex(3, -1, 8), 3);
    // requested >= count ignored, current kept (then clamp passes).
    CHECK_EQ(AmtClampStartIndex(2, 99, 8), 2);
    // current out of range clamps to 0.
    CHECK_EQ(AmtClampStartIndex(8, -1, 8), 0);
    CHECK_EQ(AmtClampStartIndex(-1, -1, 8), 0); // unsigned compare: huge -> >=count
}

// --- find-first-non-empty role ------------------------------------------------
TEST(AmtReconWindow, FindFirstNonEmptyRole) {
    // rowCounts indexed at 5*i.
    i32 rc[5 * 8] = {0};
    // current role non-empty -> returns current unchanged.
    rc[5 * 2] = 3;
    CHECK_EQ(AmtFindFirstNonEmptyRole(2, rc, 8), 2);

    // current empty, first non-empty is role 4.
    i32 rc2[5 * 8] = {0};
    rc2[5 * 4] = 1;
    CHECK_EQ(AmtFindFirstNonEmptyRole(0, rc2, 8), 4);

    // current empty, none non-empty -> parked at count (8).
    i32 rc3[5 * 8] = {0};
    CHECK_EQ(AmtFindFirstNonEmptyRole(0, rc3, 8), 8);

    // current empty but role 0 non-empty -> 0.
    i32 rc4[5 * 8] = {0};
    rc4[0] = 2;
    CHECK_EQ(AmtFindFirstNonEmptyRole(3, rc4, 8), 0);
}

// --- frame-top role select ----------------------------------------------------
TEST(AmtReconWindow, SelectRoleAtFrameTop) {
    i32 rc[5 * 8] = {0};
    rc[5 * 3] = 6;
    auto s = AmtSelectRoleAtFrameTop(3, rc, 8);
    CHECK(s.inRange);
    CHECK_EQ(s.index, 3);
    CHECK_EQ(s.selectedCount, 6);

    auto s2 = AmtSelectRoleAtFrameTop(8, rc, 8); // out of range
    CHECK(!s2.inRange);
    CHECK_EQ(s2.index, 0);
    CHECK_EQ(s2.selectedCount, -1);
}

// --- role-button vertical layout (golden vectors) -----------------------------
TEST(AmtReconWindow, RoleButtonSpan) {
    CHECK_EQ(AmtRoleButtonSpan(320, 8), 320 + 1 - 33 * 8); // 57
    CHECK_EQ(AmtRoleButtonSpan(500, 8), 237);
}
TEST(AmtReconWindow, RoleButtonYsH320Count8) {
    i32 ys[8];
    AmtComputeRoleButtonYs(320, 8, ys);
    const i32 expect[8] = {0, 42, 83, 124, 165, 206, 247, 288};
    for (int i = 0; i < 8; ++i) CHECK_EQ(ys[i], expect[i]);
}
TEST(AmtReconWindow, RoleButtonYsH500Count8) {
    i32 ys[8];
    AmtComputeRoleButtonYs(500, 8, ys);
    const i32 expect[8] = {0, 67, 134, 201, 268, 335, 402, 468};
    for (int i = 0; i < 8; ++i) CHECK_EQ(ys[i], expect[i]);
}
TEST(AmtReconWindow, RoleButtonYsH300Count4) {
    i32 ys[4];
    AmtComputeRoleButtonYs(300, 4, ys);
    const i32 expect[4] = {0, 90, 179, 268};
    for (int i = 0; i < 4; ++i) CHECK_EQ(ys[i], expect[i]);
}
TEST(AmtReconWindow, RoleButtonYsNegativeSpan) {
    // span goes negative (tiny window): trunc div, no remainder bump.
    i32 ys[8];
    AmtComputeRoleButtonYs(100, 8, ys);
    const i32 expect[8] = {0, 10, 20, 30, 40, 50, 60, 70};
    for (int i = 0; i < 8; ++i) CHECK_EQ(ys[i], expect[i]);
}

// --- overview layout / slot find ----------------------------------------------
TEST(AmtReconWindow, OverviewAltLayout) {
    CHECK(AmtOverviewSlotUsesAltLayout(0));   // top
    CHECK(AmtOverviewSlotUsesAltLayout(7));   // bottom
    CHECK(!AmtOverviewSlotUsesAltLayout(1));
    CHECK(!AmtOverviewSlotUsesAltLayout(6));
    CHECK(AmtOverviewSlotUsesAltLayout(8));   // >=7
}
TEST(AmtReconWindow, OverviewApplySlotState) {
    auto on = AmtOverviewApplySlotState(true);
    CHECK_EQ(on.enabledFlag, 1);
    CHECK_EQ(on.styleByte, 3);
    auto off = AmtOverviewApplySlotState(false);
    CHECK_EQ(off.enabledFlag, 0);
    CHECK_EQ(off.styleByte, 0);
}
TEST(AmtReconWindow, OverviewFindClickedSlot) {
    i32 objs[8] = {100, 101, 102, 103, 104, 105, 106, 107};
    CHECK_EQ(AmtOverviewFindClickedSlot(objs, 8, 104), 4);
    CHECK_EQ(AmtOverviewFindClickedSlot(objs, 8, 100), 0);
    CHECK_EQ(AmtOverviewFindClickedSlot(objs, 8, 107), 7);
    CHECK_EQ(AmtOverviewFindClickedSlot(objs, 8, 999), 8); // not found
}

// --- HasOccupiedOffice --------------------------------------------------------
TEST(AmtReconWindow, OfficeSlotOccupiedPredicate) {
    CHECK(AmtOfficeSlotOccupied({7, 42, true}));
    CHECK(!AmtOfficeSlotOccupied({3, 42, true}));   // wrong type byte
    CHECK(!AmtOfficeSlotOccupied({7, -1, true}));   // vacant
    CHECK(!AmtOfficeSlotOccupied({7, 42, false}));  // holder doesn't resolve
}
TEST(AmtReconWindow, HasOccupiedOfficeScan) {
    OfficeOccupancySlot none[3] = {{3, 1, true}, {7, -1, true}, {7, 5, false}};
    CHECK_EQ(AmtHasOccupiedOffice(none, 3), 0);

    OfficeOccupancySlot some[3] = {{3, 1, true}, {7, 5, true}, {7, 9, true}};
    CHECK_EQ(AmtHasOccupiedOffice(some, 3), 1);

    CHECK_EQ(AmtHasOccupiedOffice(none, 0), 0); // empty
}

// --- OpenOfficeWindow descriptor ----------------------------------------------
TEST(AmtReconWindow, OpenOfficeDescriptor) {
    auto d = AmtBuildOpenOfficeDescriptor();
    CHECK_EQ(d.word1, 516);
    CHECK_EQ(d.categoryByte, 6);
}
