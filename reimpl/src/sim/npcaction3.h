#pragma once
// NpcAction3 — third batch of the big NpcAction behaviour state-machine cores for
// the Guild simulation (gilde.exe). This batch translates three of the largest
// "decompiler-artifact-heavy" step machines, each driven by the He/handler record:
//
//   VIBE_NpcAction_BurglaryStep     (0x4eb518) — the burglary crime coroutine:
//       case (state -2/-1) teardown -> 0 escort-out -> 1 approach -> 2 wait-at-door
//       -> 3 break-in (gesture/violation) -> 4 packet-gate -> 5 steal/flee.
//   VIBE_NpcAction_JailCellStep     (0x4ea1e8) — the arrest/jail coroutine:
//       state -2/-1 teardown -> 0 escort-to-cell -> 1 wait-at-door -> 2 arrest
//       resolution (sound-range escape roll, transfer to cell, broadcast).
//   VIBE_NpcAction_RecruitmentState (0x4cce04) — the journeyman-recruitment
//       coroutine: gate (flag 0x04 / +132 packet pending) -> proximity check
//       drives states 0 (negotiate) / 1 (advertising delta) / 4 (retry) /
//       5 (contract / marriage-style pairing) ; teardown on state >= -2.
//
// The `state+N` switch dispatch, the per-state time-advance deltas, every
// VIBE_Math_RandomModulo / RandomFloatScaled draw (count + order), the +132 packet
// gating, the 8-slot member-id array walk (+140..+168), and the QueueRequest*
// command emissions are translated 1:1 against the IDA disassembly (the Hex-Rays
// pseudocode for these three has uninitialised-local artifacts that were resolved
// via disasm — see the .cpp provenance notes). The cross-cluster leaves (Person/
// Building/Object queries, text rendering, building-stock physics, coord convert,
// the law/violation evaluator) are routed through NpcAction3Hooks, exactly mirroring
// the NpcLeafHooks / NpcAction2Hooks pattern, so the machines are exercisable in
// isolation against a synthetic scene.
#include "guild/common/types.h"
#include "sim/he.h"

