#pragma once
// MeisterAi RuleEval decision/scoring family (gilde.exe 0x464660..0x466259).
//
// The MeisterAi_RuleEval* functions are the per-building DECISION rules the Guild-
// Master AI applies to a production building each turn: production rate, price
// policy, quality, service level, wage level, and a family of "toggle/threshold"
// settings (capacity, supply, demand, ...). They are pure scoring functions: each
//   1. computes a market "score" from the building's good-rating vs. the city rate
//      scalar,                                                  score in float
//   2. applies a building-GROUP multiplier (by BuildingType_GroupFromCode),
//   3. computes three "trend" floats from the building's three setting pairs
//      (base / mid / high), each (setting + bias) * weight,
//   4. reads the matching law record value (Gesetz_GetRecord(lawId)),
//   5. decides: if score < 1.0 && law==0  -> if highTrend > baseTrend set out=1;
//               if score > 1.0 && law==1 && midTrend > baseTrend set out=0.
//
// The four leaf reads (BuildingType_GroupFromCode, Economy_LookupRateScalar,
// Building_EvalProductionRating, Gesetz_GetRecord(lawId)->value) are forward-
// declared as an injectable environment so the scoring core is exercised byte-
// faithfully without the entity/economy/law clusters. The tuning-constant tables
// (flt_619F78.. / flt_61A088..) are recovered byte-for-byte below.
#include "guild/common/types.h"

