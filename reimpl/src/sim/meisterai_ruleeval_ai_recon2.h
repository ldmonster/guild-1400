#pragma once
// ===========================================================================
// MeisterAi automatic-management RULE EVALUATORS — pure decision reconstruction.
//
// These are the Guild-Master "auto manage my workshop" rule evaluators. Each one
// is invoked per managed building once per game-week; it reads a handful of engine
// values (the building's production rating, three of the building's weighted
// "policy" floats, the matching Gesetz/law record's current value, and — for a few
// — the city average quality and the player's relations), folds them into a single
// score, and decides whether to nudge one law/setting up or down.
//
// gilde.exe addresses (all __usercall; a1 = building record pointer @<eax>):
//   0x4647f8 VIBE_MeisterAi_RuleEvalPricePolicy       (a2 = out *@<ebx>)
//   0x464ad4 VIBE_MeisterAi_RuleEvalServiceLevel      (a2 = out *@<ebx>)
//   0x464c00 VIBE_MeisterAi_RuleEvalWageLevel         (a2 = out *@<ebx>)
//   0x464d0c VIBE_MeisterAi_RuleEvalDecreaseSetting   (a3 = out *@<ebx>)
//   0x464e4c VIBE_MeisterAi_RuleEvalStockTargetHigh   (a1 = out *@<ebx>)
//   0x46505c VIBE_MeisterAi_RuleEvalStockTargetMarket (a1 = out *@<ebx>)
//   0x46566c VIBE_MeisterAi_RuleEvalQualityTier       (a1 = out *@<ebx>)
//   0x4659e0 VIBE_MeisterAi_RuleEvalToggleB           (a3 = out *@<ebx>)
//   0x465ac8 VIBE_MeisterAi_RuleEvalDemandToggle      (a2 = out *@<ebx>)
//   0x465bc0 VIBE_MeisterAi_RuleEvalSupplyToggleA     (a2 = out *@<ebx>)
//   0x465ccc VIBE_MeisterAi_RuleEvalSupplyToggleB     (a2 = out *@<ebx>)
//   0x465dd8 VIBE_MeisterAi_RuleEvalCapacityToggle    (a2 = out *@<ebx>)
//   0x465efc VIBE_MeisterAi_RuleEvalThresholdToggleA  (a2 = out *@<ebx>)
//   0x466020 VIBE_MeisterAi_RuleEvalThresholdToggleB  (a2 = out *@<ebx>)
//   0x466138 VIBE_MeisterAi_RuleEvalThresholdToggleC  (a2 = out *@<ebx>)
//
// COUPLED LEAVES (passed as inputs so the decision math is a pure, deterministic
// function — never faked, never approximated; the live engine supplies these):
//   * group   = VIBE_BuildingType_GroupFromCode(HIBYTE(*(bldg+353)))  — building
//               trade-group classifier (1..12); selects the per-group score scale.
//   * rate    = VIBE_Economy_LookupRateScalar(idx)  — a 0..255 economy rate byte.
//   * rating  = VIBE_Building_EvalProductionRating(bldg, idx)  — production rating.
//   * f[]     = the building's float policy array (bldg as float*; indices 61/63/64/
//               66/67/69 read in the toggles; +244/252/256/264/268/276 in PricePolicy).
//   * gesetz  = VIBE_Gesetz_GetRecord(id, &rec) — the law record: cur/min/max.
//   * avgQ    = VIBE_Economy_ComputeAverageQuality() — city average quality (float).
//   * marketHi/marketLo etc — produced by the GameObject sweep, passed as ints.
//   * RandomModulo(n) draws — supplied via an explicit roll() functor.
//   * relations — VIBE_Ai_ComputePersonFavorability — passed in for PricePolicy.
//
// Recovered constants (gilde.exe, byte-exact via get_bytes — see .cpp).
//
// FP fidelity: the binary computes the score in 32-bit float, compares the score's
// raw bit pattern against 1065353216 (== 1.0f bits) i.e. `score < 1.0f`, and routes
// every float->int through VIBE_Coord_ConvertX (fistp w/ chop = trunc toward zero).
// We reproduce: float arithmetic in `float`, the `<1.0f` test, and (int) truncation.
// ===========================================================================
#include <cstdint>

#include "guild/common/types.h"

