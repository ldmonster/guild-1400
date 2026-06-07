#pragma once
// Tavern card-game AI for the Guild simulation (gilde.exe).
//
// A blackjack-style ("17") betting card game the AI plays against the player in
// the tavern's "dark corner". A hand is three cards each worth 1..6 (a die); the
// state record holds both players' hands, hand sizes, the current decision phase
// ("Aktion": 0..4), the pot, and the seat ids. The AI decides whether to Hold(2)/
// Raise/Draw(3)/Knock(4) using a per-strength threshold table and the shared CRT
// RNG, plays cards through a tiny state-transition table, and resolves the round.
//
// Translated functions (all from gilde.exe):
//   VIBE_AiCardGame_DecideMove        0x466ec4
//   VIBE_AiCardGame_CanPlayCard       0x4668d4
//   VIBE_AiCardGame_PlayCard          0x466920
//   VIBE_AiCardGame_EvaluateHand      0x466980
//   VIBE_AiCardGame_TakeTurn          0x466da0
//   VIBE_AiCardGame_ShouldRaise       0x467178
//   VIBE_AiCardGame_UpdateRoundState  0x467038
//
// Recovered tables/constants:
//   dword_46637D / byte_466381 — card-play state-transition table (9 pairs).
//   dword_466394 — 5x6 float threshold table (per strength tier 0..4).
//   flt_61A1xx / dbl_61A1xx — decision/draw float constants.
//
// The two outbound couplings are modeled as hooks so the game logic is testable
// in isolation:
//   * VIBE_Building_EvalProductionRating (0x58a794) — used as a "hand strength"
//     oracle (the AI's perceived strength of a seat). Hook: SetHandStrengthHook.
//   * VIBE_Command_QueueRequest16 (0x494630) — TakeTurn emits a bet command when
//     the active seat is the human (seat type byte +2 == 6). Hook: SetBetCmdHook.
#include "guild/common/types.h"

namespace guild::ai {

// --- Card-game state record (the `a1` base the functions index) --------------
// Recovered field offsets from DecideMove/PlayCard/EvaluateHand/UpdateRoundState.
// Two seats: "A" (own, offsets +12..+44) and "B" (opponent, +40..+68). Each seat
// stores: a seat-entity ptr (+12 / +40), a bet/stake (+0 / +4), a hand-size byte
// (+16 / +44), a 3-byte hand (cards 1..6) (+20 / +48), a decision/phase byte
// (+36 / +64), and a "mood color" word (+18 / +46). Shared: phase byte +8, the
// running pot (+68 in PlaceBet — kept separate), the active-seat ptr at +12/+40.
//   +0   i32  seat-A stake / per-card value used by TakeTurn
//   +4   i32  seat-B stake (raise increment)
//   +8   u8   round phase (1 deal, 2 reveal, 3/4 act, 5 showdown, 6 fold)
//   +12  i32  seat-A entity ptr (== a2 when it's A's move)
//   +16  u8   seat-A hand size (0..3)
//   +18  u16  seat-A mood color
//   +20  u8[] seat-A hand cards (3 bytes, values 1..6)
//   +36  u8   seat-A decision phase (Aktion: 0..4)
//   +40  i32  seat-B entity ptr (== a2 when it's B's move)
//   +44  u8   seat-B hand size
//   +46  u16  seat-B mood color
//   +48  u8[] seat-B hand cards (3 bytes)
//   +64  u8   seat-B decision phase
//   +68  i32  pot accumulator (TakeTurn adds stakes here; a1[17])
struct CardGameState {
    u8 bytes[80] = {};

