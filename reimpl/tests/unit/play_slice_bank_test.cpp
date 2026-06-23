// tests/unit/play_slice_bank_test.cpp — UNIT: the BANK/LOAN slice classifier +
// apply, on a SYNTHETIC live world (no assets).
//
//   * ClassifyBankInteraction (the golden): (side, amount) -> the opcode-15 loan
//     command (VIBE_Command_EnqueueCmd15).
//   * The apply mutates the borrower's folded cash (Person +0x0A) and debt
//     accumulator (Person +0x2C) as expected for a take vs a repay.
//   * The full slice is deterministic (same inputs -> identical world hashes).
#include "test.h"

#include "play/slice_bank.h"
#include "sim/entity.h"
#include "sim/command.h"
#include "world/city.h"

#include <cstring>

using namespace guild;
using namespace guild::play;

namespace {

// Seed one live borrower Person in g_persons with a starting cash, blanking the
// folded economy tables so the run is reproducible in-process. Returns the id.
i32 SeedBankWorld(i16 startCash) {
    std::memset(world::g_goods, 0, sizeof(world::g_goods));
    std::memset(world::g_cities, 0, sizeof(world::g_cities));
    world::g_capDivisor = 0.0f;
    world::g_cityTotalMoney = 0.0f;
    world::g_cityTotalGoods = 0.0f;

    sim::ResetEntityArrays();
    std::memset(sim::g_persons, 0, sizeof(sim::g_persons));   // clear pad bytes too
    std::memset(sim::g_personIds, 0, sizeof(sim::g_personIds));

    const i32 kBorrower = 4242;
    sim::Person& p = sim::g_persons[3];
    p.marker = 0;                 // live slot (not -1)
    p.kind   = 5;                 // a family head (GetFamilyRecord-eligible kind)
    p.id     = kBorrower;
    sim::g_personIds[3] = kBorrower;
    std::memcpy(reinterpret_cast<u8*>(&p) + kBankCashFieldOff, &startCash, sizeof startCash);
    i32 zeroDebt = 0;
    std::memcpy(reinterpret_cast<u8*>(&p) + kBankDebtFieldOff, &zeroDebt, sizeof zeroDebt);
    return kBorrower;
}

} // namespace

// --- golden: pin the EnqueueCmd15 (@0x494604) packet-staging byte offsets + the
// borrower-Person folded field offsets. These recovered constants drive the on-wire
// loan packet layout and the apply's cash/debt mutation. Values traced to
// slice_bank.h's recovered-value comments. ---
TEST(PlaySliceBankUnit, EnqueueCmd15PacketOffsetsGolden) {
    CHECK_EQ((int)kLoanCmdOpcode, 15);     // VIBE_Command_EnqueueCmd15
    CHECK_EQ((int)kLoanLenderOff, 0x10);   // v6 = a1 lender (dword)
    CHECK_EQ((int)kLoanBorrowOff, 0x14);   // v7 = a2 borrower (dword)
    CHECK_EQ((int)kLoanPlayerOff, 0x1C);   // v8 = a4 player (byte)
    CHECK_EQ((int)kLoanAmountOff, 0x1D);   // v9 = a3 amount (dword)
}

TEST(PlaySliceBankUnit, BorrowerFieldOffsetsGolden) {
    CHECK_EQ((int)kBankCashFieldOff, 0x0A);       // Person.cash (GetCashAmount)
    CHECK_EQ((int)kBankDebtFieldOff, 0x2C);       // folded debt accumulator (Person pad)
    CHECK_EQ((int)kFamilyWealthFieldOff, 18 * 4); // *((_DWORD*)family + 18) == +72
    CHECK_EQ((int)kFamilyWealthFieldOff, 72);
}

// --- classifier golden: a TAKE loan -> opcode-15 command. -----------------------
TEST(PlaySliceBankUnit, ClassifyTakeLoanGolden) {
    i32 id = SeedBankWorld(/*startCash=*/100);

    BankInteraction bi;
    bi.side = LoanSide::kTake;
    bi.lenderId = -1;             // bank sink
    bi.borrowerId = id;
    bi.amount = 250;
    bi.player = 0;

    LoanCommand c = ClassifyBankInteraction(bi);
    CHECK(c.issued);
    CHECK_EQ((int)c.opcode, 15);             // VIBE_Command_EnqueueCmd15
    CHECK(c.side == LoanSide::kTake);
    CHECK_EQ(c.lender, -1);
    CHECK_EQ(c.borrower, id);
    CHECK_EQ(c.amount, 250);
}

// --- non-loans classify as not issued. ------------------------------------------
TEST(PlaySliceBankUnit, ClassifyRejectsNonLoans) {
    i32 id = SeedBankWorld(/*startCash=*/100);

    BankInteraction base;
    base.side = LoanSide::kTake; base.lenderId = -1; base.borrowerId = id;
    base.amount = 250; base.player = 0;

    { BankInteraction bi = base; bi.side = LoanSide::kNone;
      CHECK(!ClassifyBankInteraction(bi).issued); }
    { BankInteraction bi = base; bi.amount = 0;
      CHECK(!ClassifyBankInteraction(bi).issued); }
    { BankInteraction bi = base; bi.amount = -5;
      CHECK(!ClassifyBankInteraction(bi).issued); }
}

