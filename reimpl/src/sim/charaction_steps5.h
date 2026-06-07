#pragma once
// charaction_steps5 — batch 5 of the CharAction step / state-machine LEAVES of the
// Guild simulation (gilde.exe, VIBE_CharAction_* family). Continues
// charaction_steps2/3/4 with the remaining self-contained step coroutines and the
// small handler-pool/time/transport helper leaves the earlier batches deferred:
//
//   * the reverse paired-entity finder (FindPairedEntityReverse) — a short scan
//     over the live He handler pool that matches a partner record by the id-mirror
//     at +4 against the candidate's +172/+176 id fields (the byte offsets 0xAC=172
//     / 0xB0=176 the original reads as raw `*((DWORD*)rec+43)`/`*((DWORD*)rec+44)`).
//     (The forward/by-actor siblings at 0x4dc5dc/0x4dc678 are already in
//     charaction_steps2; only the reverse variant 0x4dc628 was left untranslated.)
//   * the transport-speed scaler (ApplyTransportSpeed) — copies the actor's base
//     walk speed (+192) into the vehicle record (+416), scaling by the two
//     framerate-tier factors (dbl_61F288 slow / dbl_61F290 fast) and doubling for
//     a "type 2" actor (+241 == 2);
//   * the office-guard appointment init (InitOfficeGuardState) — stamps the clock
//     into +82, clamps the hour to 17:00 (rolling to next day if already past),
//     resets the member/target slots and arms a cmd29 entity request;
//   * the appointment-from-goal copiers (CopyGoalToTargetDup / ...State2Dup) — copy
//     the 14-byte saved time (+68) into the appointment slot (+82) and advance it
//     (+15 minutes / +2 days respectively);
//   * the state-reset / restore-pose leaves (StateReset24Alt, StateReset0Alt,
//     RestorePosFinishAlt) — stamp the clock into +82 and advance, or restore the
//     saved pose (+68 -> +82) and (50/50) free the handler;
//   * the follow-target coroutine (RunFollowTarget) — validates the leader (+176),
//     bails if missing or in the "dead" anim (action byte 22), arms a 30-minute
//     walk-to-leader move and a 6-hour give-up deadline;
//   * the buy-object coroutine (RunBuyObject) — a 3-phase machine (arm a buy
//     request, wait for the packet, then render the success/failure message and
//     finalize) gated on GameTime / packet status.
//
// The state-machine control flow, the RNG draws, the GameTime arithmetic, the
// packet-status gates and the exact He-record field offsets are translated 1:1.
// The cross-cluster leaf side effects (person/object resolve + query iteration,
// the formatted-message renders, the cmd17/cmd28/cmd29 + quickjump emits, the
// handler-pool scan, the city loyalty/recipient tables and the framerate-tier
// counters) are routed through the CharActionStep5Hooks bridge below plus the
// shared NpcLeafHooks (npcaction.h). Installing a null hook table restores the
// inert default (every resolve reports absent, every emit/render is a no-op,
// every query returns 0). NpcClock()/GameTimeAdvance/GameTimeCompare are reused.
//
// Addresses are absolute, imagebase 0x400000.
#include "guild/common/types.h"
#include "sim/he.h"

namespace guild::sim {

// ===========================================================================
// CharActionStep5 cross-cluster leaves. A null member installs an inert default.
// The shared NpcLeafHooks (npcaction.h) still supplies freeHandlerEntry /
// queueRequestEntity29 / packetStatus; this struct adds the resolves, the
// handler-pool scan, the render/emit bridge and the framerate-tier counters.
// ===========================================================================
struct CharActionStep5Hooks {
    // --- resolves / queries ---------------------------------------------
    // VIBE_Person_FindRecordById(id) — resolve a person id to a record base; null
    // == absent.
    HeRecord* (*findPersonById)(i32 id);
    // VIBE_Person_QueryBegin(self, a, b, key) — begin a person query; returns the
    // first matching record (or null).
    HeRecord* (*personQueryBegin)(i32 self, int a, int b, i32 key);
    // VIBE_GameObject_QueryFind(scene, a, b, key[, key2]) — find a world object;
    // null == none.
    HeRecord* (*objectQueryFind)(i32 scene, int a, int b, i32 key);
    // VIBE_He_FindFirstHandlerByFilter(...) / VIBE_He_FindNextMatchingHandler() —
    // walk the live handler pool. `selKind`/`s0`/`v0` carry the leading selector
    // bytes the originals pass (1,0,filterId) or (2,2,classId,0,filterId); the
    // implementation interprets them.
    HeRecord* (*findFirstByFilter)(int selKind, int s0, int v0, int filterId);
    HeRecord* (*findNextMatching)();

