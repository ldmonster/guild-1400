#pragma once
// ===========================================================================
// Bank-Meister per-frame AI decision calculator — 1:1 reconstruction (gilde.exe).
//
//   0x459264  VIBE_Ai_CalcBankmeister  (__usercall, eax = (meisterRec@eax))
//
// This is the master-NPC "brain" branch for a guild BANK (AiPlayer class 5,
// MeisterRoutine::kBank), dispatched once per AI-controllable Bank-Meister per
// evaluation tick from VIBE_Ai_EvaluateMeister (0x4533a8). It is the Meister-AI
// sibling the wave-19 fleet (ai_meister_calc_*) left uncovered — wave-19's
// DispatchMeisterCalc no-ops kBank ("their own planner module owns them"); this
// module supplies that body and documents the one-line wiring handoff.
//
// WHAT IT DOES (faithful to the decompile):
//   1. Skip if the per-tick flags2 bit 8 (meisterRec+0x1C8 & 8) is already set.
//   2. Read law-record 13 (VIBE_Gesetz_GetRecord -> world/law.h, REUSED) for the
//      bank's mandated cash-reserve target (LawRecord.threshold, field +0x18).
//   3. Count the bank's pending credit/loan handler entries (the He handler-list
//      probe FindFirstHandlerByFilter(2,0,0x23,3,bldgId) — a genuine engine leaf,
//      surfaced as a hook).
//   4. Reserve adjust: nudge the building's held cash (*(bldg+0x65)) toward the
//      law target. |diff|>3 -> a single +/-1 reserve step (delta-field state cmd);
//      |diff|<=3 -> a randomised +/-(rand%3) step whose direction depends on the
//      handler count and bounds, clamped to >=4 / <=target+3.
//   5. Coin denomination balancing: for each of the 3 coin denominations
//      (loop i=1..3), count the coins of that type at the bank's scene root
//      (GameObject_CountAtLocation — a genuine engine leaf, hook). If below
//      3*budget/20 -> MINT up to 5*budget/20 (a build-action with two cmd15
//      legs). If above 6*budget/20 -> MELT down to 4*budget/20.
//   6. Default mint/melt: if the last denomination's count is still below the
//      1.5*budget gate AND budget>accumulatedMinted -> mint the shortfall*0.8.
//      Else, if the owner person's kind byte is not 6/7 and count>2*budget ->
//      melt count-2.25*budget.
//   7. Set flags2 bit 8 (done-this-tick).
//
// All integer divisions are signed truncating (idiv); coin amounts pass through
// VIBE_Coord_ConvertX (frndint, round-toward-zero == TRUNCATION) before the cmd.
//
// BOUNDARIES surfaced as data (rule 8, NEVER faked):
//   * The lockstep COMMAND QUEUE. The original builds packets via
//     VIBE_Command_BeginDeltaPacket / AppendDeltaField / QueueRequestState22
//     (a "set reserve to N" delta packet) and EnqueueBuildingActionStart /
//     EnqueueCmd15 / EnqueueBuildingActionEnd (a mint/melt coin op), then funnels
//     them into VIBE_Command_EnqueuePacket (0x49388c) — the session lockstep queue,
//     which needs session context. As with the wave-19 MeisterCmdSink we capture
//     each emit into a BankCmdSink (the packets the original would queue); the live
//     bridge drains the sink into the real CommandQueue. This is a DISTINCT command
//     API from wave-19's 248-byte MeisterCommand (delta-packet + cmd15, not the
//     SetGrayColorThunk record), hence its own sink type here.
//   * The two engine LEAVES — He handler-list count and coin-count-at-location —
//     are surfaced as function-pointer hooks the bridge wires. Null hooks take the
//     original's null/zero-return path (no handlers / no coins).
// ===========================================================================
#include <cstdint>
#include <vector>

#include "guild/common/types.h"

