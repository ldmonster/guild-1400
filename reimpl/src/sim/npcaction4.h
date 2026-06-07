#pragma once
// NpcAction4 — fourth (and final) batch of the big NpcAction/CharAction behaviour
// state-machine cores for the Guild simulation (gilde.exe). This batch translates
// the four remaining per-NPC coroutine step machines that were deferred by the
// third agent, all sharing the He/handler-record vocabulary (state @+112,
// appointment GameTime @+82, packet gate, member/escort id arrays, QueueRequest*
// re-arm). They are the crime / patrol / combat coroutines:
//
//   VIBE_CharAction_PatrolStep      (0x4cdf74) — the guard patrol coroutine:
//       entry guard (all members dead -> +1s, entity29(-1) and free); state 0
//       escort-to-target ("GOTO_TARGET"), state 1 dispatch ("GOTO_START" wage
//       payout + return / "GOTO_NEXT" next patrol point), states 2/3 wait-at-door,
//       state 1024 lap loop. Member array @+196 (4 slots), patrol-point id array
//       @+180 (4 slots), cursor @+216, lap counter @+220, +212 force-1024 gate.
//   VIBE_CharAction_RunSabotage     (0x4e2158) — the sabotage crime coroutine, a
//       9-state machine (states -2..8) with a +200 packet gate; member at +196,
//       filter @+172, object id @+16, violation seq @+204, damage @+208, retry
//       byte @+194, city index @+8.
//   VIBE_CharAction_RaidStep        (0x4cea84) — the city-watch raid coroutine,
//       a 7-state machine (states -2..6) with a +132 packet gate; 8-slot member
//       array @+140, filters @+172/+176, the cmd39 combat packet handle @+180 and
//       resolved seq @+184.
//   VIBE_NpcAction_AttackTargetStep (0x4ed95c) — the attack coroutine; switch on
//       (state + 2), states -2..6, structurally the combat twin of RaidStep
//       (shared combat-strength resolve, cmd39 packet, the same float-physics
//       attack roll). 8-slot member array @+140, filters @+172/+176, packet @+180,
//       seq @+184.
//
// The `state+N` switch dispatch, every per-state GameTime_Advance delta, every
// VIBE_Math_RandomModulo / RandomFloatScaled draw (count + order), the packet
// gating (+132 / +200 / +180), the member-array walks, and the QueueRequest*
// command emissions are translated 1:1 against the IDA disassembly (the Hex-Rays
// output for all four has uninitialised-local artifacts — v6/v7/v17/v52 in
// Patrol's entry guard and case loops, v12/v14/v21/v35/v47 in Raid/Attack — which
// were resolved against the disasm; see the .cpp provenance notes). The
// cross-cluster leaves (Person/Building/Object queries, render/anim gesture and
// particle effects, the law/violation evaluator, the combat-strength + market
// rating-curve float physics, text/message broadcast, the wage payout) are routed
// through NpcAction4Hooks, mirroring the NpcLeafHooks / NpcAction2Hooks /
// NpcAction3Hooks pattern, so the machines are exercisable in isolation.
#include "guild/common/types.h"
#include "sim/he.h"

