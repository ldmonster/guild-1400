#pragma once
// AiMethod scoring / decision leaf family — continuation (gilde.exe
// 0x467994..0x469e1c). These are the self-contained pieces of the AI method
// planner's precondition / score evaluators: wealth-tier classification, the
// favorability comparison gates, drink consumption simulation, the wealth/class
// scoring functions, distance penalties and the small terminal "EvalReturnN"
// state machine the executor branches through.
//
// Each function is a pure transform over a person/target record plus a handful of
// cross-module leaf reads (person-record lookup, favorability, total wealth, money
// conversion, office rank, the law/Gesetz record, and two RNG sources). Those
// leaves live in other modules (entity / economy / cutscene clusters) so they are
// injected through `MethodEnv` with INERT DEFAULTS, exactly the hooks-with-defaults
// pattern: the score/decision arithmetic is exercised byte-faithfully without
// pulling in the unbuilt sim subsystems. Tests install their own leaf hooks.
//
// Recovered float constants (gilde.exe, decoded byte-for-byte; offsets are VAs):
//   flt_61A220 = 20.0    (CompareFavorability margin)
//   flt_61A224 = 0.25    flt_61A228 = 2.0    flt_61A22C = 6.0   (ComputeMoodLevel)
//   flt_61A230 = 0.33    flt_61A234 = 0.8    dbl_61A238 = 0.75  (ComputeDrinkConsumption)
//   flt_61A240 = 0.01    flt_61A244 = 0.0025 flt_61A248 = 1.1   flt_61A24C = 0.9 (ShouldInitiateConflict)
//   flt_61A250 = -1.0    flt_61A254 = 0.5    (ClassifyWealthTier)
//   flt_61A258 = 5.0     flt_61A25C = 0.1    flt_61A260 = 0.7
//   flt_61A264 = 66.666  flt_61A268 = 33.333 (EvalPurchaseDesire)
//   dbl_61A270 = 10000.0 dbl_61A278 = 0.1    dbl_61A280 = 32.0  (ComputeWealthScoreA)
//   dbl_61A288 = 10000.0 dbl_61A290 = 0.05   dbl_61A298 = 32.0  (ComputeWealthScoreB)
//   flt_61A2A0 = 0.0005  (ComputeClassWeight)   flt_61A2A8 = 0.025 (ComputeChoiceWeights)
//   flt_61A2AC = 2.5     flt_61A2B0 = 75.0   (ShouldFollowTarget)
//   flt_61A2B8 = 1/3     (ComputeDistancePenalty)
#include "guild/common/types.h"

namespace guild::ai {

// ---------------------------------------------------------------------------
// Cross-module leaf reads, injected with inert defaults. The defaults mirror a
// "cold" world: no person records, zero favorability/wealth/rank, an all-zero law
// record. RNG defaults to the shared LCG (RandomModulo / RandomFloatScaled).
// ---------------------------------------------------------------------------
struct MethodEnv {
    // VIBE_Person_FindRecordById @0x58bc6c — resolve a person id to a record. The
    // callers read the record's first word (*record, the person's own id/key word)
    // and a few byte fields. We expose: id-word, byte@+13, byte@+358. Returns false
    // when the id resolves to no record (the original returns a null pointer).
    bool (*find_person)(int personId, u16* outIdWord, u8* outByte13, u8* outByte358) = nullptr;

    // VIBE_Ai_ComputePersonFavorability @0x594330 — favorability of person a toward
    // person b (mode arg always 1 at these sites). Default 0.
    float (*favorability)(u16 a, u16 b) = nullptr;

    // VIBE_Person_ComputeTotalWealth @0x591f7c — total liquid+asset wealth of a
    // person (the `a2` table arg is a scratch/jump table the caller passes; opaque
    // here). Default 0.
    int (*total_wealth)(u16 personIdWord) = nullptr;

    // VIBE_Money_ConvertToDisplayCoord @0x58f14c — money -> display scale (uses the
    // global currency byte byte_6477A1). Default identity.
    int (*money_to_display)(int amount) = nullptr;

    // VIBE_Person_ComputeOfficeRank @0x58bccc — person's office/title rank. Default 0.
    int (*office_rank)(u16 personIdWord) = nullptr;

    // VIBE_Building_ComputeRatingCurveA @0x58a6e8 — a rating-curve sample for an
    // index (ShouldInitiateConflict samples index 4 twice). Default 0.
    float (*rating_curve)(int index) = nullptr;

