// ===========================================================================
// MeisterAi automatic-management RULE EVALUATORS — implementation.
// 1:1 translations of the gilde.exe decision math. See header for addresses and
// the full provenance / coupled-leaf list. Constants verified byte-exact via the
// IDA MCP get_bytes dump of the 0x619F98..0x61A16C .rdata block.
// ===========================================================================
#include "meisterai_ruleeval_ai_recon2.h"

#include <cstdlib>

namespace guild::sim {

// gilde.exe shared block:
//   pre   = ((double)rate + 1.0) * flt(kRateBias0)   [computed in double then -> float]
//   score = (rating - pre) * flt(kRateBias1)
float ProductionRatingScore(u8 rate, float rating) {
    // The original: v8 = ((double)(u8)rate + 1.0) * flt_*  (stored to a float v8),
    // then *(float*)&score = (Building_EvalProductionRating - v8) * flt_*.
    float pre = static_cast<float>((static_cast<double>(rate) + 1.0) *
                                   static_cast<double>(kRateBias0));
    return static_cast<float>((static_cast<double>(rating) - pre) *
                              static_cast<double>(kRateBias1));
}

// The canonical toggle decision shared by most evaluators. The binary compares
// the score's float bit pattern against 1.0f (1065353216) i.e. `score < 1.0f`,
// and the "on" branch uses `score <= 1.0f`.
int ToggleDecision(float score, float onTerm, float offTerm, float refTerm,
                   int curSetting, int* out) {
    if (score < 1.0f && curSetting == 0) {
        if (offTerm > refTerm) {           // v10 > v11
            *out = 1;
            return 1;
        }
        return 0;
    }
    if (score <= 1.0f || curSetting != 1 || onTerm <= refTerm) // v9 <= v11
        return 0;
    *out = 0;
    return 1;
}

int EvalWageLevel(u8 rate, float rating, float f61, float f63, float f64,
                  float f66, float f67, float f69, const GesetzRecord& law, int* out) {
    float score   = ProductionRatingScore(rate, rating); // rate idx 3
    score = static_cast<float>(static_cast<double>(score) * static_cast<double>(kWageScale));
    float refTerm = PolicyTerm(f61, f63);
    float onTerm  = PolicyTerm(f64, f66);
    float offTerm = PolicyTerm(f67, f69);
    return ToggleDecision(score, onTerm, offTerm, refTerm, law.cur, out);
}

int EvalServiceLevel(int group, u8 rate, float rating, float f61, float f63,
                     float f64, float f66, float f67, float f69,
                     const GesetzRecord& law, int* out) {
    float score   = ProductionRatingScore(rate, rating); // rate idx 4
    if (group == 7)
        score = static_cast<float>(static_cast<double>(score) * static_cast<double>(kServiceScaleTav));
    else if (group == 11 || group == 12)
        score = static_cast<float>(static_cast<double>(score) * static_cast<double>(kServiceScaleTrade));
    float refTerm = PolicyTerm(f61, f63);
    float onTerm  = PolicyTerm(f64, f66);
    float offTerm = PolicyTerm(f67, f69);
    return ToggleDecision(score, onTerm, offTerm, refTerm, law.cur, out);
}

int EvalDemandToggle(u8 rate, float rating, float f61, float f63, float f64,
                     float f66, float f67, float f69, const GesetzRecord& law, int* out) {
    float score   = ProductionRatingScore(rate, rating); // rate idx 4
    score = static_cast<float>(static_cast<double>(score) * static_cast<double>(kDemandScale));
    float refTerm = PolicyTerm(f61, f63);
    float onTerm  = PolicyTerm(f64, f66);
    float offTerm = PolicyTerm(f67, f69);
    return ToggleDecision(score, onTerm, offTerm, refTerm, law.cur, out);
}

int EvalSupplyToggle(u8 rate, float rating, float f61, float f63, float f64,
                     float f66, float f67, float f69, const GesetzRecord& law, int* out) {
    float score   = ProductionRatingScore(rate, rating); // rate idx 3
    score = static_cast<float>(static_cast<double>(score) * static_cast<double>(kSupplyScale));
    float refTerm = PolicyTerm(f61, f63);
    float onTerm  = PolicyTerm(f64, f66);
    float offTerm = PolicyTerm(f67, f69);
    return ToggleDecision(score, onTerm, offTerm, refTerm, law.cur, out);
}

int EvalCapacityToggle(int group, u8 rate, float rating, float f61, float f63,
                       float f64, float f66, float f67, float f69,
                       const GesetzRecord& law, int* out) {
    float score   = ProductionRatingScore(rate, rating); // rate idx 4
    float scale = (group == 11 || group == 12) ? kCapScaleTrade : kCapScaleDef;
    score = static_cast<float>(static_cast<double>(score) * static_cast<double>(scale));
    float refTerm = PolicyTerm(f61, f63);
    float onTerm  = PolicyTerm(f64, f66);
    float offTerm = PolicyTerm(f67, f69);
    return ToggleDecision(score, onTerm, offTerm, refTerm, law.cur, out);
}

int EvalThresholdToggleA(int group, u8 rate, float rating, float f61, float f63,
                         float f64, float f66, float f67, float f69,
                         const GesetzRecord& law, int* out) {
    float score   = ProductionRatingScore(rate, rating); // rate idx 2
    float scale = (group == 11) ? kThreshAg11 : kThreshADef;
    score = static_cast<float>(static_cast<double>(score) * static_cast<double>(scale));
    float refTerm = PolicyTerm(f61, f63);
    float onTerm  = PolicyTerm(f64, f66);
    float offTerm = PolicyTerm(f67, f69);
    return ToggleDecision(score, onTerm, offTerm, refTerm, law.cur, out);
}

int EvalThresholdToggleB(int group, u8 rate, float rating, float f61, float f63,
                         float f64, float f66, float f67, float f69,
                         const GesetzRecord& law, int* out) {
    float score   = ProductionRatingScore(rate, rating); // rate idx 2
    if (group == 11 || group == 12) // only scaled for these groups
        score = static_cast<float>(static_cast<double>(score) * static_cast<double>(kThreshBScale));
    float refTerm = PolicyTerm(f61, f63);
    float onTerm  = PolicyTerm(f64, f66);
    float offTerm = PolicyTerm(f67, f69);
    return ToggleDecision(score, onTerm, offTerm, refTerm, law.cur, out);
}

int EvalThresholdToggleC(int group, u8 rate, float rating, float f61, float f63,
                         float f64, float f66, float f67, float f69,
                         const GesetzRecord& law, int* out) {
    float score   = ProductionRatingScore(rate, rating); // rate idx 3
    float scale = (group == 12) ? kThreshCg12 : kThreshCDef;
    score = static_cast<float>(static_cast<double>(score) * static_cast<double>(scale));
    float refTerm = PolicyTerm(f61, f63);
    float onTerm  = PolicyTerm(f64, f66);
    float offTerm = PolicyTerm(f67, f69);
    return ToggleDecision(score, onTerm, offTerm, refTerm, law.cur, out);
}

// 0x4659e0 — distinct gate set: v11 = f64 + bias (the "on" magnitude, then scaled),
// v10 = (f61+bias)*f63 (ref), v9 = (bias+f67)*f69 (off).
int EvalToggleB(int group, float f61, float f63, float f64, float f67, float f69,
                const GesetzRecord& law, int* out) {
    float on  = static_cast<float>(static_cast<double>(f64) + kPolicyBias); // v11
    float ref = PolicyTerm(f61, f63);                                       // v10
    float off = PolicyTerm(f67, f69);                                       // v9
    if (group == 5 || group == 6)
        on = static_cast<float>(static_cast<double>(on) * static_cast<double>(kToggleBShop));
    else if (group == 7)
        on = static_cast<float>(static_cast<double>(on) * static_cast<double>(kToggleBTav));
    if (on < kToggleBLo && law.cur == 0) {
        if (ref > off) {
            *out = 1;
            return 1;
        }
        return 0;
    }
    if (static_cast<double>(on) <= kToggleBHi || law.cur != 1 || off <= ref)
        return 0;
    *out = 0;
    return 1;
}

// 0x464d0c — computed = trunc((kDecreaseBase - f43) * kDecreaseSlope).
int EvalDecreaseSetting(float f43, const GesetzRecord& law, int* out) {
    double v = (static_cast<double>(kDecreaseBase) - static_cast<double>(f43)) *
               static_cast<double>(kDecreaseSlope);
    int computed = CoordTrunc(v);
    if (computed == law.cur)
        return 0;
    *out = computed;
    return 1;
}

// 0x46566c — pure quality-tier classifier.
int ClassifyQualityTier(int total, int ref) {
    if (total < 3 * ref)
        return 4;
    if (total > 12 * ref)
        return 0;
    if (total > 8 * ref)
        return 1;
    if (total > 6 * ref)
        return 2;
    return 3;
}

// 0x46566c tail — step-toward-target with clamp to [lo,hi].
int EvalQualityTierStep(int target, const GesetzRecord& law, int* out) {
    int cur = law.cur;       // v13 in the decompile (current setting)
    int lo  = law.loBound;   // v11
    int hi  = law.hiBound;   // v12
    if (std::abs(cur - target) < 2)
        return 0;
    int v8 = target;
    if (v8 > cur) {
        v8 = cur + 1;
    } else if (v8 < cur) {
        v8 = cur - 1;
    }
    // clamp to hi (v12) first, then to lo (v11) as the decompile does.
    int v9 = (v8 >= hi) ? hi : v8;
    if (v9 <= lo) {
        *out = lo;
        return 1;
    }
    *out = (v8 >= hi) ? hi : v8;
    return 1;
}

// 0x464e4c / 0x46505c — pure weighted-target part.
int StockWeightedTarget(int loBound, int hiBound, float avgQ) {
    int span = hiBound - loBound;
    double base = static_cast<double>(loBound);
    double w;
    if (static_cast<double>(avgQ) >= static_cast<double>(kStockQ1)) {
        if (static_cast<double>(avgQ) >= kStockQ2)
            w = static_cast<double>(span) * static_cast<double>(kStockHiW);
        else
            w = static_cast<double>(span) * static_cast<double>(kStockMidW);
    } else {
        w = static_cast<double>(span) * static_cast<double>(kStockLoW);
    }
    return CoordTrunc(base + w);
}

} // namespace guild::sim
