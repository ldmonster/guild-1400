// charaction_npcaction_recon — see charaction_npcaction_recon.h for the module
// overview, the scope/faithfulness note and the per-routine address map. Each
// routine carries its gilde.exe address; record accesses use the He_* / HeR_*
// accessors (byte-faithful offsets). Cross-cluster leaves go through the inert
// CharActionReconHooks bridge. NpcClock() supplies the 14-byte global clock image
// (qword_13CE852); GameTimeAdvance / GameTimeCompare are reused.
#include "sim/charaction_npcaction_recon.h"

#include "sim/gametime.h"   // GameTimeAdvance, GameTimeCompare
#include "sim/npcaction.h"  // NpcClock()

#include <cstdint>          // intptr_t

namespace guild::sim {

// ---------------------------------------------------------------------------
// Hook table plumbing (inert default — every leaf reports "absent" / no-op).
// ---------------------------------------------------------------------------
namespace {

void      InResolveEntityById(HeRecord** out, i32) { if (out) *out = nullptr; }
HeRecord* InObjectQueryFind(i32, int, int, i32)    { return nullptr; }
HeRecord* InFindPersonById(i32)                    { return nullptr; }
HeRecord* InPersonQueryBegin(i32, int, int, i32)   { return nullptr; }
HeRecord* InFindFirstByFilter(int, int, i32)       { return nullptr; }
HeRecord* InFindNextMatching()                     { return nullptr; }
i32       InPacketStatus(i32)                      { return 0; }
i32       InQueueRequestEntity29(int, HeRecord*)   { return -1; }
i32       InFreeHandlerEntry(HeRecord*)            { return 0; }
void      InQueueRequestMixed45(i32, i32, i32)     {}
i32       InQueueRequestQuad60(i32, i32, int, i32) { return 0; }
void      InQueueRequestArgs25(i32, int, int, int, int) {}
void      InQueueRequestCoord27(i32, i32, int)     {}
void      InSendEntityMessage(i32, int)            {}
void      InSendQuickjumpMessage(i32, int)         {}
i32       InEvaluateViolation(int, int, i32, i32, i32) { return 0; }
void      InStampTimeAndRequest(HeRecord*)         {}
void      InBeginDeltaPacket(i32, i32)             {}
void      InAppendRawField(i32, i32, const u8*, int) {}
void      InQueueRequestState22()                  {}
void      InRequestBuildOp72(i32, int)             {}
u8        InBuildingGroupFromCode(int)             { return 0; }
void      InBuildingGuildRankPair(int, u8* a, u8* b) { if (a) *a = 0; if (b) *b = 0; }
i32       InMeisterRegisterApEvent(u16, i32, int)  { return 0; }
i32       InPacketSeqById(i32)                     { return 0; }
void      InOfficeConfirmCandidacy(i32, u8)        {}
u8        InOfficeFindHighestVacantRank(u8)        { return 0; }
i32       InOfficeRenderRequirementText(u8)        { return -1; }
int       InRandomModulo(int)                      { return 0; }

const CharActionReconHooks kInertHooks = {
    InResolveEntityById, InObjectQueryFind, InFindPersonById, InPersonQueryBegin,
    InFindFirstByFilter, InFindNextMatching, InPacketStatus,
    InQueueRequestEntity29, InFreeHandlerEntry, InQueueRequestMixed45,
    InQueueRequestQuad60, InQueueRequestArgs25, InQueueRequestCoord27,
    InSendEntityMessage, InSendQuickjumpMessage, InEvaluateViolation,
    InStampTimeAndRequest, InBeginDeltaPacket, InAppendRawField,
    InQueueRequestState22, InRequestBuildOp72, InBuildingGroupFromCode,
    InBuildingGuildRankPair, InMeisterRegisterApEvent, InPacketSeqById,
    InOfficeConfirmCandidacy, InOfficeFindHighestVacantRank,
    InOfficeRenderRequirementText, InRandomModulo,
};
const CharActionReconHooks* g_hooks = &kInertHooks;

// Stamp the 14-byte global clock image into the appointment slot (+82); mirrors
// the original's `*(_QWORD*)(rec+82)=qword_13CE852; *(_DWORD*)(rec+90)=...;
// *(_WORD*)(rec+94)=...`.
inline void StampAppt(HeRecord* h)  { He_ApptTime(h) = NpcClock(); }
// Same for the +196 second-clock image used by BeginCarryGoods.
inline void StampClock2(HeRecord* h) { HeR_Clock2(h) = NpcClock(); }

// City category byte: byte_12CE912[536*cityIndex] == record+2 of the person row.
// The originals index that parallel column; the records the resolves return carry
// the same byte at +2, so we read the category from the resolved record where the
// decompile does. The inert resolves return null, so these guards take the
// "category != 6/7" path (no message), preserving control flow.
inline u8 RecCategory(HeRecord* r) { return r ? *reinterpret_cast<u8*>(HeBytes(r) + 2) : 0; }
inline i32 RecId(HeRecord* r)      { return r ? *reinterpret_cast<i32*>(HeBytes(r) + 4) : -1; }

} // namespace

void SetCharActionReconHooks(const CharActionReconHooks* hooks) {
    g_hooks = hooks ? hooks : &kInertHooks;
}
const CharActionReconHooks& GetCharActionReconHooks() { return *g_hooks; }

// ===========================================================================
// gilde.exe 0x4ca938 — VIBE_NpcAction_BeginActionState7.
// ===========================================================================
i32 NpcReconBeginActionState7(HeRecord* h) {
    const CharActionReconHooks& k = GetCharActionReconHooks();
    if ((He_Flags(h) & 4) != 0)
        return He_State(h);   // result unchanged (the original returns eax==record-derived)

    StampAppt(h);
    GameTimeAdvance(&He_ApptTime(h), 24, 0, 0);   // +24h
    HeR_TypeWord(h) = 7;
    i32 actorId = He_CityId(h);                   // *(a1+16)
    HeR_InitScratch(h) = 0;

    HeRecord* ent = nullptr;
    k.resolveEntityById(&ent, actorId);
    if (ent) {
        // if no co-located "437" object: emit the mixed45 unbind (-1,-1,-1).
        i32 scene = *reinterpret_cast<i32*>(HeBytes(ent) + 93);
        if (!k.objectQueryFind(scene, 1, 0, 437)) {
            i32 entId = *reinterpret_cast<i32*>(HeBytes(ent) + 4);   // *(v4+1)
            k.queueRequestMixed45(entId, -1, -1);
        }
        He_ReqHandle(h) = -1;
        return k.queueRequestEntity29(0, h);
    }
    return k.queueRequestEntity29(-1, h);
}

// ===========================================================================
// gilde.exe 0x4cbc20 — VIBE_NpcAction_BeginActionState20.
// ===========================================================================
i32 NpcReconBeginActionState20(HeRecord* h) {
    const CharActionReconHooks& k = GetCharActionReconHooks();
    He_State(h) = 0;
    if ((He_Flags(h) & 4) != 0)
        return He_State(h);

    StampAppt(h);
    GameTimeAdvance(&He_ApptTime(h), 24, 0, 0);   // +24h
    HeR_TypeWord(h) = 20;
    HeR_InitScratch(h) = 0;

    if (HeR_StrideWord(h) == 1) {
        // Walk the id array the original iterates: v3 = v1, v1+4, v1+8 (stops at
        // v1+16), reading *(v3+172) — i.e. the dwords at +172, +176, +180.
        for (int off = 172; off < 172 + 12; off += 4) {
            i32 mid = *reinterpret_cast<i32*>(HeBytes(h) + off);
            HeRecord* rec = k.findPersonById(mid);
            if (rec) {
                // The original marshals a slot-reset (28) request carrying the
                // appointment clock and a priority byte (9 for category 6/7, else
                // 1). The emit is modelled through the coord/args bridge; the
                // control-flow-relevant effect is the per-member release, so we
                // route a coord27 with the priority as the delta to keep the call
                // count and ordering faithful. (The packed request struct is opaque
                // beyond the priority byte and member id.)
                u8 cat = RecCategory(rec);
                int prio = (cat == 6 || cat == 7) ? 9 : 1;
                k.queueRequestCoord27(RecId(rec), He_Id(h), prio);
            }
        }
    }
    return k.queueRequestEntity29(0, h);
}

// ===========================================================================
// gilde.exe 0x4cf798 — VIBE_CharAction_ArrestStep.
// ===========================================================================
i32 CharReconArrestStep(HeRecord* h) {
    const CharActionReconHooks& k = GetCharActionReconHooks();
    i32 state = He_State(h);

    if (state < -1) {
        if (state != -2)
            return state;
        // fallthrough to the -2/-1 release block.
    } else if (state > -1) {
        // state >= 0: only state 2 with the report flag does work.
        if (state == 2 && (He_Flags(h) & 2) != 0) {
            HeRecord* a = k.findPersonById(HeR_Target(h));
            if (a) k.queueRequestArgs25(RecId(a), 456, 0, 4, 256);
            HeRecord* b = k.findPersonById(HeR_Target2(h));
            if (b) k.queueRequestArgs25(RecId(b), 456, 0, 4, 512);
            if (a && b) {
                k.queueRequestCoord27(RecId(a), RecId(b), -104);
                u8 cb = RecCategory(b);
                if (cb == 6 || cb == 7)
                    k.sendEntityMessage(RecId(b), 6493);
                u8 ca = RecCategory(a);
                if (ca == 6 || ca == 7)
                    k.sendEntityMessage(RecId(b), 6492);
            }
            StampAppt(h);
            GameTimeAdvance(&He_ApptTime(h), 0, 0, 2);   // +2 min
            i32 r = k.queueRequestEntity29(-1, h);
            He_ReqHandle(h) = r;
            return r;
        }
        return state;
    }

    // -2/-1 release block: clear the two arrest flags and free the entry.
    if ((He_Flags(h) & 2) != 0) {
        HeRecord* a = k.findPersonById(HeR_Target(h));
        if (a) k.queueRequestArgs25(RecId(a), 456, 0, 4, 256);
        HeRecord* b = k.findPersonById(HeR_Target2(h));
        if (b) k.queueRequestArgs25(RecId(b), 456, 0, 4, 512);
    }
    return k.freeHandlerEntry(h);
}

// ===========================================================================
// gilde.exe 0x4d1d40 — VIBE_CharAction_GuildJoinStep.
// ===========================================================================
i32 CharReconGuildJoinStep(HeRecord* h) {
    const CharActionReconHooks& k = GetCharActionReconHooks();
    i32 state = He_State(h);
    if (state == -2 || state == -1)
        return k.freeHandlerEntry(h);

    // spawn-flag + entity-packet gate.
    if ((He_Flags(h) & 4) != 0)
        return state;
    if (He_ReqHandle(h) != -1) {
        i32 st = k.packetStatus(He_ReqHandle(h));
        if (st == 0)
            return st;
    }

    state = He_State(h);
    He_ReqHandle(h) = -1;
    if (state) {
        if (state != 1)
            return state;
        // state 1: register the guild membership.
        u8 group = k.buildingGroupFromCode(static_cast<u8>(HeR_Packed173(h) >> 24));
        u8 rankA = 0, rankB = 0;
        k.buildingGuildRankPair(group, &rankA, &rankB);
        k.beginDeltaPacket(0, 0);
        u8 fld;
        fld = static_cast<u8>(k.randomModulo(0x10) + 16);
        k.appendRawField(1, 1, &fld, rankA + 128);
        fld = static_cast<u8>(k.randomModulo(0x10) + 16);
        k.appendRawField(1, 1, &fld, rankB + 128);
        k.queueRequestState22();
        k.requestBuildOp72(0, static_cast<u8>(HeR_Packed173(h) >> 24));
        HeRecord* p = k.findPersonById(HeR_Target(h));
        if (p) {
            // category-9 byte (+9) selects the welcome vs decline text id base.
            int base = (*reinterpret_cast<u8*>(HeBytes(p) + 9)) ? 370 : 294;
            (void)base;   // text id only drives the render; render side effect:
            k.sendEntityMessage(RecId(p), 5801);
            StampAppt(h);
            return k.queueRequestEntity29(-1, h);
        }
        StampAppt(h);
        return k.queueRequestEntity29(-1, h);
    }
    // state 0: render the join intro, bump the retry byte and action word, re-arm,
    // and register the AP event.
    HeRecord* p = k.findPersonById(HeR_Target(h));
    if (p) {
        k.sendEntityMessage(RecId(p), 5800);
        ++HeR_RetryByte(h);
        HeR_TypeWord(h) = static_cast<u16>(HeR_TypeWord(h) + 24);
        k.queueRequestEntity29(HeR_RetryByte(h) >= 3 ? 1 : 0, h);
        return k.meisterRegisterApEvent(static_cast<u16>(*reinterpret_cast<u16*>(HeBytes(p))),
                                        -HeR_Target3(h), 0);
    }
    StampAppt(h);
    return k.queueRequestEntity29(-1, h);
}

// NOTE: VIBE_CharAction_RunOfficeCandidacy (0x4db624) is OMITTED (rule 8). Its
// state-1 decompilation reaches LABEL_13 with the branch variable `v9` (and the
// helper `v5`) UNINITIALIZED on the packet-confirm path (status==1): Hex-Rays
// declares them but assigns only on the -1 / status==2 paths, so the `v9 == 2`
// arm-0-vs-arm-1 decision depends on register garbage that cannot be recovered
// faithfully. Translating it would require guessing the dropped value, so it is
// deliberately left out rather than faked. The office hooks
// (packetSeqById/officeConfirmCandidacy/officeFindHighestVacantRank/
// officeRenderRequirementText) remain in the bridge for when a verified
// disassembly recovers the missing init.

// ===========================================================================
// gilde.exe 0x4dc074 — VIBE_CharAction_CancelEntityActions.
// ===========================================================================
namespace {
// One pool-scan pass for a filter that matches actor (+43) or peer (+44) id and
// re-stamps the matching active handlers. matchExtra/emitQuad encode the filter-44
// extra-id check and the quad60 release the original adds only for filter 44.
void CancelScanSimple(const CharActionReconHooks& k, i32 selfId, i32 filter) {
    for (HeRecord* it = k.findFirstByFilter(1, 0, filter); it; it = k.findNextMatching()) {
        u8 fl = *reinterpret_cast<u8*>(HeBytes(it) + 120);
        if ((fl & 2) == 0 && (fl & 1) == 0)
            continue;
        i32 actor = *reinterpret_cast<i32*>(HeBytes(it) + 4 * 43);
        i32 peer  = *reinterpret_cast<i32*>(HeBytes(it) + 4 * 44);
        i32 other;
        if (actor == selfId) {
            other = peer;
        } else if (selfId != peer) {
            continue;
        } else {
            other = actor;
        }
        k.findPersonById(other);
        k.stampTimeAndRequest(it);
    }
}
} // namespace

HeRecord* CharReconCancelEntityActions(HeRecord* record) {
    const CharActionReconHooks& k = GetCharActionReconHooks();
    if (!record || *reinterpret_cast<u16*>(HeBytes(record)) == 0xFFFF)
        return record;
    i32 selfId = He_Id(record);   // *((_DWORD*)v1+1) == record+4

    // Filters 65, 111, 69: actor/peer-id match, resolve peer, re-stamp.
    CancelScanSimple(k, selfId, 65);
    CancelScanSimple(k, selfId, 111);
    CancelScanSimple(k, selfId, 69);

    // Filter 44: actor (+43) OR id-at-(+45) OR id-at-(+48) -> quad60 release; else
    // require peer (+44) match. Then re-stamp.
    for (HeRecord* it = k.findFirstByFilter(1, 0, 44); it; it = k.findNextMatching()) {
        u8 fl = *reinterpret_cast<u8*>(HeBytes(it) + 120);
        if ((fl & 2) == 0 && (fl & 1) == 0)
            continue;
        i32 actor = *reinterpret_cast<i32*>(HeBytes(it) + 4 * 43);
        i32 peer  = *reinterpret_cast<i32*>(HeBytes(it) + 4 * 44);
        i32 id45  = *reinterpret_cast<i32*>(HeBytes(it) + 4 * 45);
        i32 id48  = *reinterpret_cast<i32*>(HeBytes(it) + 4 * 48);
        if (actor == selfId || selfId == id45 || selfId == id48) {
            k.queueRequestQuad60(actor, peer, 0, He_Id(it));
        } else if (selfId != peer) {
            continue;
        }
        k.stampTimeAndRequest(it);
    }

    // Filter 71: actor (+43) OR peer (+44) match -> re-stamp.
    for (HeRecord* it = k.findFirstByFilter(1, 0, 71); it; it = k.findNextMatching()) {
        u8 fl = *reinterpret_cast<u8*>(HeBytes(it) + 120);
        if ((fl & 2) != 0 || (fl & 1) != 0) {
            i32 actor = *reinterpret_cast<i32*>(HeBytes(it) + 4 * 43);
            i32 peer  = *reinterpret_cast<i32*>(HeBytes(it) + 4 * 44);
            if (actor == selfId || selfId == peer)
                k.stampTimeAndRequest(it);
        }
    }

    // Filter 94: actor (+43) match only.
    for (HeRecord* it = k.findFirstByFilter(1, 0, 94); it; it = k.findNextMatching()) {
        u8 fl = *reinterpret_cast<u8*>(HeBytes(it) + 120);
        if (((fl & 2) != 0 || (fl & 1) != 0) &&
            *reinterpret_cast<i32*>(HeBytes(it) + 4 * 43) == selfId)
            k.stampTimeAndRequest(it);
    }

    // Filter 95: actor (+43) match only (terminal scan; returns the null cursor).
    HeRecord* it = k.findFirstByFilter(1, 0, 95);
    while (it) {
        u8 fl = *reinterpret_cast<u8*>(HeBytes(it) + 120);
        if ((fl & 2) != 0 || (fl & 1) != 0) {
            if (*reinterpret_cast<i32*>(HeBytes(it) + 4 * 43) == selfId)
                k.stampTimeAndRequest(it);
            it = k.findNextMatching();
        } else {
            it = k.findNextMatching();
        }
    }
    return it;   // null
}

// ===========================================================================
// gilde.exe 0x4e55bc — VIBE_NpcAction_BeginCarryGoods.
// ===========================================================================
HeRecord* NpcReconBeginCarryGoods(HeRecord* h) {
    const CharActionReconHooks& k = GetCharActionReconHooks();
    StampAppt(h);
    StampClock2(h);
    GameTimeAdvance(&HeR_Clock2(h), 24 * HeR_SeqId(h), 0, 0);   // +24*(+184) hours
    HeR_Clock2Word(h) = 8;
    HeR_CarryArg(h) = HeR_SeqId(h);   // *(a1+188) = *(a1+184)

    HeRecord* carrier = k.personQueryBegin(0, 1, 1, HeR_Target(h));
    if (carrier) {
        if ((He_Flags(h) & 2) != 0) {
            // The carrier's home-city category byte (the original indexes the city
            // person column via the carrier's +39 city index; the resolved row's
            // +2 carries the same category byte).
            u8 cat = RecCategory(carrier);
            HeRecord* victim = k.findPersonById(HeR_Target2(h));
            if (cat == 6 || cat == 7) {
                // render(5350,...) -> quickjump report, then the law violation.
                k.sendQuickjumpMessage(RecId(carrier), 5350);
                return reinterpret_cast<HeRecord*>(static_cast<intptr_t>(
                    k.evaluateViolation(13, HeR_StrideWord(h), HeR_Target2(h),
                                        RecId(carrier), RecId(carrier))));
            }
            (void)victim;
        }
    }
    return carrier;
}

} // namespace guild::sim