namespace guild::sim {

// ===========================================================================
// Recovered IEEE-754 constants (resolved byte-for-byte via get_bytes).
//   Patrol wage-coord curve:
extern const float  kPatrolWageCoordMul;  // flt_61EA24 = 1/42 (0.0238095...)
//   Raid combat roll (0x61EA34 block) — byte-identical to the Attack block:
extern const double kRaidWorkstationMul;  // dbl_61EA34 = 0.01
extern const double kRaidSecurityBias;    // dbl_61EA3C = 100.0
extern const float  kRaidDefenderWeight;  // flt_61EA44 = 100.0
extern const float  kRaidAttackerWeight;  // flt_61EA48 = 125.0
//   Attack combat roll (0x61FC28 block):
extern const double kAttackWorkstationMul;// dbl_61FC28 = 0.01
extern const double kAttackSecurityBias;  // dbl_61FC30 = 100.0
extern const float  kAttackDefenderWeight;// flt_61FC38 = 100.0
extern const float  kAttackAttackerWeight;// flt_61FC3C = 125.0

// ===========================================================================
// Additional He fields these machines reach (byte offsets off the record base,
// byte-faithful to the raw `*(T*)(base+off)` accesses; recovered from disasm).
//   +0x08 (+8)   : person/city array index (word) — He_CityIndex (see he.h).
//   +0x10 (+16)  : associated object id (dword) — He_Scratch slot 0 region; the
//                  Sabotage/Raid escorts pass it as the QueueRequestNamedObject53
//                  object id. (Also used by Patrol's member-base pointer arithmetic.)
//   +0x8C (+140) : 8-slot escort/member person-id array (dword each, -1 = empty).
//                  Raid/Attack walk it as `*(dword*)(base+140+4*i)`, i in 0..7.
//   +0xAC (+172) : query-filter id A (target building / victim person id).
//   +0xB0 (+176) : query-filter id B (home/owner building id).
//   +0xB4 (+180) : Patrol: the 4-entry patrol-point id array base; Raid/Attack:
//                  the cmd39 combat-packet handle.
//   +0xB8 (+184) : Raid/Attack: the resolved cmd39 seq/cutscene-slot id (dword).
//   +0xC2 (+194) : Sabotage: pass/retry byte counter (incremented each fx tick).
//   +0xC4 (+196) : Patrol: 4-slot member id array; Sabotage: target person id.
//   +0xC8 (+200) : Sabotage: the +132-style packet gate handle (dword).
//   +0xCC (+204) : Sabotage: the violation handler seq id (dword).
//   +0xD0 (+208) : Sabotage: the damage/loss amount applied to the family record.
//   +0xD4 (+212) : Patrol: when != -1, forces state to 1024 (the lap loop).
//   +0xD8 (+216) : Patrol: patrol-point cursor (index into the +180 array).
//   +0xDC (+220) : Patrol: lap counter byte (incremented per completed lap).
// ---------------------------------------------------------------------------
inline i32& He_ObjId16(HeRecord* h)        { return *reinterpret_cast<i32*>(HeBytes(h) + 16); }
inline i32& He_Member8(HeRecord* h, int s) { return *reinterpret_cast<i32*>(HeBytes(h) + 140 + 4 * s); }
inline i32& He_FilterA4(HeRecord* h)       { return *reinterpret_cast<i32*>(HeBytes(h) + 172); }
inline i32& He_FilterB4(HeRecord* h)       { return *reinterpret_cast<i32*>(HeBytes(h) + 176); }
inline i32& He_CombatPacket(HeRecord* h)   { return *reinterpret_cast<i32*>(HeBytes(h) + 180); }
inline i32& He_CombatSeq(HeRecord* h)      { return *reinterpret_cast<i32*>(HeBytes(h) + 184); }
inline i32& He_SabPacket(HeRecord* h)      { return *reinterpret_cast<i32*>(HeBytes(h) + 200); }
inline i32& He_SabViolationSeq(HeRecord* h){ return *reinterpret_cast<i32*>(HeBytes(h) + 204); }
inline i32& He_SabDamage(HeRecord* h)      { return *reinterpret_cast<i32*>(HeBytes(h) + 208); }
inline u8&  He_SabRetry(HeRecord* h)       { return *reinterpret_cast<u8*>(HeBytes(h) + 194); }
inline i32& He_SabTarget(HeRecord* h)      { return *reinterpret_cast<i32*>(HeBytes(h) + 196); }
// Patrol-specific accessors (4-slot member array @+196, 4-slot point array @+180).
inline i32& He_PatrolMember(HeRecord* h, int s){ return *reinterpret_cast<i32*>(HeBytes(h) + 196 + 4 * s); }
inline i32& He_PatrolPoint(HeRecord* h, int s) { return *reinterpret_cast<i32*>(HeBytes(h) + 180 + 4 * s); }
inline i32& He_PatrolForce1024(HeRecord* h)    { return *reinterpret_cast<i32*>(HeBytes(h) + 212); }
inline i32& He_PatrolCursor(HeRecord* h)       { return *reinterpret_cast<i32*>(HeBytes(h) + 216); }
inline u8&  He_PatrolLap(HeRecord* h)          { return *reinterpret_cast<u8*>(HeBytes(h) + 220); }

constexpr int kHeMembers4 = 4;   // Patrol member/point arrays
constexpr int kHeMembers8R = 8;  // Raid/Attack member array

// ===========================================================================
// Leaf hooks. Tests install a recording/synthetic mock; nullptr installs an inert
// default (every effect a no-op, queries return absent). These mirror the
// originals' cross-cluster leaf calls. The opaque float-physics sub-blocks (the
// patrol wage curve, the combat attack roll, the gesture-target search and the
// particle/sound/bone-chain sabotage FX, all rendered with undefined locals by the
// decompiler) are delegated to single documented hooks while the control flow that
// surrounds them is translated exactly.
// ===========================================================================
struct NpcAction4Hooks {
    // --- shared command builders / packet gating ---
    // VIBE_Command_QueueRequestEntity29(arg, h) -> packet handle (stored at +132/+200/+180).
    i32  (*queueRequestEntity29)(int arg, HeRecord* h);
    // VIBE_Command_GetPacketStatusById(handle): nonzero == applied/acked.
    i32  (*packetStatus)(i32 handle);
    // VIBE_Command_GetPacketSeqById(handle): linked seq record id (0 if none).
    i32  (*packetSeq)(i32 handle);
    // VIBE_He_FreeHandlerEntry(h): release the handler entry; returns orig result.
    i32  (*freeHandlerEntry)(HeRecord* h);
    // VIBE_Command_QueueRequestSingle49(personId).
    void (*queueSingle49)(i32 personId);
    // VIBE_Command_QueueRequestNamedObject53(personId, objId, a, targetId, flag, name).
    void (*queueNamedObject53)(i32 personId, i32 objId, int a, i32 targetId,
                               int flag, const char* name);
    // VIBE_Command_QueueRequestFlag55(personId, flag) [Sabotage gesture].
    void (*queueFlag55)(i32 personId, u8 flag);
    // VIBE_Command_QueueRequestPair33(personId, value) [Sabotage teardown].
    void (*queuePair33)(i32 personId, i32 value);
    // VIBE_Command_QueueRequestPair36(seq, value) [Sabotage state 7].
    void (*queuePair36)(i32 seq, i32 value);
    // VIBE_Command_QueueRequestQuad43(objId, a, b, storableId) [Attack flee].
    void (*queueQuad43)(i32 objId, i32 a, i32 b, i32 storableId);
    // VIBE_Command_QueueRequest16(fromId, toId, amount, market) [Sabotage payout].
    void (*queueRequest16)(i32 fromId, i32 toId, i32 amount, u8 market);
    // VIBE_Command_EnqueueCmd15(playerId, toId, amount, market) [Patrol wage payout].
    void (*enqueueCmd15)(i32 playerId, i32 toId, i32 amount, u8 market);
    // VIBE_Command_QueueRequest39(packet) -> handle [Raid/Attack combat cmd].
    i32  (*queueRequest39)();
    // dword_12CE914[134*index] — resolve a person/city array index to its id (the
    // Person id column data leaf; the wage/transfer/violation broadcasts key off it).
    i32  (*cityIdFromIndex)(u16 index);

