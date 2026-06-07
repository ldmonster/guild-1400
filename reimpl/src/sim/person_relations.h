#pragma once
// person_relations — person social-relation collectors + per-candidate status
// flag resolution for the Guild simulation (gilde.exe). MODULE: sim person
// social cluster (namespace guild::sim).
//
// These functions build the candidate lists the relation/family/heir UI and the
// office-succession passes consume. Each walks the 536-byte Person array
// (word_12CE910, see sim/types.h) and/or the "He" handler pool (see sim/he.h),
// scores/classifies each match, and writes a fixed-stride 56-byte output entry.
//
// Translated functions (addresses absolute, imagebase 0x400000):
//   VIBE_Person_ResolveStatusFlags          0x553ce8
//   VIBE_Office_CollectFamilyHeirCandidates 0x5555c8
//   VIBE_Person_CollectRelatedNpcs          0x554e34
//   VIBE_Person_CollectTopByScore           0x555150
//
// CROSS-CLUSTER LEAVES (He-handler scan, candidate eligibility, AI favorability,
// building output, the +56 entry memset) are injected through PersonRelCtx so the
// record-walk / scoring / sort logic is exercisable in isolation. The shipping
// game installs the real cluster bridge.
#include "guild/common/types.h"
#include "sim/types.h"
#include "sim/he.h"

namespace guild::sim {

// ---------------------------------------------------------------------------
// Output candidate entry. In the 32-bit original this is a 56-byte (14-dword)
// stride record: a4[14*i] is a dword *person pointer*, the flag/score/eligibility
// fields are dwords at +28/+32/+36/+40/+44/+48, and the memset clears 56*count
// via VIBE_Light_SetGrayColorThunk. Because this reconstruction targets a 64-bit
// host the `person` pointer is 8 bytes, so the natural struct stride differs from
// 56 — we keep a *natural* struct (the collectors index it as out[i], never by
// raw byte offset) and clear sizeof(PersonRelEntry)*count. The original dword
// indices are documented per field for traceability.
//   dword 0   person record pointer (a4[14*i]; *v8 == person rec)
//   dword 7   (+28) status flag word A     (ResolveStatusFlags a1[7])
//   dword 8   (+32) status flag word B     (a1[8])
//   dword 9   (+36) status flag word C     (a1[9])
//   dword 10  (+40) status flag word D     (a1[10])
//   dword 11  (+44) relation class / favor score key (CollectRelatedNpcs +11;
//             CollectTopByScore sort key v19[11] vs next-entry v19[25])
//   dword 12  (+48) eligibility result    (EvaluateCandidateEligibility result)
// ---------------------------------------------------------------------------
struct PersonRelEntry {
    Person* person;   // dword 0   person record pointer
    i32  flagA;       // dword 7   (+28) ResolveStatusFlags a1[7]
    i32  flagB;       // dword 8   (+32) a1[8]
    i32  flagC;       // dword 9   (+36) a1[9]
    i32  flagD;       // dword 10  (+40) a1[10]
    i32  relClass;    // dword 11  (+44) relation class / favor score key
    i32  eligibility; // dword 12  (+48) EvaluateCandidateEligibility result
};

// ---------------------------------------------------------------------------
// He-handler "match record" — the only fields these collectors read off a
// handler entry the He finders return. Recovered from the raw accesses:
//   *((DWORD*)h + 43) == h+172  : owner/person id key (matched vs current player
//                                 id column dword_12CE914[134*playerIdx])
//   *((DWORD*)h + 44) == h+176  : related person id (-> FindRecordById)
//   *((DWORD*)h + 49) == h+196  : id key for the cmd24 / build-permit branch
//   *((WORD*) h +  4) == h+8    : person/city index (He_CityIndex)
// These map onto the HeRecord byte layout; the accessors below address them by
// the exact original offset.
inline i32& HeMatchKey172(HeRecord* h) { return *reinterpret_cast<i32*>(HeBytes(h) + 172); }
inline i32& HeMatchRel176(HeRecord* h) { return *reinterpret_cast<i32*>(HeBytes(h) + 176); }
inline i32& HeMatchKey196(HeRecord* h) { return *reinterpret_cast<i32*>(HeBytes(h) + 196); }

// ---------------------------------------------------------------------------
// PersonRelCtx — injected cross-cluster leaves + world state the four collectors
// consult. A null function pointer installs an inert default (see .cpp). The
// shipping game wires these to the real He pool / AI / building clusters.
// ---------------------------------------------------------------------------
struct PersonRelCtx {
    // Current player / city index — word_63CC5C. Used to index the person id
    // column dword_12CE914[134*playerIdx] (== g_personIds[playerIdx]).
    u16 currentPlayerIndex = 0;

    // Live game-date low dword — LODWORD(qword_13CE852) @0x13CE852. The relative
    // collector (OfficeCollectRelativeCandidates) compares a handler's creation date
    // (He +197) against this to pick the most-recent relative group. Default 0.
    i32 gameDateLow = 0;

