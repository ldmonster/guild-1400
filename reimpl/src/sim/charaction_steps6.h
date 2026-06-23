#pragma once
// charaction_steps6 — batch 6 of the CharAction step / state-machine coroutines of
// the Guild simulation (gilde.exe, VIBE_CharAction_* family). Where the earlier
// batches (charaction_steps2..5) translated the small self-contained leaves, this
// batch translates the LARGE multi-phase "crime / personnel" coroutines plus the
// handler registration table:
//
//   * RegisterHandlerTable (0x4db940) — the 66-entry He-handler registration list.
//     Each call registers an (init, step) handler pair for an action-type byte; the
//     first registration that fails short-circuits and returns 1. We recover the
//     full ordered table as a constant and replay the registration sequence through
//     the registerHandler hook (the He pool owns the real registry). The recovered
//     table is the golden vector.
//   * InitPruegel (0x4e3734) / RunPruegel (0x4e3894) — the "beat someone up" crime
//     coroutine. Init resolves the victim + a hired thug + a target object; Run is a
//     `state+2` switch machine (states -2..9) that walks the thug to the victim,
//     stages the assault, evaluates the law violation and pays out.
//   * InitSpionage (0x4e2e58) / RunSpionage (0x4e3164) — the industrial-espionage
//     coroutine. Init seeds an RNG scan cursor (+188) and a coprime stride (+192),
//     picks a spy target building and arms the entity request; Run is a 5-phase
//     machine (states 0..4) gated on the cmd29 entity packet (+132) that walks the
//     spy in, steps a 256-slot scan with a bit-mask filter, and reports the result.
//   * RunMeisterEinstellen (0x4dd5b4) / RunMeisterEntlassen (0x4ddac4) — the guild
//     master HIRE / FIRE personnel coroutines. Both are 4-phase switch machines
//     (states -2..3 / -2..2) that check the family purse, queue the guard-target
//     request (cmd61), wait for the seq packet, then move the recruit and pay the
//     wage (Einstellen) or the severance (Entlassen).
//   * RunMoveCrowdToObject (0x4e1054) — the "march the group to an object" coroutine
//     (state+2 switch, states -2..4) gated on a seasonal time-of-day window; it
//     drives every group member (the +140 4-slot member-id array) toward a target
//     and, on arrival, runs the harvest/payout economy step.
//
// The state-machine control flow, the RNG draws, the GameTime arithmetic, the
// cmd29 entity-packet gates, the `state+2` switch encodings, the 256-slot scan
// loops, the bit-mask filters and the exact He-record field offsets are translated
// 1:1. Every cross-cluster leaf side effect (person/object resolve + query
// iteration, the building/family/personnel/law queries, the formatted-message
// renders, the cmd16/49/53/61/etc. emits, the handler-pool scan) is routed through
// the CharActionStep6Hooks bridge below plus the shared NpcLeafHooks (npcaction.h).
// Installing a null hook table restores the inert default (every resolve reports
// absent, every emit/render a no-op, every query 0). NpcClock()/GameTimeAdvance/
// GameTimeCompare/GameTimeDiffMinutes are reused.
//
// Addresses are absolute, imagebase 0x400000.
#include "guild/common/types.h"
#include "sim/he.h"

#include <cstring>   // std::memcpy (unaligned He-field loads)