namespace guild::sim {

// ===========================================================================
// Recovered IEEE-754 float constants (resolved byte-for-byte via get_bytes).
// These feed the leaf-physics branches (escape roll / stock valuation) which the
// originals compute inline; they are exposed for the host's hook backends and for
// golden-value assertions in tests.
// ===========================================================================
//   Burglary (0x61FB80 block):
extern const double kBurglaryStockMul;   // dbl_61FB80 = 0.01   (* +55 byte)
extern const float  kBurglaryStockFloor; // flt_61FB88 = 0.001
extern const float  kBurglaryGuardMul;   // flt_61FB8C = 0.01
extern const double kBurglaryGuardMul2;  // dbl_61FB90 = 0.35
extern const double kBurglaryLootRand;   // dbl_61FB98 = 0.001  (* rand(50))
extern const float  kBurglaryLootMul;    // flt_61FBA0 = 0.0025
extern const double kBurglaryItemMul;    // dbl_61FBA8 = 0.1
//   JailCell (0x61FA8C block):
extern const float  kJailCrowdMul;       // flt_61FA8C = 0.1
extern const float  kJailStationMul;     // flt_61FA90 = 0.01
extern const double kJailEscapeBias;     // dbl_61FA98 = 0.3
extern const double kJailFineMul;        // dbl_61FAA0 = 0.5
extern const double kJailFineRandMul;    // dbl_61FAA8 (loot scatter)
//   Recruitment:
extern const float  kRecruitMoodCeil;    // flt_61E9A8 = 1.1

// ===========================================================================
// Additional He fields these machines reach (recovered from the disasm; offsets
// are byte offsets off the record base, byte-faithful to the raw accesses).
//   +0x8C (+140) : 8-slot member/escort person-id array (dword each, -1 = empty).
//                  Burglary/JailCell/Patrol all walk it as `*(dword*)(base+140+4*i)`
//                  for i in 0..7 (loop bound base+0x20 == +32 bytes == 8 dwords).
//   +0xAC (+172) : query-filter id A (victim / arrestee person id).
//   +0xB0 (+176) : query-filter id B (burglar-owner / cell-building id).
//   +0xB4 (+180) : Burglary: pending violation packet handle (+45 dword).
//                  Recruitment reuses +180/+181/+182 as cost/progress bytes.
//   +0xB8 (+184) : Burglary: target-handler filter id (+46 dword).
//                  Recruitment +184/+185 = progress / required-visits bytes.
//   +0xBC (+188) : Burglary: target person id (+47 dword).
//   +0xBA (+186) : Recruitment "moved" flag byte.  +0xBB (+187) "paired" flag.
// ---------------------------------------------------------------------------
inline i32& He_MemberId8(HeRecord* h, int slot)   // +140 + 4*slot (slots 0..7)
    { return *reinterpret_cast<i32*>(HeBytes(h) + 140 + 4 * slot); }
inline i32& He_FilterA(HeRecord* h)   { return *reinterpret_cast<i32*>(HeBytes(h) + 172); }
inline i32& He_FilterB(HeRecord* h)   { return *reinterpret_cast<i32*>(HeBytes(h) + 176); }
inline i32& He_ViolationPk(HeRecord* h){ return *reinterpret_cast<i32*>(HeBytes(h) + 180); }
inline i32& He_TargetFilter(HeRecord* h){ return *reinterpret_cast<i32*>(HeBytes(h) + 184); }
inline i32& He_TargetPersonId(HeRecord* h){ return *reinterpret_cast<i32*>(HeBytes(h) + 188); }
inline u8&  He_RecruitProgress(HeRecord* h){ return *reinterpret_cast<u8*>(HeBytes(h) + 184); }
inline u8&  He_RecruitRequired(HeRecord* h){ return *reinterpret_cast<u8*>(HeBytes(h) + 185); }
inline u8&  He_RecruitMoved(HeRecord* h){ return *reinterpret_cast<u8*>(HeBytes(h) + 186); }
inline u8&  He_RecruitPaired(HeRecord* h){ return *reinterpret_cast<u8*>(HeBytes(h) + 187); }

// Number of escort/member slots (loop walks +140..+167, 8 dwords).
constexpr int kHeMembers8 = 8;

// ===========================================================================
// Leaf hooks. Tests install a recording/synthetic mock; nullptr installs an inert
// default (every effect a no-op, queries return absent). These mirror the
// originals' cross-cluster leaf calls (Person/Building/Object queries, command
// builders, text/message broadcast, the law violation evaluator, and the two
// opaque float-physics sub-blocks — the jail escape roll and the burglary loot
// valuation — which the decompiler renders with undefined locals).
// ===========================================================================
struct NpcAction3Hooks {
    // --- command builders / packet gating (shared) ---
    // VIBE_Command_QueueRequestEntity29(arg, h) -> packet handle (stored at +132).
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
    // VIBE_Command_RequestChrMoveToUniverse(personId, objId, name, slot).
    void (*requestChrMove)(i32 personId, i32 objId, const char* name, i32 slot);
    // VIBE_Command_QueueRequestFlag55(personId, flag) [Patrol/JailCell gesture].
    void (*queueFlag55)(i32 personId, u8 flag);
    // VIBE_Command_QueueRequestPair36(seqId, value) [Burglary state 4].
    void (*queuePair36)(i32 seqId, i32 value);
    // VIBE_Command_QueueRequestQuad43(objId, a, b, storableId) [Burglary flee].
    void (*queueQuad43)(i32 objId, i32 a, i32 b, i32 storableId);
    // VIBE_Command_QueueRequest16(fromId, toId, amount, market) [money transfer].
    void (*queueRequest16)(i32 fromId, i32 toId, i32 amount, u8 market);
    // VIBE_Command_QueueRequestState22 / BeginDelta / AppendRaw — entity field set.
    //   beginDelta(objId, objId2) ; appendRaw(width, count, value, offset) ; commit().
    void (*setEntityField)(i32 objId, int width, i32 value, int offset);
    // VIBE_Command_QueueRequestSlotReset28 (jail transfer record).
    void (*queueSlotReset28)(i32 arresteeId, i32 cellBuildingId);
    // VIBE_Command_RequestBuildOp91(personId, kind, c, d) [jail flag op].
    void (*requestBuildOp91)(i32 personId, int kind);
    // VIBE_Command_EnqueueCmd15 / QueueRequestArgs25 — recruitment/advertising ops.
    void (*enqueueArgs25)(i32 personId, int a, int b, int c, int d);
    void (*enqueueBuildingActionStart)(const char* name);
    void (*enqueueBuildingActionEnd)();

    // --- entity queries ---
    // VIBE_Person_QueryBegin(a, b, c, filterId) -> opaque person handle (or 0).
    void* (*personQueryBegin)(i32 filterId);
    // VIBE_Person_FindRecordById(id) -> opaque person handle (null if absent).
    void* (*findPersonById)(i32 id);
    // VIBE_Building_FindById(id) -> opaque building handle (null if absent).
    void* (*findBuildingById)(i32 id);
    // VIBE_Building_FindStorableObject(buildingHandle) -> opaque storable (or null).
    void* (*findStorableObject)(void* building);
    // VIBE_Object_IsNearDoor(personHandle, buildingHandle) -> reached the door.
    bool  (*personNearDoor)(void* person, void* building);
    // The person/storable handle field reads the machines need:
    //   personId(h): *(dword*)(h+4) the person/entity id.
    //   personKind(h): *(byte*)(h+2) the kind/class byte (5/6/7 gate messages).
    //   personMarkerWord(h): *(word*)(h+0) (passed to ChangePlayerAction).
    //   personHasCharacter(h): *(dword*)(h+388) live-character ptr nonzero.
    //   objectIdField(h): *(dword*)(h+1) object id (storable / building).
    i32  (*personId)(void* person);
    u8   (*personKind)(void* person);
    u16  (*personMarkerWord)(void* person);
    bool (*personHasCharacter)(void* person);
    i32  (*objectIdField)(void* obj);
    // VIBE_Character_ChangePlayerAction(buildingHandle, 0, recordOrBuilding, kindWord).
    void (*changePlayerAction)(void* building, HeRecord* h, u16 kindWord);