namespace guild::ai {

// --- building input the rules read ------------------------------------------
// The building record fields the trend computation reads (float* indices 61/63,
// 64/66, 67/69 in the original; named here). +353>>24 is the type code passed to
// BuildingType_GroupFromCode.
struct RuleBuilding {
    int  typeCode = 0;          // HIBYTE(*(building+353)) -> GroupFromCode input
    float setting61 = 0.0f;     // building float[61] (base setting)
    float weight63  = 0.0f;     // building float[63]
    float setting64 = 0.0f;     // building float[64] (mid setting)
    float weight66  = 0.0f;     // building float[66]
    float setting67 = 0.0f;     // building float[67] (high setting)
    float weight69  = 0.0f;     // building float[69]
};

// --- injectable leaf reads (the four coupled calls) -------------------------
// Mirrors VIBE_BuildingType_GroupFromCode(0x58a4c8), _Economy_LookupRateScalar
// (0x579a24), _Building_EvalProductionRating(0x58a794), _Gesetz_GetRecord(0x4c244c).
struct RuleEnv {
    // group id for a building type code (e.g. 7=tavern, 11/12=market, 5/6=...).
    int (*group_from_code)(int typeCode) = nullptr;
    // city rate scalar for a good column (u8).
    u8 (*rate_scalar)(int goodColumn) = nullptr;
    // building's production rating for a good column (float).
    float (*eval_rating)(const RuleBuilding& b, int goodColumn) = nullptr;
    // law record value for a law id (the +0 field the rules test, 0/1).
    int (*law_value)(int lawId) = nullptr;
};

// --- per-rule configuration (the recovered tuning block for each rule) ------
// Each rule's 8-float block: C0 (rate scale), C1 (rating scale), bias, [pad],
// then the group multipliers + the law id. Recovered byte-for-byte (see .cpp).
struct RuleConfig {
    int   goodColumn;     // good column passed to rate_scalar / eval_rating
    float c0;             // (rateScalar+1)*c0  baseline subtracted from rating
    float c1;             // *c1                rating-to-score scale
    double bias;          // added to each setting before *weight
    int   lawId;          // Gesetz_GetRecord(lawId)
    // group multiplier: applied to `score` when group matches one of the
    // configured groups. groupA/groupB/groupC are the cases; mulA/mulB/mulC the
    // multipliers; mulDefault the fallback. A group of -1 means "unused".
    int   groupA, groupB; float mulA;     // first case (1 or 2 group ids)
    int   groupC, groupD; float mulB;     // second case
    float mulDefault;                     // fallback multiplier (1.0 = no scale)
};

// gilde.exe 0x464660..0x466259 (shared core) — evaluate a market-scored setting
// rule (ServiceLevel/WageLevel/Demand/Supply/Capacity/Threshold). All share the
// identical scoring + decision shape:
//   score = (eval_rating(b,col) - (rate_scalar(col)+1)*c0) * c1
//   score *= groupMul(group_from_code(b.typeCode))    // per-rule group cases
//   base = (b.setting61+bias)*b.weight63              // baseline trend
//   mid  = (b.setting64+bias)*b.weight66              // "lower the setting" trend
//   high = (b.setting67+bias)*b.weight69              // "raise the setting" trend
//   law  = law_value(cfg.lawId)
//   if score < 1.0 && law==0: if high > base -> *out=1, fire
//   else if score > 1.0 && law==1 && mid > base -> *out=0, fire
// Returns true if the rule fires and writes the new setting value to *out;
// false if no change.
bool RuleEvalSettingToggle(const RuleBuilding& b, const RuleEnv& env,
                           const RuleConfig& cfg, int* out);

// gilde.exe 0x4658f8 / 0x4659e0 (core) — the ToggleA/ToggleB variant. These do
// NOT use a market rate score; the group multiplier is applied to the MID trend
// (which plays the role of the score), and the base/high trends are compared
// directly. cfg.c0/c1/goodColumn are ignored (rate_scalar/eval_rating not called):
//   midScore = (b.setting64 + bias)                   // NOT * weight
//   midScore *= groupMul                              // group cases
//   base = (b.setting61+bias)*b.weight63
//   high = (b.setting67+bias)*b.weight69
//   law  = law_value(cfg.lawId)
//   if midScore < scoreThreshLow(0.5) && law==0: if base > high -> *out=1
//   else if midScore > scoreThreshHigh(0.5) && law==1 && high > base -> *out=0
// Returns true + writes *out if the setting changes.
bool RuleEvalToggleVariant(const RuleBuilding& b, const RuleEnv& env,
                           const RuleConfig& cfg, int* out);

// gilde.exe 0x465ac8 — RuleEvalDemandToggle. This rule does NOT share the
// SettingToggle shape: the disasm computes only TWO trends (base from setting61,
// high from setting67 — NO mid/setting64) and its decision is the *inverse* of
// SettingToggle's first branch:
//   score = (eval_rating(b,4) - (rate_scalar(4)+1)*c0) * c1 * 0.8   (unconditional)
//   base  = (b.setting61 + bias) * b.weight63
//   high  = (b.setting67 + bias) * b.weight69
//   law   = law_value(18)
//   if score < 1.0 && law==0: if base > high -> *out=1, fire   (base > high!)
//   else if score > 1.0 && law==1 && high > base -> *out=0, fire
// (cfg.c0/c1/bias/goodColumn=4 from RuleConfigDemandToggle; group mul is the
// unconditional 0.8 default.) Returns true + writes *out if the setting changes.
bool RuleEvalDemandToggle(const RuleBuilding& b, const RuleEnv& env,
                          const RuleConfig& cfg, int* out);

// The recovered per-rule configs (the named tuning blocks). One accessor per rule.
const RuleConfig& RuleConfigServiceLevel();      // 0x464ad4, law 3,  goodCol 4
const RuleConfig& RuleConfigWageLevel();         // 0x464c00, law 4,  goodCol 3
const RuleConfig& RuleConfigToggleA();           // 0x4658f8, law 16  (variant)
const RuleConfig& RuleConfigToggleB();           // 0x4659e0, law 17  (variant)
const RuleConfig& RuleConfigDemandToggle();      // 0x465ac8, law 18, goodCol 4
const RuleConfig& RuleConfigSupplyToggleA();     // 0x465bc0, law 19, goodCol 3
const RuleConfig& RuleConfigSupplyToggleB();     // 0x465ccc, law 20, goodCol 3
const RuleConfig& RuleConfigCapacityToggle();    // 0x465dd8, law 21, goodCol 4
const RuleConfig& RuleConfigThresholdToggleA();  // 0x465efc, law 22, goodCol 2
const RuleConfig& RuleConfigThresholdToggleB();  // 0x466020, law 23, goodCol 2
const RuleConfig& RuleConfigThresholdToggleC();  // 0x466138, law 24, goodCol 3

// gilde.exe 0x464a30 (core) — RuleEvalQualitySetting. A standalone rule (no market
// score): given the current quality setting `quality` (building byte +12, 0/1/2),
// the law value `law` (Gesetz_GetRecord(2), 0/1/2), and the building's "quality
// affinity" float `affinity` (building int +460), decide the new quality:
//   quality==0 && law==1 -> out = (affinity>=0.4 ? 0 : 2)
//   quality==1 && law==0 -> out = (affinity<0.4) + 1     (0.4 -> 2, else 1)
//   law==2 && affinity>0.8 -> out = (quality!=0)
//   else: no change.
// Returns true + writes *out if the setting changes. Thresholds 0.4 / 0.8.
bool RuleEvalQualitySetting(int quality, int law, float affinity, int* out);

} // namespace guild::ai