// --- the apply: a TAKE adds cash + debt; a REPAY does the inverse. --------------
TEST(PlaySliceBankUnit, ApplyTakeAndRepayMutateCashAndDebt) {
    i32 id = SeedBankWorld(/*startCash=*/100);
    SetBankApplyHooks(nullptr);   // inert-default record mutation

    CHECK_EQ(ReadBorrowerCash(id), 100);
    CHECK_EQ(ReadBorrowerDebt(id), 0);

    // TAKE 250.
    BankInteraction take;
    take.side = LoanSide::kTake; take.lenderId = -1; take.borrowerId = id;
    take.amount = 250; take.player = 0;
    sim::CommandQueue q; q.Init(); q.set_standalone(true);
    BankSliceResult rt = RunBankSlice(take, /*econSeed=*/0x1234, q);

    CHECK(rt.command.issued);
    CHECK(rt.enqueued);
    CHECK(rt.applied);
    CHECK_EQ(rt.cashAfter - rt.cashBefore, 250);   // principal spendable
    CHECK_EQ(rt.debtAfter - rt.debtBefore, 250);   // owed
    CHECK(rt.cashMoved());
    CHECK(rt.debtMoved());

    // REPAY 250 (fresh seed for a clean slate, then pre-charge cash + debt).
    id = SeedBankWorld(/*startCash=*/500);
    if (sim::Person* p = sim::PersonFindRecordById(id)) {
        i32 d = 250;
        std::memcpy(reinterpret_cast<u8*>(p) + kBankDebtFieldOff, &d, sizeof d);
    }
    BankInteraction repay;
    repay.side = LoanSide::kRepay; repay.lenderId = -1; repay.borrowerId = id;
    repay.amount = 250; repay.player = 0;
    sim::CommandQueue q2; q2.Init(); q2.set_standalone(true);
    BankSliceResult rr = RunBankSlice(repay, /*econSeed=*/0x1234, q2);

    CHECK(rr.command.issued);
    CHECK(rr.applied);
    CHECK_EQ(rr.cashBefore - rr.cashAfter, 250);   // money out
    CHECK_EQ(rr.debtBefore - rr.debtAfter, 250);   // owed down
}

// --- the slice is deterministic across reruns. ----------------------------------
TEST(PlaySliceBankUnit, SliceDeterministic) {
    auto runOnce = [](BankSliceResult& out) {
        i32 id = SeedBankWorld(/*startCash=*/100);
        BankInteraction bi;
        bi.side = LoanSide::kTake; bi.lenderId = -1; bi.borrowerId = id;
        bi.amount = 300; bi.player = 0;
        sim::CommandQueue q; q.Init(); q.set_standalone(true);
        out = RunBankSlice(bi, /*econSeed=*/0xBEEF, q);
    };

    BankSliceResult a, b;
    runOnce(a);
    runOnce(b);

    CHECK(a.loanChangedWorld());      // the loan moved the world hash
    CHECK(a.dayChangedWorld());       // the game-day moved it again
    CHECK(a.economyPasses > 0);

    CHECK_EQ(a.hashBefore, b.hashBefore);
    CHECK_EQ(a.hashAfterCommand, b.hashAfterCommand);
    CHECK_EQ(a.hashAfterDay, b.hashAfterDay);
    CHECK_EQ(a.cashAfter, b.cashAfter);
    CHECK_EQ(a.debtAfter, b.debtAfter);
}

// ---------------------------------------------------------------------------
// HARDENING (wave-12): an out-of-range / unknown borrower account must not read
// or write any Person record out of bounds. PersonFindRecordById returns nullptr
// for an unknown id, so the cash/debt apply and readback are inert no-ops. Drive
// far-out-of-range and negative account ids and assert no OOB (ASAN) + 0 results.
// ---------------------------------------------------------------------------
TEST(PlaySliceBankUnit, UnknownBorrowerAccountInertNoOOB) {
    SeedBankWorld(/*startCash=*/100);   // only id 4242 exists
    sim::CommandQueue q; q.Init(); q.set_standalone(true);

    BankInteraction bi;
    bi.side = LoanSide::kTake;
    bi.lenderId = -1;
    bi.borrowerId = 777777;             // no such Person -> nullptr
    bi.amount = 500;
    bi.player = 0;

    BankSliceResult r = RunBankSlice(bi, 0x5A5A, q);
    CHECK(r.command.issued);            // a positive take still classifies
    // No record -> the folded cash/debt fields cannot move; readbacks are 0.
    CHECK_EQ((long long)r.cashBefore, 0LL);
    CHECK_EQ((long long)r.cashAfter, 0LL);
    CHECK_EQ((long long)r.debtBefore, 0LL);
    CHECK_EQ((long long)r.debtAfter, 0LL);
}

TEST(PlaySliceBankUnit, NegativeBorrowerAccountInertNoOOB) {
    SeedBankWorld(/*startCash=*/100);
    sim::CommandQueue q; q.Init(); q.set_standalone(true);

    BankInteraction bi;
    bi.side = LoanSide::kRepay;
    bi.lenderId = -1;
    bi.borrowerId = -12345;            // negative id -> nullptr
    bi.amount = 50;
    bi.player = 0;

    BankSliceResult r = RunBankSlice(bi, 0x6B6B, q);
    CHECK(r.command.issued);
    CHECK_EQ((long long)r.cashAfter, 0LL);
    CHECK_EQ((long long)r.debtAfter, 0LL);
}