    // He pool find-by-filter leaves (VIBE_He_FindFirstHandlerByFilter @0x4c63f8 /
    // VIBE_He_FindNextMatchingHandler @0x4c6278). `kindFilter` is the 3rd varargs
    // value (selector 0 => kind byte) the originals pass (111, 24, 69, 65).
    HeRecord* (*heFindFirst)(int kindFilter) = nullptr;
    HeRecord* (*heFindNext)() = nullptr;

    // VIBE_He_CountMatchingEntities(id@0x4c4458) — number of live entity handlers
    // bound to person id `id`. Default 0.
    int (*heCountMatchingEntities)(i32 id) = nullptr;

    // VIBE_Building_ComputeCurrentOutput(personRec@0x57d26c) — the building output
    // scalar (compared to 100/350/650). Default 0.
    float (*buildingCurrentOutput)(Person* personRec) = nullptr;

    // VIBE_Ai_ComputePersonFavorability(a,b,mode@0x594330) — 0..100 favorability of
    // person `a` toward `b`. Default 0.
    float (*personFavorability)(int a, int b, int mode) = nullptr;

    // VIBE_Person_EvaluateCandidateEligibility(refIdx,candIdx,filter@0x5596f8) —
    // the filter result stamped into entry +48. `filter` is the opaque a3. Default 0.
    int (*evaluateEligibility)(u16 refIdx, u16 candIdx, int filter) = nullptr;
};

void SetPersonRelCtx(const PersonRelCtx* ctx);
const PersonRelCtx& GetPersonRelCtx();

// gilde.exe 0x553ce8 — VIBE_Person_ResolveStatusFlags(entry@eax).
//   `entry` is a PersonRelEntry whose +0 holds a live Person record pointer.
//   Resolves four status-flag words (entry +28/+32/+36/+40) by querying the He
//   pool (filters 111/24/69) and the building output scalar:
//     +28 (flagA): 1596 + (person+12 byte == 0); 1599 if a filter-111 handler
//          ties this person to the current player and the person's +458 sign byte
//          is negative and the id-column high bytes differ.
//     +32 (flagB): 0; 1600 if CountMatchingEntities(person id) > 0; else 1601 if a
//          filter-24 handler with matching city index + id is present.
//     +36 (flagC): 0; 1598 if a filter-69 handler ties this person to the player
//          (returns 1 early).
//     +40 (flagD): if flagC already set returns 1; else buckets the building output:
//          < 100 -> 1605; [100,350) -> 1604; [350,650) -> 1603; >= 650 -> 1602.
//   Returns 1 once any flag is decided, else 0 (person record null).
int PersonResolveStatusFlags(PersonRelEntry* entry);

// gilde.exe 0x5555c8 — VIBE_Office_CollectFamilyHeirCandidates
//   (__usercall: eax=refRec@a1, edx=capacity@a2, ecx=filter@a3, ebx=out@a4).
// Scans the He pool (filter 111) for up to 4 handlers tied to `refRec`'s id,
// resolving each related person (FindRecordById). If `capacity` is 0, returns the
// found count without writing. Otherwise memsets `capacity` entries, fills up to
// min(capacity, found) of them with the resolved heirs (ResolveStatusFlags +
// EvaluateCandidateEligibility per entry), and returns the found count.
u32 OfficeCollectFamilyHeirCandidates(Person* refRec, u32 capacity, int filter,
                                      PersonRelEntry* out);

// gilde.exe 0x554e34 — VIBE_Person_CollectRelatedNpcs
//   (__usercall: eax=refRec@a1, edx=capacity@a2, ecx=filter@a3, ebx=out@a4).
// Walks the 768-slot Person array for NPCs related to `refRec` by the 8-entry
// relation id array (refRec +92..) or by shared household (word @+80 & class @+88),
// classifying each (relation class -> entry +44); then probes a filter-65 handler
// for one extra related person. With `capacity` 0 it just counts. Otherwise it
// resolves status flags + eligibility per filled entry and returns the count.
int PersonCollectRelatedNpcs(Person* refRec, u32 capacity, int filter,
                             PersonRelEntry* out);

// gilde.exe 0x555150 — VIBE_Person_CollectTopByScore
//   (__usercall: eax=refRec@a1, edx=capacity@a2, ecx=filter@a3, ebx=out@a4).
// Walks the Person array scoring each candidate (kind 2..7) by AI favorability
// toward `refRec`; keeps only scores >= 75.0 not already related, insertion-sorts
// the top `capacity` by descending score into `out`, then resolves status flags +
// eligibility per kept entry. With `capacity` 0 it just counts qualifying persons.
int PersonCollectTopByScore(Person* refRec, u32 capacity, int filter,
                            PersonRelEntry* out);

}  // namespace guild::sim