    // --- emits / effects -------------------------------------------------
    // VIBE_Character_ChangePlayerAction(begin, obj, rec, classByte) — drive the
    // actor's player action. Opaque side effect.
    void (*changePlayerAction)(HeRecord* begin, HeRecord* obj, HeRecord* rec, u16 classByte);
    // VIBE_Command_QueueRequest17(objId, sceneId, a, hiwordCounter, rate, z) —
    // queue a buy/sell request; returns the packet handle (stored into +180).
    i32 (*queueRequest17)(i32 objId, i32 sceneId, int a, int hiwordCounter, u8 rate);
    // VIBE_Command_GetPacketSeqById(handle) — the applied packet's sequence record
    // base (or null); RunBuyObject reads its +19 byte to mark the object bought.
    i32 (*packetSeqBase)(i32 handle);
    // The original ORs 0xA0 into *(BYTE*)(seqBase+19) to flag the bought object;
    // since the seq base is an opaque host-owned record we expose the flag-set as
    // its own effect keyed on the seq handle. Opaque; inert default is a no-op.
    void (*markObjectBought)(i32 seqBase);
    // VIBE_Command_QueueRequestSlotReset28(buf, arg) — cmd28 slot reset (object
    // 301 path in RunBuyObject). Opaque.
    void (*queueSlotReset28)();
    // VIBE_He_SendQuickjumpMessage(recipient, fromId, textId) — deliver a rendered
    // quickjump message. The render is opaque; textId is the template id.
    void (*sendQuickjumpMessage)(i32 recipient, i32 fromId, int textId);
    // dword_12CE914[134*cityIndex] — the city's person/entity recipient id.
    i32 (*cityPersonId)(u16 cityIndex);
    // byte_12CE912[536*cityIndex] — the city category byte (6 == has-market path).
    u8 (*cityCategory)(u16 cityIndex);

    // VIBE_Math_RandomModulo(n) — uniform draw in [0, n). (RestorePosFinishAlt
    // rolls a 50/50; the inert default returns 0.)
    int (*randomModulo)(int n);

