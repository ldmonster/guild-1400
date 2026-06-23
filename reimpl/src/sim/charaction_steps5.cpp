// charaction_steps5 — batch 5 of the self-contained CharAction step leaves (the
// paired-entity finders, the transport-speed scaler, the office-guard init, the
// appointment-copy / state-reset leaves, and the follow-target + buy-object step
// machines). See charaction_steps5.h for the module overview and the recovered
// He-record field map. Each function carries its gilde.exe address; struct field
// accesses use the Cas5_*/He_* accessors (byte-faithful offsets).
#include "sim/charaction_steps5.h"

#include "sim/gametime.h"   // GameTimeAdvance, GameTimeCompare
#include "sim/npcaction.h"  // NpcClock(), GetNpcLeafHooks()

#include <cstring>          // std::memcpy

namespace guild::sim {

// Byte-exact, alignment-safe loads of values at arbitrary byte offsets. The x86
// binary uses unaligned `*(int*)(rec+N)` / `*(WORD*)(rec+N)` reads off He/person
// records whose fields are not naturally aligned (e.g. +39, +93); binding an i32&/
// u16& reference to those addresses is UB in portable C++ (UBSAN). These read the
// identical little-endian bytes without forming a misaligned reference.
namespace {
inline i32 LoadI32At(const HeRecord* h, int off) {
    i32 v; std::memcpy(&v, reinterpret_cast<const u8*>(h) + off, sizeof(v)); return v;
}
inline u16 LoadU16At(const HeRecord* h, int off) {
    u16 v; std::memcpy(&v, reinterpret_cast<const u8*>(h) + off, sizeof(v)); return v;
}
} // namespace

// ---------------------------------------------------------------------------
// Hook table plumbing (inert default — every leaf reports "absent"/no-op).
// ---------------------------------------------------------------------------
namespace {

HeRecord* InertFindPerson(i32)                              { return nullptr; }
HeRecord* InertPersonQueryBegin(i32, int, int, i32)        { return nullptr; }
HeRecord* InertObjectQueryFind(i32, int, int, i32)         { return nullptr; }
HeRecord* InertFindFirst(int, int, int, int)               { return nullptr; }
HeRecord* InertFindNext()                                  { return nullptr; }
void      InertChangePlayerAction(HeRecord*, HeRecord*, HeRecord*, u16) {}
i32       InertQueueRequest17(i32, i32, int, int, u8)      { return 0; }
i32       InertPacketSeqBase(i32)                          { return 0; }
void      InertMarkObjectBought(i32)                       {}
void      InertQueueSlotReset28()                          {}
void      InertSendQuickjump(i32, i32, int)                {}
void      InertQueuePurchaseFinalize32(i32, i32)           {}
i32       InertCityPersonId(u16)                           { return 0; }
u8        InertCityCategory(u16)                           { return 0; }
int       InertRandomModulo(int)                           { return 0; }
int       InertFastFrameCounter()                          { return 0; }
int       InertMedFrameCounter()                           { return 0; }

const CharActionStep5Hooks kInertHooks = {
    InertFindPerson, InertPersonQueryBegin, InertObjectQueryFind, InertFindFirst,
    InertFindNext, InertChangePlayerAction, InertQueueRequest17, InertPacketSeqBase,
    InertMarkObjectBought, InertQueueSlotReset28, InertSendQuickjump,
    InertQueuePurchaseFinalize32,
    InertCityPersonId, InertCityCategory,
    InertRandomModulo, InertFastFrameCounter, InertMedFrameCounter,
};
const CharActionStep5Hooks* g_hooks = &kInertHooks;

// Copy the 14-byte global clock image into a GameTime slot (mirrors the original's
// `*(_QWORD*)(rec+82) = qword_13CE852; *(_DWORD*)(rec+90) = unk_13CE85A;
//  *(_WORD*)(rec+94) = unk_13CE85E;` three-store sequence).
inline void StampClock(GameTime& dst) { dst = NpcClock(); }

} // namespace

void SetCharActionStep5Hooks(const CharActionStep5Hooks* hooks) {
    g_hooks = hooks ? hooks : &kInertHooks;
}
const CharActionStep5Hooks& GetCharActionStep5Hooks() { return *g_hooks; }

// ===========================================================================
// Reverse paired-entity finder. (The forward + by-actor siblings live in
// charaction_steps2; only the reverse variant was left untranslated.)
// ===========================================================================

// gilde.exe 0x4dc628 — VIBE_CharAction_FindPairedEntityReverse
i32 FindPairedEntityReverse(HeRecord* h) {
    const CharActionStep5Hooks& k = GetCharActionStep5Hooks();
    HeRecord* cand = k.findFirstByFilter(1, 0, 0, 75);
    if (!cand)
        return 1;
    i32 myId = He_Id(h);
    while (true) {
        // Roles swapped relative to the forward scan: compare cand[+176] (idx 44)
        // against cand's own id, then cand[+172] (idx 43) against ours.
        if (Cas5_IdB176(cand) == He_Id(cand)) {
            i32 candA = Cas5_IdA172(cand);   // *((_DWORD*)cand + 43)
            if (myId == candA)
                return candA ^ myId;          // == 0 on match
        }
        cand = k.findNextMatching();
        if (!cand)
            return 1;
    }
}

// ===========================================================================
// Transport-speed scaler.
// ===========================================================================

// gilde.exe 0x4de4b0 — VIBE_CharAction_ApplyTransportSpeed
HeRecord* ApplyTransportSpeed(HeRecord* actor, HeRecord* link) {
    const CharActionStep5Hooks& k = GetCharActionStep5Hooks();
    // result = *(_DWORD*)(link + 59): the vehicle record the speed is written to.
    // +59 is not pointer-aligned; read the stored pointer via memcpy to avoid a
    // misaligned reference (the byte image is identical to the original's load).
    HeRecord* veh;
    std::memcpy(&veh, HeBytes(link) + 59, sizeof(veh));
    float base = Cas5_BaseSpeed(actor);   // *(float*)(actor + 192)
    bool scaled = true;
    if (k.fastFrameCounter() > 100) {
        // fast tier: *(float*)(veh + 416) = base * dbl_61F290.
        Cas5_VehSpeed(veh) = static_cast<float>(base * kTransportFastFactor);
    } else if (k.medFrameCounter() > 200) {
        // medium tier: base * dbl_61F288.
        Cas5_VehSpeed(veh) = static_cast<float>(base * kTransportSlowFactor);
    } else {
        // no scaling: copy the dword verbatim (*(DWORD*)(veh+416) = *(DWORD*)(actor+192)).
        Cas5_VehSpeed(veh) = base;
        scaled = false;
    }
    // type-2 actors get an extra slow-factor multiply (the original's shared
    // LABEL_10 after both the scaled and the verbatim paths).
    (void)scaled;
    if (Cas5_ActorType(actor) == 2)
        Cas5_VehSpeed(veh) = static_cast<float>(Cas5_VehSpeed(veh) * kTransportSlowFactor);
    return veh;
}

// ===========================================================================
// Office-guard appointment init.
// ===========================================================================

// gilde.exe 0x4db5a4 — VIBE_CharAction_InitOfficeGuardState
HeRecord* InitOfficeGuardState(HeRecord* h) {
    if ((He_Flags(h) & kHeAlreadySpawned) != 0)   // (+120 & 4) != 0 -> already done
        return h;
    StampClock(He_ApptTime(h));                    // +82 = clock
    if (He_ApptTime(h).hour >= 17)                 // *(WORD*)(rec+86) >= 0x11
        GameTimeAdvance(&He_ApptTime(h), 24, 0, 0);// +24 hours (roll to next day)
    He_ApptTime(h).hour = 17;                      // *(WORD*)(rec+86) = 17
    He_ApptTime(h).minute = 0;                     // *(DWORD*)(rec+88) = 0
    Cas5_IdA172(h) = 0;                            // *(DWORD*)(rec+172) = 0
    Cas5_IdB176(h) = 27;                           // *(DWORD*)(rec+176) = 27
    Cas5_IdC180(h) = -1;                           // *(DWORD*)(rec+180) = -1
    *reinterpret_cast<i32*>(HeBytes(h) + 184) = -1;// *(DWORD*)(rec+184) = -1
    He_ReqHandle(h) = GetNpcLeafHooks().queueRequestEntity29(0, h);  // cmd29(0) -> +132
    return h;
}

// ===========================================================================
// Appointment-from-goal copiers.
// ===========================================================================

// Shared copy of the 14-byte saved time (+68) into the appointment slot (+82).
// Mirrors the original's four explicit stores (dword/dword/dword/word).
static void CopySavedToAppt(HeRecord* h) {
    He_ApptTime(h) = He_SavedTime(h);
}

// gilde.exe 0x4dda88 — VIBE_CharAction_CopyGoalToTargetDup
i32 CopyGoalToTargetDup(HeRecord* h) {
    CopySavedToAppt(h);
    return GameTimeAdvance(&He_ApptTime(h), 0, 0, 15);   // +15 minutes
}

// gilde.exe 0x4e0d54 — VIBE_CharAction_CopyGoalToTargetState2Dup
i32 CopyGoalToTargetState2Dup(HeRecord* h) {
    CopySavedToAppt(h);
    return GameTimeAdvance(&He_ApptTime(h), 2, 0, 0);    // edx=2 -> +2 hours (wraps to days at 24)
}

// ===========================================================================
// State-reset / restore-pose leaves.
// ===========================================================================

// gilde.exe 0x4d1674 — VIBE_CharAction_RestorePosFinishAlt
i32 RestorePosFinishAlt(HeRecord* h) {
    const CharActionStep5Hooks& k = GetCharActionStep5Hooks();
    // Restore the saved pose (+68 -> +82, 14 bytes via four stores).
    He_ApptTime(h) = He_SavedTime(h);
    // RandomModulo(2): on a nonzero (low word) draw, free the handler; otherwise
    // return the draw (0). The original tests `(_WORD)result`.
    i32 roll = k.randomModulo(2);
    if (static_cast<u16>(roll) != 0)
        return GetNpcLeafHooks().freeHandlerEntry(h);
    return roll;
}

// gilde.exe 0x4d113c — VIBE_CharAction_StateReset24Alt
i32 StateReset24Alt(HeRecord* h) {
    StampClock(He_ApptTime(h));
    return GameTimeAdvance(&He_ApptTime(h), 24, 0, 0);   // +24 hours
}

// gilde.exe 0x4d1904 — VIBE_CharAction_StateReset0Alt
i32 StateReset0Alt(HeRecord* h) {
    StampClock(He_ApptTime(h));
    return GameTimeAdvance(&He_ApptTime(h), 0, 0, 5);    // +5 minutes
}

// ===========================================================================
// Follow-target coroutine.
// ===========================================================================

// gilde.exe 0x4e1b74 — VIBE_CharAction_RunFollowTarget
i32 RunFollowTarget(HeRecord* h) {
    const CharActionStep5Hooks& k = GetCharActionStep5Hooks();
    // Resolve the leader (+176); free if absent.
    HeRecord* leader = k.findPersonById(Cas5_IdB176(h));
    if (!leader)
        return GetNpcLeafHooks().freeHandlerEntry(h);
    // leader[+380] (idx 95) is the leader's current action record; if its first
    // byte is 22 ("dead"/terminal anim), free.
    HeRecord* leaderAct;  // +380 is not pointer-aligned; load via memcpy (no UB).
    std::memcpy(&leaderAct, HeBytes(leader) + 380, sizeof(leaderAct));
    if (leaderAct && *reinterpret_cast<u8*>(leaderAct) == 22)
        return GetNpcLeafHooks().freeHandlerEntry(h);
    // Begin the actor's own person query keyed off +16.
    i32 sceneKey = Cas5_SceneCol(h);
    HeRecord* self = k.personQueryBegin(sceneKey, 1, 1, sceneKey);
    if (!self)
        return GetNpcLeafHooks().freeHandlerEntry(h);
    // Optional follow object (+180): resolve it if set.
    HeRecord* obj = nullptr;
    if (Cas5_IdC180(h) != -1) {
        i32 selfScene = LoadI32At(self, 93);  // (unaligned)
        obj = k.objectQueryFind(selfScene, 2, 6, Cas5_IdC180(h));
    }
    // Stamp the clock into +82, then snapshot the stamped +82 image into the
    // saved-pose (+68) slot (the original reads +82..+94 into temps after the stamp
    // and copies them to +68..+80).
    StampClock(He_ApptTime(h));
    He_SavedTime(h) = He_ApptTime(h);
    // Drive the player action toward the leader. The classByte is read from a
    // register (ecx) that the preceding query calls leave holding the resolved
    // record's class word; we model it as self's first word (see report — the
    // exact source register is indeterminate at the binary level).
    u16 cls = *reinterpret_cast<u16*>(HeBytes(self));
    k.changePlayerAction(self, obj, h, cls);
    // Stamp the clock into the scratch (+96) slot — AFTER changePlayerAction, as
    // the original does (0x4e1c30, post-call).
    *reinterpret_cast<GameTime*>(HeBytes(h) + 96) = NpcClock();
    // Schedule the walk for +30 minutes (+82): Advance(+82, 0, 0, 30) -> ebx=30.
    GameTimeAdvance(&He_ApptTime(h), 0, 0, 30);
    // Snapshot the appointment into the give-up block (+184) and push +6 hours.
    // The original is Advance(+184, 6, 0, 0) (edx=6 adds to the hour count), NOT a
    // +360-minute advance.
    *reinterpret_cast<GameTime*>(HeBytes(h) + 184) = He_ApptTime(h);
    return GameTimeAdvance(reinterpret_cast<GameTime*>(HeBytes(h) + 184), 6, 0, 0);
}

// ===========================================================================
// Buy-object coroutine.
// ===========================================================================

// gilde.exe 0x4dd1b0 — VIBE_CharAction_RunBuyObject
u32 RunBuyObject(HeRecord* h) {
    const CharActionStep5Hooks& k = GetCharActionStep5Hooks();
    u32 state = static_cast<u32>(He_State(h));   // a1[28] == +112
    if (state == 0) {
        He_State(h) = 1;        // a1[28] = 1
        return 1;
    }
    if (state <= 1) {
        // state 1: wait for the appointment.
        GameTime clk = NpcClock();
        i32 cmp = GameTimeCompare(&He_ApptTime(h), &clk);
        if ((static_cast<u32>(cmp) & 0x80000000u) == 0)
            return static_cast<u32>(cmp);   // not due yet (cmp >= 0)
        // due: begin the actor's person query keyed off +16.
        HeRecord* self = k.personQueryBegin(Cas5_SceneCol(h), 1, 1, He_Id(h));
        if (!self)
            return static_cast<u32>(GetNpcLeafHooks().freeHandlerEntry(h));
        // find the target object (+176) and queue a cmd17 buy request.
        i32 selfScene = LoadI32At(self, 93);  // (unaligned)
        k.objectQueryFind(selfScene, 1, 1, Cas5_IdB176(h));
        // hiword counter: the original reads the DWORD at +170 and `sar 16` it
        // (0x4dd482: mov ebx,[ebp+0AAh]; 0x4dd489: sar ebx,10h) — a SIGNED >>16 of
        // the dword at offset 170 (== the sign-extended low word of +172). NOT a
        // masked HIWORD of +172.
        int hiword = LoadI32At(h, 170) >> 16;
        i32 handle = k.queueRequest17(Cas5_IdB176(h), 0, 1, hiword, 0);
        u32 prevState = static_cast<u32>(He_State(h));
        Cas5_IdC180(h) = handle;          // a1[45] = handle (+180)
        He_State(h) = static_cast<i32>(prevState + 1);   // a1[28] = state + 1
        return static_cast<u32>(handle);
    }
    if (state != 2)
        return state;
    // state 2: gate on the request packet (+180) status.
    i32 status = GetNpcLeafHooks().packetStatus(Cas5_IdC180(h));
    if (!status)
        return static_cast<u32>(status);
    i32 seqBase = k.packetSeqBase(Cas5_IdC180(h));
    HeRecord* self = k.personQueryBegin(Cas5_SceneCol(h), 1, 1, He_Id(h));
    if (!self)
        return static_cast<u32>(GetNpcLeafHooks().freeHandlerEntry(h));
    // object id == 301 path: cmd28 slot reset.
    u16 objType = *reinterpret_cast<u16*>(HeBytes(self));
    if (objType == 301)
        k.queueSlotReset28();
    // city-category gate (byte_12CE912[536*selfCity] == 6): render + quickjump.
    u16 selfCity = LoadU16At(self, 39);  // (unaligned)
    if (k.cityCategory(selfCity) == 6) {
        HeRecord* found = k.objectQueryFind(LoadI32At(self, 93),  // (unaligned)
                                            1, 1, Cas5_IdB176(h));
        i32 recipient = k.cityPersonId(selfCity);
        // The original renders one of two templates (6202 "not bought" when the
        // object is missing or is type 253, else 6200 "bought") and both go out as
        // a quickjump message id 1429. The render is opaque here.
        int textId = (!found || *reinterpret_cast<u16*>(found) == 253) ? 6202 : 6200;
        (void)textId;
        k.sendQuickjumpMessage(recipient, He_Id(h), 1429);
        // Mark the resolved packet sequence record as bought: *(BYTE*)(seq+19) |= 0xA0.
        if (seqBase)
            k.markObjectBought(seqBase);
    }
    // Purchase-finalize tail (runs regardless of the city-category branch — the
    // category block falls through to loc_4DD37A): resolve the object again
    // (+176), then emit QueueRequestFlagBlob32(4, {buyer_dword @ self+1,
    // object_dword @ obj+2}). The interleaved GameTime advance there (0x4dd3c0)
    // computes into the blob's first dword which is then OVERWRITTEN by *(self+1),
    // so the advance result is discarded; we skip it.
    HeRecord* finalObj = k.objectQueryFind(LoadI32At(self, 93), 1, 1, Cas5_IdB176(h));
    i32 buyerBlob = LoadI32At(self, 1);                          // *(DWORD*)(self+1)
    i32 objBlob   = finalObj ? LoadI32At(finalObj, 2) : 0;       // *(DWORD*)(obj+2)
    k.queuePurchaseFinalize32(buyerBlob, objBlob);
    return static_cast<u32>(GetNpcLeafHooks().freeHandlerEntry(h));
}

} // namespace guild::sim
