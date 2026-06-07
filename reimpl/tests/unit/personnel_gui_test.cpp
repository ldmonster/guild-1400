// Unit tests for guild::gui personnel_gui — the deterministic decision/layout
// kernels behind the staff-book / recruitment dialogs. Golden vectors computed
// with python3 against the recovered formulas.
#include "test.h"

#include "gui/personnel_gui.h"

using namespace guild;
using namespace guild::gui;

// --- PayWorkerClampDisplay (0x53b515..0x53b535): clamp 4800, signed >>5 ---
TEST(PersonnelGui, PayWorkerClampDisplay) {
    CHECK_EQ(PayWorkerClampDisplay(0), 0);
    CHECK_EQ(PayWorkerClampDisplay(31), 0);
    CHECK_EQ(PayWorkerClampDisplay(32), 1);
    CHECK_EQ(PayWorkerClampDisplay(100), 3);
    CHECK_EQ(PayWorkerClampDisplay(160), 5);
    CHECK_EQ(PayWorkerClampDisplay(4799), 149);
    CHECK_EQ(PayWorkerClampDisplay(4800), 150);
    CHECK_EQ(PayWorkerClampDisplay(5000), 150);   // clamped first
}

// --- RowLabelDecide (0x53bad6..): 3-way row-label state machine ---
TEST(PersonnelGui, RowLabelDecide) {
    // handler matched + has item -> HandlerText (2), regardless of person
    CHECK(RowLabelDecide(true, true, true, true) == RowLabelState::HandlerText);
    CHECK(RowLabelDecide(true, true, false, false) == RowLabelState::HandlerText);
    // handler matched but no item -> falls through to person
    CHECK(RowLabelDecide(true, false, true, true) == RowLabelState::ItemName);
    // no handler, person present with item -> ItemName (1)
    CHECK(RowLabelDecide(false, false, true, true) == RowLabelState::ItemName);
    // person present but no item flag -> Empty (0)
    CHECK(RowLabelDecide(false, false, true, false) == RowLabelState::Empty);
    // no handler, no person -> Empty (0)
    CHECK(RowLabelDecide(false, false, false, false) == RowLabelState::Empty);
    CHECK_EQ(static_cast<int>(RowLabelState::Empty), 0);
    CHECK_EQ(static_cast<int>(RowLabelState::ItemName), 1);
    CHECK_EQ(static_cast<int>(RowLabelState::HandlerText), 2);
}

// --- CandidateGridCellPos (0x55dc28..): 3-col card grid (panelW=300, cardW=80) ---
TEST(PersonnelGui, CandidateGridCellPos) {
    GridCell c0 = CandidateGridCellPos(0, 300, 80);
    CHECK_EQ(c0.x, 5);   CHECK_EQ(c0.y, 5);
    GridCell c1 = CandidateGridCellPos(1, 300, 80);
    CHECK_EQ(c1.x, 110); CHECK_EQ(c1.y, 5);
    GridCell c2 = CandidateGridCellPos(2, 300, 80);
    CHECK_EQ(c2.x, 215); CHECK_EQ(c2.y, 5);
    GridCell c3 = CandidateGridCellPos(3, 300, 80);
    CHECK_EQ(c3.x, 5);   CHECK_EQ(c3.y, 135);  // new row
    GridCell c4 = CandidateGridCellPos(4, 300, 80);
    CHECK_EQ(c4.x, 110); CHECK_EQ(c4.y, 135);
    GridCell c5 = CandidateGridCellPos(5, 300, 80);
    CHECK_EQ(c5.x, 215); CHECK_EQ(c5.y, 135);
}

// --- RecruitOfferComputeMode (0x55de4d..0x55de7e) ---
TEST(PersonnelGui, RecruitOfferComputeMode) {
    CHECK_EQ(RecruitOfferComputeMode(false, 0, 0), 1);  // no handler
    CHECK_EQ(RecruitOfferComputeMode(true, 0, 0), 2);   // default
    CHECK_EQ(RecruitOfferComputeMode(true, 1, 0), 3);   // byte187 rejected
    CHECK_EQ(RecruitOfferComputeMode(true, 0, 1), 4);   // byte186 pending
    CHECK_EQ(RecruitOfferComputeMode(true, 1, 1), 4);   // 186 wins (checked last)
}