    // --- entity queries ---
    // VIBE_Person_QueryBegin(a, b, c, filterId) -> opaque person handle (or null).
    void* (*personQueryBegin)(i32 filterId);
    // VIBE_Person_FindRecordById(id) -> opaque person handle (null if absent).
    void* (*findPersonById)(i32 id);
    // VIBE_Building_FindStorableObject(buildingHandle) -> opaque storable (or null).
    void* (*findStorableObject)(void* building);
    // VIBE_Person_GetFamilyRecord(personRowPtr) -> opaque family record (or null).
    void* (*getFamilyRecord)(i32 cityIndex);
    // family record +40 (dword index 10): accumulate `add` into the family ledger.
    void  (*familyLedgerAdd)(void* family, i32 add);
    // Person/building handle field reads:
    //   personId(h): *(dword*)(h+1) (object/building id) OR *(dword*)(h+4) (person id);
    //     the originals read both depending on the slot — see objectIdField / personIdField.
    //   objectIdField(h): *(dword*)(h+1) object id (storable / building / scene node).
    //   personIdField(h): *(dword*)(h+4) person/entity id.
    //   personMarkerWord(h): *(word*)(h+0) the kind/class marker word.
    //   personHasCharacter(h): *(dword*)(h+388) live-character ptr nonzero.
    //   personOwnerWord(h): *(word*)(h+39) owner/player word (0xFFFF == none).
    //   personKind(h): *(byte*)(h+2) the kind byte (6/7 gate news messages).
    i32  (*objectIdField)(void* obj);
    i32  (*personIdField)(void* person);
    u16  (*personMarkerWord)(void* person);
    bool (*personHasCharacter)(void* person);
    u16  (*personOwnerWord)(void* person);
    u8   (*personKind)(void* person);
    // VIBE_Object_IsNearDoor / live-character-active probe used by wait states:
    //   personActionActive(personHandle): the per-character +296 "action active"
    //   flag (the wait loops compare it against the dispatched action). Returns the
    //   raw +296 dword (0 == idle/arrived).
    i32  (*personActionActive)(void* person);
    // VIBE_Character_ChangePlayerAction(buildingHandle, h, kindWord).
    void (*changePlayerAction)(void* building, HeRecord* h, u16 kindWord);

