#include "ai/meisterai2.h"

#include <cmath>

#include "util/math_random.h"

// MeisterAi RuleEval decision/scoring family — continuation. See meisterai2.h for
// the banner, recovered constants and per-rule shapes. Every body is a faithful
// 1:1 translation of the Hex-Rays pseudocode + disassembly; the FPU truncation
// (VIBE_Coord_ConvertX) is reproduced with std::trunc and the integer arithmetic
// (range = high - low, etc.) is preserved exactly.

namespace guild::ai {

namespace {

// VIBE_Coord_ConvertX (0x5c6b08): round st0 toward zero, then read as int. The
// originals do `call Coord_ConvertX; fistp tmp; mov reg, tmp`, i.e. (int)trunc(x).
inline int TruncToInt(double x) { return static_cast<int>(std::trunc(x)); }

inline int Rng(const RuleEnv2& env, int n) {
    if (env.rng_mod) return env.rng_mod(n);
    return guild::util::RandomModulo(static_cast<u16>(n));
}

// --- recovered constants ----------------------------------------------------
constexpr float  kIncFlt_619FFC = -2.0f;   // IncreaseSetting bias
constexpr float  kIncFlt_61A000 = 5.0f;    // IncreaseSetting scale
constexpr float  kAdjFlt_61A004 = -2.0f;   // AdjustSetting bias
constexpr float  kAdjFlt_61A008 = 5.0f;    // AdjustSetting scale
constexpr float  kSeasFlt_61A044 = 3.0f;   // SeasonalStock non-tavern bias
constexpr double kSeasDbl_61A048 = -2.0;   // SeasonalStock tavern bias
constexpr float  kRloFlt_61A050 = 0.6600000262260437f;  // RangeLow default coeff
constexpr float  kRloFlt_61A054 = 0.33000001311302185f; // RangeLow group-8 coeff
constexpr float  kIntFlt_61A058 = 3.0f;    // InterpolatedTarget bias
constexpr float  kIntFlt_61A05C = 0.0014285714132711291f; // 1/700
constexpr float  kRhiFlt_61A060 = 0.44999998807907104f; // RangeHigh default coeff
constexpr float  kRhiFlt_61A064 = 0.550000011920929f;   // RangeHigh group-9 coeff
constexpr double kCbDbl_61A068  = -2.0;    // CostBenefit bias
constexpr float  kCbFlt_61A070  = 1.100000023841858f;   // CostBenefit default mul
constexpr float  kCbFlt_61A074  = 0.800000011920929f;   // CostBenefit group-7 mul
constexpr float  kCbFlt_61A078  = 1.2000000476837158f;  // CostBenefit group 11/12 mul
constexpr float  kCbFlt_61A07C  = 0.5f;    // CostBenefit score low threshold
constexpr double kCbDbl_61A080  = 0.5;     // CostBenefit score high threshold

} // namespace

// gilde.exe 0x464d84 — RuleEvalIncreaseSetting.
int RuleEvalIncreaseSetting(const Rule2Building& b, const RuleEnv2& env, int* out) {
    // value = (int)trunc((f43 + (-2.0)) * 5.0)
    int value = TruncToInt((b.f43 + kIncFlt_619FFC) * kIncFlt_61A000); // +0xAC
    if (value >= 4)               // cmp edx,4 / jge -> clamp to 4
        value = 4;
    Rule2Law law = env.law_record(6);
    if (value == law.current)     // cmp ecx,[var_14] (record dword[6])
        return 0;
    *out = value;
    return 1;
}

// gilde.exe 0x464ddc — RuleEvalAdjustSetting.
int RuleEvalAdjustSetting(const Rule2Building& b, const RuleEnv2& env, int* out) {
    env.group_from_code(b.typeCode);   // looked up, then discarded (faithful)
    int value = TruncToInt((b.f43 + kAdjFlt_61A004) * kAdjFlt_61A008); // +0xAC
    if (value >= 4)               // cmp edx,4 / jge -> 4
        value = 4;
    else if (value < 1)           // cmp edx,1 / jge -> else clamp up to 1
        value = 1;
    Rule2Law law = env.law_record(7);
    if (value == law.current)
        return 0;
    *out = value;
    return 1;
}

// gilde.exe 0x4653c0 — RuleEvalRangeLow.
int RuleEvalRangeLow(const Rule2Building& b, const RuleEnv2& env, int* out) {
    int group = env.group_from_code(b.typeCode);
    if (env.city_count() < 4)          // cmp qword_13CE852, 4 / jge
        return 0;
    Rule2Law law = env.law_record(11);
    int range = law.high - law.low;    // var_2C - var_30
    if (group == 8) {
        int target = TruncToInt(static_cast<double>(range) * kRloFlt_61A054
                                + static_cast<double>(law.low));
        if (law.current > target) {    // cmp eax,[var_10] / jle -> return 0
            *out = law.low + (Rng(env, 3) & 0xFFFF);
            return 1;
        }
        return 0;
    }
    int target = TruncToInt(static_cast<double>(range) * kRloFlt_61A050
                            + static_cast<double>(law.low));
    if (law.current >= target)         // cmp eax,[var_10] / jge -> return 0
        return 0;
    *out = law.high - (Rng(env, 3) & 0xFFFF);
    return 1;
}

// gilde.exe 0x465588 — RuleEvalRangeHigh.
int RuleEvalRangeHigh(const Rule2Building& b, const RuleEnv2& env, int* out) {
    int group = env.group_from_code(b.typeCode);
    if (env.city_count() < 4)
        return 0;
    Rule2Law law = env.law_record(13);
    int range = law.high - law.low;
    if (group == 9) {
        int target = TruncToInt(static_cast<double>(range) * kRhiFlt_61A064
                                + static_cast<double>(law.low));
        if (law.current < target) {    // cmp eax,[var_10] / jge -> return 0
            *out = law.high - (Rng(env, 6) & 0xFFFF);
            return 1;
        }
        return 0;
    }
    int target = TruncToInt(static_cast<double>(range) * kRhiFlt_61A060
                            + static_cast<double>(law.low));
    if (law.current <= target)         // cmp eax,[var_10] / jle -> return 0
        return 0;
    *out = law.low + (Rng(env, 6) & 0xFFFF);
    return 1;
}

// gilde.exe 0x4654a4 — RuleEvalInterpolatedTarget.
int RuleEvalInterpolatedTarget(const Rule2Building& b, const RuleEnv2& env, int* out) {
    float a = kIntFlt_61A058 - b.f43;          // 3.0 - *(building+0xAC)
    float c = b.f7 * kIntFlt_61A05C;           // *(building+0x1C) * (1/700)
    float prod = a * c;
    // t = clamp(prod, 0, 1): if prod < 1.0 && prod <= 0.0 -> 0; else min(prod,1).
    float t;
    if (prod < 1.0f && prod <= 0.0f) {
        t = 0.0f;
    } else {
        t = (prod >= 1.0f) ? 1.0f : prod;
    }
    Rule2Law law = env.law_record(12);
    int target = TruncToInt(static_cast<double>(law.high - law.low) * t
                            + static_cast<double>(law.low));
    if (std::abs(target - law.current) <= 3)   // (int)abs32(v3 - current) <= 3
        return 0;
    *out = target;
    return 1;
}

// gilde.exe 0x4652a8 — RuleEvalSeasonalStock.
int RuleEvalSeasonalStock(const Rule2Building& b, const RuleEnv2& env, int* out) {
    int group = env.group_from_code(b.typeCode);
    Rule2Law law = env.law_record(10);
    int range = law.high - law.low;            // var_3C - var_40
    if (group == 7) {
        int target = TruncToInt((static_cast<double>(b.f43) + kSeasDbl_61A048)
                                * static_cast<double>(range)
                                + static_cast<double>(law.low));
        // clamp(target, low, high)
        if (target >= law.high)      target = law.high;
        else if (target < law.low)   target = law.low;
        if (target > law.current) {  // cmp eax,[var_2C] / jle -> return 0
            *out = target;
            return 1;
        }
        return 0;
    }
    int target = TruncToInt((static_cast<double>(kSeasFlt_61A044) - static_cast<double>(b.f43))
                            * static_cast<double>(range)
                            + static_cast<double>(law.low));
    if (target >= law.high)      target = law.high;
    else if (target < law.low)   target = law.low;
    if (target < law.current) {  // cmp eax,[var_2C] / jge -> return 0
        *out = target;
        return 1;
    }
    return 0;
}

// gilde.exe 0x4657b0 — RuleEvalCostBenefit.
int RuleEvalCostBenefit(const Rule2Building& b, const RuleEnv2& env, int* out) {
    int group = env.group_from_code(b.typeCode);
    double v13 = static_cast<double>(b.f52) + kCbDbl_61A068;  // f52 - 2.0
    double v12 = kCbDbl_61A068 + static_cast<double>(b.f55);  // f55 - 2.0
    float score = static_cast<float>(v13 > v12 ? v13 : v12);  // max
    float base = (b.f61 + static_cast<float>(kCbDbl_61A068)) * b.f63; // (f61-2)*f63
    float high = (static_cast<float>(kCbDbl_61A068) + b.f67) * b.f69; // (f67-2)*f69

    double mul;
    if (group == 11 || group == 12)      mul = static_cast<double>(score) * kCbFlt_61A078;
    else if (group == 7)                 mul = static_cast<double>(score) * kCbFlt_61A074;
    else                                 mul = static_cast<double>(score) * kCbFlt_61A070;
    float s = static_cast<float>(mul);

    Rule2Law law = env.law_record(15);
    if (s < kCbFlt_61A07C && law.enable == 0) {
        if (base > high) {
            *out = 1;
            return 1;
        }
        return 0;
    }
    if (s <= kCbDbl_61A080 || law.enable != 1 || high <= base)
        return 0;
    *out = 0;
    return 1;
}

} // namespace guild::ai
