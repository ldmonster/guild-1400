#pragma once
// MeisterAi / AiMethod continuation — Wave 14 slice (gilde.exe).
//
// This module continues the AI method-planner scorers and the session-decision
// orchestrator that drives them, plus a small family of MeisterAi event-table
// aggregators. Everything here is a deterministic transform over scalar inputs
// (the score/decision arithmetic) or over fixed-stride global tables (the event
// aggregators); the cross-module *leaves* the originals reach through (person-
// record lookup, favorability, total wealth, distance, RNG, the command-builder
// emitters and the per-event tick dispatch) live in unbuilt subsystems, so they
// are injected through hooks-with-inert-defaults (MethodEnv reused from aimethod2,
// plus a Meister3Hooks struct defined in the library .cpp). Tests install their
// own leaves.
//
// Functions translated (original address — symbol):
//   0x467768  VIBE_AiMethod_EvalSocialInteraction
//   0x467df0  VIBE_AiMethod_EvalPurchaseDesire
//   0x4684ec  VIBE_AiMethod_ComputeChoiceWeights
//   0x468964  VIBE_AiMethod_RollWeatherActivity
//   0x4a481c  VIBE_AiMethod_EvaluateSessionDecision
//   0x46a488  VIBE_AiMethod_ApplyDrinkAction
//   0x46abd8  VIBE_AiMethod_ApplyEatAction
//   0x469c5c  VIBE_AiMethod_BroadcastGroupState
//   0x4c71f0  VIBE_MeisterAi_RequestCmd107
//   0x4c727c  VIBE_MeisterAi_RequestCmd122
//   0x4c7430  VIBE_MeisterAi_RequestPersonCmd34
//   0x4c6f0c  VIBE_MeisterAi_TickRegisteredEvents
//
// Recovered float/double constants (gilde.exe, decoded byte-for-byte; VAs):
//   flt_61A1FC = 0.25   flt_61A200 = 2.0   flt_61A204 = 6.0     (EvalSocialInteraction mood)
//   flt_61A208 = -2.0   flt_61A20C = 0.66  flt_61A210 = 0.33
//   flt_61A214 = 0.01   flt_61A218 = 1.1   flt_61A21C = 0.9     (EvalSocialInteraction)
//   flt_61A258 = 5.0    flt_61A25C = 0.1   flt_61A260 = 0.7
//   flt_61A264 = 66.666 flt_61A268 = 33.333                     (EvalPurchaseDesire)
//   flt_61A2A8 = 0.025  (ComputeChoiceWeights)
//   1077936128 = 3.0f   1099956224 = 18.0f  (EvalSocialInteraction early-out bit patterns)
#include "guild/common/types.h"

#include "ai/aimethod2.h"  // MethodEnv (the shared leaf-injection struct)