    // VIBE_Gesetz_GetRecord @0x4c244c, law 0 — the callers read record dword[6]
    // (the EvalPurchaseDesire "wealth gate" current value). Default 0.
    int (*law_record_d6)(int lawId) = nullptr;

    // VIBE_Math_Distance2D @0x58525c — distance between two scene points. The
    // ComputeChoiceWeights caller samples 3 destinations; default 0.
    float (*distance2d)(int idxA, int idxB) = nullptr;

    // VIBE_Cutscene_RandFloat @0x4aca48 / VIBE_Cutscene_RandInt @0x4ac9e8 — the
    // drink-sim RNG. RandFloat returns a 0..1 float; RandInt(n) returns rand%n.
    // Default: RandFloat -> 0.5, RandInt -> 0 (deterministic, no LCG draw).
    float (*cutscene_randfloat)(int ctx, int step) = nullptr;
    int   (*cutscene_randint)(unsigned n, int ctx) = nullptr;

    // RandomModulo(n) — defaults to the shared LCG (ai::RandomModulo).
    int (*rng_mod)(u16 n) = nullptr;
    // RandomFloatScaled() — defaults to the shared LCG (util::RandomFloatScaled).
    double (*rng_float)() = nullptr;
};

// Return a MethodEnv whose function pointers are all null (-> inert defaults are
// applied internally). Tests start from this and override the leaves they exercise.
MethodEnv DefaultMethodEnv();

// ---------------------------------------------------------------------------
// The drink-class table (gilde.exe dword_466445 @0x466445). Seven 16-byte entries,
// one per drink/wealth class 0..6. The accessors read it at deliberately misaligned
// offsets (the original packs id, an int weight and two floats into overlapping
// dwords); we keep the raw 116 bytes and mirror the exact pointer arithmetic.
//   class id  = dword@(+0)>>24             (entry+3 byte)
//   weight    = int @(entry+7)             (dword_46644C[entry])  = {10,13,16,19,23,27,32}
//   base      = float@(entry+11)           (v3[2])                = {20,40,60,80,100,120,140}
//   randScale = float@(entry+15)           (v3[3])                = {40,60,80,100,120,140,160}
// ---------------------------------------------------------------------------
constexpr int kDrinkClassCount = 7;

// Returns a pointer into the table at byte offset (entry*16 + 3) for class `cid`,
// mirroring VIBE_AiMethod_LookupClassEntry. nullptr when no class matches.
const u8* LookupClassEntry(u8 cid);

// gilde.exe 0x4680b0 — VIBE_AiMethod_LookupClassEntry (re-exported as the table
// scan above). Returns the matched entry's drink "base"/"randScale" pair.
bool LookupDrinkParams(u8 cid, int* outWeight, float* outBase, float* outRandScale);

// ---------------------------------------------------------------------------
// Terminal "EvalReturnN" state machine (the executor's per-method result mapper).
// ---------------------------------------------------------------------------

// gilde.exe 0x469da8 — VIBE_AiMethod_StubReturnZero (__stdcall). return 0.
char StubReturnZero();
// gilde.exe 0x469db4 — VIBE_AiMethod_StubReturn6. return 6.
char StubReturn6();
// gilde.exe 0x469dc8 — VIBE_AiMethod_StubReturn7. return 7.
char StubReturn7();
// gilde.exe 0x469ddc — VIBE_AiMethod_StubReturn8. return 8.
char StubReturn8();
// gilde.exe 0x469df0 — VIBE_AiMethod_StubReturn1. return 1.
char StubReturn1();
// gilde.exe 0x469e04 — VIBE_AiMethod_StubReturn2. return 2.
char StubReturn2();
// gilde.exe 0x469e18 — VIBE_AiMethod_StubReturn3. return 3.
char StubReturn3();

// gilde.exe 0x469d9c — VIBE_AiMethod_EvalReturnSix(al=result, bl=armed, +ctx).
//   if (result) return StubReturnZero(); if (!armed) return 6; return result;
char EvalReturnSix(char result, char armed);
// gilde.exe 0x469db8 — VIBE_AiMethod_EvalReturnSeven(al=result, bl=armed, +ctx).
//   if (result) return StubReturnZero(); if (!armed) return 7; return result;
char EvalReturnSeven(char result, char armed);

// ---------------------------------------------------------------------------
// Score / precondition evaluators.
// ---------------------------------------------------------------------------

// gilde.exe 0x467d60 — VIBE_AiMethod_ClassifyWealthTier(eax=person).
//   tier = trunc(person.f1 + (-1.0));  (f1 = *(float*)(person+4))
//   if frac(person.f1) >= 0.5: ++tier;     (frac via floor)
//   maps tier -> wealth class byte: <0->0, 0..3->0, 4..5->1, 6..7->2, 8..9->3,
//   10..11->4, 12..13->5, 14..16->6, else 7.
char ClassifyWealthTier(float gaugeValue);

// gilde.exe 0x4679a4 — VIBE_AiMethod_CompareFavorability(eax=ctx). Reads three
// person ids out of the ctx record (ctx+40 -> A's record; A+24 -> B id; A+20 ->
// C id) then compares fav(A,B) vs fav(C,A):
//   if A's record missing -> return 1.
//   d = fav(B->id, A->idWord) ; e = fav(C->id, A->idWord)   (mode 1)
//   if d - e >  20.0 -> 1; if e - d <= 20.0 -> RandomModulo(2); else 0.
// `aId`,`bId`,`cId` are the three resolved person id words (B,C resolved via the
// chained record reads); `favBA`/`favCA` are fav(B,A) and fav(C,A).
int CompareFavorability(bool aFound, float favBA, float favCA, const MethodEnv& env);

// gilde.exe 0x467c24 — VIBE_AiMethod_ShouldInitiateConflict(eax=targetWord, edx=ctx).
//   if (ctx[18]==1 || ctx[2]==1) return 0;
//   aggr = fav(rec(ctx[6]), target)*0.01 ; def = fav(rec(ctx_chain[5]), target)*0.01
//   baseA = ratingCurve(4); baseB = ratingCurve(4);
//   defScore = ctx_field4^2 * 0.0025 + baseB;
//   if (ctx[17]) { aggr*=1.1; def*=0.9; }
//   return def + defScore < aggr + baseA;
// `gateBlocked` = (ctx[18]==1 || ctx[2]==1). favA/favB the two favorabilities,
// `field4` = *(float*)(ctx_chain+4), `flag68` = ctx[17] (the *(ctx+68)!=0 branch).
int ShouldInitiateConflict(bool gateBlocked, float favA, float favB, float field4,
                           bool flag68, const MethodEnv& env);

// gilde.exe 0x467ae0 — VIBE_AiMethod_ComputeDrinkConsumption(eax=actor). Simulates
// repeated drinks for a tavern actor, accumulating spend until the budget (actor.f3
// = *(float*)(actor+12)) is hit or a per-drink cap (budget*0.33, decayed *0.8 each
// round) is exceeded. classByte = *(actor+56); rounds = *(actor+60); halfPrice =
// *(actor+64). Writes the result flag to *outFlag and -spend to *outSpend.
// Returns the flag (1 == drank). Uses env.cutscene_randfloat/randint.
int ComputeDrinkConsumption(u8 classByte, float budget, int rounds, bool halfPrice,
                            int* outFlag, float* outSpend, const MethodEnv& env);

// gilde.exe 0x467ed4 / 0x467f88 — VIBE_AiMethod_ComputeWealthScore{A,B}. Sum of two
// persons' display-wealth, floored at 10000, then  w*coef / (log10(w)+1) * 32.
//   A: coef 0.1 ; B: coef 0.05.
int ComputeWealthScoreA(u16 idA, u16 idB, const MethodEnv& env);
int ComputeWealthScoreB(u16 idA, u16 idB, const MethodEnv& env);

// gilde.exe 0x46803c — VIBE_AiMethod_ComputeClassWeight(eax=persons, esi=table).
//   w = trunc((wealth(a)+wealth(b)) * 0.0005);
//   look up the drink-class weight for class `classByte` (else weight of class 0).
//   return classWeight * w.
int ComputeClassWeight(u16 idA, u16 idB, u8 classByte, const MethodEnv& env);

// gilde.exe 0x4685fc — VIBE_AiMethod_ShouldFollowTarget(eax=ctx, edx=target).
//   if (ctx.f1 == target[131]) return 0;
//   rankDelta = (rank(target) - rank(self)) * 2.5;
//   score = fav(self, target) + rankDelta;
//   return RandomFloatScaled()*75.0 <= score.
// `selfWord`/`targetWord` resolved id words; `equal` = (ctx.f1==target[131]).
int ShouldFollowTarget(bool equal, u16 selfWord, u16 targetWord, const MethodEnv& env);

// gilde.exe 0x468910 — VIBE_AiMethod_ComputeDistancePenalty(ebx=scale, eax=a, edx=b).
//   return trunc((3*(rank(a) - rank(b)) + 30) * scale * (1/3)).
int ComputeDistancePenalty(int scale, u16 idA, u16 idB, const MethodEnv& env);

} // namespace guild::ai
