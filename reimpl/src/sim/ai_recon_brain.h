// ai_recon_brain.h — 1:1 reconstruction of the pure AI decision/scoring kernels
// from gilde.exe (Die Gilde / Europa 1400). These are the arithmetic / decision
// cores extracted faithfully from the "Meister" AI planner, the AI scoring
// helpers and the AI-method dispatch family.
//
// SCOPE / FIDELITY NOTE
// --------------------
// The original AI functions are deeply coupled to engine-global state: the He
// handler tables (dword_B56xxx), the entity array (word_12CE910[268*i] stride),
// the command queue (VIBE_Command_*), GameObject queries and Person records.
// Those couplings are *plumbing*, not decision logic. Per the project rules
// (implement the pure scoring/decision/dispatch logic 1:1; hook coupled leaves
// with inert defaults; never fake), this file reconstructs ONLY the pure,
// self-contained arithmetic kernels that can be verified by golden vectors, and
// exposes the coupled leaves as caller-supplied inputs / hook structs.
//
// Every kernel carries its gilde.exe provenance (address + symbol).

#pragma once

#include "guild/common/types.h"

namespace guild::sim {

// ---------------------------------------------------------------------------
// Need-tier cascade — shared kernel of
//   gilde.exe 0x469e1c VIBE_AiMethod_EvalGoToDrink  (thirst path)
//   gilde.exe 0x46a4d8 VIBE_AiMethod_EvalGoToEat    (hunger path)
//
// Both functions map a person's need byte (a1[128+i], an unsigned __int8) to a
// discrete "time cost" tier via an identical 5-threshold cascade:
//
//   v13 >= 0x2A  ? (>=0x54 ? (>=0x7E ? (>=0xA8 ? (>=0xD2 ? 10 : 7) : 5) : 3) : 2)
//                : 1
//
// i.e. need<42 -> 1, [42,84) -> 2, [84,126) -> 3, [126,168) -> 5,
//      [168,210) -> 7, >=210 -> 10.   (decompile @0x469fd6..0x46a289)
// ---------------------------------------------------------------------------
inline i32 AiNeedTier(u8 needByte) {
    if (needByte >= 0x2A) {
        if (needByte >= 0x54) {
            if (needByte >= 0x7E) {
                if (needByte >= 0xA8) {
                    if (needByte >= 0xD2)
                        return 10;   // 0x46a21b / 0x46a289
                    return 7;        // 0x46a20b / 0x46a279
                }
                return 5;            // 0x46a1f6 / 0x46a265
            }
            return 3;                // 0x46a1e1 / 0x46a251
        }
        return 2;                    // 0x46a1cc / 0x46a23d
    }
    return 1;                        // 0x46a124 / 0x469fdc
}

// EvalGoToDrink/Eat then forms a candidate score for slot i as
//   tier_i + debugBonus     (where debugBonus = -1.0f when the DebugCmd dispatch
//                            returns bit 2 set, else 0.0f; @0x469faf/0x46a153)
// truncated toward zero (VIBE_Coord_ConvertX == (int)(double)). This is the cost
// compared against the person's stat (a1[101], "available time/credit"):
//   the slot is VALID iff cost <= a1[101].   (@0x46a03a)
// Exposed as a pure helper; debugBonus and the stat budget are caller inputs.
inline i32 AiNeedSlotCost(u8 needByte, float debugBonus) {
    double v = static_cast<double>(AiNeedTier(needByte)) + static_cast<double>(debugBonus);
    return static_cast<i32>(v);   // truncation matches VIBE_Coord_ConvertX
}

inline bool AiNeedSlotValid(u8 needByte, float debugBonus, i32 statBudget) {
    return AiNeedSlotCost(needByte, debugBonus) <= statBudget;
}

// The per-slot "preference ratio" the planner stores for a still-valid slot
// (@0x46a2f0):  ratio_i = (double)(i16)needByte / (double)rateScalar
// where rateScalar = VIBE_Economy_LookupRateScalar(slotType), but substituted
// with 252 when the lookup returns 0 (@0x469f86). Exposed pure.
inline double AiNeedSlotRatio(i16 needByteSigned, i32 rateScalar) {
    i32 r = (rateScalar == 0) ? 252 : rateScalar;
    return static_cast<double>(needByteSigned) / static_cast<double>(r);
}

// dbl_61A3B0 — the sentinel the drink/eat eval compares the need against to
// decide "slot is unusable" (@0x46a194 / 0x46a2b8). In the binary it is the
// double 0.0 (a -1 sentinel narrowed to (i16) then promoted compares non-equal;
// the live value at 0x61A3B0 is 0.0). Kept named for provenance.
constexpr double kAiNeedDeadSentinel = 0.0;

// ---------------------------------------------------------------------------
// gilde.exe 0x459264 VIBE_Ai_CalcBankmeister — pure numeric kernels.
//
// The bankmeister AI does two independent numeric jobs; both are reconstructed
// here 1:1. The coupling (He outstanding-loan count, command emission, vault
// counting) is provided as inputs.
//
// Float constants (verified via get_bytes):
//   dbl_6198C8 = 1.03   dbl_6198D0 = 1.5   dbl_6198D8 = 2.25   dbl_6198E0 = 0.8
// ---------------------------------------------------------------------------
constexpr double kBankReserveLow   = 1.03;  // dbl_6198C8 (low-reserve buy factor)
constexpr double kBankReserveMidHi = 1.5;   // dbl_6198D0 (mid threshold * capital)
constexpr double kBankReserveHi    = 2.25;  // dbl_6198D8 (oversupply threshold)
constexpr double kBankReserveSell  = 0.8;   // dbl_6198E0 (deficit refill factor)

// Interest-rate adjustment state machine (@0x4592d3..0x4595a3).
//   cur  = current set rate (person[101])
//   law  = the law-13 target rate (v42, from Gesetz record 13)
//   loans= number of outstanding loan handlers (i, He filter 35)
//   r0,r1= two independent VIBE_Math_RandomModulo(3) draws (0..2) consumed in the
//          order the binary draws them (only the branch that runs reads them).
// Returns the new rate to push.  Pure; reproduces every clamp.
inline i32 BankmeisterNewRate(i32 cur, i32 law, i32 loans, i32 randUp, i32 randDown) {
    long long d = static_cast<long long>(cur) - static_cast<long long>(law);
    long long absd = (d < 0) ? -d : d;        // |cur-law|
    if (absd > 3) {
        // Far from target: single-step toward it. (@0x4592eb)
        return (cur <= law) ? (cur + 1) : (cur - 1);
    }
    // Close to target.  (@0x4594df)
    if (loans == 0 && (law - 2) < cur) {
        // Few/no loans and rate not far below target-2: nudge down a little.
        // v44 = cur - 1 - rand(3); clamp >= law-2; clamp >= 4.  (@0x4594f3)
        i32 v = cur - 1 - randDown;
        if ((law - 2) > v) v = law - 2;
        if (v < 4) v = 4;
        return v;
    }
    if (loans <= 3) {
        // Stable: hold the current rate. (@0x459552 LABEL_11 path, no push)
        return cur;
    }
    if (cur >= law) {
        return cur;   // already at/above target with many loans: hold
    }
    // Many loans, rate below target: raise toward target. (@0x45957f)
    // v44 = cur + 1 + rand(3); clamp <= law+3.
    i32 v = cur + 1 + randUp;
    if ((law + 3) < v) v = law + 3;
    return v;
}

// Reserve-rebalance amounts (@0x45935b..0x4596d9). For each vault tier the AI
// looks at the counted stock `count` against fractions of its capital `capital`:
//   low band  : count < 3*capital/20  -> BUY  (int)((5*capital/20 - count)*1.03)
//   high band : count > 6*capital/20  -> SELL (int)((count - 4*capital/20)*1.03)
// These use integer division exactly as the binary (truncating, signed).
struct BankReserveOp {
    enum Kind { None, Buy, Sell } kind = None;
    i32 rawDelta = 0;   // v47: the integer stock delta before the *1.03 scale
    i32 amount   = 0;   // v45/v43: scaled order amount, (int)(rawDelta*1.03)
};
inline BankReserveOp BankmeisterReserveTier(i32 count, i32 capital) {
    BankReserveOp op;
    if (count < 3 * capital / 20) {
        op.kind = BankReserveOp::Buy;
        op.rawDelta = 5 * capital / 20 - count;             // v47 (@0x459390)
        op.amount = static_cast<i32>(static_cast<double>(op.rawDelta) * kBankReserveLow);
        return op;
    }
    if (count > 6 * capital / 20) {
        op.kind = BankReserveOp::Sell;
        op.rawDelta = count - 4 * capital / 20;             // v47 (@0x4595f3)
        op.amount = static_cast<i32>(static_cast<double>(op.rawDelta) * kBankReserveLow);
        return op;
    }
    return op;
}

// Final capital top-up / draw-down after the per-tier pass (@0x45942e..0x4596d9).
//   count   = total vault count, capital = bank capital, netMoved = running v10
// If count < capital*1.5 and (capital - netMoved) > 0:
//      deficit refill = (int)((capital - netMoved) * 0.8)            -> BUY
// Else if (not a 6/7 building type) and count > 2*capital:
//      oversupply sell = (int)((double)count - capital*2.25)         -> SELL
// `buildingTypeBlocksSell` is the (type==6||type==7) gate (@0x459691).
inline BankReserveOp BankmeisterFinalBalance(i32 count, i32 capital, i32 netMoved,
                                             bool buildingTypeBlocksSell) {
    BankReserveOp op;
    if (static_cast<double>(count) < static_cast<double>(capital) * kBankReserveMidHi) {
        i32 v19 = capital - netMoved;
        if (v19 > 0) {
            op.kind = BankReserveOp::Buy;
            op.rawDelta = v19;
            op.amount = static_cast<i32>(static_cast<double>(v19) * kBankReserveSell);
            return op;
        }
    }
    if (!buildingTypeBlocksSell && 2 * capital < count) {
        op.kind = BankReserveOp::Sell;
        op.rawDelta = count;
        double v39 = static_cast<double>(count) - static_cast<double>(capital) * kBankReserveHi;
        op.amount = static_cast<i32>(v39);
        return op;
    }
    return op;
}

// ---------------------------------------------------------------------------
// gilde.exe 0x4686a4 VIBE_AiMethod_EvalGroupComposition — pure kernels.
//
// Plans a "group" (escort/retinue) by balancing member ranks against the
// leader's rank and a target size. The coupled parts (Person rank lookups,
// command emission, wealth) are inputs; the arithmetic is reconstructed 1:1.
//
// flt_61A2B4 = 0.001 (verified via get_bytes).
// ---------------------------------------------------------------------------
constexpr float kGroupWealthScale = 0.0010000000474974513f; // flt_61A2B4

// Input validation cascade (@0x4686b3..0x4686d2). Returns the exact negative
// code the binary returns, or 0 if all checks pass. (recordPtr non-null is the
// caller's responsibility; here `hasLeader`/`leaderId` model *a1 and (*a1).)
enum class GroupValidate : i32 {
    Ok          = 0,
    NullStruct  = -1,   // !a1
    NullLeader  = -2,   // !*a1
    WantMaleBad = -3,   // a1[36] > 5
    WantFemBad  = -4,   // a1[40] > 5
    SizeBad     = -5,   // a1[48]==0 || a1[48] > 4
};
inline GroupValidate GroupCompositionValidate(bool hasStruct, bool hasLeader,
                                              u32 wantA, u32 wantB, u8 size) {
    if (!hasStruct) return GroupValidate::NullStruct;
    if (!hasLeader) return GroupValidate::NullLeader;
    if (wantA > 5u) return GroupValidate::WantMaleBad;
    if (wantB > 5u) return GroupValidate::WantFemBad;
    if (size == 0 || size > 4u) return GroupValidate::SizeBad;
    return GroupValidate::Ok;
}

// The leader rank "balance target" (@0x46870b..0x46874c):
//   v10 = (5 - wantB) - ((rank - (rank>>31 ... )) >> 2) + rank + (5 - wantA) - rank/3
// The middle term is the binary's arithmetic-shift floor-divide of `rank` by 4
// (round toward -inf), implemented here exactly. rank/3 is C truncating divide.
inline i32 GroupCompositionTarget(u32 wantA, u32 wantB, i32 rank) {
    i32 v6 = 5 - static_cast<i32>(wantB);
    i32 v7 = 5 - static_cast<i32>(wantA);
    // (rank - (__CFSHL__(rank>>31,2)+4*(rank>>31))) >> 2  ==  floor(rank/4)
    i32 floorDiv4 = rank >> 2;   // arithmetic shift on signed == floor divide by 4
    return v6 - floorDiv4 + rank + v7 - rank / 3;
}

// Per-member adjustment classification (@0x46877c..0x4687a5):
//   diff = target - memberRank
//   if diff < -1 -> -2 ;  else clamp diff to <= 2.
// Stored as a signed byte slot.code (then dispatched below).
inline i8 GroupMemberAdjust(i32 target, i32 memberRank) {
    i32 diff = target - memberRank;
    if (diff < -1) return static_cast<i8>(-2);
    if (diff >= 2) return 2;
    return static_cast<i8>(diff);
}

// Score buckets per adjustment code (@0x4687db switch). Each is base + rand(span).
// Returns {base, span, negate} so callers feed the exact RandomModulo(span) draw.
// codes:  0xFE -> -(rand(16)+16)   0xFF -> -(rand(8)+8)
//         0    ->  rand(8)+8       1   ->  rand(16)+16   2 -> rand(16)+24
// (default: unchanged; caller keeps prior a3)
struct GroupScoreBucket { i32 base; u32 span; bool negate; bool valid; };
inline GroupScoreBucket GroupMemberScoreBucket(i8 code) {
    switch (static_cast<u8>(code)) {
        case 0xFE: return { 16, 16, true,  true };  // -(rand(16)+16)
        case 0xFF: return { 8,  8,  true,  true };  // -(rand(8)+8)
        case 0x00: return { 8,  8,  false, true };  //  rand(8)+8
        case 0x01: return { 16, 16, false, true };  //  rand(16)+16
        case 0x02: return { 24, 16, false, true };  //  rand(16)+24
        default:   return { 0,  0,  false, false };
    }
}
inline i32 GroupMemberScore(i8 code, u16 randDraw) {
    GroupScoreBucket b = GroupMemberScoreBucket(code);
    if (!b.valid) return 0;
    i32 v = static_cast<i32>(randDraw) + b.base;
    return b.negate ? -v : v;
}

// The group's overall "value" weight (@0x4688d5):
//   v19 = (unsigned)(extra - wantA + 5 - wantB) * 0.001 * wealth * size
//   result stored truncated to int (a1[44]).
// `extra` is the loop's residual register (v18); modeled as a caller input.
inline i32 GroupCompositionValue(u32 extra, u32 wantA, u32 wantB,
                                 i32 wealth, u8 size) {
    u32 n = extra - wantA + 5 - wantB;   // unsigned, matches (unsigned int) cast
    double v = static_cast<double>(n)
             * static_cast<double>(kGroupWealthScale)
             * static_cast<double>(wealth)
             * static_cast<double>(size);
    return static_cast<i32>(v);
}

} // namespace guild::sim