namespace guild::sim {

// ===========================================================================
// Extra He / record field offsets used by the batch-6 coroutines (byte-faithful,
// recovered from the decompilations). These extend the he.h map additively; the
// accessors address the record by explicit offset (mirrors `*(T*)(base+off)`).
//   +0x60 (+96)   : scratch GameTime B (a second 14-byte clock snapshot).
//   +0xA9 (+169)  : packed dword; byte 3 (>>24) = the office sub-method index
//                   the personnel coroutines key the wage table off.
//   +0xAC (+172)  : object/scan id (dword); also member-count byte (Crowd: +172 lo).
//   +0xB0 (+176)  : member-iteration counter / second person id (dword).
//   +0xB4 (+180)  : scan cursor / second target id (dword).
//   +0xBC (+188)  : spy scan position (dword) / Meister recruit packet id.
//   +0xC0 (+192)  : spy scan stride (dword, coprime to 256).
//   +0xC4 (+196)  : spy target person id (dword).
//   +0xC8 (+200)  : Pruegel victim person id / scratch GameTime C (+200..+213).
//   +0xCC (+204)  : Pruegel/Spionage packet handle (dword) / scratch.
//   +0xD0 (+208)  : Pruegel object id / spawned flag byte (+208).
//   +0xD4 (+212)  : Pruegel gesture target id (dword).
//   +0xD8 (+216)  : payout amount / "spawn flag 0x400" mirror (dword).
//   +0xE0 (+224)  : Meister move-destination object id (dword).
//   +0xE4 (+228)  : Meister request handle / wage (dword).
//   +0xE8 (+232)  : Meister resolved recruit entity id (dword).
// ===========================================================================
inline GameTime& He_ScratchTimeB(HeRecord* h) { return *reinterpret_cast<GameTime*>(HeBytes(h) + 96); }
// Office sub-method index: the original reads the unaligned DWORD at +169 and does a
// signed arithmetic shift right 24 (disasm `mov ebx,[ebp+0A9h]; sar ebx,18h`), i.e.
// the sign-extended high byte of the +169 dword (== signed byte at +172). NOT the
// low byte at +169. Returns the signed value.
inline i32 He_SubMethodByte(HeRecord* h) {
    i32 v; std::memcpy(&v, HeBytes(h) + 169, sizeof(v)); return v >> 24;  // sar 24 (signed)
}
inline i32&   He_F172(HeRecord* h)             { return *reinterpret_cast<i32*>(HeBytes(h) + 172); }
inline u8&    He_MemberCountByte(HeRecord* h)  { return *reinterpret_cast<u8*>(HeBytes(h) + 172); }
inline i32&   He_F176(HeRecord* h)             { return *reinterpret_cast<i32*>(HeBytes(h) + 176); }
inline i32&   He_F180(HeRecord* h)             { return *reinterpret_cast<i32*>(HeBytes(h) + 180); }
inline i32&   He_F184(HeRecord* h)             { return *reinterpret_cast<i32*>(HeBytes(h) + 184); }
inline i32&   He_F188(HeRecord* h)             { return *reinterpret_cast<i32*>(HeBytes(h) + 188); }
inline i32&   He_F192(HeRecord* h)             { return *reinterpret_cast<i32*>(HeBytes(h) + 192); }
inline i32&   He_F196(HeRecord* h)             { return *reinterpret_cast<i32*>(HeBytes(h) + 196); }
inline i32&   He_F200(HeRecord* h)             { return *reinterpret_cast<i32*>(HeBytes(h) + 200); }
inline GameTime& He_ScratchTimeC(HeRecord* h)  { return *reinterpret_cast<GameTime*>(HeBytes(h) + 200); }
inline i32&   He_F204(HeRecord* h)             { return *reinterpret_cast<i32*>(HeBytes(h) + 204); }
inline i32&   He_F208(HeRecord* h)             { return *reinterpret_cast<i32*>(HeBytes(h) + 208); }
inline u8&    He_F208b(HeRecord* h)            { return *reinterpret_cast<u8*>(HeBytes(h) + 208); }
inline i32&   He_F212(HeRecord* h)             { return *reinterpret_cast<i32*>(HeBytes(h) + 212); }
inline i32&   He_F216(HeRecord* h)             { return *reinterpret_cast<i32*>(HeBytes(h) + 216); }
inline i32&   He_F224(HeRecord* h)             { return *reinterpret_cast<i32*>(HeBytes(h) + 224); }
inline i32&   He_F228(HeRecord* h)             { return *reinterpret_cast<i32*>(HeBytes(h) + 228); }
inline i32&   He_F232(HeRecord* h)             { return *reinterpret_cast<i32*>(HeBytes(h) + 232); }

// ===========================================================================
// CharActionStep6 cross-cluster leaves. A null member installs an inert default.
// The shared NpcLeafHooks (npcaction.h) still supplies freeHandlerEntry /
// queueRequestEntity29 / packetStatus; this struct adds the resolves, the queries,
// the handler-pool scan and the render/emit bridge these coroutines reach.
// ===========================================================================
struct CharActionStep6Hooks {
    // --- resolves / queries ---------------------------------------------
    // VIBE_Person_QueryBegin(self, a, b, key) — begin a person query; first match.
    HeRecord* (*personQueryBegin)(i32 self, int a, int b, i32 key);
    // VIBE_Person_FindRecordById(id) — resolve a person id to a record base.
    HeRecord* (*findPersonById)(i32 id);
    // VIBE_Person_GetFamilyRecord(personRow) — the family/guild accounting record
    // (or null). The coroutines bump its money fields (+40 / +24 / +76).
    HeRecord* (*familyRecord)(u16 cityIndex);
    // VIBE_GameObject_QueryFind(scene, a, b, key[, key2]) — find a world object.
    HeRecord* (*objectQueryFind)(i32 scene, int a, int b, i32 key);
    // VIBE_GameObject_ResolveEntityById(out, a, id, b) — resolve an entity by id and
    // write the record base into *out (null on miss).
    void (*resolveEntityById)(HeRecord** out, i32 id);
    // VIBE_He_FindFirstHandlerByFilter(s0, v0, filterId) / FindNextMatchingHandler.
    HeRecord* (*findFirstByFilter)(int s0, int v0, i32 filterId);
    HeRecord* (*findNextMatching)();
    // VIBE_ObjectSearch_FindNearestEntity(self, kind, ..., outId) — nearest scene
    // entity scan; writes the found id into *outId, returns nonzero on success.
    int (*findNearestEntity)(u16 cityIndex, int kind, i32* outId);
    // VIBE_Object_IsNearDoor(rec, objId) — proximity gate; nonzero when adjacent.
    int (*isNearDoor)(HeRecord* rec, i32 objId);

