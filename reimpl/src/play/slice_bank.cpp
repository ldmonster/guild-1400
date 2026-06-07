// Wave 28 PLAY P5 — the BANK / LOAN (Kredit) vertical slice implementation.
// See slice_bank.h for the grounding (ConfirmLoanRequest @0x51a100 ->
// EnqueueCmd15 @0x494604 opcode-15 loan packet; family.+18 += amount; the per-day
// loan-interest cascade in RunEconomyTurn / ProcessLoanRepayments @0x57b304).
//
// REUSE of real reconstructions (called, never redefined — ODR):
//   sim::PersonFindRecordById                 (entity.cpp; the live borrower row the
//                                              apply mutates, folded by HashFullWorld)
//   sim::CommandQueue::EnqueuePacket / FlushSendQueue / ExecCommands / set_handler
//                                             (command.cpp; the REAL lockstep codec)
//   play::RunEconomyTurn / SeedEconomyTurnState (turn_economy.cpp; the real game-day
//                                              loan-interest cascade)
//   play::HashFullWorld                       (world_digest.cpp; the determinism oracle)
//   crt::Srand                                (crt/rand.h)
#include "play/slice_bank.h"

#include <cstring>

#include "crt/rand.h"
#include "play/turn_economy.h"
#include "play/world_digest.h"
#include "sim/entity.h"                // PersonFindRecordById + Person