// --- RecruitOfferBonusRoll (0x55e263..0x55e39d): deterministic RNG stub ---
namespace {
// A fully deterministic "rng": returns floor(n/2) for each call so the brackets are
// pinned. rank<=0: count=floor(3/2)+1=2, idx=floor(5/2)=2.
int FixedRng(int n) { return n / 2; }
}
TEST(PersonnelGui, RecruitOfferBonusRoll) {
    OfferBonus b0 = RecruitOfferBonusRoll(0, FixedRng);
    CHECK_EQ(b0.count, 2);       // rand(3)+1 = 1+1
    CHECK_EQ(b0.strIndex, 2);    // rand(5) = 2

    OfferBonus bn = RecruitOfferBonusRoll(-5, FixedRng);
    CHECK_EQ(bn.count, 2);
    CHECK_EQ(bn.strIndex, 2);

    OfferBonus b1 = RecruitOfferBonusRoll(1, FixedRng);
    CHECK_EQ(b1.count, 1);       // clamp(1)
    CHECK_EQ(b1.strIndex, 2 + 5);  // rand(5)+5

    OfferBonus b2 = RecruitOfferBonusRoll(2, FixedRng);
    CHECK_EQ(b2.count, 2);
    CHECK_EQ(b2.strIndex, 2 + 9);  // rand(4)+9 = floor(4/2)=2 +9

    OfferBonus b3 = RecruitOfferBonusRoll(3, FixedRng);
    CHECK_EQ(b3.count, 3);
    CHECK_EQ(b3.strIndex, 1 + 12);  // rand(3)+12 = floor(3/2)=1 +12

    OfferBonus b9 = RecruitOfferBonusRoll(9, FixedRng);
    CHECK_EQ(b9.count, 3);          // clamp to 3
    CHECK_EQ(b9.strIndex, 1 + 12);  // rank>=3 branch
}

// --- RecruitOfferFindSlotIndex (0x55e029..) ---
TEST(PersonnelGui, RecruitOfferFindSlotIndex) {
    const int ids[4] = { 10, 20, 30, 40 };
    CHECK_EQ(RecruitOfferFindSlotIndex(10, ids), 0);
    CHECK_EQ(RecruitOfferFindSlotIndex(20, ids), 1);
    CHECK_EQ(RecruitOfferFindSlotIndex(30, ids), 2);
    CHECK_EQ(RecruitOfferFindSlotIndex(40, ids), 3);
    CHECK_EQ(RecruitOfferFindSlotIndex(99, ids), 4);  // no match -> terminal index
    // duplicate first id is found at slot 0 (loop body never advances).
    const int dup[4] = { 7, 7, 7, 7 };
    CHECK_EQ(RecruitOfferFindSlotIndex(7, dup), 0);
}

// --- StaffBookBuildingRowLayout (0x53c225..) ---
TEST(PersonnelGui, StaffBookBuildingRowLayout) {
    StaffRowLayout r0 = StaffBookBuildingRowLayout(0);
    CHECK_EQ(r0.yA, 10); CHECK_EQ(r0.yB, 8); CHECK_EQ(r0.yC, 72);
    StaffRowLayout r1 = StaffBookBuildingRowLayout(1);
    CHECK_EQ(r1.yA, 120); CHECK_EQ(r1.yB, 118); CHECK_EQ(r1.yC, 192);
    StaffRowLayout r2 = StaffBookBuildingRowLayout(2);
    CHECK_EQ(r2.yA, 230); CHECK_EQ(r2.yB, 228); CHECK_EQ(r2.yC, 312);
    StaffRowLayout r3 = StaffBookBuildingRowLayout(3);
    CHECK_EQ(r3.yA, 340); CHECK_EQ(r3.yB, 338); CHECK_EQ(r3.yC, 432);
}