    // --- law / violation ---
    // VIBE_Gesetz_EvaluateViolation(crime, a, victimId, cityId, perpId) -> handle.
    i32  (*evaluateViolation)(int crime, i32 victimId, i32 cityId, i32 perpId);
    // VIBE_He_FindFirstHandlerByFilter(filterId) -> opaque handler (or null); the
    //   match's +53 dword (recordId) is read via handlerRecordId().
    void* (*findHandlerByFilter)(i32 filterId);
    i32  (*handlerRecordId)(void* handler);

    // --- text / message broadcast (UI/Text cluster; no observable sim state) ---
    // sendMessage(targetId, textId): a coalesced stand-in for the
    //   VIBE_Text_RenderFormattedMessage + VIBE_He_Send*Message broadcast pairs.
    void (*sendMessage)(i32 targetId, int textId);

    // --- opaque float-physics / render sub-blocks (resolved control flow around) ---
    // patrolWageRoll(personHandle): the per-member wage accumulation (the bone-coord
    //   curve `(coord*flt + 1)` then a RandomModulo scatter). Returns the wage units
    //   accumulated for this member. Consumes RandomModulo internally in the game.
    i32  (*patrolWageRoll)(void* member);
    // sabotageGestureTarget(personHandle, h): the VIBE_CharAction_FindGestureTarget
    //   render/anim leaf. Returns the resolved target handler seq (>0 == found a
    //   gesture target; 0 == none -> the no-gesture branch).
    i32  (*sabotageGestureTarget)(void* person, HeRecord* h);
    // sabotageDamageRoll(perpRow, victimRow): the VIBE_Building_ComputeRatingCurveA
    //   + RandomFloatScaled discovery roll. Returns true if the sabotage succeeds
    //   (the original compares `curve + rand >= 1.0`). Consumes one RandomFloatScaled.
    bool (*sabotageDamageRoll)(void* perp, void* victim);
    // sabotagePlayFx(h): the particle/sound/bone-chain explosion FX (case 5). No
    //   observable sim state; returns the number of fx ticks emitted (the original
    //   loops a small count via RandomModulo).
    void (*sabotagePlayFx)(HeRecord* h);
    // combatAttackRoll(perp, victim, workstationCount, securityByte): the shared
    //   Raid/Attack discovery roll. Returns true if the attack is *detected*
    //   (the original compares `defense*weight >= rand(0x4B) + strength*weight`).
    //   Consumes one RandomModulo(0x4B) internally.
    bool (*combatAttackRoll)(int workstationCount, int securityByte, bool isAttack);
    // combatStrength(attacker, defender, perpRow): VIBE_Combat_ComputeAttackerStrength.
    i32  (*combatStrength)(void* attacker, void* defender);
};

void SetNpcAction4Hooks(const NpcAction4Hooks* hooks);
const NpcAction4Hooks& GetNpcAction4Hooks();

// ===========================================================================
// gilde.exe 0x4cdf74 — VIBE_CharAction_PatrolStep(h@eax, edi, queryCtx@esi).
//   Entry guard: state==-1 -> free; flag 0x04 set -> (state==-2 free, else noop);
//   scan member array @+196 (4 slots) for any live person — none -> +1s,
//   entity29(-1), free. +212 != -1 forces state 1024. Then the state switch over
//   0 / 1 / 2 / 3 / 1024. Returns the original eax (free result or packet handle).
HeRecord* NpcAction4_PatrolStep(HeRecord* h);

// gilde.exe 0x4e2158 — VIBE_CharAction_RunSabotage(h@eax, edi, queryCtx@esi).
//   Packet gate on +200, then a 9-case switch over state (-2..8). Returns the
//   original al (low byte of the result handle / free result).
HeRecord* NpcAction4_RunSabotage(HeRecord* h);

// gilde.exe 0x4cea84 — VIBE_CharAction_RaidStep(h@eax, edi, queryCtx@esi).
//   state==-2 entry (flag 0x02 -> arm state 1 + the v71 "first-pass" flag, else
//   free); packet gate on +132; then a switch over (state+1) cases 0..6. Returns
//   the original eax.
HeRecord* NpcAction4_RaidStep(HeRecord* h);

// gilde.exe 0x4ed95c — VIBE_NpcAction_AttackTargetStep(h@eax, queryCtx@esi).
//   Switch on (state + 2), cases 0..7 (states -2..5/6). The combat twin of Raid.
//   Returns the original eax.
HeRecord* NpcAction4_AttackTargetStep(HeRecord* h);

// Registration entry point for this batch. Records the (action-type -> fn) bindings
// without clobbering the first/second/third agents' tables. Returns the count.
int RegisterNpcActions4();

// Returns the registered step fn for `type`, or nullptr if not one of this batch.
HeRecord* (*NpcAction4_TableEntry(int type))(HeRecord*);

} // namespace guild::sim
