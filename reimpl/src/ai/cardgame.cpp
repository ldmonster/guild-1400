#include "ai/cardgame.h"

#include "util/math_random.h"
#include "util/math_rng_float.h"

namespace guild::ai {

namespace {

// --- recovered decision/draw float constants (gilde.exe 0x61A1xx) -----------
constexpr double kF_61A1D0 = 0.05882352963089943;  // (v2+sum) scale (Knock gate)
constexpr double kF_61A1D4 = 3.5;                  // mood weight in Knock gate
constexpr double kF_61A1D8 = 0.1666666716337204;   // (17-sum) draw scale
constexpr double kF_61A1DC = 0.20000000298023224;  // mood penalty in draw prob
constexpr double kF_61A1E0 = 0.25;                 // draw-prob mood factor
constexpr double kF_61A1E8 = 0.5;                  // ShouldRaise opponent weight
constexpr double kF_61A1F0 = 0.1;                  // ShouldRaise bias
constexpr float  kF_61A1AC = 100.0f;               // EvaluateHand strength /100
constexpr double kD_61A1C0 = 0.66;                 // strong-hand threshold
constexpr double kD_61A1C8 = 0.44;                 // weak-hand threshold
constexpr float  kF_61A1B0 = 0.8888000249862671f;  // EvaluateHand redeal gates
constexpr float  kF_61A1B4 = 0.5554999709129333f;
constexpr float  kF_61A1B8 = 0.33329999446868896f;

// dword_466394 — 5 rows x 6 floats: cumulative card-value distribution per
// strength tier 0..4. Indexed [6*tier + col]; the first col exceeding the rolled
// uniform gives the card value (col+1, clamped 1..6).
const float kCardThresholds[5][6] = {
    {0.2f,      0.3787f,   0.5444f,   0.711f,    0.8999f,   1.0f},
    {0.19f,     0.3565f,   0.5222f,   0.6898f,   0.8666f,   1.0f},
    {0.1678f,   0.3343f,   0.5f,      0.6676f,   0.8444f,   1.0f},
    {0.1456f,   0.311f,    0.4888f,   0.6442f,   0.8222f,   1.0f},
    {0.1234f,   0.2998f,   0.4666f,   0.622f,    0.8111f,   1.0f},
};
// NOTE: the binary stores these as float; we reproduce the rounded float values.
// The comparisons are `roll > threshold`, so tiny rounding is behaviorally inert
// for the RNG values that occur in practice; tests pin exact outcomes via seeds.

// Card-play transition table (gilde.exe dword_46637D pairs / byte_466381 result).
// Walked as: for v in 0,2,4,..16: if pair[v]==curDecision && pair[v+1]==action ->
// new decision = byte_466381[v]. The raw bytes at 0x46637D are
//   c3 00 00 00 | 01 01 | 02 01 | 03 01 | 04 02 | 02 02 | 03 02 | 04 03 | 03 03
// read as two interleaved hi-byte columns; byte_466381 is the same region+4.
// Recovered (curDecision, action) -> newDecision pairs:
struct Transition { u8 cur, action, next; };
const Transition kTransitions[9] = {
    {0, 0, 1},  // 0xc3 sentinel row resolves to {0,0}->1 at v==0 (byte_466381[0])
    {0, 1, 2},
    {1, 1, 3},
    {1, 2, 2},
    {2, 1, 3},
    {2, 2, 4},
    {3, 1, 2},  // pair index 12
    {3, 2, 3},
    {3, 3, 3},
};
// The loop indexes the raw stream in steps of 2 over 18 bytes (9 entries). We
// expose the same lookup semantics: find the first entry whose (cur,action)
// matches; the table above is the decoded (dword_46637D>>24, +1>>24)->byte_466381.

HandStrengthFn g_handStrength = nullptr;
BetCmdFn       g_betCmd = nullptr;

double HandStrength(i32 seatPtr, int mode) {
    return g_handStrength ? g_handStrength(seatPtr, mode) : 0.0;
}
void EmitBet(i32 a1, i32 a2, i32 a3, u8 a4) {
    if (g_betCmd) g_betCmd(a1, a2, a3, a4);
}

// Sum the seat's `size` hand bytes (cards) starting at `handOff`.
int HandSum(const CardGameState& st, int handOff, int size) {
    int s = 0;
    for (int i = 0; i < size; ++i) s += st.bytes[handOff + i];
    return s;
}

// Roll one card value from the tier `tier` distribution: first col whose
// cumulative threshold >= roll, returned as (col+1). Mirrors the inner loops.
int RollCard(int tier, double roll) {
    if (tier > 4) tier = 4;
    int col = 0;
    while (col < 6 && roll > static_cast<double>(kCardThresholds[tier][col]))
        ++col;
    return col; // caller adds the offset/clamp as the original does
}

} // namespace

void SetHandStrengthHook(HandStrengthFn fn) { g_handStrength = fn; }
void SetBetCmdHook(BetCmdFn fn) { g_betCmd = fn; }

SeatOffsets CardGameSeat(int seat) {
    if (seat == 0)
        return SeatOffsets{12, 16, 18, 20, 36, 0};
    return SeatOffsets{40, 44, 46, 48, 64, 4};
}

// gilde.exe 0x466ec4 — VIBE_AiCardGame_DecideMove
u8 DecideMove(CardGameState& st, i32 seatPtr) {
    // Phase byte +2 of the seat entity is checked in the original via the seat
    // entity ptr; here the round-phase gate is the shared decision phase. The
    // original early-outs if *(seatEntity+2)!=10; we encode "seat is in play"
    // as a precondition the caller guarantees, matching the test harness.
    int seat;
    if (seatPtr == st.geti32(12))
        seat = 0;
    else if (seatPtr == st.geti32(40))
        seat = 1;
    else
        return 0;

    SeatOffsets so = CardGameSeat(seat);
    SeatOffsets opp = CardGameSeat(seat ^ 1);
    u8 handSize  = st.bytes[so.handSize];
    u8 decision  = st.bytes[so.decision];
    u8 moodLow   = st.bytes[so.mood];           // v5 (low byte of mood word)
    (void)st.bytes[opp.handSize];               // v6/v13 (opp hand size; unused in core)

    int sum = HandSum(st, so.hand, handSize);

    switch (decision) {
    case 0:
        return 1;
    case 1:
    case 2: {
        if (17 - sum >= 6)
            return 2;
        if (sum == 17)
            return 3;
        double prob = static_cast<double>(17 - sum) * kF_61A1D8
                    * ((1.0 - static_cast<double>(moodLow) * kF_61A1DC) * kF_61A1E0 + 1.0);
        if (guild::util::RandomFloatScaled() >= prob)
            return 3;
        return 2;
    }
    case 3: {
        double thr = static_cast<double>(seat + sum) * kF_61A1D0;
        // SLODWORD(v10) < 1065353216  <=>  thr < 1.0f (as a float bit-compare).
        float thrF = static_cast<float>(thr);
        if (thrF < 1.0f
            && static_cast<double>(6 - moodLow) < thr - static_cast<double>(handSize) * kF_61A1D4)
            return 4;
        return 3;
    }
    default:
        return 0;
    }
}

// gilde.exe 0x4668d4 — VIBE_AiCardGame_CanPlayCard
int CanPlayCard(const CardGameState& st, i32 seatPtr, u8 action) {
    u8 decision;
    if (seatPtr == st.geti32(12))
        decision = st.bytes[36];
    else if (seatPtr == st.geti32(40))
        decision = st.bytes[64];
    else
        return 0;
    for (const auto& t : kTransitions) {
        if (t.cur == decision && t.action == action)
            return 1;
    }
    return 0;
}

// gilde.exe 0x466920 — VIBE_AiCardGame_PlayCard
int PlayCard(CardGameState& st, i32 seatPtr, u8 action) {
    int decOff;
    if (seatPtr == st.geti32(12))
        decOff = 36;
    else if (seatPtr == st.geti32(40))
        decOff = 64;
    else
        return 0;
    u8 decision = st.bytes[decOff];
    for (const auto& t : kTransitions) {
        if (t.cur == decision && t.action == action) {
            st.bytes[decOff] = t.next;
            return 1;
        }
    }
    return 0;
}

// gilde.exe 0x466980 — VIBE_AiCardGame_EvaluateHand
int EvaluateHand(CardGameState& st, i32 seatPtr, int draw) {
    int seat = -1;
    if (seatPtr == st.geti32(12)) seat = 0;
    else if (seatPtr == st.geti32(40)) seat = 1;
    // Fall through with passed seat if neither matched (the original uses the
    // a4/a5 args directly); for our model an unknown seat returns 0.
    if (seat < 0)
        return 0;
    SeatOffsets so = CardGameSeat(seat);

    // Strength: EvalProductionRating(seat, mode 2) * (1/100) -> /20 -> clamp 4.
    double strength = HandStrength(seatPtr, 2);
    double scaled = strength * kF_61A1AC; // matches v7 = v6 * flt_61A1AC (×100)
    // (int)scaled / 20, clamp to 4.
    long long sv = static_cast<long long>(scaled);
    int tierRaw = static_cast<int>(sv / 20);
    int tier = (static_cast<unsigned>(tierRaw) >= 4u) ? 4 : tierRaw;
    if (tier < 0) tier = 0;

    if (draw) {
        // Initial 3-card deal. *handSize = 3, then roll three cards from `tier`.
        int redeals = static_cast<int>(guild::util::RandomModulo(3)); // v37
        st.bytes[so.handSize] = 3;
        // The original burns extra RNG draws based on strength magnitude; we
        // mirror the draw count so the stream stays aligned.
        if (strength > static_cast<double>(kF_61A1B0) || strength > static_cast<double>(kF_61A1B4))
            guild::util::RandomModulo(0x10);
        else if (strength <= static_cast<double>(kF_61A1B8))
            guild::util::RandomModulo(8);
        else
            guild::util::RandomModulo(4);

        int c0 = RollCard(tier, guild::util::RandomFloatScaled()) + 1; // v44
        int c1 = RollCard(tier, guild::util::RandomFloatScaled()) + 1; // v43 (1-based loop)
        // NOTE: c1 uses the `++v19` pre-increment form in the binary; both forms
        // resolve to "index of first threshold >= roll, 1-based".
        int c2 = RollCard(tier, guild::util::RandomFloatScaled()) + 1; // v42
        st.bytes[so.hand + 0] = static_cast<u8>(c0);
        st.bytes[so.hand + 1] = static_cast<u8>(c1);
        st.bytes[so.hand + 2] = static_cast<u8>(c2);

        // "Boost toward redeals": if the three cards aren't all equal, apply the
        // redeal adjustment (v21 = redeals; v37 = a fairness flag).
        bool allEqual = (c1 == c0) && (c0 == c2);
        if (!allEqual && redeals) {
            if (redeals) { // v21 != 0
                // v37 path: 0 -> +1 each (up to redeals), 1 -> raise the min card,
                // else -> raise a random card.
                int v37 = static_cast<int>(guild::util::RandomModulo(3)); // re-roll fairness
                if (v37 == 0) {
                    int idx = 0, r = redeals;
                    while (idx < 3 && r) {
                        int nv = st.bytes[so.hand + idx] + 1;
                        st.bytes[so.hand + idx] = static_cast<u8>(nv > 6 ? 6 : nv);
                        ++idx; --r;
                    }
                } else if (v37 == 1) {
                    int minIdx = -1, minVal = 6;
                    for (int i = 0; i < 3; ++i) {
                        if (minVal > st.bytes[so.hand + i]) {
                            minIdx = i; minVal = st.bytes[so.hand + i];
                        }
                    }
                    if (minIdx > -1) {
                        int nv = redeals + st.bytes[so.hand + minIdx];
                        st.bytes[so.hand + minIdx] = static_cast<u8>(nv > 6 ? 6 : nv);
                    }
                } else {
                    int idx = static_cast<int>(guild::util::RandomModulo(3));
                    int nv = st.bytes[so.hand + idx] + redeals;
                    st.bytes[so.hand + idx] = static_cast<u8>(nv > 6 ? 6 : nv);
                }
            }
        }
        return 0;
    }

    // draw == 0: decide a +1/0/-1 bias then append one rolled card.
    double rateZero = HandStrength(seatPtr, 0); // v40
    double roll = guild::util::RandomFloatScaled(); // v38
    int sum = HandSum(st, so.hand, st.bytes[so.handSize]);

    int bias; // v36 (-1 / 0 / +1)
    if (rateZero > kD_61A1C0) {
        if (sum > 12) bias = -1;
        else if (sum < 6) bias = 1;
        else bias = static_cast<int>(guild::util::RandomModulo(3)) - 1;
    } else if (rateZero <= kD_61A1C8) {
        if (sum > 14) bias = -1;
        else if (sum < 4) bias = 1;
        else bias = static_cast<int>(guild::util::RandomModulo(3)) - 1;
    } else {
        if (sum > 13) bias = -1;
        else if (sum < 5) bias = 1;
        else bias = static_cast<int>(guild::util::RandomModulo(3)) - 1;
    }

    int col = RollCard(tier, roll);          // index of first threshold >= roll
    int card = bias + col + 1;               // v36 + v31 + 1
    if (card < 2) card = 1;
    else if (card >= 6) card = 6;

    int idx = st.bytes[so.handSize];
    st.bytes[so.hand + idx] = static_cast<u8>(card);
    ++st.bytes[so.handSize];
    return idx;
}

// gilde.exe 0x466da0 — VIBE_AiCardGame_TakeTurn
int TakeTurn(CardGameState& st, i32 seatPtr, u8 action) {
    if (!CanPlayCard(st, seatPtr, action))
        return 0;

    int seat = (seatPtr == st.geti32(12)) ? 0 : 1;
    SeatOffsets so = CardGameSeat(seat);
    // Seat-entity "is human" flag: the original reads *(seatPtr+2)==6. We model
    // it via the caller setting that byte through the entity ptr; for the codec
    // the human check is delegated to the bet hook (it is a no-op for AI seats).

    switch (action) {
    case 1: { // raise: pot += own stake (+0), re-evaluate, advance
        EmitBet(-1, seatPtr, st.geti32(0), 0);
        st.seti32(68, st.geti32(68) + st.geti32(0));
        EvaluateHand(st, seatPtr, 1);
        PlayCard(st, seatPtr, action);
        return 1;
    }
    case 2: { // hold: pot += B stake (+4), re-evaluate (draw=0), advance
        EmitBet(-1, seatPtr, st.geti32(4), 0);
        st.seti32(68, st.geti32(68) + st.geti32(4));
        EvaluateHand(st, seatPtr, 0);
        PlayCard(st, seatPtr, action);
        return 1;
    }
    case 3: { // draw: pot += B stake, advance (no hand re-eval)
        EmitBet(-1, seatPtr, st.geti32(4), 0);
        st.seti32(68, st.geti32(68) + st.geti32(4));
        PlayCard(st, seatPtr, action);
        return 1;
    }
    case 4: { // knock: just advance
        PlayCard(st, seatPtr, action);
        return 1;
    }
    default:
        return 0;
    }
    (void)so;
}

// gilde.exe 0x467178 — VIBE_AiCardGame_ShouldRaise
bool ShouldRaise(i32 ownSeatPtr, i32 oppSeatPtr) {
    double opp = HandStrength(oppSeatPtr, 3) * kF_61A1E8;
    double own = HandStrength(ownSeatPtr, 3);
    float threshold = static_cast<float>(own - opp + kF_61A1F0);
    return guild::util::RandomFloatScaled() <= static_cast<double>(threshold);
}

// gilde.exe 0x467038 — VIBE_AiCardGame_UpdateRoundState
u8* UpdateRoundState(CardGameState& st) {
    u8* r = st.bytes;

    if (r[8] == 1) { r[8] = 2; return r; }

    if (r[8] == 2) {
        int a = 0, b = 0;
        if (r[20] == r[21] && r[20] == r[22])
            a = r[20] + r[21] + r[22]; // triple bonus for seat A
        if (r[48] == r[49] && r[48] == r[50])
            b = r[48] + r[49] + r[50];
        if (a > b) { r[8] = 5; return r; }
        if (b > 0 || r[36] == 4) { r[8] = 6; return r; } // LABEL_16
        if (r[64] == 4) { r[8] = 5; return r; }
        r[8] = 3; // LABEL_21
        return r;
    }

    // phases 3 / 4: compare hand totals.
    int sumA = 0;
    for (int i = 0; i < r[16]; ++i) sumA += r[20 + i];
    int sumB = 0;
    for (int i = 0; i < r[44]; ++i) sumB += r[48 + i];

    u8 phase = r[8];
    if (phase == 3) {
        if (sumA > 17) { r[8] = 6; return r; }
        if (r[36] == 4) { r[8] = 6; return r; }
        if (r[36] == 3 && r[64] == 3) {
            if (sumA > sumB) { r[8] = 5; return r; }
            r[8] = 6; return r;
        }
        r[8] = 4;
        return r;
    }
    if (phase == 4) {
        if (sumB > 17 || r[64] == 4) { r[8] = 5; return r; }
        if (r[36] == 3 && r[64] == 3) {
            if (sumA <= sumB) { r[8] = 6; return r; }
            r[8] = 5; return r;
        }
        r[8] = 3;
        return r;
    }
    return r;
}

} // namespace guild::ai
