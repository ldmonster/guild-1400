#include "sim/combat_slots2.h"

#include <cfenv>
#include <cmath>

namespace guild::sim {

// gilde.exe 0x57eb64 — VIBE_Combat_AccumulateThreatStats — cash -> tier rounding.
// Disasm @0x57ebcc..0x57ec45 (verified): GetCashAmount returns st0 (x87 80-bit), the
// scale is applied with `fmul ds:flt_625994` (so cash*0.1f happens at 80-bit), the
// clamp branches compare with `fcomp`, and the chosen value is spilled through a
// 32-bit FLOAT slot (`fstp [esp+...var_C]`, var_C is `float`: v12/v14). Only THEN is
// it converted to int via VIBE_Coord_ConvertX(); (int)v12. ConvertX @0x5c6b08 sets
// the x87 CW high byte to 0x1F (RC=11 = round-toward-ZERO), frndint, restores — i.e.
// it TRUNCATES toward zero. So the faithful path is: compute the clamp, round-trip the
// clamped value through `float`, THEN truncate toward zero (a double->int truncation
// could differ in the float-boundary edge cases the original collapses by spilling to
// a 32-bit float first).
int RoundTier(double cash) {
    double scaled = cash * static_cast<double>(kThreatCashScale);
    // Mirror the original's branch order: the < 7.0 / <= 0.0 cascade collapses to
    // a clamp into [0, 7].
    double clamped;
    if (scaled >= static_cast<double>(kThreatTierCeiling))
        clamped = static_cast<double>(kThreatTierCeiling);   // v14 = 7.0
    else if (scaled <= 0.0)
        clamped = 0.0;                                       // v12 = 0.0
    else
        clamped = scaled;                                    // v14 = cash
    // The original spills the clamped value to a 32-bit float (var_C) before ConvertX.
    float spilled = static_cast<float>(clamped);
    return static_cast<int>(std::trunc(spilled));  // ConvertX = truncate toward zero
}

// gilde.exe 0x57eb64 — VIBE_Combat_AccumulateThreatStats.
ThreatStats AccumulateThreatStats(const std::vector<ThreatSlot>& slots) {
    ThreatStats st;

    for (const ThreatSlot& s : slots) {
        if (!s.eligible)            // marker != -1 && active && busyRank < 10
            continue;
        ++st.totalActive;           // ++dword_1234938

        // tier = round(clamp(cash * 0.1, 0..7)).  RoundTier takes the RAW cash and
        // applies the 0.1 scale (matching the original's GetCashAmount() * 0.1).
        int tier = RoundTier(s.cash);
        ++st.tierCount[tier];       // ++dword_123493C[v13]

        // fillCount < requiredCap -> a slot is "underfull".
        if (static_cast<double>(s.fillCount) < static_cast<double>(s.requiredCap))
            st.underfullCount += 1.0;   // flt_123495C += 1.0

        st.curOutSum[tier] += s.currentOutput;  // flt_1234960[v13] += ComputeCurrentOutput
        st.maxOutSum[tier] += s.maxOutput;      // flt_1234980[v13] += ComputeMaxOutput
    }

    // Normalization pass (the `do { ... } while (result != 8)` loop). Note the
    // original divides through unconditionally — empty tiers therefore produce the
    // platform's IEEE 0/0 == NaN; we preserve that observably (the report code
    // simply renders the value, it does not branch on it).
    for (int tier = 0; tier < kThreatTiers; ++tier) {
        st.outputPct[tier] = st.curOutSum[tier] / st.maxOutSum[tier]
                             * static_cast<double>(kOutputPercentage);
        st.avgMaxOut[tier] = st.maxOutSum[tier] / static_cast<double>(st.tierCount[tier]);
    }

    // flt_123495C = underfullCount / totalActive * 100.0.
    st.underfullPct = st.underfullCount / static_cast<double>(st.totalActive)
                      * static_cast<double>(kOutputPercentage);
    return st;
}

namespace {

// 3D Euclidean distance between two world points (matches the inlined
// fsqrt(dx*dx + dy*dy + dz*dz) in both nearest-object scanners). The deltas are
// computed in float (the original uses float intermediates) then summed/sqrt'd
// in double.
inline double Dist3D(const float a[3], const float b[3]) {
    float dx = a[0] - b[0];
    float dy = a[1] - b[1];
    float dz = a[2] - b[2];
    return std::sqrt(static_cast<double>(dx * dx + dy * dy + dz * dz));
}

} // namespace

// gilde.exe 0x48b4d4 — VIBE_Combat_FindNearestWareObject.
const void* FindNearestWareObject(const std::vector<GatheredObject>& gathered,
                                  const float searcherPos[3]) {
    const void* best = nullptr;       // v3 = 0
    double bestDist = 1000000.0;      // v13 = 1000000.0
    for (const GatheredObject& obj : gathered) {
        double d = Dist3D(searcherPos, obj.pos);   // v14
        if (d < bestDist) {                        // if (v7 < v13)
            best = obj.identity;
            bestDist = d;
        }
    }
    return best;                      // return v3
}

// gilde.exe 0x48b5d8 — VIBE_Combat_FindNearestConquerTarget.
const void* FindNearestConquerTarget(const std::vector<GatheredObject>& gathered,
                                     const float searcherPos[3], int searcherTeam) {
    const void* best = nullptr;       // v2 = 0
    bool bestIsSelf = false;          // tracks the final self-owned reject test
    double bestScore = 1000000.0;     // v15 = 1000000.0

    for (const GatheredObject& obj : gathered) {
        double dist = Dist3D(searcherPos, obj.pos);   // v16 = sqrt(...)
        double score;
        if (obj.hasOwner && searcherTeam != obj.ownerTeam) {
            // enemy-owned: owner present and on a different team.
            score = dist * kConquerEnemyMul;          // dist * 10.0
        } else if (!obj.hasOwner) {
            // unowned: the original's `if (!owner) goto LABEL_7` keeps the raw dist.
            score = dist;
        } else if (obj.ownerIsSelf) {
            // self-owned: owner == searcher -> dist * 25.0.
            score = dist * kConquerSelfMul;
        } else {
            // friend-owned by someone else (same team, owner != self) -> dist * 15.0.
            score = dist * kConquerFriendMul;
        }

        if (score < bestScore) {                      // if (v16 < v15)
            best = obj.identity;
            bestScore = score;
            bestIsSelf = obj.ownerIsSelf;
        }
    }

    // Final reject: `if (a1 == *(v2+512)) return 0;` — drop a self-owned winner.
    if (bestIsSelf)
        return nullptr;
    return best;
}

// gilde.exe 0x49080c — VIBE_Combat_AutoIssueRetreatOrders (fighting-unit count).
// For each non-empty roster entry (id != -1) it resolves the combat unit and reads
// the state byte (+0x008): a unit counts iff state != 0 (dead/empty) and != 4
// (already retreating).
int CountFightingUnits(const std::vector<u8>& states) {
    int count = 0;
    for (u8 state : states) {
        if (state != 0 && state != 4)   // if (v6) { if (v6 != 4) ++v4; }
            ++count;
    }
    return count;
}

// gilde.exe 0x49080c — the retreat morale gate.
//   v30 = (double)foeCount / (double)friendCount;
//   if (v30 > 0.5 || RandomModulo(100) <= 20)  -> press the attack
bool ShouldPressAttack(int friendCount, int foeCount, int rng100) {
    double ratio = static_cast<double>(foeCount) / static_cast<double>(friendCount);
    if (ratio > kRetreatRatioThreshold)
        return true;
    return rng100 <= 20;
}

} // namespace guild::sim