    // --- recruitment leaves ---
    // VIBE_Recruit_CheckRecruitProximity(idA, idB): -1024/-1025/-1026 (abort),
    //   -1027 (refused), 0 (out of range), 1 (in-range/progress), other (waiting).
    i32  (*recruitProximity)(i32 idA, i32 idB);
    // VIBE_Recruit_ComputeRecruitmentCost(idA@eax, idB@edx) -> required-visit
    // count byte (disasm 0x4cd46d/0x4cd473: eax=+172 FilterA, edx=+176 FilterB).
    u8   (*recruitCost)(i32 idA, i32 idB);
    // VIBE_NpcAction_ComputeWanderPathCoords(h, recordA) — arm the wander walk.
    void (*computeWanderPath)(HeRecord* h, void* recordA);

    // --- text / message broadcast (UI/Text cluster; no observable sim state) ---
    // SendMessage(targetId, textId, ...): a coalesced stand-in for the
    //   VIBE_Text_RenderFormattedMessage + VIBE_He_SendEntityMessage/Quickjump pairs.
    void (*sendMessage)(i32 targetId, int textId);

    // --- law / violation ---
    // VIBE_Gesetz_EvaluateViolation(crime, a, victimId, cityId, perpId) -> handle.
    i32  (*evaluateViolation)(int crime, i32 victimId, i32 cityId, i32 perpId);

    // --- opaque float-physics sub-blocks (resolved control flow around them) ---
    // jailEscapeRoll(cellBuilding, arrestee): the sound-range escape probability
    //   roll. Returns true if the prisoner escapes (the original compares
    //   v68 + 0.3 > RandomFloatScaled()). Consumes one RandomFloatScaled internally
    //   in the real game; the mock decides the outcome deterministically.
    bool (*jailEscapeRoll)(void* cellBuilding, void* arrestee);
    // burglaryLootValuation(victimBuilding, burglarBuilding, memberCount): the
    //   stock-valuation + per-item theft scatter. Returns the total loot value
    //   transferred (the original sums QueueRequest16/17 amounts). Consumes the
    //   loot RandomModulo draws internally in the real game.
    i32  (*burglaryLootValuation)(void* victim, void* burglar, int memberCount);
    // burglaryDetectionRoll(victimBuilding, guardStations): the discovery roll
    //   (RandomFloatScaled() < threshold). Returns true if the heist is detected.
    bool (*burglaryDetectionRoll)(void* victim);
};

void SetNpcAction3Hooks(const NpcAction3Hooks* hooks);
const NpcAction3Hooks& GetNpcAction3Hooks();

// ===========================================================================
// gilde.exe 0x4eb518 — VIBE_NpcAction_BurglaryStep(h@eax, queryCtx@esi).
//   Switch on (state + 2), 8 cases (state -2..5). See header note for phases.
//   Returns the record (the dispatcher ignores the result except the free path).
HeRecord* NpcAction3_BurglaryStep(HeRecord* h);

// gilde.exe 0x4ea1e8 — VIBE_NpcAction_JailCellStep(h@eax, queryCtx@esi).
//   Switch on (state + 2), 5 cases (state -2..2). See header note for phases.
HeRecord* NpcAction3_JailCellStep(HeRecord* h);

// gilde.exe 0x4cce04 — VIBE_NpcAction_RecruitmentState(h@eax, edi, queryCtx@esi).
//   Gated coroutine (flag 0x04 / +132 packet pending) over states 0/1/4/5 with a
//   teardown on state >= -2. Returns the record / orig result code.
HeRecord* NpcAction3_RecruitmentState(HeRecord* h);

// Registration entry point for this batch. The dispatch table (funcs_5766CB) maps
// action-type indices to these CharAction-style coroutines via the He record; this
// records the (type -> fn) bindings without clobbering the first/second agents'
// NpcAction_TableEntry / RegisterNpcActions2 mappings. Returns the count registered.
int RegisterNpcActions3();

// Returns the registered step fn for `type` (the address-keyed dispatch entry), or
// nullptr if not one of this batch's three machines.
HeRecord* (*NpcAction3_TableEntry(int type))(HeRecord*);

} // namespace guild::sim