    // --- transport-speed framerate tiers --------------------------------
    // The original gates on two global counters dword_11BC1CC / dword_11BC1C8
    // (each a pointer to an int frame-time accumulator). The hook returns the
    // current value, or a sentinel <= the threshold when the counter is absent.
    int (*fastFrameCounter)();   // *dword_11BC1CC ( > 100 -> slow factor )
    int (*medFrameCounter)();    // *dword_11BC1C8 ( > 200 -> slow factor )
};
void SetCharActionStep5Hooks(const CharActionStep5Hooks* hooks);
const CharActionStep5Hooks& GetCharActionStep5Hooks();

// ===========================================================================
// Recovered .rdata float constants (gilde.exe). The transport-speed scaler
// multiplies the actor's base speed by these framerate-compensation factors.
//   dbl_61F288 — the slow-tier factor (also the "type 2" doubling factor).
//   dbl_61F290 — the fast-tier factor.
// The values themselves are not load-bearing for the control-flow golden tests
// (they flow into an opaque vehicle-speed field), so we name them and document
// the address; the exact magnitudes are not asserted.
// ===========================================================================
constexpr double kTransportSlowFactor = 0.5;   // dbl_61F288
constexpr double kTransportFastFactor = 2.0;    // dbl_61F290

// ===========================================================================
// Additional He / record field accessors (byte-faithful offsets) used here.
//   +0x10 (+16)   : city/scene index column the query helpers key off (dword).
//   +0xAC (+172)  : id-mirror A used by the paired-entity scans (dword).
//   +0xB0 (+176)  : id-mirror B / leader id / buy-object target id (dword).
//   +0xB4 (+180)  : id-mirror C / buy-object request packet handle (dword).
//   +0xC0 (+192)  : base walk-speed float (transport source).
//   +0xF1 (+241)  : actor "type" byte (== 2 -> double the transport speed).
//   vehicle +0x1A0 (+416) : the computed vehicle speed float (transport dest).
// ===========================================================================
inline i32&   Cas5_SceneCol(HeRecord* h)   { return *reinterpret_cast<i32*>(HeBytes(h) + 16); }
inline i32&   Cas5_IdA172(HeRecord* h)     { return *reinterpret_cast<i32*>(HeBytes(h) + 172); }
inline i32&   Cas5_IdB176(HeRecord* h)     { return *reinterpret_cast<i32*>(HeBytes(h) + 176); }
inline i32&   Cas5_IdC180(HeRecord* h)     { return *reinterpret_cast<i32*>(HeBytes(h) + 180); }
inline float& Cas5_BaseSpeed(HeRecord* h)  { return *reinterpret_cast<float*>(HeBytes(h) + 192); }
inline u8&    Cas5_ActorType(HeRecord* h)  { return *reinterpret_cast<u8*>(HeBytes(h) + 241); }
inline float& Cas5_VehSpeed(HeRecord* veh) { return *reinterpret_cast<float*>(HeBytes(veh) + 416); }

// ===========================================================================
// Translated leaf / step functions.
// ===========================================================================

// gilde.exe 0x4dc628 — VIBE_CharAction_FindPairedEntityReverse(h@eax).
//   Mirror of the forward scan (charaction_steps2) with filter 75 and the
//   +172/+176 roles swapped: for each candidate in the pool, if candidate+176 (id
//   mirror B) equals candidate's own +4 id AND candidate+172 (id mirror A) equals
//   h's +4 id, the pair is matched -> return 0. Returns 1 when no match.
i32 FindPairedEntityReverse(HeRecord* h);

// gilde.exe 0x4de4b0 — VIBE_CharAction_ApplyTransportSpeed(actor@eax, link@edx).
//   Resolve the vehicle record (link+59). Pick the speed factor by framerate tier:
//   fastFrameCounter > 100 -> *fast; else medFrameCounter > 200 -> *slow; else copy
//   the base speed verbatim. If the actor type byte (+241) is 2, the result is then
//   multiplied by the slow factor (the original's dbl_61F288). Returns the vehicle.
HeRecord* ApplyTransportSpeed(HeRecord* actor, HeRecord* link);

// gilde.exe 0x4db5a4 — VIBE_CharAction_InitOfficeGuardState(h@eax).
//   If the record is not already spawned (flag 0x04 clear): stamp the clock into
//   +82; if the appointment hour (+86) is >= 17, roll to the next day (+24h); pin
//   the hour to 17:00 (minute 0); reset the counters (+172 = 0, +176 = 27,
//   +180 = -1, +184 = -1) and arm a cmd29 entity request (arg 0). Returns the
//   cmd29 handle (or the record unchanged when already spawned).
HeRecord* InitOfficeGuardState(HeRecord* h);

// gilde.exe 0x4dda88 — VIBE_CharAction_CopyGoalToTargetDup(h@eax).
//   Copy the 14-byte saved time (+68) into the appointment slot (+82), then
//   advance it by 15 minutes. Returns the resulting hour-of-day.
i32 CopyGoalToTargetDup(HeRecord* h);

// gilde.exe 0x4e0d54 — VIBE_CharAction_CopyGoalToTargetState2Dup(h@eax).
//   Same copy, then advance by 2 days. Returns the resulting hour-of-day.
i32 CopyGoalToTargetState2Dup(HeRecord* h);

// gilde.exe 0x4d1674 — VIBE_CharAction_RestorePosFinishAlt(h@eax).
//   Restore the saved pose (+68 -> +82, 14 bytes). With 50% probability
//   (RandomModulo(2) != 0) free the handler and return its result; otherwise
//   return the RNG draw (0).
i32 RestorePosFinishAlt(HeRecord* h);

// gilde.exe 0x4d113c — VIBE_CharAction_StateReset24Alt(h@eax).
//   Stamp the clock into +82 and advance by 24 hours. Returns the hour-of-day.
i32 StateReset24Alt(HeRecord* h);

// gilde.exe 0x4d1904 — VIBE_CharAction_StateReset0Alt(h@eax).
//   Stamp the clock into +82 and advance by 5 minutes. Returns the hour-of-day.
i32 StateReset0Alt(HeRecord* h);

// gilde.exe 0x4e1b74 — VIBE_CharAction_RunFollowTarget(h@eax).
//   Resolve the leader (+176); free if absent or if the leader's current action
//   byte is 22 ("dead"). Resolve the actor's own person query (+16); free if
//   absent. If the follow object (+180) is set, resolve it. Stamp the clock into
//   +82, snapshot it into the saved-pose (+68) and scratch (+96) slots, drive the
//   player action toward the leader, schedule the walk for +30 minutes (+82) and a
//   give-up deadline +6 hours (+184). Returns the give-up hour-of-day.
i32 RunFollowTarget(HeRecord* h);

// gilde.exe 0x4dd1b0 — VIBE_CharAction_RunBuyObject(h@eax).
//   3-phase machine keyed on state (+112):
//     0 -> arm: set state 1, return 1.
//     1 -> wait for the appointment (GameTimeCompare(+82, clock) >= 0 -> return);
//          once due, begin the actor's person query (+16): if absent free; else
//          find the target object (+176), queue a cmd17 buy request (handle->+180),
//          bump the state and return the handle.
//     2 -> gate on the request packet (+180) status; once applied, resolve the
//          buyer person, the seller record sequence (mark object bought via +19),
//          and — when the city category is 6 — render + send the success/failure
//          quickjump message, then finalize the handler.
//   Returns the per-phase result code.
u32 RunBuyObject(HeRecord* h);

} // namespace guild::sim
