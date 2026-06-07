#pragma once
// Wave 28 PLAY P5 — the BANK / LOAN (Kredit) vertical slice: a faithful
// click -> loan dialog -> REAL loan command -> sim effect -> render slice for the
// money-lending system (namespace guild::play).
//
// It mirrors the slice_market / slice_council / playable_slice pattern: resolve a
// bank interaction (take or repay a loan), classify it, emit the REAL loan command
// packet, route it through the REAL sim::CommandQueue lockstep codec, apply it
// (mutating the borrower's folded cash + a folded debt accumulator), then advance
// ONE game-day and prove the loan state evolved while the whole run stays
// deterministic (HashFullWorld differs pre/post and is byte-identical on rerun).
//
// GROUNDING (decompiled this wave — the live player take-loan path):
//
//   * VIBE_Credit_ShowTakeLoanDialog @0x51a244 is the take-loan UI loop; the COMMIT
//     is VIBE_Credit_ConfirmLoanRequest @0x51a100:
//        person = VIBE_Person_FindRecordById(*(req+176));    // the borrower
//        if (CheckResourceAmount(*(req+180), player) && ShowMessageBox(...)) {
//            VIBE_Command_EnqueueCmd15(*(lender+1), *(req+176), *(req+180), player);
//            family = VIBE_Person_GetFamilyRecord(person);
//            if (family) *((_DWORD*)family + 18) += *(req+180);   // += loan amount
//        }
//     So the loan is wired with VIBE_Command_EnqueueCmd15 @0x494604 (OPCODE 15) and
//     its money effect is `family.+18(dword) += loanAmount` (the dynasty wealth /
//     loan-principal accumulator in the family table word_13C3110[82*familyId]).
//
//   * VIBE_Command_EnqueueCmd15 @0x494604 packs the opcode-15 packet:
//        v6=a1@+0x10 (lender id), v7=a2@+0x14 (borrower id),
//        v8=a4@+0x1C (player, byte), v9=a3@+0x1D (loan amount, dword), bytes[0]=15.
//
//   * The per-day loan-INTEREST charge is already reconstructed in the economy turn
//     (play::RunEconomyTurn -> the Amt loan pass: 4-loanLawSlot+10 base interest,
//     interestPaid accumulator; grounded in VIBE_Amt_ProcessLoanRepayments @0x57b304
//     QueueRequest16 repayment / QueueRequestCoord27 wage-garnish / Pair33
//     foreclose). The slice runs that real day after the loan.
//
// WIRED REAL siblings: the opcode-15 EnqueueCmd15 packet layout + the REAL
// sim::CommandQueue codec (EnqueuePacket/FlushSendQueue/ExecCommands), the real
// borrower record (sim::PersonFindRecordById, the live g_persons row the apply
// mutates — folded by HashFullWorld), play::RunEconomyTurn (the real per-day loan
// interest cascade), play::HashFullWorld (the determinism oracle).
//
// INERT-default GAP (declared honestly): the binary's LITERAL loan-principal write
// is `family.+18 += amount` into the FAMILY/dynasty table (word_13C3110), which is
// NOT one of the regions HashFullWorld folds (it folds g_persons / g_objects raw
// bytes + the world tables, not the family table). So the slice's deterministic,
// folded loan effect is applied to the BORROWER'S Person record (the row the loan
// targets and GetCashAmount reads): it CREDITS the borrower's cash (+0x0A word, the
// principal becomes spendable) and accrues the principal into a folded debt
// accumulator dword in the borrower's Person pad (kBankDebtFieldOff). A repay does
// the inverse. Both are exposed as installable BankApplyHooks with inert-by-default
// reconstructions DEFINED IN slice_bank.cpp (the build model), overridable by tests.
// The literal family-+18 write is additionally offered as an installable hook.
//
// Additive: no edits to any existing .cpp/.h.
#include <cstdint>

#include "guild/common/types.h"
#include "sim/command.h"