    // --- personnel / economy queries -------------------------------------
    // VIBE_Personnel_ComputeWageByCategory(catRow, classByte, subMethod) — the wage
    // (a float, truncated to int by the caller). Opaque; inert default 0.
    int (*computeWage)(u16 cityIndex, int classByte, int subMethod);
    // VIBE_Person_SumCurrencyHeld(catRow) — the family's available money.
    int (*sumCurrencyHeld)(u16 cityIndex);
    // byte_12CE912[536*cityIndex] — the city/recipient category byte (6/7 = market).
    u8 (*cityCategory)(u16 cityIndex);
    // dword_12CE914[134*cityIndex] — the city's person/entity recipient id.
    i32 (*cityPersonId)(u16 cityIndex);

    // --- emits / effects -------------------------------------------------
    // VIBE_Character_ChangePlayerAction(begin, obj, rec, classByte).
    void (*changePlayerAction)(HeRecord* begin, HeRecord* obj, HeRecord* rec, u16 classByte);
    // VIBE_Command_QueueRequestGuardTarget61(person, mode, sub, hi) — cmd61; handle.
    i32 (*queueGuardTarget61)(HeRecord* person, int mode, int sub, int hi);
    // VIBE_Command_RequestBuildOp73Str(...) — the spy/assault op73 emit; handle.
    i32 (*requestBuildOp73)(int a, i32 personId, int b, int c, int d, int e);
    // VIBE_Command_QueueRequest16(toId, recipient, value, flag) — money/result emit.
    void (*queueRequest16)(i32 toId, i32 recipient, int value, u8 flag);
    // VIBE_Command_RequestChrMoveToUniverse(entityId, selfId, dest) — move a recruit.
    void (*requestChrMove)(i32 entityId, i32 selfId, i32 destObjId);
    // VIBE_Command_QueueRequestSingle49(personId) — single-target nav request.
    void (*queueSingle49)(i32 personId);
    // VIBE_Command_QueueRequestNamedObject53(personId, objId, key, mode) — named-obj
    // navigation request; returns a packet handle.
    i32 (*queueNamedObject53)(i32 personId, i32 objId, i32 key, int mode);
    // VIBE_Command_QueueRequestCoord27(fromId, toId, delta) — coordinate request.
    void (*queueCoord27)(i32 fromId, i32 toId, int delta);
    // VIBE_Command_QueueRequestPair33(handle, flag) / Pair36(a,b).
    void (*queuePair33)(i32 handle, int flag);
    void (*queuePair36)(i32 a, i32 b);
    // VIBE_Gesetz_EvaluateViolation(kind, sev, victimId, recipient, objId) — law
    // evaluation; returns a packet handle.
    i32 (*evaluateViolation)(int kind, int sev, i32 victimId, i32 recipient, i32 objId);
    // VIBE_He_SendEntityMessage(toId, textId) — deliver a rendered message.
    void (*sendEntityMessage)(i32 toId, int textId);
    // VIBE_He_SendQuickjumpMessage(recipient, fromId, textId) — quickjump message.
    void (*sendQuickjumpMessage)(i32 recipient, i32 fromId, int textId);
    // VIBE_Command_GetPacketSeqById(handle) — the applied packet's sequence record
    // base (or null); the coroutines read its +4 entity id and +0 class word.
    HeRecord* (*packetSeqBase)(i32 handle);
    // VIBE_AiMethod_RollWeatherActivity() — the Pruegel weather/activity coin flip.
    int (*rollWeatherActivity)();
    // VIBE_Inventory_IsObjectSlotActive(rec, itemId) — the Pruegel "has weapon" gate.
    int (*inventorySlotActive)(i32 itemId);
    // VIBE_CharAction_FindGestureTarget(buf) — Pruegel state-9 partner finder; the
    // returned record base (or null).
    HeRecord* (*findGestureTarget)();

