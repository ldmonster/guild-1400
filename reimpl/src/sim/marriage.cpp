// marriage — marriage / courtship / family-formation cluster.
// Faithful 1:1 port of the gilde.exe control flow. The candidate collectors reuse
// person_relations.cpp's PersonRelEntry / PersonRelCtx substrate (the He pool
// finders + eligibility leaf are the same injected ones); the marriage orchestrator
// routes its UI / command / text leaves through MarriageCtx. The record-walk /
// counting / selection logic is verbatim.
#include "sim/marriage.h"
#include "sim/entity.h"        // g_persons, PersonFindRecordById

#include <cstring>

namespace guild::sim {

// Person record byte-offset reads (the originals fold these into raw accesses).
static inline u8  PByte(const Person* p, int off) {
    return reinterpret_cast<const u8*>(p)[off];
}
static inline i16 PWord(const Person* p, int off) {
    i16 v; std::memcpy(&v, reinterpret_cast<const u8*>(p) + off, sizeof v); return v;
}
static inline i32 PDword(const Person* p, int off) {
    i32 v; std::memcpy(&v, reinterpret_cast<const u8*>(p) + off, sizeof v); return v;
}

// Unaligned dword read of the He handler's date field @ +197.
i32 HeReadDate197(HeRecord* h) {
    i32 v; std::memcpy(&v, HeBytes(h) + 197, sizeof v); return v;
}

// He pool finders / eligibility — reuse the SAME PersonRelCtx leaves
// person_relations routes through (so a single ctx drives both modules).
static HeRecord* HeFirst(int kind) {
    const PersonRelCtx& c = GetPersonRelCtx();
    return c.heFindFirst ? c.heFindFirst(kind) : nullptr;
}
static HeRecord* HeNext() {
    const PersonRelCtx& c = GetPersonRelCtx();
    return c.heFindNext ? c.heFindNext() : nullptr;
}
static int Eligible(u16 ref, u16 cand, int filter) {
    const PersonRelCtx& c = GetPersonRelCtx();
    return c.evaluateEligibility ? c.evaluateEligibility(ref, cand, filter) : 0;
}

// NOTE: unlike CollectFamilyHeirCandidates / CollectRelatedNpcs (person_relations),
// the two collectors here do NOT pre-zero `out` — the originals have no
// VIBE_Light_SetGrayColorThunk memset call; they write entries densely from index 0.

// ===========================================================================
// gilde.exe 0x58c454 — VIBE_Person_CountAdultChildren.
// ---------------------------------------------------------------------------
//   a1=person, a2=outCount. Walk the 5-entry child-id array at person+104..+120
//   (edx from a1+12 to a1+32 step +4, read [edx+0x5C]); resolve each id. For each
//   resolved child that is a live actor (+8 != 0): ++count; if its age word (+10)
//   is > 11 set the adult flag. Write count to *a2 (if non-null); return adult flag.
// ===========================================================================
int PersonCountAdultChildren(const Person* person, i32* outCount) {
    const u8* base = reinterpret_cast<const u8*>(person);
    int adultFlag = 0;   // esi (v4)
    int count = 0;       // ecx (v8) — xor ecx,ecx at 0x58c460

    // 5 relation/child id slots: a1+104, +108, +112, +116, +120.
    for (int rel = 0; rel < 5; ++rel) {
        i32 childId;
        std::memcpy(&childId, base + 104 + 4 * rel, sizeof childId);  // [edx+0x5C]
        Person* child = PersonFindRecordById(childId);
        if (child) {
            if (PByte(child, 8) != 0) {                  // live-actor gate (> 0)
                ++count;
                if (static_cast<u16>(PWord(child, 10)) > 0xB)   // adult: age > 11
                    adultFlag = 1;
            }
        }
    }

    if (outCount)
        *outCount = count;
    return adultFlag;
}

// ===========================================================================
// gilde.exe 0x5556c4 — VIBE_Office_CollectRelativeCandidates.
// ---------------------------------------------------------------------------
//   a1=refRec, a2=capacity, a3=filter, a4=out. Filter-44 handlers each carry up to
//   6 relative ids at +172/+176/+180/+184/+188/+192. A handler's group "includes"
//   refRec when one of the resolved relatives is the refRec record (pointer eq).
//
//   capacity == 0 : count pass. For each handler resolve ids #1.. in order; the
//     chain breaks (to the next handler) the moment an id fails to resolve. The
//     running resolved-count `edx` is added to the total ONLY at the chain end and
//     ONLY if refRec is in the group (the original's `if (edi) ebp += edx`).
//
//   capacity != 0 : fill pass. Pick the handler with the smallest
//     (handlerDate197 - gameDateLow) that is <= the running minimum (init 200) and
//     whose group includes refRec (ids #1..#5 must resolve); refill `out` from its 6
//     relative records (skipping nulls), tagging each with the class table
//     {0,1,2,3,3,4}, the handler id at +13, status flags + eligibility. A later,
//     more-recent qualifying handler overwrites the result.
// ===========================================================================
int OfficeCollectRelativeCandidates(Person* refRec, u32 capacity, int filter,
                                    PersonRelEntry* out) {
    int total = 0;  // ebp (v4)

    // -------- count pass --------
    if (capacity == 0) {
        for (HeRecord* h = HeFirst(44); h; h = HeNext()) {
            Person* r1 = PersonFindRecordById(HeMatchKey172(h));
            if (!r1) continue;                            // id#1 fail -> next handler
            int found = 1;                                // edx
            int inGroup = (r1 == refRec) ? 1 : 0;         // edi

            Person* r2 = PersonFindRecordById(HeMatchRel176(h));
            if (!r2) continue;
            ++found; if (r2 == refRec) inGroup = 1;

            Person* r3 = PersonFindRecordById(HeMatchRel180(h));
            if (!r3) continue;
            ++found; if (r3 == refRec) inGroup = 1;

            Person* r4 = PersonFindRecordById(HeMatchRel184(h));
            if (!r4) continue;
            ++found; if (r4 == refRec) inGroup = 1;

            Person* r5 = PersonFindRecordById(HeMatchRel188(h));
            if (!r5) continue;
            ++found; if (r5 == refRec) inGroup = 1;

            // id#6: resolution failure here does NOT skip — falls through to the
            // `if (inGroup) total += found` tail (the original's loc_5557DA).
            Person* r6 = PersonFindRecordById(HeMatchRel192(h));
            if (r6) {
                ++found;
                if (r6 == refRec) { total += found; continue; }  // 5557be fall-through
            }
            if (inGroup)
                total += found;                            // loc_5557C0
        }
        return total;
    }

    // -------- fill pass --------
    const i32 gameDateLow = GetPersonRelCtx().gameDateLow;  // LODWORD(qword_13CE852)
    int minDelta = 200;  // v30 (var_24), init 0xC8

    for (HeRecord* h = HeFirst(44); h; h = HeNext()) {
        // Date gate: only consider handlers at least as recent as the running min.
        const i32 delta = HeReadDate197(h) - gameDateLow;   // [h+0xC5] - gameDate
        if (delta > minDelta)
            continue;

        // Resolve the 6 relative records; ids #1..#5 must all resolve or we skip.
        Person* recs[6];
        int cls[6] = {0, 1, 2, 3, 3, 4};   // class table (var_40..var_2C)
        int inGroup = 0;                   // edx

        recs[0] = PersonFindRecordById(HeMatchKey172(h));
        if (!recs[0]) continue;
        if (recs[0] == refRec) inGroup = 1;

        recs[1] = PersonFindRecordById(HeMatchRel176(h));
        if (!recs[1]) continue;
        if (recs[1] == refRec) inGroup = 1;

        recs[2] = PersonFindRecordById(HeMatchRel180(h));
        if (!recs[2]) continue;
        if (recs[2] == refRec) inGroup = 1;

        recs[3] = PersonFindRecordById(HeMatchRel184(h));
        if (!recs[3]) continue;
        if (recs[3] == refRec) inGroup = 1;

        recs[4] = PersonFindRecordById(HeMatchRel188(h));
        if (!recs[4]) continue;
        if (recs[4] == refRec) inGroup = 1;

        // id#6 may be null; if it resolves and equals refRec the group qualifies,
        // otherwise the group qualifies iff some earlier id matched (inGroup).
        recs[5] = PersonFindRecordById(HeMatchRel192(h));
        if (recs[5]) {
            if (recs[5] == refRec) inGroup = 1;
            else if (!inGroup) continue;     // 5559ca/cc: not in group, skip handler
        } else {
            recs[5] = nullptr;
            if (!inGroup) continue;          // 5559ca/cc: id#6 null & not in group
        }

        // Commit: this becomes the new most-recent qualifying handler.
        minDelta = delta;
        const i32 handlerId = static_cast<i32>(h->id);   // *(DWORD)(handler+4)
        int written = 0;                                 // ebp
        const int bound = 4 * static_cast<int>(capacity);   // var_18 (in dword units)
        // edi steps 0,4,..; recs[edi/4]; bound by 24 (6 entries) and 4*capacity.
        for (int i = 0; i < 24 && i < bound; i += 4) {
            Person* rec = recs[i / 4];
            if (!rec)
                continue;                                // skip null relative slot
            PersonRelEntry* e = &out[written];
            e->person   = rec;                           // *esi = rec
            e->relClass = cls[i / 4];                    // esi+0x2C (+44)
            // entry +0x34 (+52, dword 13) holds the handler id (handler+4).
            // PersonRelEntry models dwords 0..12; the original writes dword 13 too,
            // but the collectors only consume +0..+48, so the handler id (+52) is
            // not surfaced as a PersonRelEntry field. We keep the documented note.
            PersonResolveStatusFlags(e);
            i32 candMarker = static_cast<u16>(e->person->marker);  // [*esi] word
            ++written;
            e->eligibility = Eligible(static_cast<u16>(refRec->marker),
                                      static_cast<u16>(candMarker), filter);
        }
        (void)handlerId;
        // The original then `jmp loc_555811` (find next handler); a more-recent
        // qualifying handler will refill `out` and update minDelta. The returned
        // count is the LAST fill's `written`.
        total = written;
    }
    return total;
}

// ===========================================================================
// gilde.exe 0x5559d8 — VIBE_Office_CollectSpouseAndBusinessCandidates.
// ---------------------------------------------------------------------------
//   a1=refRec, a2=capacity, a4=out, a3=filter. Spouse (filter-71, only if the
//   +457 status byte has bit 0x4) + business partners (filter-24, key @+196).
// ===========================================================================
int OfficeCollectSpouseAndBusinessCandidates(Person* refRec, u32 capacity,
                                             int filter, PersonRelEntry* out) {
    Person* spouse = nullptr;   // RecordById (esi)
    int count = 0;              // v6 (ebp)
    const i32 refId = PDword(refRec, 4);     // *((DWORD*)a1 + 1)

    // Resolve the spouse from a filter-71 handler whose +172 or +176 key matches
    // refRec id; the OTHER key resolves to the spouse. Shared by both passes.
    auto resolveSpouse = [&]() -> Person* {
        for (HeRecord* h = HeFirst(71); h; h = HeNext()) {
            const i32 k172 = HeMatchKey172(h);
            const i32 k176 = HeMatchRel176(h);
            if (k172 == refId)
                return PersonFindRecordById(k176);
            if (k176 == refId)
                return PersonFindRecordById(k172);
        }
        return nullptr;
    };

    // -------- count pass --------
    if (capacity == 0) {
        if ((PByte(refRec, 457) & 4) != 0) {
            spouse = resolveSpouse();
            if (spouse)
                count = 1;
        }
        // Business partners: filter-24 handlers, partner id @ +196 (dword 49).
        for (HeRecord* h = HeFirst(24); h; h = HeNext()) {
            if (PersonFindRecordById(HeMatchKey196(h)))
                ++count;
        }
        return count;
    }

    // -------- fill pass --------
    if ((PByte(refRec, 457) & 4) != 0) {
        spouse = resolveSpouse();
        if (spouse) {
            out[0].relClass = 0;    // a3[11] = 0
            count = 1;
            out[0].person = spouse; // *a3 = spouse
        }
    }

    // Append business partners after the spouse (entry +11 set to 1), up to capacity.
    HeRecord* h = HeFirst(24);
    if (h) {
        PersonRelEntry* dst = &out[count];   // &a3[14*v6]
        do {
            if (static_cast<u32>(count) >= capacity)
                break;
            Person* partner = PersonFindRecordById(HeMatchKey196(h));
            if (partner) {
                dst->person   = partner;     // *(v16-14) = v18
                dst->relClass = 1;           // *(v16-3) = 1
                ++count;
                ++dst;
            }
            h = HeNext();
        } while (h);
    }

    // Resolve status flags + eligibility for every filled entry.
    for (int i = 0; i < count; ++i) {
        PersonRelEntry* e = &out[i];
        PersonResolveStatusFlags(e);
        i32 candMarker = static_cast<u16>(e->person->marker);   // **v19
        e->eligibility = Eligible(static_cast<u16>(PWord(refRec, 0)),
                                  static_cast<u16>(candMarker), filter);
    }
    return count;
}

// ===========================================================================
// VIBE_NpcAction_BeginMarriage context plumbing + orchestrator.
// ===========================================================================
static const MarriageCtx kInertMarriageCtx{};
static const MarriageCtx* g_marriageCtx = &kInertMarriageCtx;
void SetMarriageCtx(const MarriageCtx* ctx) {
    g_marriageCtx = ctx ? ctx : &kInertMarriageCtx;
}
const MarriageCtx& GetMarriageCtx() { return *g_marriageCtx; }

static Person* RunPicker(const char* prompt, i32 excludeMarker) {
    return g_marriageCtx->runPartnerPicker
         ? g_marriageCtx->runPartnerPicker(prompt, excludeMarker) : nullptr;
}
static void RenderText(char* buf, int textId, u16 arg) {
    if (g_marriageCtx->renderText) g_marriageCtx->renderText(buf, textId, arg);
    else buf[0] = '\0';
}
static void QueueCourtship(i32 idA, i32 idB, int coord) {
    if (g_marriageCtx->queueCourtship) g_marriageCtx->queueCourtship(idA, idB, coord);
}
static void SendMessage(i32 id, const char* buf, int textId) {
    if (g_marriageCtx->sendEntityMessage) g_marriageCtx->sendEntityMessage(id, buf, textId);
}

// gilde.exe 0x5687b0 — VIBE_NpcAction_BeginMarriage.
//   ctxKind == 6 : pick both partners via the office-overview picker (the 2nd pick
//     excludes the 1st via its marker, prompts 153 then 155). Otherwise resolve the
//     two partners directly from partnerAId/partnerBId. Either unresolved -> 0.
//   On success: two reciprocal courtship commands (A->B, B->A, coord -40); send the
//     wedding announcement to any partner that is a player-class person (kind 6/7).
int NpcActionBeginMarriage(u8 ctxKindByte, i32 partnerAId, i32 partnerBId) {
    Person* a = nullptr;   // edi
    Person* b = nullptr;   // esi
    int begun = 0;         // ebp

    if (ctxKindByte == 6) {
        // Interactive: pick A (prompt 153, no exclude), then B (prompt 155,
        // excluding A's marker). Empty-string prompts are built by RenderText.
        char prompt[256];
        RenderText(prompt, 153, 0);
        a = RunPicker(prompt, -1);
        if (!a)
            return 0;
        RenderText(prompt, 155, static_cast<u16>(a->marker));
        b = RunPicker(prompt, static_cast<i16>(a->marker));
        if (!b)
            return 0;
    } else {
        // Direct: resolve both partners from the supplied ids.
        a = PersonFindRecordById(partnerAId);   // [a2+4]
        b = PersonFindRecordById(partnerBId);   // [a2+8]
        if (!a)
            return 0;
        if (!b)
            return 0;
    }

    begun = 1;
    const i32 idA = PDword(a, 4);   // [edi+4]
    const i32 idB = PDword(b, 4);   // [esi+4]
    QueueCourtship(idA, idB, -40);  // A->B
    QueueCourtship(idB, idA, -40);  // B->A

    char msg[1024];
    // Partner A: announce naming B if A is a player-class person (kind 6 or 7).
    const u8 kindA = PByte(a, 2);
    if (kindA == 6 || kindA == 7) {
        RenderText(msg, 3247, static_cast<u16>(b->marker));   // 0xCAF
        SendMessage(idA, msg, 1418);
    }
    // Partner B: announce naming A if B is a player-class person (kind 6 or 7).
    const u8 kindB = PByte(b, 2);
    if (kindB == 6 || kindB == 7) {
        RenderText(msg, 3248, static_cast<u16>(a->marker));   // 0xCB0 (A marker x2)
        SendMessage(idB, msg, 1418);
    }
    return begun;
}

}  // namespace guild::sim