namespace guild::play {

namespace {

// ---------------------------------------------------------------------------
// Default (inert-by-default) cash/debt apply — DEFINED here so the unified build
// links; tests may install BankApplyHooks to override.
//
// The loan principal is applied to the BORROWER'S Person record (folded by
// HashFullWorld): cash (+0x0A word, the GetCashAmount field) becomes spendable, and
// the principal accrues into a folded debt accumulator dword (+0x2C, a documented
// slice-private pad field). The binary's LITERAL write is family.+18 += amount into
// the UNFOLDED family table (word_13C3110) — offered as an optional hook only.
// ---------------------------------------------------------------------------
const BankApplyHooks* g_hooks = nullptr;

void DefaultApplyCash(i32 borrowerId, i64 delta) {
    sim::Person* p = sim::PersonFindRecordById(borrowerId);
    if (!p) return;
    u8* base = reinterpret_cast<u8*>(p);
    i16 cur;
    std::memcpy(&cur, base + kBankCashFieldOff, sizeof cur);
    i32 nv = static_cast<i32>(cur) + static_cast<i32>(delta);
    if (nv < 0) nv = 0;                       // cash word is non-negative
    cur = static_cast<i16>(nv);
    std::memcpy(base + kBankCashFieldOff, &cur, sizeof cur);
}

void DefaultApplyDebt(i32 borrowerId, i64 delta) {
    sim::Person* p = sim::PersonFindRecordById(borrowerId);
    if (!p) return;
    u8* base = reinterpret_cast<u8*>(p);
    i32 cur;
    std::memcpy(&cur, base + kBankDebtFieldOff, sizeof cur);
    cur += static_cast<i32>(delta);
    if (cur < 0) cur = 0;                      // debt never goes negative
    std::memcpy(base + kBankDebtFieldOff, &cur, sizeof cur);
}

void RunApplyCash(i32 id, i64 delta) {
    if (g_hooks && g_hooks->applyCash) g_hooks->applyCash(id, delta);
    else                               DefaultApplyCash(id, delta);
}
void RunApplyDebt(i32 id, i64 delta) {
    if (g_hooks && g_hooks->applyDebt) g_hooks->applyDebt(id, delta);
    else                               DefaultApplyDebt(id, delta);
}
void RunApplyFamilyWealth(i32 id, i64 delta) {
    // Inert by default: the family table is not part of the folded world.
    if (g_hooks && g_hooks->applyFamilyWealth) g_hooks->applyFamilyWealth(id, delta);
}

// ---------------------------------------------------------------------------
// The opcode-15 loan-command handler: decode the EnqueueCmd15 staging and apply
// the cash + debt move. The real engine's lockstep apply for this loan channel
// produces (borrower cash up, family principal up); we apply the deterministic net
// effect on the folded borrower record (cash + debt accumulator) and route the
// family-table principal write through the optional hook.
//
// The packet does NOT carry the take-vs-repay sign (the binary uses two distinct
// dialogs that emit the same EnqueueCmd15 channel). We encode it: a TAKE stamps the
// lender at +0x10; a REPAY stamps the marker -2 there, so the handler distinguishes
// the inverse apply (faithful to the slice's classify -> packet -> apply contract).
// ---------------------------------------------------------------------------
void LoanCmdHandler(sim::CommandQueue& /*q*/, sim::CommandPacket& pkt,
                    sim::AckEntry* /*ack*/) {
    i32 lender   = static_cast<i32>(pkt.get32(kLoanLenderOff));   // -1 take / -2 repay
    i32 borrower = static_cast<i32>(pkt.get32(kLoanBorrowOff));
    i32 amount   = static_cast<i32>(pkt.get32(kLoanAmountOff));

    LoanSide side = (lender == -2) ? LoanSide::kRepay : LoanSide::kTake;
    i64 amt = static_cast<i64>(amount);
    if (side == LoanSide::kTake) {
        RunApplyCash(borrower, +amt);            // principal becomes spendable
        RunApplyDebt(borrower, +amt);            // owed
        RunApplyFamilyWealth(borrower, +amt);    // family.+18 += amount (literal)
    } else {
        RunApplyCash(borrower, -amt);            // pay it back
        RunApplyDebt(borrower, -amt);            // owed down
        RunApplyFamilyWealth(borrower, -amt);
    }
}

} // namespace

// ===========================================================================
// Classifier — interaction -> loan command.
// ===========================================================================
LoanCommand ClassifyBankInteraction(const BankInteraction& bi) {
    LoanCommand cmd;
    cmd.side = bi.side;
    if (bi.side == LoanSide::kNone)
        return cmd;                       // nothing chosen
    if (bi.amount <= 0)
        return cmd;                       // no principal

    cmd.issued   = true;
    cmd.opcode   = kLoanCmdOpcode;        // 15
    cmd.lender   = bi.lenderId;
    cmd.borrower = bi.borrowerId;
    cmd.amount   = bi.amount;
    cmd.player   = bi.player;
    return cmd;
}

void SetBankApplyHooks(const BankApplyHooks* hooks) { g_hooks = hooks; }

void InstallBankCommandHandler(sim::CommandQueue& q) {
    q.set_handler(kLoanCmdOpcode, &LoanCmdHandler);
}

i64 ReadBorrowerCash(i32 borrowerId) {
    sim::Person* p = sim::PersonFindRecordById(borrowerId);
    if (!p) return 0;
    i16 v;
    std::memcpy(&v, reinterpret_cast<u8*>(p) + kBankCashFieldOff, sizeof v);
    return v;
}

i64 ReadBorrowerDebt(i32 borrowerId) {
    sim::Person* p = sim::PersonFindRecordById(borrowerId);
    if (!p) return 0;
    i32 v;
    std::memcpy(&v, reinterpret_cast<u8*>(p) + kBankDebtFieldOff, sizeof v);
    return v;
}

// Build the opcode-15 packet (1:1 with EnqueueCmd15 @0x494604) for a loan.
namespace {
sim::CommandPacket BuildLoanPacket(const LoanCommand& c) {
    sim::CommandPacket pkt{};
    pkt.bytes[0] = kLoanCmdOpcode;                              // v5[0] = 15
    // v6 = a1: a TAKE stamps the lender (-1 == bank sink); a REPAY stamps -2 so the
    // apply distinguishes the inverse (the binary uses two dialogs, one channel).
    i32 lender = (c.side == LoanSide::kRepay) ? -2 : c.lender;
    pkt.put32(kLoanLenderOff, static_cast<u32>(lender));        // v6 = a1
    pkt.put32(kLoanBorrowOff, static_cast<u32>(c.borrower));    // v7 = a2
    pkt.bytes[kLoanPlayerOff] = c.player;                       // v8 = a4 (byte)
    pkt.put32(kLoanAmountOff, static_cast<u32>(c.amount));      // v9 = a3 (dword)
    return pkt;
}
} // namespace

// ===========================================================================
// RunBankSlice — the loan slice over the already-loaded live world.
// ===========================================================================
BankSliceResult RunBankSlice(const BankInteraction& bi,
                             std::uint32_t econSeed, sim::CommandQueue& q) {
    BankSliceResult r;

    // --- snapshot BEFORE -----------------------------------------------------
    r.cashBefore = ReadBorrowerCash(bi.borrowerId);
    r.debtBefore = ReadBorrowerDebt(bi.borrowerId);
    // Re-anchor the RNG right before the hash: the base digest folds the live CRT
    // RNG state, so pin it so hashBefore is reproducible across reruns in-process.
    crt::Srand(econSeed);
    r.hashBefore = HashFullWorld();

    // --- step: CLASSIFY ------------------------------------------------------
    r.command = ClassifyBankInteraction(bi);
    if (!r.command.issued) {
        r.cashAfter = r.cashBefore;
        r.debtAfter = r.debtBefore;
        crt::Srand(econSeed);
        r.hashAfterCommand = HashFullWorld();
        r.hashAfterDay     = r.hashAfterCommand;
        return r;
    }

    // --- step: BUILD + ENQUEUE + APPLY through the REAL CommandQueue codec ---
    InstallBankCommandHandler(q);
    SetBankApplyHooks(nullptr);   // inert-default cash/debt mutation
    sim::CommandPacket pkt = BuildLoanPacket(r.command);
    u32 before = q.send_count();
    i32 slot = q.EnqueuePacket(pkt);
    r.ringSlot = slot;
    r.enqueued = (slot >= 0) && (q.send_count() != before);
    if (r.enqueued && q.standalone()) {
        q.FlushSendQueue();
        q.ExecCommands();             // -> LoanCmdHandler -> cash + debt move
        r.applied = true;
    }
    r.cashAfter = ReadBorrowerCash(bi.borrowerId);
    r.debtAfter = ReadBorrowerDebt(bi.borrowerId);
    crt::Srand(econSeed);
    r.hashAfterCommand = HashFullWorld();

    // --- step: GAME-DAY (the REAL economy passes incl. loan interest; seeded) -
    crt::Srand(econSeed);
    EconomyTurnState st = SeedEconomyTurnState();
    st.day = 0;
    EconomyTurnDeltas d = RunEconomyTurn(st);
    r.economyPasses   = d.passesRun;
    r.interestThisDay = d.interestThisTurn;
    crt::Srand(econSeed);
    r.hashAfterDay = HashFullWorld();
    return r;
}

} // namespace guild::play
