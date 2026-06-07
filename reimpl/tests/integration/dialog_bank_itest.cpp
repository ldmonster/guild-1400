// tests/integration/dialog_bank_itest.cpp — Wave 30 PLAY B2: REAL bank take-loan
// dialog RENDER + scripted confirm click -> real opcode-15 command.
//
// Renders the take-loan dialog (parsed from a synthetic FRM2 form through the REAL
// parser, laid out with the amount slider + one row per lender + confirm/cancel
// buttons) to a software surface and asserts the widgets DRAW (non-clear pixels land
// on the row/button rects); then a scripted confirm click (with a selected lender)
// routes through slice_bank's REAL opcode-15 loan command over a live real-format
// world, moving the borrower's folded cash + debt. Deterministic.
#include "test.h"

#include "play/dialog_bank.h"
#include "play/slice_bank.h"
#include "render/surface.h"
#include "sim/entity.h"
#include "sim/command.h"
#include "world/city.h"

#include <cstring>
#include <vector>

using namespace guild;
using namespace guild::play;

namespace {

// The parchment clear colour dialog_bank.cpp uses.
constexpr u8 kCR = 188, kCG = 170, kCB = 124;

// Seed a small real-format world with a known live borrower (mirrors the bank itest
// seed). Returns the borrower id.
i32 SeedWorld(i16 startCash) {
    std::memset(world::g_goods, 0, sizeof(world::g_goods));
    std::memset(world::g_cities, 0, sizeof(world::g_cities));
    world::g_capDivisor = 0.0f;
    world::g_cityTotalMoney = 0.0f;
    world::g_cityTotalGoods = 0.0f;

    sim::ResetEntityArrays();
    std::memset(sim::g_persons, 0, sizeof(sim::g_persons));
    std::memset(sim::g_personIds, 0, sizeof(sim::g_personIds));

    const i32 kBorrower = 6100;
    for (int i = 0; i < 4; ++i) {
        sim::Person& p = sim::g_persons[i];
        p.marker = 0; p.kind = 5; p.id = 6100 + i;
        sim::g_personIds[i] = 6100 + i;
        i16 c = (i16)(startCash + 10 * i);
        std::memcpy(reinterpret_cast<u8*>(&p) + kBankCashFieldOff, &c, sizeof c);
        i32 zero = 0;
        std::memcpy(reinterpret_cast<u8*>(&p) + kBankDebtFieldOff, &zero, sizeof zero);
    }
    return kBorrower;
}

} // namespace

// Render the dialog: every laid-out widget draws (non-clear pixels at the rects).
TEST(DialogBankItest, WidgetsRenderNonClear) {
    std::vector<LenderChoice> lenders = { {501, 5000}, {502, 8000} };
    BankDialog d = BuildSyntheticBankDialog(lenders, 6100, 1000, 0, 63, 72, 477, 545);

    BankRenderStats st;
    render::Surface* surf = RenderBankDialog(d, 560, 640, st);
    CHECK(surf != nullptr);
    if (!surf) return;

    CHECK_EQ(st.rowsDrawn, 2);
    CHECK_EQ(st.buttonsDrawn, 2);            // confirm + cancel
    CHECK(st.widgetsDrawn >= 6);
    CHECK(st.nonClearPixels > 0);

    // A pixel inside the confirm button is non-clear (it drew there).
    const BankWidgetRect* btn = nullptr;
    for (const auto& w : d.widgets)
        if (w.role == BankWidgetRole::kConfirmBtn) btn = &w;
    CHECK(btn != nullptr);
    if (btn) {
        u8 px[3];
        render::SurfaceGetPixelRgb(surf, btn->x + btn->w / 2, btn->y + btn->h / 2, px);
        bool nonClear = (px[0] != kCR || px[1] != kCG || px[2] != kCB);
        CHECK(nonClear);
    }
    render::SurfaceDestroy(surf);
}

// A scripted confirm click emits the real opcode-15 command (correct fields) and
// applies it, moving the borrower's folded cash + debt.
TEST(DialogBankItest, ConfirmClickEmitsLoanAndMovesCashDebt) {
    SetBankApplyHooks(nullptr);
    i32 id = SeedWorld(/*startCash=*/200);

    std::vector<LenderChoice> lenders = { {501, 5000}, {502, 8000} };
    std::vector<u8> form = MakeSyntheticBankForm(63, 72, 477, 545);

    sim::CommandQueue q; q.Init(); q.set_standalone(true);
    BankDialogResult r = RunBankDialog(form.data(), form.size(), lenders,
                                       /*borrower*/id, /*amount*/1000, /*player*/0,
                                       /*selectedRow*/0, /*econSeed*/0xC0FFEE, q,
                                       /*fbW*/560, /*fbH*/640);

    // The dialog rendered + the click hit confirm.
    CHECK(r.render.nonClearPixels > 0);
    CHECK(r.click.hitConfirm);

    // The REAL opcode-15 command was emitted + applied.
    CHECK(r.commandIssued);
    CHECK(r.slice.command.issued);
    CHECK_EQ((int)r.slice.command.opcode, 15);
    CHECK(r.slice.enqueued);
    CHECK(r.slice.applied);

    // The folded records moved: cash + debt each rose by the loan amount.
    CHECK_EQ(r.slice.cashAfter - r.slice.cashBefore, 1000);
    CHECK_EQ(r.slice.debtAfter - r.slice.debtBefore, 1000);
    CHECK(r.slice.cashMoved());
    CHECK(r.slice.debtMoved());

    // The loan + the game-day each moved the world.
    CHECK(r.slice.loanChangedWorld());
    CHECK(r.slice.dayChangedWorld());
}

// Deterministic: the same scripted dialog interaction reproduces the same outcome.
TEST(DialogBankItest, Deterministic) {
    SetBankApplyHooks(nullptr);
    std::vector<LenderChoice> lenders = { {501, 5000} };
    std::vector<u8> form = MakeSyntheticBankForm(63, 72, 477, 545);

    auto runOnce = [&](BankDialogResult& out) {
        i32 id = SeedWorld(/*startCash=*/200);
        sim::CommandQueue q; q.Init(); q.set_standalone(true);
        out = RunBankDialog(form.data(), form.size(), lenders, id, 1000, 0, 0,
                            0xABCDEF, q, 560, 640);
    };

    BankDialogResult a, b;
    runOnce(a);
    runOnce(b);

    CHECK_EQ((long long)a.slice.hashBefore, (long long)b.slice.hashBefore);
    CHECK_EQ((long long)a.slice.hashAfterCommand, (long long)b.slice.hashAfterCommand);
    CHECK_EQ((long long)a.slice.hashAfterDay, (long long)b.slice.hashAfterDay);
    CHECK_EQ(a.slice.cashAfter, b.slice.cashAfter);
    CHECK_EQ(a.render.nonClearPixels, b.render.nonClearPixels);
}