namespace guild::sim {

// ---------------------------------------------------------------------------
// Captured bank command boundary (the lockstep command queue, surfaced as data).
// The bank emits exactly two logical command shapes:
//   * kReserveSet — VIBE_Command_BeginDeltaPacket + AppendDeltaField(type4,1,&val,
//     col) + QueueRequestState22: "set the bank's cash-reserve column to `value`".
//     `col` is the building delta column word (bldgCash field offset - dword_11AA474
//     base); reproduced verbatim as `reserveColumn`.
//   * kCoinOp     — EnqueueBuildingActionStart("Bankmeister") + two EnqueueCmd15
//     legs + EnqueueBuildingActionEnd: a MINT (positive) or MELT (negative) of
//     `amount` coins of denomination `coinType` at the bank, crediting/debiting
//     actor `actorId` for building `buildingId`. `coinDenom` is the loop index i
//     (1..3) for the per-denomination ops, or the byte_6477A1 currency byte for
//     the default op (matches the original's two cmd15 leg arguments).
// ---------------------------------------------------------------------------
struct BankCommand {
    enum Kind : u8 { kReserveSet = 0, kCoinOp = 1 };
    Kind kind = kReserveSet;

    // --- common ---
    i32  buildingId = 0;     // *(bldg+1)

    // --- kReserveSet ---
    i32  reserveValue  = 0;  // the clamped target reserve (var_1C)
    u16  reserveColumn = 0;  // delta column word (bldgCash off - delta base)

    // --- kCoinOp ---
    bool mint      = false;  // true = mint (add coins), false = melt (remove)
    i32  amount    = 0;      // coin count moved (Coord_ConvertX-truncated)
    i32  actorId   = 0;      // g_personIds[owner] — owning player actor
    u8   coinDenom = 0;      // denomination index (1..3) / currency byte
};

struct BankCmdSink {
    std::vector<BankCommand> emitted;
    void push(const BankCommand& c) { emitted.push_back(c); }
};

// The active sink for the current CalcBankmeister invocation. The live engine
// bridge sets this before dispatch and drains it after; tests inspect it directly.
// When null, the planner runs all its decision logic but skips the capture (still
// faithful — the command queue is the boundary). Defined in ai_meister_bank.cpp.
extern BankCmdSink* g_bankCmdSink;

// ---------------------------------------------------------------------------
// Engine leaves (rule 8). Surfaced as a function-pointer table the live bridge
// wires to the real reimpl leaves. NEVER faked: a null hook takes the same path
// the original would on the null/zero return.
// ---------------------------------------------------------------------------
struct BankAiLeaves {
    // VIBE_He_FindFirstHandlerByFilter(2,0,filterCode,3,key) + iterate via
    // VIBE_He_FindNextMatchingHandler: the original counts how many handler
    // entries match. We surface the whole count loop as one leaf returning that
    // count (i in the decompile). filterCode==0x23 (35), key==building id.
    //   gilde.exe 0x4c63f8 / 0x4c6278.
    int (*heCountMatching)(int filterCode, i32 key) = nullptr;

    // VIBE_GameObject_CountAtLocation(sceneRootId): counts game objects (coins of
    // the active denomination) at the bank's scene root. The original passes the
    // bank's scene-root id (*(bldg+0x5D)); the coin-denomination selection is the
    // engine's own per-call query state, so the reimpl bridge threads `coinDenom`
    // (loop index, 1..3, or the currency byte for the default op) so the count is
    // denomination-specific.  gilde.exe 0x58f1f4.
    int (*coinCountAtLocation)(i32 sceneRootId, u8 coinDenom) = nullptr;
};

extern const BankAiLeaves* g_bankLeaves;

// ===========================================================================
// 0x459264 — VIBE_Ai_CalcBankmeister. `meisterRec` is the Bank-Meister's
// 536-byte person record (g_persons[i] base — the original a1/eax). Mutates the
// record's flags2 byte (+0x1C8 bit 8) and pushes BankCommands into g_bankCmdSink.
// Returns the original int result (last EnqueueCmd15 result / passthrough).
// ===========================================================================
int CalcBankmeister(u8* meisterRec);

// Test/setup helper: clear the bank sink + leaves pointers.
void ResetBankAiState();

} // namespace guild::sim
