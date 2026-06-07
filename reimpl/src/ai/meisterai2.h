#pragma once
// MeisterAi RuleEval decision/scoring family — continuation (gilde.exe
// 0x464d84..0x4657f5). These are the remaining per-building DECISION rules the
// Guild-Master AI applies each turn that the first pass (src/ai/rule_eval.cpp)
// deferred. They share the same shape as the toggle family but additionally:
//   * read a numeric stock LOW/HIGH/CURRENT triple out of the matching law
//     (Gesetz) record (record dword indices 1, 2 and 6),
//   * truncate-toward-zero a float interpolation (VIBE_Coord_ConvertX == trunc),
//   * draw a small RNG jitter (VIBE_Math_RandomModulo) when adjusting a target.
//
// Each rule is a pure function over: a building's float members, the building's
// GROUP (BuildingType_GroupFromCode of its type byte), the law record, an
// economy-gate (qword_13CE852, the "number of buildings/markets in the city"
// counter the rules require to be >= 4 or >= 10), and the shared LCG. The four
// coupled leaf reads are injected through `RuleEnv2` so the scoring/decision core
// is exercised byte-faithfully without the entity / economy / law clusters.
//
// Recovered constants (gilde.exe, decoded byte-for-byte; offsets are VAs):
//   flt_619FFC = -2.0    flt_61A000 = 5.0    (IncreaseSetting)
//   flt_61A004 = -2.0    flt_61A008 = 5.0    (AdjustSetting)
//   flt_61A044 = 3.0     dbl_61A048 = -2.0   (SeasonalStock)
//   flt_61A050 = 0.66    flt_61A054 = 0.33   (RangeLow)
//   flt_61A058 = 3.0     flt_61A05C = 1/700  (InterpolatedTarget)
//   flt_61A060 = 0.45    flt_61A064 = 0.55   (RangeHigh)
//   dbl_61A068 = -2.0    flt_61A070 = 1.1    flt_61A074 = 0.8
//   flt_61A078 = 1.2     flt_61A07C = 0.5    dbl_61A080 = 0.5  (CostBenefit)
#include "guild/common/types.h"