    // VIBE_Math_RandomModulo(n) — uniform draw in [0, n). Inert default 0.
    int (*randomModulo)(int n);

    // VIBE_He_RegisterHandlerByType(type, init, step) — register one handler pair;
    // returns nonzero on failure (the registration loop short-circuits). Inert
    // default returns 0 (success). Used by RegisterHandlerTable.
    int (*registerHandler)(u8 type, u32 initAddr, u32 stepAddr);
};
void SetCharActionStep6Hooks(const CharActionStep6Hooks* hooks);
const CharActionStep6Hooks& GetCharActionStep6Hooks();

// ===========================================================================
// Recovered handler-registration table (VIBE_CharAction_RegisterHandlerTable
// @0x4db940). 66 entries in REGISTRATION ORDER (the order the original emits the
// calls — note it is NOT sorted by type). Each entry pairs an action-type byte
// with the init-handler and step-handler function addresses (absolute, imagebase
// 0x400000). This is the golden vector the unit test asserts byte-for-byte.
// ===========================================================================
struct HandlerReg {
    u8  type;       // action-type byte the handler pair is registered under
    u32 initAddr;   // init handler address (gilde.exe)
    u32 stepAddr;   // step handler address (gilde.exe)
};
constexpr int kCharActionHandlerCount = 66;
extern const HandlerReg kCharActionHandlerTable[kCharActionHandlerCount];

// ===========================================================================
// Translated coroutines.
// ===========================================================================

// gilde.exe 0x4db940 — VIBE_CharAction_RegisterHandlerTable.
//   Replays kCharActionHandlerTable through registerHandler in order; returns 1 at
//   the first failed registration, else returns the last call's result (0).
i32 RegisterHandlerTable();

// gilde.exe 0x4e3734 — VIBE_CharAction_InitPruegel(h@eax).
//   Stamp the clock into +82. If the actor id (+16) is unset (-1), pick a thug
//   (person query class 22, falling back to 15); store its id at +16. Reset the
//   assault packet (+200 = -1), arm the op73 "Pruegel" request (handle -> +204).
//   Resolve the victim (+180); pick its escort (+92) or a nearby class-4 person, or
//   the nearest scene entity; store that target id at +208. Returns the target id.
i32 InitPruegel(HeRecord* h);

// gilde.exe 0x4e3894 — VIBE_CharAction_RunPruegel(h@eax).
//   `state+2` switch coroutine (states -2..9). Walks the thug to the victim, stages
//   the assault, evaluates the law violation (Gesetz), pays the family and unwinds.
//   See the .cpp for the per-phase translation. Returns the per-phase result.
u32 RunPruegel(HeRecord* h);

// gilde.exe 0x4e2e58 — VIBE_CharAction_InitSpionage(h@eax).
//   If not already spawned (flag 0x400 clear): resolve the spy (+196). Seed the
//   scan cursor (+188 = RandomModulo(256)) and the coprime stride (+192 =
//   stride-table[RandomModulo(16)]). Stamp +82 (+1 minute). Scan the handler pool
//   for an existing spy on the same target (counting matches); pick the spy-target
//   building (resolve +172, else nearest scene entity), arm two clock snapshots
//   (+82 / +200) advanced 72 or 96 hours by the dispatch type, set the success flag
//   (+196-mirror) and arm the entity request. Returns the cmd29 handle.
i32 InitSpionage(HeRecord* h);

// gilde.exe 0x4e3164 — VIBE_CharAction_RunSpionage(h@eax).
//   Outer guards: bail if state >= 0xFFFFFFFE, if already spawned (flag 0x04), and
//   gate on the entity packet (+132) once armed. 5-phase switch (states 0..4):
//     0 -> pay out the spied earnings, change the action and re-arm;
//     1 -> step the 256-slot scan with a bit-mask filter to the next target, queue
//          the nav requests, arm the move;
//     2/3 -> walk-in / near-door gates that advance the clock and re-arm;
//     4 -> finalize.
//   Returns the per-phase result (the cmd29 handle or a status code).
u32 RunSpionage(HeRecord* h);

// gilde.exe 0x4dd5b4 — VIBE_CharAction_RunMeisterEinstellen(h@eax).
//   Guild-master HIRE coroutine. 4-phase switch (states -2..3):
//     0 -> arm (state -> 1);
//     1 -> wait for the appointment; once due, check the family purse against the
//          wage, scan the 12-slot member array for a free recruit slot (+176>0),
//          queue the guard-target request (+228), advance +1 second, state -> 2;
//     2 -> gate on the request packet (+228); resolve the recruit move destination,
//          issue the move, advance +1 second, state -> 3;
//     3 -> scan the member array, pay the wage (cmd16), bump the family purse (+76),
//          and finalize.
//   Returns the per-phase result.
u32 RunMeisterEinstellen(HeRecord* h);

// gilde.exe 0x4ddac4 — VIBE_CharAction_RunMeisterEntlassen(h@eax).
//   Guild-master FIRE coroutine. 4-phase switch (states -2..2) — the structural
//   twin of Einstellen with the single-slot (count 1) member scan, the severance
//   wage (sub-method = +169>>24) and the move-back / random reschedule on completion.
//   Returns the per-phase result.
u32 RunMeisterEntlassen(HeRecord* h);

// gilde.exe 0x4e1054 — VIBE_CharAction_RunMoveCrowdToObject(h@eax).
//   "March the group to an object" coroutine. Outer gate: a seasonal time-of-day
//   window (the hour-of-day must fall outside [winterStart, winterEnd) for the
//   season, or state must be terminal) — if not in window, advance +5 minutes and
//   return. Bails if the city index (+8) or city id (+12) is unset. `state+2`
//   switch (states -2..4): drive every group member (the +140 4-slot member array)
//   toward the target object, gate arrivals on isNearDoor, and on full arrival run
//   the harvest/payout economy step. Returns the per-phase result.
u32 RunMoveCrowdToObject(HeRecord* h);

} // namespace guild::sim