    i32 geti32(int off) const {
        return static_cast<i32>(bytes[off] | (bytes[off + 1] << 8)
             | (bytes[off + 2] << 16) | (static_cast<u32>(bytes[off + 3]) << 24));
    }
    void seti32(int off, i32 v) {
        bytes[off]     = static_cast<u8>(v);
        bytes[off + 1] = static_cast<u8>(v >> 8);
        bytes[off + 2] = static_cast<u8>(v >> 16);
        bytes[off + 3] = static_cast<u8>(v >> 24);
    }
    u16 getu16(int off) const { return static_cast<u16>(bytes[off] | (bytes[off + 1] << 8)); }
};

// Per-seat addressing helper. seat==0 -> A (+12/+16/+18/+20/+36),
// seat==1 -> B (+40/+44/+46/+48/+64). Matches the if/else seat-resolution the
// originals do by comparing a2 against a1[3] (+12) and a1[10] (+40).
struct SeatOffsets {
    int entityPtr, handSize, mood, hand, decision, stake;
};
SeatOffsets CardGameSeat(int seat); // seat 0 == A, 1 == B

// --- outbound hooks ----------------------------------------------------------
// Building_EvalProductionRating(seatEntityPtr, mode): the AI's "hand strength"
// oracle. The original passes a seat entity ptr and a mode (0/2/3/4). Default
// returns 0.0 so the game logic runs deterministically in tests.
using HandStrengthFn = double (*)(i32 seatEntityPtr, int mode);
void SetHandStrengthHook(HandStrengthFn fn);

// Command_QueueRequest16(a1, a2, a3, a4): the bet/transfer command TakeTurn emits
// when the active seat is the human. Default is a no-op recorder-free stub.
using BetCmdFn = void (*)(i32 a1, i32 a2, i32 a3, u8 a4);
void SetBetCmdHook(BetCmdFn fn);

// --- functions ---------------------------------------------------------------

// gilde.exe 0x466ec4 — VIBE_AiCardGame_DecideMove (__usercall, al=, eax=state,
// edx=seatPtr). Returns the AI's chosen action for `seatPtr`'s seat:
//   0 reject (wrong phase / unknown seat), or the action code for the seat's
//   current decision byte: phase 0 -> 1 (deal); phase 1/2 -> 2(hold) or 3(draw)
//   by a strength-scaled probability vs the hand total toward 17; phase 3 ->
//   3(draw) or 4(knock). Uses the seat's hand sum vs target 17 and RandomFloatScaled.
u8 DecideMove(CardGameState& st, i32 seatPtr);

// gilde.exe 0x4668d4 — VIBE_AiCardGame_CanPlayCard (eax=state, edx=seatPtr,
// bl=action). True iff (currentDecision, action) is a valid transition in the
// 9-pair table dword_46637D. Returns 0 for unknown seat.
int CanPlayCard(const CardGameState& st, i32 seatPtr, u8 action);

// gilde.exe 0x466920 — VIBE_AiCardGame_PlayCard. Applies `action` to the seat's
// decision byte via the transition table (sets it to byte_466381[pairIdx]).
// Returns 1 on a valid transition, 0 otherwise.
int PlayCard(CardGameState& st, i32 seatPtr, u8 action);

// gilde.exe 0x466980 — VIBE_AiCardGame_EvaluateHand (al=, eax=state, edx=seatPtr,
// ebx=draw). If draw!=0: deals a fresh 3-card hand for the seat using the
// strength threshold table + RNG (initial deal). If draw==0: decides to add(+1)/
// keep(0)/discard(-1) a card based on the hand total and the seat strength, then
// appends one threshold-rolled card. Returns the seat hand index touched.
int EvaluateHand(CardGameState& st, i32 seatPtr, int draw);

// gilde.exe 0x466da0 — VIBE_AiCardGame_TakeTurn (eax=state, edx=seatPtr,
// bl=action). Validates `action` via CanPlayCard, then for action 1(raise)/
// 2(hold)/3(draw)/4(knock): emits a bet command if the seat is human, adds the
// stake to the pot, re-evaluates the hand (for 1/2), and advances the decision
// via PlayCard. Returns 1 on success, 0 if rejected.
int TakeTurn(CardGameState& st, i32 seatPtr, u8 action);

// gilde.exe 0x467178 — VIBE_AiCardGame_ShouldRaise (fastcall, a1=ownSeatPtr,
// a2=oppSeatPtr). Probabilistic raise decision comparing own vs opponent
// strength (mode 3): rand <= own - opp*0.5 + 0.1.
bool ShouldRaise(i32 ownSeatPtr, i32 oppSeatPtr);

// gilde.exe 0x467038 — VIBE_AiCardGame_UpdateRoundState. Advances the shared
// phase byte (+8) of the round given both seats' hands/decisions. Implements the
// reveal/showdown/fold resolution. Mutates st in place; returns &st.bytes[0].
u8* UpdateRoundState(CardGameState& st);

} // namespace guild::ai
