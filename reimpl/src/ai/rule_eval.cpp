#include "ai/rule_eval.h"

// MeisterAi RuleEval decision/scoring family (gilde.exe 0x464660..0x466259).
//
// Recovered tuning constants (byte-for-byte; offsets are gilde.exe VAs). The
// market-scored rules all use the same C0/C1 baseline coefficients:
//   flt_*C0 = 0.003968254197388887  (== 1/252, the per-rate-scalar baseline)
//   flt_*C1 = 0.02380952425301075   (== 1/42,  the rating-to-score scale)
//   dbl_*bias = -2.0                (added to each setting before * weight)
// Per-rule group multipliers + law ids and the 0.5 thresholds for the Toggle
// variant are recovered individually below (each accessor names its source VA).
//
// DEFERRED (same family, deeper coupling — see banner in meister_economy.cpp):
//   * RuleEvalProductionRate (0x464660) and RuleEvalPricePolicy (0x4647f8): these
//     additionally branch on He_SumPlayerHandlerValues (worker-value aggregation)
//     and (for price) Office_LookupHolderCharacter + Ai_ComputePersonFavorability,
//     and emit multi-way settings (0/1/2/4), not a 0/1 toggle. Their decision
//     trees are not the shared shape and are deferred with the other coupled
//     passes.
//   * RuleEvalStockTarget* / RangeLow/High / InterpolatedTarget / QualityTier /
//     CostBenefit / Increase/Decrease/AdjustSetting (0x464d0c..0x46566c): these
//     read live building memory through register-aliased Coord_ConvertX / law
//     records that the decompiler cannot resolve into a pure signature; deferred.

