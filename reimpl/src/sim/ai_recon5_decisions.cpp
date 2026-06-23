// ai_recon5_decisions.cpp — see ai_recon5_decisions.h for provenance & scope.
#include "sim/ai_recon5_decisions.h"

#include <cmath>
#include <cstring>

namespace guild::sim {

const AiRecon5Hooks kInertAiRecon5Hooks{};

// flt_62675C = 0x38000100 reinterpreted as float (~3.0518e-5, the RNG scale).
static inline double RandScale() {
    u32 bits = 0x38000100u;
    f32 f;
    std::memcpy(&f, &bits, sizeof(f));
    return static_cast<double>(f);
}

// gilde.exe 0x58b910 — VIBE_Math_RandomFloatScaled
//   return (double)(int)VIBE_Util_RandNext() * flt_62675C;
double RandomFloatScaled(const AiRecon5Hooks& hk) {
    i32 r = hk.randNext ? hk.randNext() : 0;
    return static_cast<double>(r) * RandScale();
}

// gilde.exe 0x58b89c — VIBE_Math_RandomModulo
//   return a1 ? (int)VIBE_Util_RandNext() % a1 : 0;
i32 RandomModulo(const AiRecon5Hooks& hk, u16 n) {
    if (!n)
        return 0;
    i32 r = hk.randNext ? hk.randNext() : 0;
    return r % n;
}

// gilde.exe 0x46670c — VIBE_AiCardGame_PlaceBet  (__usercall, eax = (a1@eax, a2@bl))
CardBet AiCardGame_PlaceBet(const AiRecon5Hooks& hk,
                            i32 personId, u8 personTypeByte, u8 betMult,
                            f32 familyAggIn) {
    CardBet out;
    out.familyAgg = familyAggIn;

    // GetFamilyRecord(a1) is fetched first but only used for the +22 float (the
    // familyAgg we receive). The guard:
    //   if (!a1 || *(a1+2)!=6 || !FamilyRecord || (a2!=1 && a2!=2 && a2!=4)) return 0;
    // FamilyRecord!=0 is implied by a valid personId here.
    if (personId == 0 || personTypeByte != 6 ||
        (betMult != 1 && betMult != 2 && betMult != 4)) {
        out.ok = false;
        return out;
    }

    // VIBE_Light_SetGrayColorThunk(0,4,..)  — display-only side effect, omitted.
    // mood color word:
    i16 mood = hk.selectMoodColor ? hk.selectMoodColor() : 0;
    out.moodColor = mood;

    // *(state+68) = 0;  (pot accumulator reset — state-owned, not modeled here)

    // byte_6477A1 == 0 (recovered).
    const u8 byte_6477A1 = 0;
    i32 wealth = hk.personTotalWealth ? hk.personTotalWealth(personId) : 0;
    i32 disp = hk.moneyToDisplay ? hk.moneyToDisplay(wealth, byte_6477A1) : 0;

    double v22 = static_cast<double>(disp);
    const double dbl_61A188 = 10000.0;   // 0x40C3880000000000
    const double dbl_61A190 = 0.1;       // 0x3FB999999999999A
    double v23 = (dbl_61A188 <= v22) ? static_cast<double>(disp) : 10000.0;

    // v12 = v23 * 0.1 / (log10(v23) + 1.0) * ((double)a2 * 0.25);
    const float flt_61A198 = 0.25f;      // 0x3E800000
    double v12 = v23 * dbl_61A190 / (std::log10(v23) + 1.0) *
                 (static_cast<double>(betMult) * static_cast<double>(flt_61A198));

    // VIBE_Coord_ConvertX() — scratch fp side effect (ignored). The rate mode for
    // MultiplyByRate (v13) comes from that thunk; with inert hooks we pass 0.
    i32 stakeA = hk.moneyMulByRate ? hk.moneyMulByRate(static_cast<i32>(v12), 0)
                                   : static_cast<i32>(v12);
    out.stakeA = stakeA;

    // decision/phase bytes (+36,+37,+64) cleared; state-owned.
    const float flt_61A19C = 0.1f;       // 0x3DCCCCCD
    double v16 = static_cast<double>(stakeA) * static_cast<double>(flt_61A19C);
    out.stakeB = static_cast<i32>(v16);

    // Seat assignment + family-aggression nudge (two more RNG draws).
    const float flt_61A1A0 = -0.2f;      // 0xBE4CCCCD
    const float flt_61A1A4 = 0.11f;      // 0x3DE147AE
    const float flt_61A1A8 = 0.2f;       // 0x3E4CCCCD

    double pick = RandomFloatScaled(hk);
    if (pick <= static_cast<double>(familyAggIn)) {
        // human seat -> B (+40); sentinel -> A (+12); mood at +46.
        out.humanSeat = 1;
        float v25 = familyAggIn + flt_61A1A0;            // agg - 0.2
        out.familyAgg =
            v25 - static_cast<float>(RandomFloatScaled(hk)) * flt_61A1A4;
    } else {
        // human seat -> A (+12); sentinel -> B (+40); mood at +18.
        out.humanSeat = 0;
        float v24 = familyAggIn + flt_61A1A8;            // agg + 0.2
        out.familyAgg =
            static_cast<float>(RandomFloatScaled(hk)) * flt_61A1A4 + v24;
    }

    out.ok = true;
    return out;
}

// gilde.exe 0x47d568 — VIBE_AiPlayer_ExecThreaten  (__usercall, eax = (a1@ebx))
ThreatenDecision AiPlayer_ExecThreaten(const AiRecon5Hooks& hk,
                                       u8 cmdByte, bool targetResolved) {
    ThreatenDecision out;
    if (cmdByte != 7) {           // if (*a1 != 7) return 0;
        out.result = 0;
        return out;
    }
    if (!targetResolved) {        // FindRecordById(*(a1+1)) == 0 -> return 0
        out.result = 0;
        return out;
    }
    // EnqueueBuildingActionStart(aip_ExecDrohen) — coupling.
    // gate: ComputeRatingCurveA(4,..) + RandomFloatScaled() >= 1.0
    double rating = hk.ratingCurveA ? static_cast<double>(hk.ratingCurveA()) : 0.0;
    double r = RandomFloatScaled(hk);
    out.doSlotReset = (rating + r >= 1.0);
    // RequestBuildOp90(...) + EnqueueBuildingActionEnd() — couplings.
    out.result = 1;
    return out;
}

// gilde.exe 0x4675b8 — VIBE_AiMethod_SelectConversationTarget (__usercall, a1@eax)
ConvResult AiMethod_SelectConversationTarget(const AiRecon5Hooks& hk,
                                             const ConvCandidate* slots,
                                             int slotCount,
                                             u8 personTypeByte,
                                             const ConvSelfRecord& self) {
    ConvResult out;

    // Scan the 5-slot ring (orig: v3 = a1+12 .. v20 = a1+32, +4 stride).
    //   v19 (candidatesPresent) ++ when record resolves
    //   v5  (eligible)         ++ when alive byte set AND relation > 0x0B,
    //                            collecting the record into v17[1..].
    int candidatesPresent = 0;   // v19
    int eligible = 0;            // v5
    i32 pool[8] = {0};           // v17[] collected eligible records (cap 6 in orig)
    for (int k = 0; k < slotCount; ++k) {
        const ConvCandidate& c = slots[k];
        if (!c.present)
            continue;
        ++candidatesPresent;
        if (c.aliveByte) {
            if (c.relation > 0x0Bu) {
                if (eligible < 8)
                    pool[eligible] = c.recordId;
                ++eligible;
            }
        }
    }

    // v4 = eligible ? pool[RandomModulo(eligible)] : 0;
    i32 pick = 0;
    if (eligible) {
        int idx = RandomModulo(hk, static_cast<u16>(eligible));
        if (idx >= 0 && idx < 8)
            pick = pool[idx];
    }

    if (personTypeByte == 5) {
        // "leave" path.
        if (pick) {
            out.haveTarget = true;
            out.targetId = pick;
            return out;
        }
        if (!candidatesPresent)           // if (!v19) return 0;
            return out;
        // Emit relation-clear commands for self + each ring member matching
        // self's relation ids (couplings). We report how many slots matched.
        int rel = 0;                      // *(a1+4)
        (void)rel;
        out.relClearCount = candidatesPresent; // surrogate count of the scan
        return out;                       // returns 0 in all cases here
    }

    // default path: prefer self if talkable.
    //   if (self && *(self+8) && (t=*(self+2)) in (1,10) && !(self[229]&4)) return self;
    if (self.present && self.aliveByte) {
        i8 t = static_cast<i8>(self.typeByte);
        if (t > 1 && t < 10 && (self.flag229 & 4) == 0) {
            out.haveTarget = true;
            out.targetId = self.recordId;
            return out;
        }
    }
    // else: random pick if its type in (1,10)
    if (pick) {
        // v16 = *(v4+2); if (v16 > 1 && v16 < 10) return v4;
        // Type byte of the picked record is carried alongside the pool; find it.
        for (int k = 0; k < slotCount; ++k) {
            if (slots[k].present && slots[k].recordId == pick) {
                i8 t = static_cast<i8>(slots[k].typeByte);
                if (t > 1 && t < 10) {
                    out.haveTarget = true;
                    out.targetId = pick;
                }
                break;
            }
        }
    }
    return out;
}

// gilde.exe 0x47a524 — VIBE_AiObject_CountInventoryMatch (__usercall, a1@eax,a2@edx,a3@ebx)
InvMatch AiObject_CountInventoryMatch(const InvIngredient* ings, int recipeCount,
                                      u8 threshold) {
    InvMatch out;
    // for (v23 = 0; ; ) { if (recipeCount <= v23) return 0; ... }
    for (int v23 = 0;; ++v23) {
        if (recipeCount <= v23) {     // *(v19+34) <= v23 -> return 0
            out.matched = 0;
            return out;
        }
        const InvIngredient& g = ings[v23];
        u16 cls = g.classWord & 0x7FFFu;   // HIBYTE(v4) &= ~0x80  (mask top bit)
        u8 cat = g.itemCategory;           // item-class-table[65*cls]

        bool missing = false;
        i32 supplyId = -1;
        bool found = false;                // v6

        if (cat != 33) {
            if (cat == 2 || cat == 6) {
                // owned-stock scan (QueryFind type 2): count !=253 entries (v8),
                // stop on class match -> found, supplyId from object.
                int owned = g.ownedCount;  // v8
                if (g.foundInStock) {
                    found = true;
                    supplyId = g.supplyObjId;
                }
                // LABEL_10: if (v8 < threshold && !found) try for-sale (type 6).
                if (owned < threshold && !found) {
                    if (g.foundForSale) {
                        found = true;
                        supplyId = g.supplyObjId;
                    }
                    // if (v14 < threshold && !found) -> LABEL_36 need-check
                    if (owned < threshold && !found) {
                        // !QueryFind(cls) && extra-ingredient gates satisfied
                        bool need = !g.needCovered;
                        bool gateA = (g.extraGateA == 0) || g.extraGateAOk;
                        bool gateB = (g.extraGateB == 0) || g.extraGateBOk;
                        if (need && gateA && gateB)
                            missing = true;   // break
                    }
                }
            } else {
                // LABEL_36 directly (cat is neither 33, 2 nor 6).
                bool need = !g.needCovered;
                bool gateA = (g.extraGateA == 0) || g.extraGateAOk;
                bool gateB = (g.extraGateB == 0) || g.extraGateBOk;
                if (need && gateA && gateB)
                    missing = true;           // break
            }
        }
        // cat == 33 -> always satisfiable, fall through to next ingredient.

        if (missing) {
            out.matched = 1;
            out.classWord = cls;             // *a1 = masked class
            out.objId = found ? supplyId : -1; // *a2 = v18 ? id : -1
            return out;
        }
        // v3 += 2; ++v23 (loop)
    }
}

} // namespace guild::sim