namespace guild::play {

// ===========================================================================
// The bank interaction the slice replays: take or repay a loan of `amount`
// between `lenderId` (the bank/lender person) and `borrowerId` for `player`.
// ===========================================================================
enum class LoanSide {
    kNone   = 0,
    kTake   = 1,   // take a loan:  cash += amount, debt += amount
    kRepay  = 2,   // repay a loan: cash -= amount, debt -= amount
};

struct BankInteraction {
    LoanSide side       = LoanSide::kNone;
    i32      lenderId   = -1;     // the lender/bank person id (op15 a1, -1 == bank sink)
    i32      borrowerId = 0;      // the borrowing person id (op15 a2 = *(req+176))
    i32      amount     = 0;      // the loan principal (op15 a3 = *(req+180))
    u8       player     = 0;      // byte_6477A1 (the acting player slot)
};

// ===========================================================================
// The classified loan COMMAND (the golden: interaction -> wire command). 1:1 with
// VIBE_Credit_ConfirmLoanRequest's VIBE_Command_EnqueueCmd15 emission.
// ===========================================================================
inline constexpr u8 kLoanCmdOpcode = 15;        // VIBE_Command_EnqueueCmd15

// EnqueueCmd15 packet-staging offsets (recovered @0x494604):
inline constexpr u32 kLoanLenderOff = 0x10;     // v6 = lender id   (a1)
inline constexpr u32 kLoanBorrowOff = 0x14;     // v7 = borrower id (a2)
inline constexpr u32 kLoanPlayerOff = 0x1C;     // v8 = player byte (a4)
inline constexpr u32 kLoanAmountOff = 0x1D;     // v9 = amount      (a3, dword)

// The borrower-Person folded fields the apply mutates (g_persons raw bytes, folded
// by HashFullWorld). Cash is the real GetCashAmount field (+0x0A); the debt
// accumulator is a documented slice-private pad dword (the binary's literal +18
// write lands in the unfolded family table — see header note).
inline constexpr u32 kBankCashFieldOff = 0x0A;  // Person.cash word (GetCashAmount)
inline constexpr u32 kBankDebtFieldOff = 0x2C;  // folded debt accumulator (Person pad)

// The family-table dword the binary literally increments (NOT folded by
// HashFullWorld; exposed as an installable hook). family base + 18*4 == +72.
inline constexpr u32 kFamilyWealthFieldOff = 18 * 4;  // *((_DWORD*)family + 18)

struct LoanCommand {
    bool     issued    = false;  // a valid loan (take/repay, amount > 0)
    u8       opcode    = 0;      // kLoanCmdOpcode (15) when issued
    LoanSide side      = LoanSide::kNone;
    i32      lender    = -1;     // a1
    i32      borrower  = 0;      // a2
    i32      amount    = 0;      // a3
    u8       player    = 0;      // a4
};

// 1:1 classifier: resolve a BankInteraction into its LoanCommand. A non-loan (no
// side / amount <= 0) yields {issued=false}.
LoanCommand ClassifyBankInteraction(const BankInteraction& bi);

// ===========================================================================
// Bank apply hooks — the leaves not covered by one reconstructed sibling.
// Installable with inert-by-default reconstructions defined in slice_bank.cpp.
// ===========================================================================
struct BankApplyHooks {
    // Credit/debit the borrower's spendable cash (signed delta into Person +0x0A).
    // Default mutates the live record via sim::PersonFindRecordById (clamped at 0).
    void (*applyCash)(i32 borrowerId, i64 delta) = nullptr;
    // Accrue/relieve the folded debt accumulator (signed delta into Person +0x2C).
    // Default mutates the live record (clamped at 0).
    void (*applyDebt)(i32 borrowerId, i64 delta) = nullptr;
    // OPTIONAL: the binary's literal family-table write (family.+18 += delta). If
    // non-null it is invoked with the borrower id + signed amount; the default is a
    // no-op (the family table is not part of the folded world — see header).
    void (*applyFamilyWealth)(i32 borrowerId, i64 delta) = nullptr;
};
void SetBankApplyHooks(const BankApplyHooks* hooks);

// Install the opcode-15 loan-command handler on `q` (decodes the EnqueueCmd15
// staging and applies the cash + debt mutation). Idempotent per queue.
void InstallBankCommandHandler(sim::CommandQueue& q);

// Read the current cash (Person +0x0A) of a live borrower (0 if not found).
i64 ReadBorrowerCash(i32 borrowerId);
// Read the current folded debt accumulator (Person +0x2C) (0 if not found).
i64 ReadBorrowerDebt(i32 borrowerId);

// ===========================================================================
// The result of one bank click -> command -> apply -> game-day cycle.
// ===========================================================================
struct BankSliceResult {
    // --- the classified + emitted command ---
    LoanCommand command{};
    bool   enqueued = false;       // a packet hit the send ring
    i32    ringSlot = -1;          // EnqueuePacket ring slot
    bool   applied  = false;       // FlushSendQueue + ExecCommands ran

    // --- before / after the loan (the sim effect) ---
    i64    cashBefore = 0;
    i64    cashAfter  = 0;
    i64    debtBefore = 0;
    i64    debtAfter  = 0;

    // --- the game-day (the real per-day loan-interest cascade) ---
    int    economyPasses = 0;
    i64    interestThisDay = 0;     // RunEconomyTurn's loan-interest charge

    // --- determinism oracle (the three world hashes) ---
    std::uint64_t hashBefore       = 0;  // HashFullWorld() before the loan
    std::uint64_t hashAfterCommand = 0;  // ... after the loan applied
    std::uint64_t hashAfterDay     = 0;  // ... after the game-day

    bool loanChangedWorld() const { return hashBefore != hashAfterCommand; }
    bool dayChangedWorld()  const { return hashAfterCommand != hashAfterDay; }
    bool cashMoved()        const { return cashBefore != cashAfter; }
    bool debtMoved()        const { return debtBefore != debtAfter; }
};

// ===========================================================================
// RunBankSlice — the loan slice over the ALREADY-LOADED live world.
//
//   1. snapshot the borrower's cash + debt + HashFullWorld() (before),
//   2. classify the interaction -> LoanCommand,
//   3. build the opcode-15 packet, enqueue through the REAL sim::CommandQueue,
//      flush + exec so the apply handler mutates cash + debt,
//   4. HashFullWorld() (after command),
//   5. advance ONE game-day via play::RunEconomyTurn (seeded by `econSeed`) — the
//      real per-day loan-interest cascade,
//   6. HashFullWorld() (after day), snapshot cash + debt (after).
//
// The caller has populated the live sim arrays (synthetic seed or io::LoadWorld)
// with a live Person whose id == bi.borrowerId. `q` is a standalone CommandQueue
// (Init()'d). Returns the run result.
BankSliceResult RunBankSlice(const BankInteraction& bi,
                             std::uint32_t econSeed, sim::CommandQueue& q);

} // namespace guild::play
