#include "ai/meisterai3.h"

#include <cmath>

#include "ai/method.h"            // ai::RandomModulo (shared LCG modulo)
#include "util/math_rng_float.h"  // util::RandomFloatScaled

namespace guild::ai {

// ---------------------------------------------------------------------------
// Recovered float / double constants (raw bytes -> values; see header). Decoded
// byte-for-byte from gilde.exe at the listed VAs.
// ---------------------------------------------------------------------------
namespace {
// EvalSocialInteraction.
constexpr float flt_61A1FC = 0.25f;  // social mood scale
constexpr float flt_61A200 = 2.0f;   // social mood floor
constexpr float flt_61A204 = 6.0f;   // social mood ceil gate
constexpr float flt_61A208 = -2.0f;  // trait bias
constexpr float flt_61A20C = 0.66f;  // trait high band
constexpr float flt_61A210 = 0.33f;  // trait low band
constexpr float flt_61A214 = 0.01f;  // favorability scale
constexpr float flt_61A218 = 1.1f;   // favBA weight
constexpr float flt_61A21C = 0.9f;   // favCA weight
// The social early-out compares the raw float bit pattern of (ctx+4) against
// these two integer constants: 3.0f and 18.0f.
constexpr float kSocialLow  = 3.0f;   // 1077936128
constexpr float kSocialHigh = 18.0f;  // 1099956224
// EvalPurchaseDesire.
constexpr float flt_61A258 = 5.0f;        // law-gate base
constexpr float flt_61A25C = 0.1f;        // law-gate slope
constexpr float flt_61A260 = 0.7f;        // law-gate offset
constexpr float flt_61A264 = 66.66600037f;  // upper desire threshold
constexpr float flt_61A268 = 33.33300018f;  // lower desire threshold
// ComputeChoiceWeights.
constexpr float flt_61A2A8 = 0.025f;  // wealth-budget scale

// VIBE_Coord_ConvertX (the value is already on the FPU stack; the result is
// consumed as (int)v, i.e. truncation toward zero).
inline double TruncToZero(double v) { return std::trunc(v); }

// --- MethodEnv leaf defaults (aimethod2's Bind lives in its TU's anonymous
// namespace, so we re-bind the leaves this module actually draws on). ---------
int    Def_rng_mod(u16 n) { return ai::RandomModulo(n); }
double Def_rng_float() { return util::RandomFloatScaled(); }
float  Def_favorability(u16, u16) { return 0.0f; }
int    Def_total_wealth(u16) { return 0; }
int    Def_money_to_display(int a) { return a; }
int    Def_office_rank(u16) { return 0; }
float  Def_rating_curve(int) { return 0.0f; }
float  Def_distance2d(int, int) { return 0.0f; }
float  Def_cutscene_randfloat(int, int) { return 0.5f; }
int    Def_cutscene_randint(unsigned, int) { return 0; }
int    Def_law_record_d6(int) { return 0; }
bool   Def_find_person(int, u16* a, u8* b, u8* c) {
    if (a) *a = 0;
    if (b) *b = 0;
    if (c) *c = 0;
    return false;
}

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

// --- inert default leaves for the command/dispatch hooks --------------------
void Def_request_build_op90(int, int) {}
void Def_queue_slot_reset28(int, u8, int, int) {}
void Def_emit_group_state(int, int) {}
void Def_queue_coord27(int, int, int) {}
void Def_update_handler_worldpos(int) {}
void Def_dispatch_handler(int, u8) {}

Meister3Hooks MakeDefaultHooks() {
    Meister3Hooks h;
    h.request_build_op90      = Def_request_build_op90;
    h.queue_slot_reset28      = Def_queue_slot_reset28;
    h.emit_group_state        = Def_emit_group_state;
    h.queue_coord27           = Def_queue_coord27;
    h.update_handler_worldpos = Def_update_handler_worldpos;
    h.dispatch_handler        = Def_dispatch_handler;
    return h;
}

Meister3Hooks& HookSlot() {
    static Meister3Hooks h = MakeDefaultHooks();
    return h;
}

// Bind a hooks struct so every leaf is non-null (defaults where caller left null).
Meister3Hooks BindHooks(const Meister3Hooks& in) {
    Meister3Hooks d = MakeDefaultHooks();
    Meister3Hooks e = in;
    if (!e.request_build_op90)      e.request_build_op90 = d.request_build_op90;
    if (!e.queue_slot_reset28)      e.queue_slot_reset28 = d.queue_slot_reset28;
    if (!e.emit_group_state)        e.emit_group_state = d.emit_group_state;
    if (!e.queue_coord27)           e.queue_coord27 = d.queue_coord27;
    if (!e.update_handler_worldpos) e.update_handler_worldpos = d.update_handler_worldpos;
    if (!e.dispatch_handler)        e.dispatch_handler = d.dispatch_handler;
    return e;
}
} // namespace

Meister3Hooks SetMeister3Hooks(const Meister3Hooks& hooks) {
    Meister3Hooks prev = HookSlot();
    HookSlot() = BindHooks(hooks);
    return prev;
}
Meister3Hooks GetMeister3Hooks() { return HookSlot(); }

// ---------------------------------------------------------------------------
// gilde.exe 0x467768 — VIBE_AiMethod_EvalSocialInteraction
// ---------------------------------------------------------------------------
int EvalSocialInteraction(float gauge, float trait, int budgetI, float budgetF,
                          float favBA, float favCA, const MethodEnv& env) {
    MethodEnv e = Bind(env);

    // Stage 1: gauge early-outs (the original compares raw float bit patterns).
    if (gauge <= kSocialLow)
        return 0;
    if (gauge >= kSocialHigh)
        return 1;

    // Mood level (same triple as ComputeMoodLevel) -> v24.
    double v2 = static_cast<double>(gauge) * flt_61A1FC;
    double mood;
    {
        double v1 = static_cast<double>(gauge) * flt_61A1FC;
        if (v1 > flt_61A200 && v1 >= static_cast<double>(flt_61A204)) {
            mood = 6.0;
        } else if (v2 <= flt_61A200) {
            mood = 2.0;
        } else {
            mood = v2;
        }
    }
    int v24 = static_cast<int>(TruncToZero(mood));

    // Stage 2: trait-banded spend threshold.
    double v11 = static_cast<double>(trait) + flt_61A208;
    int v13;
    if (v11 <= static_cast<double>(flt_61A20C)) {
        if (v11 > static_cast<double>(flt_61A210))
            v13 = 90 * v24;
        else
            v13 = 70 * v24;
    } else {
        v13 = 190 * v24;
    }
    float threshold = static_cast<float>(v13);
    double budget = static_cast<double>(budgetI) + budgetF;
    if (static_cast<double>(threshold) > budget)
        return 1;

    // Stage 3: favorability tie-break.
    float v19 = favBA * flt_61A214;
    float v23 = favCA * flt_61A214;
    if (v23 * flt_61A21C >= v19 * flt_61A218 || v19 >= static_cast<double>(v23))
        return static_cast<u16>(e.rng_mod(2));
    return 0;
}

// ---------------------------------------------------------------------------
// gilde.exe 0x467df0 — VIBE_AiMethod_EvalPurchaseDesire
// ---------------------------------------------------------------------------
int EvalPurchaseDesire(float wealthGauge, int lawWealthCur, float favPersonCtx,
                       bool personByte358, u8 personByte13, const MethodEnv& env) {
    (void)env;  // favorability is supplied pre-resolved (favPersonCtx)

    int v7 = ClassifyWealthTier(wealthGauge);  // base desire 0..7

    double t = (static_cast<double>(flt_61A258) - static_cast<double>(lawWealthCur))
                   * flt_61A25C + flt_61A260;
    float fav = favPersonCtx;

    if (t * flt_61A264 >= static_cast<double>(fav)) {
        if (t * flt_61A268 >= static_cast<double>(fav))
            v7 += 2;
        else
            ++v7;
    }

    // Original: if (v7 != 5 || byte358) { ...; } else { v7 = 4; }
    if (v7 != 5 || personByte358) {
        if (v7 == 7 && personByte13 < 2u)
            v7 = 6;
        else if (v7 <= 0)
            goto clamp;  // skip the >=9 check
    } else {
        v7 = 4;
    }
    if (v7 >= 9)
        return 9;
clamp:
    if (v7 <= 0)
        return 0;
    return v7;
}

// ---------------------------------------------------------------------------
// gilde.exe 0x4684ec — VIBE_AiMethod_ComputeChoiceWeights
// ---------------------------------------------------------------------------
ChoiceWeights ComputeChoiceWeights(int personWealth, const float dist[3],
                                   const MethodEnv& env) {
    MethodEnv e = Bind(env);

    double v3 = static_cast<double>(personWealth) * flt_61A2A8;
    int budget = static_cast<int>(TruncToZero(v3));  // v18

    ChoiceWeights out;
    float d[3];
    float maxd = 0.0f;  // v20
    for (int k = 0; k < 3; ++k) {
        d[k] = dist[k];
        if (d[k] > maxd)
            maxd = d[k];
        // each slot picks a random sub-option in [0,3)
        out.pick[k] = static_cast<u16>(e.rng_mod(3));
    }

    for (int k = 0; k < 3; ++k) {
        double frac = static_cast<double>(d[k] / maxd);  // v13
        double w = static_cast<double>(budget) * frac;   // v14
        out.weight[k] = static_cast<int>(TruncToZero(w));
    }
    return out;
}

// ---------------------------------------------------------------------------
// gilde.exe 0x468964 — VIBE_AiMethod_RollWeatherActivity
// ---------------------------------------------------------------------------
bool RollWeatherActivity(float ratingSample, double rng01, bool flags13, bool flags10,
                         u8* outWeatherByte) {
    float v5 = static_cast<float>(rng01);
    float v6 = ratingSample + v5;   // base
    u8 weatherByte = 47;
    float bonus = 0.0f;             // v7

    if (flags13) {
        bonus = -0.25f;
        weatherByte = 13;
    } else if (flags10) {
        bonus = 0.1f;
        weatherByte = 10;
    }
    if (outWeatherByte)
        *outWeatherByte = weatherByte;
    return static_cast<double>(v6 + bonus) > 1.0;
}

// ---------------------------------------------------------------------------
// gilde.exe 0x4a481c — VIBE_AiMethod_EvaluateSessionDecision
// ---------------------------------------------------------------------------
int EvaluateSessionDecision(const SessionInputs& in, const MethodEnv& env,
                            int* outCmdCoord27) {
    MethodEnv e = Bind(env);
    int cmd27 = 0;
    int result = in.sessionType;  // LOBYTE(result) = *(_BYTE*)a2

    switch (in.sessionType) {
        case 0:
            if (in.match0)
                result = EvalSocialInteraction(in.socGauge, in.socTrait, in.socBudgetI,
                                               in.socBudgetF, in.socFavBA, in.socFavCA, e);
            break;
        case 1:
            if (in.match1)
                result = (static_cast<u16>(e.rng_mod(in.randModulus)) != 0) ? 1 : 0;  // RandomBoolCheck
            break;
        case 2:
            if (in.match2)
                result = static_cast<u16>(e.rng_mod(in.randModulus));  // RandomValue
            break;
        case 3:
            if (in.match3) {
                result = CompareFavorability(in.favAFound, in.favBA, in.favCA, e);
                if (result == 1) {
                    // VIBE_Command_QueueRequestCoord27(self, other, 35)
                    HookSlot().queue_coord27(/*selfId*/ 0, /*otherId*/ 0, 35);
                    ++cmd27;
                }
            }
            break;
        case 4:
            if (in.match4)
                result = ShouldInitiateConflict(in.conflictGate, in.conflictFavA,
                                                in.conflictFavB, in.conflictField4,
                                                in.conflictFlag68, e);
            break;
        case 5:
            if (in.match5)
                result = EvalPurchaseDesire(in.purWealthGauge, in.purLawCur, in.purFav,
                                            in.purByte358, in.purByte13, e);
            break;
        case 6:
            if (in.match6)
                result = static_cast<u16>(e.rng_mod(in.randModulus));  // RandomModulo(2)
            break;
        default:
            return result;
    }

    if (outCmdCoord27)
        *outCmdCoord27 = cmd27;
    return result;
}

// ---------------------------------------------------------------------------
// gilde.exe 0x46a488 / 0x46abd8 — ApplyDrinkAction / ApplyEatAction
// ---------------------------------------------------------------------------
char ApplyDrinkAction(int actorBuildId, int cost, int slotId, u8 slotKind) {
    Meister3Hooks h = HookSlot();
    h.request_build_op90(actorBuildId, -cost);
    // drink: field7 = slot's build id (opaque here), field8 = -1.
    h.queue_slot_reset28(slotId, slotKind, /*field7*/ 96, /*field8*/ -1);
    return 4;
}

char ApplyEatAction(int actorBuildId, int cost, int slotId, u8 slotKind) {
    Meister3Hooks h = HookSlot();
    h.request_build_op90(actorBuildId, -cost);
    // eat: field8 = field7 (the slot's own value), not -1.
    h.queue_slot_reset28(slotId, slotKind, /*field7*/ 96, /*field8*/ 96);
    return 5;
}

// ---------------------------------------------------------------------------
// gilde.exe 0x469c5c — VIBE_AiMethod_BroadcastGroupState
// ---------------------------------------------------------------------------
int RollGroupStateMask(const MethodEnv& env) {
    MethodEnv e = Bind(env);
    int mask = 1;  // bit0 always set
    mask |= (static_cast<u16>(e.rng_mod(2)) ? 2 : 0);
    mask |= (static_cast<u16>(e.rng_mod(3)) ? 4 : 0);
    mask |= (static_cast<u16>(e.rng_mod(3)) ? 8 : 0);
    mask |= (static_cast<u16>(e.rng_mod(2)) ? 16 : 0);
    return mask;
}

int BroadcastGroupState(const u8* slotIsLeader, int slotCount, const MethodEnv& env) {
    Meister3Hooks h = HookSlot();
    int broadcasts = 0;
    for (int i = 0; i < slotCount; ++i) {
        if (slotIsLeader && slotIsLeader[i] == 3) {  // byte+2 == 3
            int mask = RollGroupStateMask(env);
            h.emit_group_state(i, mask);
            ++broadcasts;
        }
    }
    return broadcasts;
}

// ---------------------------------------------------------------------------
// gilde.exe 0x4c71f0 / 0x4c727c — RequestCmd107 / RequestCmd122
// ---------------------------------------------------------------------------
namespace {
bool RequestSlotResetCmd(bool handlerExists, int masterBuildId, u8 type) {
    if (handlerExists)
        return false;  // a pending handler already covers this; nothing to emit
    // The original assembles the slot-reset payload (type byte, master build id from
    // dword_12CE914[word_63CC5C], field6 = -1, kind byte = 2, the packed game-time
    // tail) and queues it. We collapse the payload to the build id + type.
    Meister3Hooks h = HookSlot();
    h.queue_slot_reset28(masterBuildId, /*slotKind*/ type, /*field7*/ masterBuildId,
                         /*field8*/ -1);
    return true;
}
} // namespace

bool RequestCmd107(bool handlerExists, int masterBuildId) {
    return RequestSlotResetCmd(handlerExists, masterBuildId, 107);
}

bool RequestCmd122(bool handlerExists, int masterBuildId) {
    return RequestSlotResetCmd(handlerExists, masterBuildId, 122);
}

// ---------------------------------------------------------------------------
// gilde.exe 0x4c7430 — VIBE_MeisterAi_RequestPersonCmd34
// ---------------------------------------------------------------------------
bool RequestPersonCmd34(bool personValid, bool handlerMatch, int personSlotWord) {
    if (!personValid)
        return false;       // QueryBegin failed or slot word == 0xFFFF
    if (handlerMatch)
        return false;       // a pending type-34 handler already targets this person
    Meister3Hooks h = HookSlot();
    // payload: type 34, kind byte 2, flag byte 1, indexed by the person's slot word.
    h.queue_slot_reset28(personSlotWord, /*slotKind*/ 34, /*field7*/ 2, /*field8*/ 1);
    return true;
}

// ---------------------------------------------------------------------------
// gilde.exe 0x4c6f0c — VIBE_MeisterAi_TickRegisteredEvents
// ---------------------------------------------------------------------------
int TickRegisteredEvents(const ApHandler* handlers, int count) {
    Meister3Hooks h = HookSlot();
    int ticked = 0;
    int limit = (count < 1024) ? count : 1024;  // the loop caps at 1024
    for (int i = 0; i < limit; ++i) {
        const ApHandler& hd = handlers[i];
        if (hd.active && (hd.flags & 0x10) != 0 && hd.type < 0x88) {
            h.update_handler_worldpos(i);
            h.dispatch_handler(i, hd.type);
            ++ticked;
        }
    }
    return ticked;
}

} // namespace guild::ai
