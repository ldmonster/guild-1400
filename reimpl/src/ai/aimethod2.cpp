#include "ai/aimethod2.h"

#include <cmath>
#include <cstring>

#include "ai/method.h"            // ai::RandomModulo (the shared LCG modulo)
#include "util/math_rng_float.h"  // util::RandomFloatScaled

namespace guild::ai {

// ---------------------------------------------------------------------------
// Recovered float constants (raw bytes -> values; see header).
// ---------------------------------------------------------------------------
namespace {
constexpr float flt_61A220 = 20.0f;     // CompareFavorability margin
constexpr float flt_61A230 = 0.33f;     // drink per-round cap fraction
constexpr float flt_61A234 = 0.8f;      // drink cap decay
constexpr double dbl_61A238 = 0.75;     // drink 1-in-4 spend scale
constexpr float flt_61A240 = 0.01f;     // conflict favorability scale
constexpr float flt_61A244 = 0.0025f;   // conflict defense field^2 scale
constexpr float flt_61A248 = 1.1f;      // conflict aggressor mult (flag68)
constexpr float flt_61A24C = 0.9f;      // conflict defender mult (flag68)
constexpr float flt_61A250 = -1.0f;     // wealth-tier bias
constexpr float flt_61A254 = 0.5f;      // wealth-tier round-up threshold
constexpr double dbl_61A270 = 10000.0;  // wealthA floor
constexpr double dbl_61A278 = 0.1;      // wealthA coef
constexpr double dbl_61A280 = 32.0;     // wealthA gain
constexpr double dbl_61A288 = 10000.0;  // wealthB floor
constexpr double dbl_61A290 = 0.05;     // wealthB coef
constexpr double dbl_61A298 = 32.0;     // wealthB gain
constexpr float flt_61A2A0 = 0.0005f;   // class-weight scale
constexpr float flt_61A2AC = 2.5f;      // follow rank-delta scale
constexpr float flt_61A2B0 = 75.0f;     // follow RNG threshold
constexpr float flt_61A2B8 = 1.0f / 3.0f; // distance-penalty scale (0.33333334f)

// trunc toward zero — VIBE_Coord_ConvertX (the value is already on the FPU stack;
// the result is consumed as (int)v, i.e. truncation toward zero).
inline double TruncToZero(double v) { return std::trunc(v); }

// VIBE_Math_NormalizeAngle @0x5eef4c: StoreAndZero stores trunc(x) (ConvertX rounds
// toward zero per the 0x1F control word), then if x<0 it adds dbl_62BFFC (== -1.0).
// So the result is trunc(x) for x>=0 and trunc(x)-1.0 for x<0. This is NOT std::floor:
// for a negative *integer* x (e.g. -2.0) it returns x-1 (-3.0), whereas floor(-2.0)==-2.0.
// Model the binary exactly.
inline double NormalizeAngle(double v) {
    double t = std::trunc(v);
    if (v < 0.0)
        t += -1.0;
    return t;
}

// log10(x) — the binary computes __FYL2X__(x, log10(2)) == log2(x)*log10(2) ==
// log10(x). Use the identity directly.
inline double Log10(double v) { return std::log10(v); }

// --- inert default leaves ---------------------------------------------------
bool Def_find_person(int, u16* a, u8* b, u8* c) {
    if (a) *a = 0;
    if (b) *b = 0;
    if (c) *c = 0;
    return false; // cold world: no record
}
float  Def_favorability(u16, u16) { return 0.0f; }
int    Def_total_wealth(u16) { return 0; }
int    Def_money_to_display(int amount) { return amount; }
int    Def_office_rank(u16) { return 0; }
float  Def_rating_curve(int) { return 0.0f; }
int    Def_law_record_d6(int) { return 0; }
float  Def_distance2d(int, int) { return 0.0f; }
float  Def_cutscene_randfloat(int, int) { return 0.5f; }
int    Def_cutscene_randint(unsigned, int) { return 0; }
int    Def_rng_mod(u16 n) { return ai::RandomModulo(n); }
double Def_rng_float() { return util::RandomFloatScaled(); }

// Resolve a MethodEnv to one with all leaves bound (defaults where null).
MethodEnv Bind(const MethodEnv& in) {
    MethodEnv e = in;
    if (!e.find_person)        e.find_person = Def_find_person;
    if (!e.favorability)       e.favorability = Def_favorability;
    if (!e.total_wealth)       e.total_wealth = Def_total_wealth;
    if (!e.money_to_display)   e.money_to_display = Def_money_to_display;
    if (!e.office_rank)        e.office_rank = Def_office_rank;
    if (!e.rating_curve)       e.rating_curve = Def_rating_curve;
    if (!e.law_record_d6)      e.law_record_d6 = Def_law_record_d6;
    if (!e.distance2d)         e.distance2d = Def_distance2d;
    if (!e.cutscene_randfloat) e.cutscene_randfloat = Def_cutscene_randfloat;
    if (!e.cutscene_randint)   e.cutscene_randint = Def_cutscene_randint;
    if (!e.rng_mod)            e.rng_mod = Def_rng_mod;
    if (!e.rng_float)          e.rng_float = Def_rng_float;
    return e;
}

// --- drink-class table (dword_466445 @0x466445), 116 bytes, byte-exact. --------
// Seven 16-byte entries. The accessors read it at misaligned offsets, matching the
// original pointer arithmetic (id>>24 at +3, int weight at +7, base float at +11,
// randScale float at +15 — the last 3 bytes of randScale overlap the next entry's
// leading zero bytes, which is exactly how the shipped table is laid out).
// 119 bytes: the seven 16-byte entries (112 B) plus the 3 leading bytes of the
// adjacent table that class 6's randScale float (+15) reads into (00 00 20 43 ==
// 160.0). This is the deliberate overlap of the original packed layout.
const u8 kDrinkTable[119] = {
    0x00, 0x80, 0x3f, 0x00, 0x00, 0x00, 0x00, 0x0a, 0x00, 0x00, 0x00, 0x00, 0x00, 0xa0, 0x41, 0x00,
    0x00, 0x20, 0x42, 0x01, 0x00, 0x00, 0x00, 0x0d, 0x00, 0x00, 0x00, 0x00, 0x00, 0x20, 0x42, 0x00,
    0x00, 0x70, 0x42, 0x02, 0x00, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x70, 0x42, 0x00,
    0x00, 0xa0, 0x42, 0x03, 0x00, 0x00, 0x00, 0x13, 0x00, 0x00, 0x00, 0x00, 0x00, 0xa0, 0x42, 0x00,
    0x00, 0xc8, 0x42, 0x04, 0x00, 0x00, 0x00, 0x17, 0x00, 0x00, 0x00, 0x00, 0x00, 0xc8, 0x42, 0x00,
    0x00, 0xf0, 0x42, 0x05, 0x00, 0x00, 0x00, 0x1b, 0x00, 0x00, 0x00, 0x00, 0x00, 0xf0, 0x42, 0x00,
    0x00, 0x0c, 0x43, 0x06, 0x00, 0x00, 0x00, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0c, 0x43, 0x00,
    0x00, 0x20, 0x43,
};

inline u32 LoadU32(const u8* p) {
    u32 v; std::memcpy(&v, p, 4); return v;
}
inline i32 LoadI32(const u8* p) {
    i32 v; std::memcpy(&v, p, 4); return v;
}
inline float LoadF32(const u8* p) {
    float v; std::memcpy(&v, p, 4); return v;
}
} // namespace

MethodEnv DefaultMethodEnv() { return MethodEnv{}; }

// gilde.exe 0x4680b0 — VIBE_AiMethod_LookupClassEntry
//   v2=0; while (dword_466445[v2]>>24 != cid) { v2+=4; if (v2>=28) return 0; }
//   return (char*)&dword_466445[v2] + 3;
// dword_466445[v2] reads byte offset 4*v2 (v2 in {0,4,8,...,24} -> byte {0,16,...}).
const u8* LookupClassEntry(u8 cid) {
    int v2 = 0; // dword index
    while ((LoadU32(kDrinkTable + 4 * v2) >> 24) != cid) {
        v2 += 4;
        if (v2 >= 28)
            return nullptr;
    }
    return kDrinkTable + 4 * v2 + 3;
}

bool LookupDrinkParams(u8 cid, int* outWeight, float* outBase, float* outRandScale) {
    // ComputeClassWeight/ComputeDrinkConsumption scan with a 16-byte stride; this is
    // the same 7-entry table. weight at entry+7, base at entry+11, randScale +15.
    for (int n = 0; n < kDrinkClassCount; ++n) {
        const u8* e = kDrinkTable + n * 16;
        if ((LoadU32(e) >> 24) == cid) {
            if (outWeight)    *outWeight = LoadI32(e + 7);
            if (outBase)      *outBase = LoadF32(e + 11);
            if (outRandScale) *outRandScale = LoadF32(e + 15);
            return true;
        }
    }
    return false;
}

// --- terminal stubs ---------------------------------------------------------
char StubReturnZero() { return 0; }   // 0x469da8
char StubReturn6() { return 6; }      // 0x469db4
char StubReturn7() { return 7; }      // 0x469dc8
char StubReturn8() { return 8; }      // 0x469ddc
char StubReturn1() { return 1; }      // 0x469df0
char StubReturn2() { return 2; }      // 0x469e04
char StubReturn3() { return 3; }      // 0x469e18

// gilde.exe 0x469d9c — VIBE_AiMethod_EvalReturnSix
char EvalReturnSix(char result, char armed) {
    if (result)
        return StubReturnZero();
    if (!armed)
        return 6;
    return result;
}

// gilde.exe 0x469db8 — VIBE_AiMethod_EvalReturnSeven
char EvalReturnSeven(char result, char armed) {
    if (result)
        return StubReturnZero();
    if (!armed)
        return 7;
    return result;
}

// gilde.exe 0x467d60 — VIBE_AiMethod_ClassifyWealthTier
//   v1 = person.f1 + (-1.0);  ConvertX -> v5 = trunc(v1)
//   v4 = person.f1; if (v4 - floor(v4) >= 0.5) ++v5;
//   v5<0 -> 0; switch over [0,16] -> wealth class; default 7.
char ClassifyWealthTier(float gaugeValue) {
    double v1 = static_cast<double>(gaugeValue) + flt_61A250;
    int v5 = static_cast<int>(TruncToZero(v1));
    double v4 = static_cast<double>(gaugeValue);
    if (v4 - NormalizeAngle(v4) >= flt_61A254)
        ++v5;
    if (v5 < 0)
        return 0;
    switch (v5) {
        case 0: case 1: case 2: case 3:   return 0;
        case 4: case 5:                   return 1;
        case 6: case 7:                   return 2;
        case 8: case 9:                   return 3;
        case 10: case 11:                 return 4;
        case 12: case 13:                 return 5;
        case 14: case 15: case 16:        return 6;
        default:                          return 7;
    }
}

// gilde.exe 0x4679a4 — VIBE_AiMethod_CompareFavorability
//   if (!record(ctx+40)) return 1;
//   d = fav(B, A, 1); e = fav(C, A, 1);
//   if (d - e > 20.0) return 1;
//   if (e - d <= 20.0) return RandomModulo(2);
//   return 0;
int CompareFavorability(bool aFound, float favBA, float favCA, const MethodEnv& env) {
    MethodEnv e = Bind(env);
    if (!aFound)
        return 1;
    if (favBA - favCA > flt_61A220)
        return 1;
    if (favCA - favBA <= flt_61A220)
        return static_cast<u16>(e.rng_mod(2));
    return 0;
}

// gilde.exe 0x467c24 — VIBE_AiMethod_ShouldInitiateConflict
//   if (gateBlocked) return 0;
//   v13 = favA * 0.01; v12 = favB * 0.01;
//   v9 = ratingCurve(4); v10 = ratingCurve(4);
//   v11 = field4*field4*0.0025 + v10;
//   if (flag68) { v13 *= 1.1; v12 *= 0.9; }
//   return v12 + v11 < v13 + v9;
int ShouldInitiateConflict(bool gateBlocked, float favA, float favB, float field4,
                           bool flag68, const MethodEnv& env) {
    MethodEnv e = Bind(env);
    if (gateBlocked)
        return 0;
    float v13 = favA * flt_61A240;
    float v12 = favB * flt_61A240;
    float v9 = e.rating_curve(4);
    float v10 = e.rating_curve(4);
    float v11 = field4 * field4 * flt_61A244 + v10;
    if (flag68) {
        v13 = v13 * flt_61A248;
        v12 = v12 * flt_61A24C;
    }
    return (v12 + v11 < v13 + v9) ? 1 : 0;
}

// gilde.exe 0x467ae0 — VIBE_AiMethod_ComputeDrinkConsumption
//   *outFlag(actor+72) = 0; class = actor[56];
//   find drink params for class (v3); budget = actor.f3 (actor+12); spent=0;
//   cap = budget * 0.33;  rounds = actor+60;  halfPrice = actor+64;
//   for (i=0; rounds>0; ) {
//     priceMul = halfPrice ? 0.5 : 1.0;
//     drink = RandFloat()*randScale*priceMul + base;
//     next = spent + drink;
//     if (next >= budget) { spent=next; if (RandInt(4)) spent *= 0.75; flag=1; break; }
//     if (drink > cap) { flag=1; spent += cap; break; }
//     ++i; spent = next; cap *= 0.8;
//     if (i >= rounds) break;
//   }
//   *outSpend(actor+76) = -spent; return flag;
int ComputeDrinkConsumption(u8 classByte, float budget, int rounds, bool halfPrice,
                            int* outFlag, float* outSpend, const MethodEnv& env) {
    MethodEnv e = Bind(env);

    int flag = 0;
    float base = 0.0f, randScale = 0.0f;
    int weight = 0;
    bool found = LookupDrinkParams(classByte, &weight, &base, &randScale);
    (void)weight;
    // When no class matches the original keeps v3 == null and would fault; the
    // shipped table covers classes 0..6, so a cold-default class falls back to 0
    // values (base/randScale stay 0) for safety in the reimpl.
    if (!found) { base = 0.0f; randScale = 0.0f; }

    float spent = 0.0f;
    float cap = budget * flt_61A230;   // v13
    float priceMul = halfPrice ? 0.5f : 1.0f; // computed each round in the original

    if (rounds > 0) {
        int i = 0;
        while (true) {
            priceMul = halfPrice ? 0.5f : 1.0f;
            float drink = static_cast<float>(e.cutscene_randfloat(0, i)) * randScale * priceMul + base; // v10
            double next = static_cast<double>(spent) + drink; // v6
            if (next >= budget) {                  // budget reached
                spent = static_cast<float>(next);
                if (e.cutscene_randint(4u, 0))     // 3-in-4: tip down to 0.75
                    spent = static_cast<float>(spent * dbl_61A238);
                flag = 1;
                break;
            }
            if (drink > static_cast<double>(cap)) { // per-round cap exceeded
                flag = 1;
                spent = spent + cap;
                break;
            }
            ++i;
            spent = static_cast<float>(next);
            cap = cap * flt_61A234;
            if (i >= rounds)
                break;
        }
    }

    if (outFlag)  *outFlag = flag;
    if (outSpend) *outSpend = -spent;
    return flag;
}

namespace {
// shared body for ComputeWealthScore{A,B}: same shape, differing coef.
int WealthScore(u16 idA, u16 idB, double floorV, double coef, double gain,
                const MethodEnv& e) {
    int wA = e.total_wealth(idA);
    int wB = e.total_wealth(idB);
    int dA = e.money_to_display(wA);
    int dB = e.money_to_display(wB);
    double v9 = static_cast<double>(dA + dB);
    double v8 = (floorV <= v9) ? v9 : 10000.0;
    double v6 = v8 * coef / (Log10(v8) + 1.0) * gain;
    return static_cast<int>(TruncToZero(v6));
}
} // namespace

// gilde.exe 0x467ed4 — VIBE_AiMethod_ComputeWealthScoreA
int ComputeWealthScoreA(u16 idA, u16 idB, const MethodEnv& env) {
    return WealthScore(idA, idB, dbl_61A270, dbl_61A278, dbl_61A280, Bind(env));
}

// gilde.exe 0x467f88 — VIBE_AiMethod_ComputeWealthScoreB
int ComputeWealthScoreB(u16 idA, u16 idB, const MethodEnv& env) {
    return WealthScore(idA, idB, dbl_61A288, dbl_61A290, dbl_61A298, Bind(env));
}

// gilde.exe 0x46803c — VIBE_AiMethod_ComputeClassWeight
//   v6 = (wealth(a)+wealth(b)) * 0.0005; ConvertX; v10 = trunc(v6);
//   scan dword_466445 (stride 16) for class==classByte; weight = dword_46644C[entry];
//   if no match: weight = dword_46644C[0] (== first entry's weight, 10).
//   return weight * v10.
int ComputeClassWeight(u16 idA, u16 idB, u8 classByte, const MethodEnv& env) {
    MethodEnv e = Bind(env);
    int wA = e.total_wealth(idA);
    int wB = e.total_wealth(idB);
    double v6 = static_cast<double>(wA + wB) * flt_61A2A0;
    int v10 = static_cast<int>(TruncToZero(v6));

    int weight = 0;
    if (!LookupDrinkParams(classByte, &weight, nullptr, nullptr)) {
        // fall through to dword_46644C[0] == first entry's weight.
        LookupDrinkParams(0, &weight, nullptr, nullptr);
        // (the original indexes dword_46644C[0] directly; class 0's weight is 10)
        weight = LoadI32(kDrinkTable + 7); // dword_46644C[0]
    }
    return weight * v10;
}

// gilde.exe 0x4685fc — VIBE_AiMethod_ShouldFollowTarget
//   if (ctx.f1 == target[131]) return ctx.f1 ^ target[131] (== 0);
//   v6 = rank(self); rankDelta = (rank(self) - rank(other)) * 2.5;   [see note]
//   v10 = fav(self, target, 1) + rankDelta;
//   return RandomFloatScaled()*75.0 <= v10;
// The original computes (rank(target_word) - rank(self_word)); we pass the two id
// words and let the env resolve ranks. fav is fav(self, target).
int ShouldFollowTarget(bool equal, u16 selfWord, u16 targetWord, const MethodEnv& env) {
    MethodEnv e = Bind(env);
    if (equal)
        return 0; // v4 ^ v3 with v3==v4 -> 0
    // The original: v6 = rank(target); v9 = (v6 - rank(self)) * 2.5.
    int rTarget = e.office_rank(targetWord);
    int rSelf = e.office_rank(selfWord);
    float v9 = static_cast<float>(rTarget - rSelf) * flt_61A2AC;
    float v10 = e.favorability(selfWord, targetWord) + v9;
    return (static_cast<double>(e.rng_float()) * flt_61A2B0 <= v10) ? 1 : 0;
}

// gilde.exe 0x468910 — VIBE_AiMethod_ComputeDistancePenalty
//   r0 = rank(a); r1 = rank(b);
//   v7 = (3*(r0 - r1) + 30) * scale * (1/3); ConvertX; return trunc(v7).
int ComputeDistancePenalty(int scale, u16 idA, u16 idB, const MethodEnv& env) {
    MethodEnv e = Bind(env);
    int v6 = e.office_rank(idA);
    int v5 = e.office_rank(idB);
    double v7 = static_cast<double>((3 * (v6 - v5) + 30) * scale) * flt_61A2B8;
    return static_cast<int>(TruncToZero(v7));
}

} // namespace guild::ai