namespace guild::ai {

// --- the building inputs these rules read -----------------------------------
// Field byte offsets are the originals: *(float*)(building + 0xAC) is the primary
// "demand/affinity" scalar (index 43); +0x1C is a secondary scalar (index 7); the
// CostBenefit rule reads the float setting/weight pairs at indices 52,55,61,63,67,69.
struct Rule2Building {
    int   typeCode = 0;     // HIBYTE(*(building+353)) -> GroupFromCode input
    float f43  = 0.0f;      // *(float*)(building+0xAC)  primary scalar
    float f7   = 0.0f;      // *(float*)(building+0x1C)  secondary scalar
    float f52  = 0.0f;      // CostBenefit: setting pair A value
    float f55  = 0.0f;      // CostBenefit: setting pair B value
    float f61  = 0.0f;      // CostBenefit: base setting
    float f63  = 0.0f;      // CostBenefit: base weight
    float f67  = 0.0f;      // CostBenefit: high setting
    float f69  = 0.0f;      // CostBenefit: high weight
};

// --- the law (Gesetz) record values the rules read --------------------------
// VIBE_Gesetz_GetRecord copies a 36-byte record; the stock rules read three of its
// dwords: index 1 (low bound), index 2 (high bound), index 6 (current value at
// byte +24). The toggle-style rules read index 0 (the 0/1 enable flag).
struct Rule2Law {
    int enable  = 0;        // record dword[0]  (0/1 toggle; CostBenefit)
    int low     = 0;        // record dword[1]  (byte +4,  stock low bound)
    int high    = 0;        // record dword[2]  (byte +8,  stock high bound)
    int current = 0;        // record dword[6]  (byte +24, current setting value)
};

// --- injectable leaf reads --------------------------------------------------
struct RuleEnv2 {
    // group id for a building type code (mirrors BuildingType_GroupFromCode 0x58a4c8).
    int (*group_from_code)(int typeCode) = nullptr;
    // the matching law record for a law id (mirrors Gesetz_GetRecord 0x4c244c).
    Rule2Law (*law_record)(int lawId) = nullptr;
    // the city's building/market counter gate (qword_13CE852).
    int (*city_count)() = nullptr;
    // RNG: RandomModulo(n) (defaults to the shared LCG when null).
    int (*rng_mod)(int n) = nullptr;
};

// gilde.exe 0x464d84 — RuleEvalIncreaseSetting(eax=building, ebx=out). Computes
//   target = (int)trunc((b.f43 - 2.0) * 5.0)        clamped to <= 4
// and, if it differs from the law's current value (law 6), writes it to *out.
// Returns 1 if changed, 0 otherwise.
int RuleEvalIncreaseSetting(const Rule2Building& b, const RuleEnv2& env, int* out);

// gilde.exe 0x464ddc — RuleEvalAdjustSetting(eax=building, ebx=out). As above but
//   target = (int)trunc((b.f43 - 2.0) * 5.0)        clamped to [1, 4]
// and compared against the law's current value (law 7). (The original also looks
// up the building group and discards it.) Returns 1 if changed, 0 otherwise.
int RuleEvalAdjustSetting(const Rule2Building& b, const RuleEnv2& env, int* out);

// gilde.exe 0x4653c0 — RuleEvalRangeLow(ebx=out, eax=building). Requires
// city_count >= 4. Interpolates a low watermark inside the law's [low,high] range:
//   group 8: target = trunc(range*0.33 + low); if current > target ->
//            *out = low + rand(3), return 1.
//   else:    target = trunc(range*0.66 + low); if current < target ->
//            *out = high - rand(3), return 1.
// Returns 0 otherwise.  (range = high - low; law 11.)
int RuleEvalRangeLow(const Rule2Building& b, const RuleEnv2& env, int* out);

// gilde.exe 0x465588 — RuleEvalRangeHigh(ebx=out, eax=building). Requires
// city_count >= 4. Mirrors RangeLow with the upper coefficients (law 13):
//   group 9: target = trunc(range*0.55 + low); if current < target ->
//            *out = high - rand(6), return 1.
//   else:    target = trunc(range*0.45 + low); if current > target ->
//            *out = low + rand(6), return 1.
// Returns 0 otherwise.
int RuleEvalRangeHigh(const Rule2Building& b, const RuleEnv2& env, int* out);

// gilde.exe 0x4654a4 — RuleEvalInterpolatedTarget(eax=building, ebx=out). Computes
// a 0..1 blend factor t = clamp((3.0 - b.f43) * (b.f7 / 700), 0, 1) and the law's
//   target = (int)trunc((high - low) * t + low)      (law 12)
// then writes it to *out unless |target - current| <= 3. Returns 1 if changed.
int RuleEvalInterpolatedTarget(const Rule2Building& b, const RuleEnv2& env, int* out);

// gilde.exe 0x4652a8 — RuleEvalSeasonalStock(eax=building, ebx=out). Tavern group
// (7) and non-tavern use mirrored interpolation across the law's [low,high]:
//   group 7:  target = clamp(trunc((b.f43 - 2.0)*range + low), low, high);
//             if target > current -> *out = target, return 1.
//   else:     target = clamp(trunc((3.0 - b.f43)*range + low), low, high);
//             if target < current -> *out = target, return 1.
// Returns 0 otherwise.  (range = high - low; law 10.)
int RuleEvalSeasonalStock(const Rule2Building& b, const RuleEnv2& env, int* out);

// gilde.exe 0x4657b0 — RuleEvalCostBenefit(eax=building, ecx=?, ebx=out). A
// toggle-shaped rule (like RuleEvalSettingToggle) whose "score" is
//   score = max(b.f52 - 2.0, b.f55 - 2.0) * groupMul
// with groupMul: groups 11/12 -> 1.2 (flt_61A078); group 7 -> 0.8; else 1.1.
// base = (b.f61 - 2.0)*b.f63, high = (b.f67 - 2.0)*b.f69, law enable = law 15:
//   score < 0.5 && enable==0: if base > high -> *out=1, return 1.
//   score > 0.5 && enable==1 && high > base  -> *out=0, return 1.
// Returns 0 otherwise.
int RuleEvalCostBenefit(const Rule2Building& b, const RuleEnv2& env, int* out);

} // namespace guild::ai