namespace guild::ai {

// ---------------------------------------------------------------------------
// gilde.exe 0x467768 — VIBE_AiMethod_EvalSocialInteraction(eax=ctx).
// A two-stage social check. Stage 1 reads the ctx mood gauge (ctx+4):
//   if gauge <= 3.0f  -> return 0;     (bit pattern 1077936128)
//   if gauge >= 18.0f -> return 1;     (bit pattern 1099956224)
//   mood = ComputeMoodLevel-style trunc of gauge*0.25 clamped at [2.0, 6.0].
// Stage 2 derives a spend threshold from a partner trait `partnerTrait` =
// *(float*)(partner+268)+(-2.0): if trait>0.66 -> base 90; else if trait>0.33 ->
// 70; else 190; threshold = base*mood (as float). If threshold > budget where
// budget = (int)*(ctx+36) + *(float*)(ctx+28), return 1. Otherwise compare two
// scaled favorabilities (each *0.01): d = favBA, e = favCA; if e*0.9 >= d*1.1 or
// d >= e -> RandomModulo(2); else 0.
//   gauge   = *(float*)(ctx+4)
//   trait   = *(float*)(partner+268)
//   budgetI = *(int*)(ctx+36)    budgetF = *(float*)(ctx+28)
//   favBA   = fav(A,B)   favCA   = fav(C,B)   (the two ComputePersonFavorability sites)
int EvalSocialInteraction(float gauge, float trait, int budgetI, float budgetF,
                          float favBA, float favCA, const MethodEnv& env);

// ---------------------------------------------------------------------------
// gilde.exe 0x467df0 — VIBE_AiMethod_EvalPurchaseDesire(eax=ctx, edx=person).
//   tier   = ClassifyWealthTier(person.gauge);                 // base desire 0..7
//   lawCur = law(0).d6;                                        // wealth-gate value
//   t      = (5.0 - lawCur) * 0.1 + 0.7;                       // gate scale
//   fav    = ComputePersonFavorability(personIdWord, ctxWord, 1);
//   if (t*66.666 >= fav) { if (t*33.333 >= fav) tier += 2; else ++tier; }
//   if (tier == 5 && !personByte358) tier = 4;
//   else if (tier == 7 && personByte13 < 2) tier = 6;
//   else if (tier <= 0) goto clamp;                            // (falls through)
//   if (tier >= 9) return 9;
//   clamp: if (tier <= 0) return 0; else return tier.
// `wealthGauge` -> ClassifyWealthTier input; `lawWealthCur` = law(0).d6;
// `favPersonCtx` = fav(person, ctx); `personByte358`,`personByte13` person flags.
int EvalPurchaseDesire(float wealthGauge, int lawWealthCur, float favPersonCtx,
                       bool personByte358, u8 personByte13, const MethodEnv& env);

// ---------------------------------------------------------------------------
// gilde.exe 0x4684ec — VIBE_AiMethod_ComputeChoiceWeights(edx=out, eax=persons).
// Distributes a wealth budget across three destinations weighted by distance.
//   budget = trunc((total_wealth) * 0.025);
//   for k in 0..2: d[k] = distance2d(0, dest[k]); maxd = max(d);
//                  out.base[k] = kChoiceTable[k][0];                       // out+4
//                  out.pick[k] = kChoiceTable[k][1 + RandomModulo(3)];     // out+5
//   for k in 0..2: out.weight[k] = trunc(budget * (d[k]/maxd));           // out+0
// `personWealth` is the VIBE_Person_ComputeTotalWealth(persons) result; `dist` the
// three sampled distances (env.distance2d at the three dest indices). Per slot the
// original writes a fixed "base" byte and a random pick byte resolved through a
// per-slot 4-byte option table (dword_4664B8): row k = {base, opt0, opt1, opt2}, the
// pick = row[1 + RandomModulo(3)]. row0={4,0,3,1} row1={3,1,2,0} row2={2,0,4,1}.
struct ChoiceWeights {
    int weight[3] = {0, 0, 0};
    int base[3]   = {0, 0, 0};  // dword_4664B8[k][0]   (written to out+4)
    int pick[3]   = {0, 0, 0};  // dword_4664B8[k][1 + RandomModulo(3)]  (out+5)
};
ChoiceWeights ComputeChoiceWeights(int personWealth, const float dist[3],
                                   const MethodEnv& env);

// ---------------------------------------------------------------------------
// gilde.exe 0x468964 — VIBE_AiMethod_RollWeatherActivity(ebx=outWeatherByte).
//   base = ratingCurve(3) + RandomFloatScaled();
//   weatherByte = 47; bonus = 0;
//   if (weatherFlags & 2 at type 13) { bonus = -0.25; weatherByte = 13; }
//   else if (weatherFlags & 2 at type 10) { bonus = 0.1; weatherByte = 10; }
//   if (out) *out = weatherByte;
//   return base + bonus > 1.0;
// `ratingSample` = ratingCurve(3); `rng01` = RandomFloatScaled() draw; `flags13`/
// `flags10` are the (DispatchByType & 2) results for weather types 13 and 10.
bool RollWeatherActivity(float ratingSample, double rng01, bool flags13, bool flags10,
                         u8* outWeatherByte);

// ---------------------------------------------------------------------------
// gilde.exe 0x4a481c — VIBE_AiMethod_EvaluateSessionDecision(eax=actor, edx=session).
// The session orchestrator: clears actor.result(+148)=-1, then dispatches on the
// session-type byte (session+0) into one of the scorers, storing its result back
// into actor+148 when the actor's own id (actor+16) matches the session's relevant
// participant id. Type 3 additionally emits a coord-27 command on result==1.
//   self = actor.id (actor+16)
//   types: 0 social, 1 randbool, 2 randval, 3 favorability(+cmd), 4 conflict,
//          5 purchase, 6 coin-flip.  Returns the stored result (or the type byte
//          when nothing matched).
// `inputs` carries the pre-resolved scalar leaves each branch needs; `env` provides
// the RNG / favorability leaves used by the delegated scorers. The id-match guards
// are expressed via the SessionInputs booleans (selfMatchesX).
struct SessionInputs {
    u8  sessionType   = 0;
    // per-type id-match guards (self == participant id):
    bool match0       = false;  // actor.id == session+24
    bool match1       = false;  // actor.id == session+20
    bool match2       = false;  // actor.id == session+20
    bool match3       = false;  // actor.id == session+40
    bool match4       = false;  // actor.id == any of session+28/+32/+36
    bool match5       = false;  // actor.id == session+28
    bool match6       = false;  // actor.id == session+24
    // type 0 (social) scalar leaves:
    float socGauge = 0.f, socTrait = 0.f, socBudgetF = 0.f, socFavBA = 0.f, socFavCA = 0.f;
    int   socBudgetI = 0;
    // NOTE: types 1/2/6 take NO modulus input — the binary hardcodes them
    // (RandomBoolCheck=2 @0x46797c, RandomValue=7 @0x467994, case6=2 @0x4a48e2).
    // type 3 (favorability) leaves:
    bool  favAFound = false; float favBA = 0.f, favCA = 0.f;
    // type 4 (conflict) leaves:
    bool  conflictGate = false; float conflictFavA = 0.f, conflictFavB = 0.f, conflictField4 = 0.f; bool conflictFlag68 = false;
    // type 5 (purchase) leaves:
    float purWealthGauge = 0.f; int purLawCur = 0; float purFav = 0.f; bool purByte358 = false; u8 purByte13 = 0;
};
// On type 3 with result==1 the original emits VIBE_Command_QueueRequestCoord27;
// the count of those emissions is reported via outCmdCoord27 (defaults nullptr).
int EvaluateSessionDecision(const SessionInputs& in, const MethodEnv& env,
                            int* outCmdCoord27 = nullptr);

// ---------------------------------------------------------------------------
// gilde.exe 0x46a488 / 0x46abd8 — VIBE_AiMethod_ApplyDrinkAction / ApplyEatAction.
// Emit the "consume" command (build-op-90 debit of session.cost, then a slot-reset)
// and return the action code (4 for drink, 5 for eat). The two differ only in the
// returned code and one command field (drink sets field8 = -1; eat sets it to the
// slot id). They route the two command emissions through Meister3Hooks.
// `actorBuildId` = *(actor+4); `cost` = *(session+16); `slotKind` = *(session+4)
// (the byte copied into the slot-reset payload). Returns the action code.
char ApplyDrinkAction(int actorBuildId, int cost, int slotId, u8 slotKind);
char ApplyEatAction(int actorBuildId, int cost, int slotId, u8 slotKind);

// ---------------------------------------------------------------------------
// gilde.exe 0x469c5c — VIBE_AiMethod_BroadcastGroupState. Walks the person slot
// table (word_12CE910, 536-byte stride); for every slot whose byte+2 == 3 it rolls
// a 4-bit state mask (bit0 always set; bit1 = RandomModulo(2), bit2 = RandomModulo(3)
// nonzero, bit3 = RandomModulo(3) nonzero, bit4 = RandomModulo(2)) and emits a
// delta-field command carrying the mask. We expose the pure mask roll over a
// supplied list of "is group leader" flags; the command emission is a hook.
// Returns the number of slots that broadcast (== number of leaders).
int BroadcastGroupState(const u8* slotIsLeader, int slotCount, const MethodEnv& env);

// Roll one group-state mask (the bit pattern the original assembles). Exposed for
// golden-vector testing of the RNG-driven bit assembly.
int RollGroupStateMask(const MethodEnv& env);

// ---------------------------------------------------------------------------
// gilde.exe 0x4c71f0 / 0x4c727c — VIBE_MeisterAi_RequestCmd107 / RequestCmd122.
// Idempotent command emitters: look for an already-pending handler of the given
// command type (VIBE_He_FindFirstHandlerByFilter(1,0,type)); if one exists, return
// it (no emission). Otherwise emit a slot-reset command (QueueRequestSlotReset28)
// tagged with that type for the current master (word_63CC5C indexes dword_12CE914)
// with field6 = -1 and a kind byte of 2. RequestCmd107 uses type 107, RequestCmd122
// uses 122 — they are otherwise byte-identical. `handlerExists` is whether the
// filter found a pending handler. Returns true if a command was emitted.
bool RequestCmd107(bool handlerExists, int masterBuildId);
bool RequestCmd122(bool handlerExists, int masterBuildId);

// gilde.exe 0x4c7430 — VIBE_MeisterAi_RequestPersonCmd34(esi=person). Queries the
// person (QueryBegin(person,1,5,15)); only proceeds when the query succeeds AND the
// person's slot word (+39) != 0xFFFF. Then scans pending handlers of type 34 for one
// whose field4 (handler+16) equals the person's id (person+1); if such a handler
// already exists, no command is emitted. Otherwise emits a slot-reset command tagged
// type 34, kind byte 2, flag byte 1, indexed by the person's slot word.
//   `personValid`  = QueryBegin succeeded && person[+39] != 0xFFFF
//   `handlerMatch` = a pending type-34 handler already targets this person
// Returns true if a command was emitted.
bool RequestPersonCmd34(bool personValid, bool handlerMatch, int personSlotWord);

// ---------------------------------------------------------------------------
// gilde.exe 0x4c6f0c — VIBE_MeisterAi_TickRegisteredEvents. Walks the registered-
// handler table (byte_11D6040, 332-byte stride, up to 1024 entries bounded by
// dword_632248). For each live, flagged handler whose type byte < 0x88 it updates
// the handler world position then dispatches funcs_4C6EE9[type](). We model the
// table as a list of handler descriptors and route the world-pos update + the
// per-type dispatch through Meister3Hooks. Returns the number of handlers ticked.
struct ApHandler {
    u8  active   = 0;  // byte+0 (0 == empty slot)
    u8  flags    = 0;  // byte+120 (bit 0x10 must be set to tick)
    u8  type     = 0;  // dispatch index (must be < 0x88)
};
int TickRegisteredEvents(const ApHandler* handlers, int count);

// ---------------------------------------------------------------------------
// Cross-module command/dispatch leaves for the orchestration + tick functions,
// routed through an installable hooks struct with inert defaults (defined in the
// library .cpp). Tests install their own to observe emissions / drive dispatch.
// ---------------------------------------------------------------------------
struct Meister3Hooks {
    // VIBE_Command_RequestBuildOp90 @0x495b58 — debit `amount` from build `buildId`.
    void (*request_build_op90)(int buildId, int amount) = nullptr;
    // VIBE_Command_QueueRequestSlotReset28 @0x4948c8 — emit a slot-reset packet.
    void (*queue_slot_reset28)(int slotId, u8 slotKind, int field7, int field8) = nullptr;
    // VIBE_Command_BeginDeltaPacket/AppendDeltaField/QueueRequestState22 — the
    // group-state broadcast emission, collapsed to one "emit mask" leaf.
    void (*emit_group_state)(int slotIndex, int mask) = nullptr;
    // VIBE_Command_QueueRequestCoord27 @0x494878 — the type-3 coord command.
    void (*queue_coord27)(int selfId, int otherId, int code) = nullptr;
    // VIBE_He_UpdateHandlerWorldPos @0x4c6cdc — refresh a handler's world position.
    void (*update_handler_worldpos)(int handlerIndex) = nullptr;
    // funcs_4C6EE9[type]() — the per-type tick dispatch. Default: no-op.
    void (*dispatch_handler)(int handlerIndex, u8 type) = nullptr;
};

// Install a hooks struct (returns the previous one). Passing a default-constructed
// struct restores all inert defaults.
Meister3Hooks SetMeister3Hooks(const Meister3Hooks& hooks);
Meister3Hooks GetMeister3Hooks();

} // namespace guild::ai
