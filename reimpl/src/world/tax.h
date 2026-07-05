#pragma once
// Tax-collection formulas for the Guild economy (gilde.exe).
//
// Translated (formula core):
//   VIBE_Tax_CollectTradeIncome     0x57aa88
//   VIBE_Tax_CollectOfficeAllTaxes  0x57b214  (dispatch skeleton)
//
// The originals read the live AiPlayer columns (account value at
// dword_12CEAAC[134*player]) and COMMIT the result through the command/network
// lockstep (VIBE_Command_QueueRequest16). Mutations must route through that
// queue, so the commit is exposed as a forward-declared hook
// (TaxCommitToCommandQueue) that the engine/tests supply. The pure arithmetic
// (rate * 0.01 * value, clamped at 0) is reproduced byte-faithfully.
#include "guild/common/types.h"

namespace guild::world {

// gilde.exe flt_625884 == 0.01f (the single-precision constant, not exact 0.01).
constexpr float kTaxRateScale = 0.01f;

// Trade-tax amount for a player.
//   lawThreshold : Gesetz_GetRecord(8) field at +24 (the configured rate, e.g.
//                  a percentage 0..100). Read from the city's finance law book.
//   accountValue : dword_12CEAAC[134*player] (the player's trade account worth).
// Returns max(0, (float)((float)lawThreshold * 0.01f * (float)accountValue)),
// truncated to int — exactly as the original computes v24 before committing.
i32 TaxComputeTradeIncome(i32 lawThreshold, i32 accountValue);

// MUTATION HOOK (Command lockstep). The original calls VIBE_Command_QueueRequest16
// to enqueue the tax transfer; we forward-declare it so the logic stays testable.
// `payerObjectId` / `payerId` identify the taxed account; `amount` is the tax.
// Returns true if the command was queued (single-player short-circuit returns
// the same as the real ack path).
bool TaxCommitToCommandQueue(i32 payerObjectId, i32 payerId, i32 amount);

// Install a commit hook (the engine wires its real command-queue enqueuer;
// tests use it to observe the amount that would be enqueued).
void TaxSetCommitHook(bool (*hook)(i32 payerObjectId, i32 payerId, i32 amount));

// gilde.exe 0x57aa88 — VIBE_Tax_CollectTradeIncome (formula + commit dispatch).
//   flags bit0 -> queue the network command (TaxCommitToCommandQueue)
//   flags bit1 -> add the amount to the city tax accumulator (out param)
// Returns 0 only when law-book slot 8 was already full (>=6 active finance
// laws); otherwise 1 — even for a non-positive account (0x57ab34 `test ebx,ebx;
// jle` skips the writes but still falls through to `mov eax,1`).
struct TradeTaxResult {
    i32  amount = 0;     // computed tax (>=0)
    bool committed = false;
    bool addedToCity = false;
};
int TaxCollectTradeIncome(i32 lawThreshold, int activeFinanceLaws,
                          i32 accountValue, i32 payerObjectId, i32 payerId,
                          int flags, TradeTaxResult* out, i32* cityAccumulator);

} // namespace guild::world