namespace guild::ai {

namespace {

// The shared baseline coefficients (identical across every market-scored rule).
constexpr float  kC0   = 0.003968254197388887f; // flt_*C0 (1/252)
constexpr float  kC1   = 0.02380952425301075f;   // flt_*C1 (1/42)
constexpr double kBias = -2.0;                    // dbl_*bias

// Comparing a positive float `score` against 1.0f via its int bit-pattern: the
// original tests `*(int*)&score <= 1065353216` (== float 1.0f). For non-negative
// IEEE floats the integer ordering equals the float ordering, so this is exactly
// `score <= 1.0f`. We keep the float comparison (behavior-identical).

// group multiplier helper: returns the per-rule multiplier for a building group.
// Each rule configures up to two match cases (groupA/groupB -> mulA,
// groupC/groupD -> mulB) and a default. A group id of -1 disables a slot.
float GroupMul(const RuleConfig& cfg, int group) {
    if (group == cfg.groupA || group == cfg.groupB) return cfg.mulA;
    if (group == cfg.groupC || group == cfg.groupD) return cfg.mulB;
    return cfg.mulDefault;
}

} // namespace

// gilde.exe shared core — market-scored setting rule.
bool RuleEvalSettingToggle(const RuleBuilding& b, const RuleEnv& env,
                           const RuleConfig& cfg, int* out) {
    int group = env.group_from_code(b.typeCode);
    u8  scalar = env.rate_scalar(cfg.goodColumn);

    float baseline = (static_cast<double>(scalar) + 1.0) * cfg.c0;
    float score = (env.eval_rating(b, cfg.goodColumn) - baseline) * cfg.c1;

    float base = (b.setting61 + cfg.bias) * b.weight63; // v14 baseline trend
    float mid  = (b.setting64 + cfg.bias) * b.weight66; // v11 lower-setting trend
    float high = (b.setting67 + cfg.bias) * b.weight69; // v13 raise-setting trend

    score *= GroupMul(cfg, group);

    int law = env.law_value(cfg.lawId);

    if (score < 1.0f && law == 0) {
        if (high > base) {
            *out = 1;
            return true;
        }
        return false;
    }
    // else branch: requires score > 1.0, law == 1, mid > base.
    if (score <= 1.0f || law != 1 || mid <= base)
        return false;
    *out = 0;
    return true;
}

// gilde.exe 0x4658f8 / 0x4659e0 — ToggleA/B variant.
bool RuleEvalToggleVariant(const RuleBuilding& b, const RuleEnv& env,
                           const RuleConfig& cfg, int* out) {
    int group = env.group_from_code(b.typeCode);

    float midScore = (b.setting64 + cfg.bias);          // v11 (NOT * weight)
    float base     = (b.setting61 + cfg.bias) * b.weight63; // v10
    float high     = (b.setting67 + cfg.bias) * b.weight69; // v9

    midScore *= GroupMul(cfg, group);

    int law = env.law_value(cfg.lawId);

    // thresholds: flt_61A098 / flt_61A0B8 = 0.5 (low), dbl_61A0A0 / dbl_61A0C0 =
    // 0.5 (high). Both equal 0.5 in ToggleA and ToggleB.
    constexpr float kThreshLow  = 0.5f;
    constexpr double kThreshHigh = 0.5;

    if (midScore < kThreshLow && law == 0) {
        if (base > high) {
            *out = 1;
            return true;
        }
        return false;
    }
    if (midScore <= kThreshHigh || law != 1 || high <= base)
        return false;
    *out = 0;
    return true;
}

// --- recovered per-rule configs ---------------------------------------------
// Each block: {goodColumn, c0, c1, bias, lawId, groupA,groupB,mulA,
//              groupC,groupD,mulB, mulDefault}.

const RuleConfig& RuleConfigServiceLevel() {
    // 0x464ad4: col 4, law 3. groups: 7 -> 1.1 (flt_619FDC); 11/12 -> 0.9
    // (flt_619FD8); else 1.0 (no scale).
    static const RuleConfig c{4, kC0, kC1, kBias, 3,
        7, -1, 1.100000023841858f,
        11, 12, 0.8999999761581421f,
        1.0f};
    return c;
}

const RuleConfig& RuleConfigWageLevel() {
    // 0x464c00: col 3, law 4. unconditional * 1.1 (flt_619FF0).
    static const RuleConfig c{3, kC0, kC1, kBias, 4,
        -1, -1, 1.100000023841858f,   // never matches -> falls to default
        -1, -1, 0.0f,
        1.100000023841858f};
    return c;
}

const RuleConfig& RuleConfigToggleA() {
    // 0x4658f8: variant, law 16. midScore group: 5/6 -> 1.5 (flt_61A094);
    // 7 -> 0.9 (flt_61A090); else 1.0.
    static const RuleConfig c{0, kC0, kC1, kBias, 16,
        5, 6, 1.5f,
        7, -1, 0.8999999761581421f,
        1.0f};
    return c;
}

const RuleConfig& RuleConfigToggleB() {
    // 0x4659e0: variant, law 17. same group cases as ToggleA (flt_61A0B0/B4).
    static const RuleConfig c{0, kC0, kC1, kBias, 17,
        5, 6, 1.5f,
        7, -1, 0.8999999761581421f,
        1.0f};
    return c;
}

const RuleConfig& RuleConfigDemandToggle() {
    // 0x465ac8: col 4, law 18. unconditional * 0.8 (flt_61A0D8).
    static const RuleConfig c{4, kC0, kC1, kBias, 18,
        -1, -1, 0.800000011920929f,
        -1, -1, 0.0f,
        0.800000011920929f};
    return c;
}

const RuleConfig& RuleConfigSupplyToggleA() {
    // 0x465bc0: col 3, law 19. unconditional * 1.1 (flt_61A0F0).
    static const RuleConfig c{3, kC0, kC1, kBias, 19,
        -1, -1, 1.100000023841858f,
        -1, -1, 0.0f,
        1.100000023841858f};
    return c;
}

const RuleConfig& RuleConfigSupplyToggleB() {
    // 0x465ccc: col 3, law 20. unconditional * 1.1 (flt_61A108).
    static const RuleConfig c{3, kC0, kC1, kBias, 20,
        -1, -1, 1.100000023841858f,
        -1, -1, 0.0f,
        1.100000023841858f};
    return c;
}

const RuleConfig& RuleConfigCapacityToggle() {
    // 0x465dd8: col 4, law 21. 11/12 -> 1.1 (flt_61A124); else 0.9 (flt_61A120).
    static const RuleConfig c{4, kC0, kC1, kBias, 21,
        11, 12, 1.100000023841858f,
        -1, -1, 0.0f,
        0.8999999761581421f};
    return c;
}

const RuleConfig& RuleConfigThresholdToggleA() {
    // 0x465efc: col 2, law 22. 11 -> 1.3 (flt_61A13C); else 0.9 (flt_61A138).
    static const RuleConfig c{2, kC0, kC1, kBias, 22,
        11, -1, 1.2999999523162842f,
        -1, -1, 0.0f,
        0.8999999761581421f};
    return c;
}

const RuleConfig& RuleConfigThresholdToggleB() {
    // 0x466020: col 2, law 23. 11/12 -> 1.1 (flt_61A150); else 1.0 (no scale).
    static const RuleConfig c{2, kC0, kC1, kBias, 23,
        11, 12, 1.100000023841858f,
        -1, -1, 0.0f,
        1.0f};
    return c;
}

const RuleConfig& RuleConfigThresholdToggleC() {
    // 0x466138: col 3, law 24. 12 -> 1.3 (flt_61A16C); else 0.9 (flt_61A168).
    static const RuleConfig c{3, kC0, kC1, kBias, 24,
        12, -1, 1.2999999523162842f,
        -1, -1, 0.0f,
        0.8999999761581421f};
    return c;
}

// gilde.exe 0x464a30 — RuleEvalQualitySetting.
bool RuleEvalQualitySetting(int quality, int law, float affinity, int* out) {
    // The original tests the building's affinity float via its int bit-pattern
    // against 0.4f (1053609165) and 0.8f (1061997773). For non-negative floats
    // the comparisons are equivalent to the float comparisons used here.
    constexpr float kLow  = 0.4000000059604645f;  // 1053609165
    constexpr float kHigh = 0.800000011920929f;   // 1061997773

    // Faithful to the decompile's nested branch on (quality, law):
    //   outer `if (quality || law != 1)` false  => quality==0 && law==1
    if (quality == 0 && law == 1) {
        *out = (affinity >= kLow) ? 0 : 2;
        return true;
    }
    if (quality == 1 && law == 0) {
        // affinity<0.4 -> 2, else 1.
        *out = (affinity < kLow) + 1;
        return true;
    }
    if (law == 2 && affinity > kHigh) {
        // drop the "high" setting when affinity is high enough: quality!=0 -> 1.
        *out = (quality != 0);
        return true;
    }
    return false;
}

} // namespace guild::ai
