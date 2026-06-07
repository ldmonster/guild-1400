#pragma once
// Need decay / random-need-selection for the Guild AI (gilde.exe).
//   AiNeeds_ApplyRandomDecayField        0x58adc0
//   AiNeeds_PickRandomNeedAndClearGroup  0x58aea8  (translated)
//   AiNeeds_PickRandomNeedAndClearGroupB 0x58b0cc  (translated)
//   AiNeeds_PickRandomFlagFromEight      0x58b2f4
//   AiNeeds_PickRandomFlagFromFourA      0x58b4e8
//   AiNeeds_PickRandomFlagFromFourB      0x58b614
//
// These are the seeded-random need pickers. Each one:
//   1. builds an N-slot eligibility array (N = 4 or 8), default 0, overriding a
//      slot to 1 when the matching need-flag bits in the person record (+44, a
//      packed 32-bit need word) are set,
//   2. picks a start slot = RandNext() % N and scans forward (mod N) for the
//      first eligible (==1) slot — RNG-seeded, hence determinism-critical,
//      reproduced bit-exactly via guild::crt::RandNext,
//   3. maps the chosen slot to a need-id, clears that need's flag bits in the
//      need word, and (in the binary) emits a delta command + a stock adjustment.
//
// AI emits commands rather than mutating state directly (lockstep determinism):
// the command emission and the building stock adjustment are routed through a
// forward-declared hook (NeedsCommandHook) so the deterministic selection logic
// is testable in isolation. The hook receives the new need word and (for the
// decay/clear functions) the stock delta to apply.
#include "guild/common/types.h"
#include "ai/types.h"

namespace guild::ai {

// The "person" the need functions operate on is addressed at +4 (entity id),
// +44 (the packed 32-bit need word; also +44/+45/+46 byte views). We model just
// the fields the functions read/write.
struct NeedAgent {
    i16 type = 0;     // *(_WORD*)person   — building/need group code (stock target)
    i32 id = 0;       // *(person+4)       — entity id (delta-packet target)
    u32 needWord = 0; // *(person+44)      — packed need-flag word (read+cleared)
};

// Command/stock hook (the AI->command boundary). `newNeedWord` is the post-clear
// need word the delta command carries; `stockDelta` is the amount passed to
// VIBE_Building_AdjustStockAndNotify (negative = consume). Called once per
// successful pick. A null hook means "selection only" (the logic still runs).
struct NeedsCommandHook {
    virtual ~NeedsCommandHook() = default;
    // BeginDeltaPacket(id) + AppendDeltaField(needWord@+44) + QueueRequestState22.
    virtual void EmitNeedDelta(i32 entityId, u32 newNeedWord) = 0;
    // VIBE_Building_AdjustStockAndNotify(type, stockDelta, person).
    virtual void AdjustStock(i16 type, i32 stockDelta) = 0;
};

// --- decay ------------------------------------------------------------------

// gilde.exe 0x58adc0 — VIBE_AiNeeds_ApplyRandomDecayField. Early-outs (returns 0)
// when the low nibble of need byte (+44) is already set. Otherwise:
//   mag = (word_64773C ? RandNext() % 16 : 0);
//   if (!mag) return 0;
//   needWord = (needWord & ~0xF) | (mag & 0xF);          // store mag in low nibble
//   stockDelta = -(int)(mag * 40.0f);
//   hook->EmitNeedDelta(id, needWord); hook->AdjustStock(type, stockDelta);
// Returns the stock delta magnitude consumed (mag * 40, as the original returns
// the AdjustStock result; we return the int amount removed). Consumes exactly one
// RandNext draw when word_64773C != 0.
int ApplyRandomDecayField(NeedAgent& a, NeedsCommandHook* hook);

// --- random need pick (forward-scan selection) ------------------------------

// Shared selection core (the identical RandNext()%N + forward-scan loop in every
// picker). `eligible[0..n)` are the 0/1 slots; returns the chosen slot index or
// -1 if none eligible. Consumes exactly one RandNext draw. Exposed for testing.
int PickEligibleSlot(const int* eligible, int n);

// gilde.exe 0x58b4e8 — VIBE_AiNeeds_PickRandomFlagFromFourA. 4-slot pick over the
// need-flag groups (0xF0, 0xF00, 0x3000, 0x1C000). Maps slot 0..3 -> need-id
// {2,3,4,5} and clears that group's bits. Returns the chosen need-id (0 == none).
u8 PickRandomFlagFromFourA(NeedAgent& a, NeedsCommandHook* hook);

// gilde.exe 0x58b614 — VIBE_AiNeeds_PickRandomFlagFromFourB. 4-slot pick over
// groups (0xE0000, 0x700000, 0x1800000, 0xF00). Maps slot 0..3 -> need-id
// {6,7,8,3} and clears the bits. Returns the chosen need-id (0 == none).
u8 PickRandomFlagFromFourB(NeedAgent& a, NeedsCommandHook* hook);

// gilde.exe 0x58b2f4 — VIBE_AiNeeds_PickRandomFlagFromEight. 8-slot pick over
// groups (0xF0,0xF00,0x3000,0x1C000,0xE0000,0x700000,0x1800000,0x1E000000).
// Maps slot 0..7 -> need-id {2,3,4,5,6,7,8,9} and clears the bits. Returns the
// chosen need-id (0 == none).
u8 PickRandomFlagFromEight(NeedAgent& a, NeedsCommandHook* hook);

// --- random need pick that *refills* the chosen field with a fresh value ----
//
// PickRandomNeedAndClearGroup{,B} are the two "consume + restock" pickers. Unlike
// the FlagFrom* variants (which only clear a satisfied need), these:
//   1. build the same 4-slot eligibility array (slot i eligible iff the matching
//      need-flag bits are set in the need word — table dword_5830E0/dword_5830F4
//      are all-zero in the shipped data, so eligibility is the need bits alone),
//   2. forward-scan from RandNext()%4 for the first eligible slot,
//   3. map the slot to a need-id (A: {2,3,4,5}, B: {5,6,7,8}); if need-id >= 10
//      they bail (returns 0),
//   4. look up the need-id row in the per-need table kNeedRestockTable (recovered
//      from unk_647728: {scale, cap} per need-id) and pick a fresh random
//      magnitude `m = RandNext() % (u16)(int)(cap * 0.5)` (returns 0 if that
//      bound or the draw is 0),
//   5. write `m`'s low bits into the chosen field of the need word (a true
//      assignment, not a clear), and consume `(int)(m * scale)` stock.
// Each successful call consumes exactly TWO RandNext draws (slot + magnitude);
// an early bail after the slot pick consumes one. Returns the chosen need-id
// (0 == nothing picked / bailed).
//
// gilde.exe 0x58aea8 — VIBE_AiNeeds_PickRandomNeedAndClearGroup  (low group)
u8 PickRandomNeedAndClearGroup(NeedAgent& a, NeedsCommandHook* hook);

// gilde.exe 0x58b0cc — VIBE_AiNeeds_PickRandomNeedAndClearGroupB (high group)
u8 PickRandomNeedAndClearGroupB(NeedAgent& a, NeedsCommandHook* hook);

} // namespace guild::ai
