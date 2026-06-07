// tests/integration/play_slice_bank_itest.cpp — INTEGRATION: the BANK/LOAN slice
// over a small real-format world. A TAKE then a REPAY each route through the REAL
// sim::CommandQueue codec (the opcode-15 EnqueueCmd15 loan packet) and mutate the
// borrower's real folded record:
//   * cash (Person +0x0A, the GetCashAmount field),
//   * the folded debt accumulator (Person +0x2C).
// Asserts HashFullWorld differs pre/post the loan AND the game-day, and the whole
// run is byte-identical on rerun (the determinism oracle).
#include "test.h"

#include "play/slice_bank.h"
#include "play/world_digest.h"
#include "sim/entity.h"
#include "sim/command.h"
#include "world/city.h"

#include <cstring>

using namespace guild;
using namespace guild::play;

namespace {

// Build a small real-format world: a few live Persons in g_persons, the folded
// economy tables blanked so the run is reproducible. Returns the borrower id.
i32 SeedRealFormatWorld(i16 startCash) {
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
        p.marker = 0;
        p.kind   = 5;
        p.id     = 6100 + i;
        sim::g_personIds[i] = 6100 + i;
        i16 c = (i16)(startCash + 10 * i);
        std::memcpy(reinterpret_cast<u8*>(&p) + kBankCashFieldOff, &c, sizeof c);
        i32 zero = 0;
        std::memcpy(reinterpret_cast<u8*>(&p) + kBankDebtFieldOff, &zero, sizeof zero);
    }
    return kBorrower;
}

// Run a TAKE (400) then a REPAY (150) on the borrower, returning the two results.
void RunTakeThenRepay(i16 startCash, u32 econSeed,
                      BankSliceResult& takeOut, BankSliceResult& repayOut) {
    i32 id = SeedRealFormatWorld(startCash);

    BankInteraction take;
    take.side = LoanSide::kTake; take.lenderId = -1; take.borrowerId = id;
    take.amount = 400; take.player = 0;
    sim::CommandQueue q1; q1.Init(); q1.set_standalone(true);
    takeOut = RunBankSlice(take, econSeed, q1);

    BankInteraction repay;
    repay.side = LoanSide::kRepay; repay.lenderId = -1; repay.borrowerId = id;
    repay.amount = 150; repay.player = 0;
    sim::CommandQueue q2; q2.Init(); q2.set_standalone(true);
    repayOut = RunBankSlice(repay, econSeed, q2);
}

} // namespace

// A take then a repay each move cash + debt and step the hashes.
TEST(PlaySliceBankItest, TakeThenRepayMutateFoldedRecordsAndHashSteps) {
    SetBankApplyHooks(nullptr);

    BankSliceResult take, repay;
    RunTakeThenRepay(/*startCash=*/200, /*econSeed=*/0xC0FFEE, take, repay);

    // --- TAKE: cash up, debt up, hashes step ---
    CHECK(take.command.issued);
    CHECK_EQ((int)take.command.opcode, 15);   // EnqueueCmd15 loan packet
    CHECK(take.enqueued);
    CHECK(take.applied);
    CHECK_EQ(take.cashAfter - take.cashBefore, 400);
    CHECK_EQ(take.debtAfter - take.debtBefore, 400);
    CHECK(take.loanChangedWorld());
    CHECK(take.dayChangedWorld());

    // --- REPAY: cash down, debt down, hashes step ---
    CHECK(repay.command.issued);
    CHECK(repay.applied);
    CHECK_EQ(repay.cashBefore - repay.cashAfter, 150);
    CHECK_EQ(repay.debtBefore - repay.debtAfter, 150);
    CHECK(repay.loanChangedWorld());
    CHECK(repay.economyPasses > 0);   // the real per-day cascade ran
    // (repay.dayChangedWorld() is not asserted: the take's earlier game-day already
    // drove the folded economy tables to their seeded post-day values, so the repay's
    // identically-seeded day re-derives the same tables — the day is deterministic.)

    // After both, the borrower still owes 250 (400 taken - 150 repaid).
    CHECK_EQ(repay.debtAfter, 250);
}

// The full take-then-repay run is byte-identical on rerun (determinism).
TEST(PlaySliceBankItest, RunDeterministicAcrossReruns) {
    BankSliceResult a1, a2, b1, b2;
    RunTakeThenRepay(/*startCash=*/200, /*econSeed=*/0x1357, a1, a2);
    RunTakeThenRepay(/*startCash=*/200, /*econSeed=*/0x1357, b1, b2);

    CHECK_EQ(a1.hashBefore, b1.hashBefore);
    CHECK_EQ(a1.hashAfterCommand, b1.hashAfterCommand);
    CHECK_EQ(a1.hashAfterDay, b1.hashAfterDay);
    CHECK_EQ(a2.hashAfterCommand, b2.hashAfterCommand);
    CHECK_EQ(a2.hashAfterDay, b2.hashAfterDay);
    CHECK_EQ(a1.cashAfter, b1.cashAfter);
    CHECK_EQ(a2.debtAfter, b2.debtAfter);

    // A different seed diverges the day trajectory.
    BankSliceResult c1, c2;
    RunTakeThenRepay(/*startCash=*/200, /*econSeed=*/0x2468, c1, c2);
    CHECK(c1.hashAfterDay != a1.hashAfterDay);
}