namespace guild::sim {

// ---------------------------------------------------------------------------
// Recovered scalar constants (byte-exact). Where the original stored an 8-byte
// double bias it is reproduced as double; per-score scales are float.
// ---------------------------------------------------------------------------
// Shared production-rating pre-scale: (rate+1) * kRateBias0 ; rating folded by
// (rating - that) * kRateBias1.  flt_619FE0/F98/0C8/0DC/0F4/10C/128/140/154/FC8.
constexpr float  kRateBias0   = 0.00396825f;  // 1/252
constexpr float  kRateBias1   = 0.02380952f;  // 1/42
constexpr double kPolicyBias  = -2.0;         // dbl_*A0/D0/E8/100/118/130/148/160/A8/FE8

// PricePolicy (0x4647f8) — group-dependent additive bias + ratio thresholds.
constexpr float  kPriceAddDefault = 5.0f;   // flt_619FA8
constexpr float  kPriceAddCraft   = 3.0f;   // flt_619FAC (groups 9,1,3,2)
constexpr float  kPriceAddTrade   = 7.0f;   // flt_619FB0 (groups 11,12)
constexpr float  kPriceTaxSlope   = 0.1f;   // flt_619FB4 ; *(40 - law.field10)
constexpr float  kPriceLowRatio   = 0.5f;   // flt_619FB8
constexpr float  kPriceFavHi      = 0.75f;  // flt_619FBC
constexpr float  kPriceFavLo      = 0.25f;  // flt_619FC0
constexpr float  kPriceHiRatio    = 1.39999998f; // flt_619FC4

// ServiceLevel (0x464ad4)
constexpr float  kServiceScaleTrade = 0.89999998f; // flt_619FD8 (groups 11,12)
constexpr float  kServiceScaleTav   = 1.10000002f; // flt_619FDC (group 7)
// WageLevel (0x464c00)
constexpr float  kWageScale         = 1.10000002f; // flt_619FF0
// DecreaseSetting (0x464d0c)
constexpr float  kDecreaseBase      = 3.0f;        // flt_619FF4
constexpr float  kDecreaseSlope     = 5.0f;        // flt_619FF8

// QualityTier (0x46566c) — pure tier classifier thresholds use multiples of the
// reference market offset (see ClassifyQualityTier).

// StockTargetHigh (0x464e4c) / StockTargetMarket (0x46505c)
constexpr float  kStockQ1       = 0.33000001f;     // flt_61A00C / 028
constexpr double kStockQ2       = 0.66;            // dbl_61A010 / 030
constexpr float  kStockHiW      = 0.66000003f;     // flt_61A018 / 038 (avgQ>=Q2)
constexpr float  kStockMidW     = 0.44f;           // flt_61A01C / 03C (Q1<=avgQ<Q2)
constexpr float  kStockLoW      = 0.22f;           // flt_61A020 / 040 (avgQ<Q1)
constexpr float  kStockHighRef  = 0.25f;           // flt_61A024 (StockTargetHigh only)

// ToggleB (0x4659e0)
constexpr float  kToggleBTav    = 0.89999998f;     // flt_61A0B0 (group 7)
constexpr float  kToggleBShop   = 1.5f;            // flt_61A0B4 (groups 5,6)
constexpr float  kToggleBLo     = 0.5f;            // flt_61A0B8
constexpr double kToggleBHi     = 0.5;             // dbl_61A0C0
// DemandToggle (0x465ac8)
constexpr float  kDemandScale   = 0.80000001f;     // flt_61A0D8
// SupplyToggleA/B (0x465bc0 / 0x465ccc)
constexpr float  kSupplyScale   = 1.10000002f;     // flt_61A0F0 / 108
// CapacityToggle (0x465dd8)
constexpr float  kCapScaleTrade = 1.10000002f;     // flt_61A124 (groups 11,12)
constexpr float  kCapScaleDef   = 0.89999998f;     // flt_61A120
// ThresholdToggleA (0x465efc)
constexpr float  kThreshAg11    = 1.29999995f;     // flt_61A13C (group 11)
constexpr float  kThreshADef    = 0.89999998f;     // flt_61A138
// ThresholdToggleB (0x466020) — group 11/12 scaled by 1.1
constexpr float  kThreshBScale  = 1.10000002f;     // flt_61A150
// ThresholdToggleC (0x466138)
constexpr float  kThreshCg12    = 1.29999995f;     // flt_61A16C (group 12)
constexpr float  kThreshCDef    = 0.89999998f;     // flt_61A168

// ---------------------------------------------------------------------------
// VIBE_Coord_ConvertX (0x5c6b08) — float->int truncation toward zero (fistp/chop).
// ---------------------------------------------------------------------------
inline int CoordTrunc(double v) { return static_cast<int>(v); }

// ---------------------------------------------------------------------------
// A Gesetz/law record snapshot. VIBE_Gesetz_GetRecord(id,&rec) fills a 24-byte
// struct; the evaluators read three int fields and (PricePolicy) a u16 at +10.
// We model exactly the fields the decompile touches:
//   cur  = rec[0]  (the current law value; output target compares against this)
//   minV = rec[+0/+4 depending on call — the lo bound, *a1 clamp floor)
//   maxV = rec[+8] (the hi bound, *a1 clamp ceiling)
//   field10 = u16 at +10 (only PricePolicy's tax term: 40 - field10)
// For the toggles, the decompile reads v[?]==0 / ==1 as the "current setting"
// (0 = off, 1 = on); that is `cur` here.
// ---------------------------------------------------------------------------
struct GesetzRecord {
    int cur     = 0;   // rec+0
    int loBound = 0;   // rec+4
    int hiBound = 0;   // rec+8
    u16 field10 = 0;   // rec+10
};

// ---------------------------------------------------------------------------
// The three weighted policy terms every toggle evaluator computes from the
// building float array (indices 61/63, 64/66, 67/69):
//   term = (f[i] + kPolicyBias) * f[j]
// Exposed for golden testing.
// ---------------------------------------------------------------------------
inline float PolicyTerm(float fi, float weight) {
    return static_cast<float>((static_cast<double>(fi) + kPolicyBias)) * weight;
}

// ===========================================================================
// SCORE: shared production-rating fold used by every toggle + price/wage/service.
//   pre   = ((double)rate + 1.0) * kRateBias0
//   score = (rating - pre) * kRateBias1
// Returns the raw score (before any per-group scale).
// ===========================================================================
float ProductionRatingScore(u8 rate, float rating);

// ===========================================================================
// The canonical TOGGLE decision (used verbatim by DemandToggle/SupplyA/SupplyB/
// CapacityToggle/ThresholdA/B/C/ServiceLevel/WageLevel/ToggleB). Inputs:
//   score  : the (already per-group-scaled) production score.
//   onTerm : (f[64]+bias)*f[66]   — the "currently on, should I turn off?" gate.
//   offTerm: (f[67]+bias)*f[69]   — the "currently off, should I turn on?" gate.
//   refTerm: (f[61]+bias)*f[63]   — the reference both gates compare against.
//   curSetting : the law's current 0/1 value (rec.cur).
// Logic (1:1):
//   if (score < 1.0f && curSetting==0) { if (offTerm > refTerm) {*out=1; return 1;} return 0; }
//   if (score <= 1.0f || curSetting!=1 || onTerm <= refTerm) return 0;
//   *out = 0; return 1;
// (ServiceLevel uses f[64]/f[61]/f[67] in the order onTerm=f64, ref=f61, off=f67;
//  ToggleB uses a different gate set — see EvalToggleB.)
// Returns 1 with *out set if a change is decided, else 0.
// ===========================================================================
int ToggleDecision(float score, float onTerm, float offTerm, float refTerm,
                   int curSetting, int* out);

// ===========================================================================
// Per-evaluator entry points. Each takes the recovered engine reads as inputs.
// `f` is the building float policy array base (callers pass the live bldg as
// float*). For golden tests we pass a small array covering indices [61..69].
// ===========================================================================

// 0x464c00 — Wage level. score = ProductionRatingScore(rate, rating); then
// ToggleDecision(score, onTerm=f64, offTerm=f67, refTerm=f61, cur, out).
int EvalWageLevel(u8 rate, float rating, float f61, float f63, float f64,
                  float f66, float f67, float f69, const GesetzRecord& law, int* out);

// 0x464ad4 — Service level. group 7 → score*=1.1 ; groups 11/12 → score*=0.9.
int EvalServiceLevel(int group, u8 rate, float rating, float f61, float f63,
                     float f64, float f66, float f67, float f69,
                     const GesetzRecord& law, int* out);

// 0x465ac8 — Demand toggle. score *= 0.8 (unconditional).
int EvalDemandToggle(u8 rate, float rating, float f61, float f63, float f64,
                     float f66, float f67, float f69, const GesetzRecord& law, int* out);

// 0x465bc0 / 0x465ccc — Supply toggle A/B (identical math). score *= 1.1.
int EvalSupplyToggle(u8 rate, float rating, float f61, float f63, float f64,
                     float f66, float f67, float f69, const GesetzRecord& law, int* out);

// 0x465dd8 — Capacity toggle. groups 11/12 → score*=1.1 else score*=0.9.
int EvalCapacityToggle(int group, u8 rate, float rating, float f61, float f63,
                       float f64, float f66, float f67, float f69,
                       const GesetzRecord& law, int* out);

// 0x465efc — Threshold toggle A. group 11 → score*=1.3 else score*=0.9.
int EvalThresholdToggleA(int group, u8 rate, float rating, float f61, float f63,
                         float f64, float f66, float f67, float f69,
                         const GesetzRecord& law, int* out);

// 0x466020 — Threshold toggle B. groups 11/12 → score*=1.1 (else unscaled).
int EvalThresholdToggleB(int group, u8 rate, float rating, float f61, float f63,
                         float f64, float f66, float f67, float f69,
                         const GesetzRecord& law, int* out);

// 0x466138 — Threshold toggle C. group 12 → score*=1.3 else score*=0.9.
int EvalThresholdToggleC(int group, u8 rate, float rating, float f61, float f63,
                         float f64, float f66, float f67, float f69,
                         const GesetzRecord& law, int* out);

// 0x4659e0 — Toggle B. Uses a different gate set:
//   ref = (f61+bias)*f63 ; on = f64+bias ; off = (f67+bias)*f69
//   groups 5/6 → on *= 1.5 ; group 7 → on *= 0.9.
//   if (on < kToggleBLo && cur==0) { if (ref > off) {*out=1; return 1;} return 0; }
//   if (on <= kToggleBHi || cur!=1 || off <= ref) return 0; *out=0; return 1;
int EvalToggleB(int group, float f61, float f63, float f64, float f67, float f69,
                const GesetzRecord& law, int* out);

// 0x464d0c — Decrease setting. score = (kDecreaseBase - f43) * kDecreaseSlope
// truncated to int; then compares against the law value `groupVal` (the original
// reads VIBE_Gesetz_GetRecord(5,...) field; the decompile compares v5==v8 where
// v8 is the gesetz field and v5 is the computed group value). If equal → 0, else
// *out = computedValue, return 1.  We expose the pure score+compare:
//   computed = CoordTrunc((kDecreaseBase - f43) * kDecreaseSlope)
//   if (computed == law.cur) return 0; *out = computed; return 1;
// (f43 = *(float*)(bldg+172), i.e. building float index 43.)
int EvalDecreaseSetting(float f43, const GesetzRecord& law, int* out);

// 0x46566c — Quality tier. Pure classifier + step-toward-target clamp.
// ClassifyQualityTier(total, ref): tiers by multiples of `ref`:
//   total < 3*ref            → 4
//   total > 12*ref           → 0
//   total > 8*ref            → 1
//   total > 6*ref            → 2
//   else                     → 3
int ClassifyQualityTier(int total, int ref);

// The step-toward-target decision (0x46566c tail). law.cur = current setting,
// law.loBound = floor, law.hiBound = ceiling, `target` = ClassifyQualityTier result.
//   if abs(law.cur - target) < 2  → no change (return 0)
//   step the target one toward cur, clamp to [loBound,hiBound], write *out, return 1.
// Reproduces the exact branch structure of the binary.
int EvalQualityTierStep(int target, const GesetzRecord& law, int* out);

// ===========================================================================
// StockTarget weighting (0x464e4c / 0x46505c) — the score part is pure:
//   base = lo ; span = hi - lo ;
//   if avgQ >= Q1 : if avgQ >= Q2 → w=kStockHiW else w=kStockMidW
//   else                                            w=kStockLoW
//   val = CoordTrunc(base + span*w)
//   StockTargetHigh: if group NOT in {1,2,3,5} → --val
//   StockTargetMarket: if group IN {1,2,3,5}   → --val
//   then a market-reference bump: if marketCur < 3*marketRef → val += roll()+1
//   then: if cur-3 > val → step DOWN (cur-1-roll2), clamp to >= lo
//         else if cur+3 < val → step UP (cur+1+roll2), clamp to <= hi
//         else 0.
// `roll`/`roll2` are the RandomModulo draws (StockHigh: mod3/mod3; Market: mod3/mod2).
// Returns the truncated weighted target (pre-bump) for golden testing.
// ===========================================================================
int StockWeightedTarget(int loBound, int hiBound, float avgQ);

} // namespace guild::sim
