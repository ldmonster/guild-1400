// tests/unit/dialog_bank_test.cpp — Wave 30 PLAY B2: REAL bank take-loan dialog
// LAYOUT unit. Loads a SYNTHETIC FRM2 form (root + slider + row-list + button +
// slider, mirroring the real 5-window shape) through the REAL parser
// (Form_ParseResourceFile) and asserts the loan-panel widget LAYOUT the dialog
// builds: window rects, the amount slider, one lender row per lender, confirm/cancel
// buttons, and that a confirm click (with a selected lender row) maps to the right
// loan command intent (kTake / borrower / amount / selected lender).
#include "test.h"

#include "play/dialog_bank.h"

#include <vector>

using namespace guild;
using namespace guild::play;

// The real parser builds the synthetic form's five windows; the dialog classifies
// the root / row / button / slider windows.
TEST(DialogBankUnit, RealParserBuildsFiveWindows) {
    std::vector<LenderChoice> lenders = { {501, 5000}, {502, 8000} };
    BankDialog d = BuildSyntheticBankDialog(lenders, /*borrower*/71001, /*amount*/1000,
                                            /*player*/0, /*x*/63, /*y*/72,
                                            /*w*/477, /*h*/545);
    CHECK(d.formParsed);
    CHECK(d.frm2);
    CHECK(d.formId >= 0);
    CHECK_EQ(d.windowCount, 5);             // root + 2 sliders + row list + button
    CHECK(d.rootWindow >= 0);
    CHECK(d.rowWindow >= 0);
    CHECK(d.buttonWindow >= 0);
    CHECK(d.sliderWindow >= 0);
    CHECK_EQ(d.panelX, 63);
    CHECK_EQ(d.panelY, 72);
    CHECK_EQ(d.panelW, 477);
    CHECK_EQ(d.panelH, 545);

    int windowRects = 0;
    for (const auto& w : d.widgets)
        if (w.role == BankWidgetRole::kWindow) ++windowRects;
    CHECK_EQ(windowRects, 5);
}

// One lender row per lender + an amount slider + confirm/cancel buttons are laid out.
TEST(DialogBankUnit, RowsSliderAndButtonsLaidOut) {
    std::vector<LenderChoice> lenders = { {501, 5000}, {502, 8000}, {503, 3000} };
    BankDialog d = BuildSyntheticBankDialog(lenders, 71001, 1000, 0, 63, 72, 477, 545);

    CHECK_EQ(d.rowCount(), 3);
    CHECK_EQ((int)d.lenders.size(), 3);
    CHECK_EQ(d.buttonCount(), 2);            // confirm + cancel

    // Rows are stacked downward inside the real row-list window, carrying lender ids.
    int prevY = -1;
    for (const auto& w : d.widgets) {
        if (w.role != BankWidgetRole::kLenderRow) continue;
        CHECK_EQ(w.lenderId, lenders[w.lenderIndex].lenderId);
        CHECK_EQ(w.h, 16);                   // the REAL Window_AddChildWindow row height
        CHECK(w.y > prevY);
        prevY = w.y;
    }

    int sliders = 0, confirm = 0, cancel = 0;
    for (const auto& w : d.widgets) {
        if (w.role == BankWidgetRole::kAmountSlider) ++sliders;
        if (w.role == BankWidgetRole::kConfirmBtn)   ++confirm;
        if (w.role == BankWidgetRole::kCancelBtn)    ++cancel;
    }
    CHECK_EQ(sliders, 1);
    CHECK_EQ(confirm, 1);
    CHECK_EQ(cancel, 1);
}

// A confirm click (with a selected lender row) maps to the right loan intent, and the
// REAL slice classifier turns that into an opcode-15 loan command.
TEST(DialogBankUnit, ConfirmClickMapsToLoanIntent) {
    std::vector<LenderChoice> lenders = { {501, 5000}, {502, 8000} };
    const i32 kBorrower = 71001, kAmount = 1500;
    BankDialog d = BuildSyntheticBankDialog(lenders, kBorrower, kAmount, 0, 63, 72, 477, 545);

    const BankWidgetRect* btn = nullptr;
    for (const auto& w : d.widgets)
        if (w.role == BankWidgetRole::kConfirmBtn) btn = &w;
    CHECK(btn != nullptr);
    if (!btn) return;

    // Select lender row 1 and click confirm.
    BankDialogClick c = ClickBankDialog(d, btn->x + btn->w / 2, btn->y + btn->h / 2,
                                        /*selectedRow*/1);
    CHECK(c.hitConfirm);
    CHECK(c.interaction.side == LoanSide::kTake);
    CHECK_EQ(c.interaction.borrowerId, kBorrower);
    CHECK_EQ(c.interaction.amount, kAmount);
    CHECK_EQ(c.interaction.lenderId, 502);    // lenders[1].lenderId

    // The intent feeds the REAL bank classifier -> an opcode-15 loan command.
    LoanCommand cmd = ClassifyBankInteraction(c.interaction);
    CHECK(cmd.issued);
    CHECK_EQ((int)cmd.opcode, 15);
    CHECK(cmd.side == LoanSide::kTake);
    CHECK_EQ(cmd.borrower, kBorrower);
    CHECK_EQ(cmd.amount, kAmount);
}

// A click that lands off the confirm button yields no loan intent.
TEST(DialogBankUnit, ClickOffConfirmYieldsNone) {
    std::vector<LenderChoice> lenders = { {501, 5000} };
    BankDialog d = BuildSyntheticBankDialog(lenders, 71001, 1000, 0, 63, 72, 477, 545);
    BankDialogClick c = ClickBankDialog(d, d.panelX + 1, d.panelY + 1, 0);
    CHECK(!c.hitConfirm);
    CHECK(c.interaction.side == LoanSide::kNone);
}
