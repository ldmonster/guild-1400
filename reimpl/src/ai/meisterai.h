#pragma once
// MeisterAi turn director — economy/intrigue DATA/RULES core (gilde.exe).
//
// VIBE_MeisterAi_ProcessPlayerTurn (0x5321ec) is the per-faction Guild-Master AI
// turn: it walks the player's buildings + workers and emits network commands
// (Command_Queue*/Enqueue*) for security sweeps, tax payouts, worker mood/relation
// adjustments, production stocking, and confrontations. The full body is deeply
// coupled to the entity arrays, the scene-tree query (GameObject_QueryFind), the
// building-value/coord/combat clusters and ~20 command builders, so the giant
// pass is DEFERRED. What is ported here are its self-contained DATA/RULES cores,
// each a pure function (the command emission goes through a mock hook so the
// sequence is verifiable):
//
//   * SecurityHeatDecrement — the per-object "heat" sweep rule
//       newHeat = heat - 2*securityLevel;  clamp(<0) -> 0    (the v123 math).
//   * MoodRelationDelta — the per-worker mood delta from the two attitude scalars
//       (worker+61 / worker+65) and the player<->worker relation value (the v124
//       HIBYTE accumulation). Returns a signed 8-bit delta.
//   * TaxPayout — the per-banker (class 5) tax: 16000 * multiplier (AiPlayer+583).
//   * MoodDecayRoll — the end-of-building random mood penalty rule.
//   * ConfrontationDecision — given the post-delta relation, decide whether to
//     spawn a confrontation action and which variant (52 vs 51).
//
// Float constants (gilde.exe): dbl_6234A8 = 0.01, dbl_6234B0 = 0.9,
// dbl_6234B8 = 0.5, flt_6234C0 = 42.0.
#include "guild/common/types.h"

namespace guild::ai {

// dbl_6234A8 — the attitude->price scale (worker+61 * 0.01) used when valuing a
// worker's output for the QueueRequest16 wage/stock command.
constexpr double kAttitudePriceScale = 0.01;   // dbl_6234A8
constexpr double kCoordScaleA        = 0.9;    // dbl_6234B0
constexpr double kCoordBiasB         = 0.5;    // dbl_6234B8
constexpr float  kCoordThreshold     = 42.0f;  // flt_6234C0

// gilde.exe 0x532e89 (core) — security-heat sweep decrement. `heat` is the
// object's heat counter (byte at +55), `securityLevel` the guarding building's
// security level. Returns the clamped new heat. Faithful to the byte-truncating
// arithmetic: the subtraction is computed in the HIBYTE of a dword and only the
// non-negative result is kept (a negative result clamps to 0).
u8 SecurityHeatDecrement(u8 heat, int securityLevel);

// gilde.exe 0x53265f..0x5327f7 (core) — per-worker mood/relation delta.
//   attitudeA = worker+61, attitudeB = worker+65, relation = player<->worker.
// Accumulates a signed 8-bit delta from the two attitude scalars, faithfully
// reproducing the /3 and /5 integer divisions, the min-1 clamps, and the sign
// rules. Returns the signed delta (the v124>>24 byte).
i8 MoodRelationDelta(int attitudeA, int attitudeB, int relation);

// gilde.exe 0x532e73 (core) — banker (class 5) tax payout amount.
//   16000 * multiplier, where `multiplier` is AiPlayer+583 (u8).
i32 TaxPayout(u8 multiplier);

// gilde.exe 0x532485 (core) — end-of-building mood-decay roll. `kind` is the
// object kind byte (*v109 == AiPlayer+0), `flagBit0` the low bit of the object's
// flag word (Begin[45] & 1). When the roll fires, returns the (negative) mood
// delta to apply (-(RandomModulo(3)+2)); returns 0 when the rule does not fire.
// Consumes the RNG exactly as the original (RandomModulo(100) then RandomModulo(3)).
int MoodDecayRoll(u8 kind, bool flagBit0);

// gilde.exe 0x5328a3 (core) — confrontation spawn decision. Given the worker's
// post-delta relation `newRelation`, decides whether to spawn a confrontation
// action. Returns: 0 = none, 52 = "challenge" variant, 51 = "duel" variant.
// Consumes RNG: RandomModulo(0x4A) for the gate, then RandomModulo(100) for the
// variant. (The +N time-advance for the appointment is left to the caller.)
int ConfrontationDecision(int newRelation);

// --- a small driver exercising the rule cores against a building list --------
// A synthetic worker the turn director processes (the fields the rule cores read).
struct TurnWorker {
    i32 personId = 0;      // dword_12CE914 column value (used in commands)
    int attitudeA = 100;   // worker+61
    int attitudeB = 100;   // worker+65
    int relation  = 0;     // player<->worker relation value
    u8  kind       = 0;    // worker+0 kind byte
    bool flagBit0  = false; // Begin[45]&1
};

// Command-emission hook: the director routes every decision through this so the
// emitted sequence is verifiable (mirrors the Command_Queue*/Coord27/Cmd15 calls).
//   op: a small tag identifying which command (see TurnOp); a/b/c: its operands.
enum TurnOp : int {
    kTurnTax        = 1,   // EnqueueCmd15 banker tax (amount in c)
    kTurnMoodCoord  = 2,   // QueueRequestCoord27 mood delta (delta in c)
    kTurnDecay      = 3,   // AdjustMoodAndNotify decay (negative delta in c)
    kTurnConfront   = 4,   // QueueRequestSlotReset28 confrontation (variant in c)
};
using TurnCmdFn = void (*)(int op, i32 a, i32 b, i32 c);

// Drives the per-worker mood/relation + decay rules over `workers`, emitting the
// resulting commands through `emit`. `playerId` is the faction id (for relation).
// Returns the number of commands emitted (the original's v3 accumulator).
int RunWorkerMoodPass(i32 playerId, const TurnWorker* workers, int count, TurnCmdFn emit);

} // namespace guild::ai
