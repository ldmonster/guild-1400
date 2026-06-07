#pragma once
// court_council2 — additional deterministic candidate-collection, vote/category
// tally, candidate rating, and the court-trial verdict scoring FSM for the Guild
// simulation (gilde.exe). MODULE: world office / court-council cluster
// (namespace guild::world).
//
// This file completes the office-election / court-trial slice with the
// person-array candidate scanners and the court verdict scorer that the
// VIBE_Office_RunCouncilSession / VIBE_Office_RunCourtTrial session FSMs drive.
// The record-walk / scoring / sort / verdict logic is a verbatim 1:1 port; the
// cross-cluster leaves (AI favorability, candidate eligibility, status-flag
// resolution, the live building array, the AI-needs weight, the RNG) are routed
// through installable hooks with inert defaults so the math is exercisable in
// isolation. The shipping game installs the real cluster bridge.
//
// Translated functions (addresses absolute, imagebase 0x400000):
//   VIBE_Office_TallyCategoryCounts                0x47fdfc
//   VIBE_Office_CollectCategoryMatchedCandidates   0x555d5c
//   VIBE_Office_CollectGuildSuccessorCandidates    0x555ba8
//   VIBE_Office_CollectNearestCandidatesByDistance 0x555378
//   VIBE_Office_ApplyCandidateRatingBars           0x556ba0
//   VIBE_NpcAction_EvaluateCourtTrial              0x4747dc
//
// Reused reconstructed callees (extern, NOT redefined here):
//   guild::sim::PersonRelEntry / PersonResolveStatusFlags / PersonRelCtx
//                                                  (sim/person_relations.h)
//   guild::world::OfficeCollectSuccessorCandidates (world/office.h)
//   guild::world::OfficeDefBookCat                 (world/office.h)
//   guild::sim::g_persons / kPersonCapacity        (sim/entity.h, sim/types.h)
//   guild::ai::ComputePersonFavorability           (ai/favorability.h)
//   guild::util::ConvertX                          (util/coord.h)
//   guild::util::RandomModulo                      (util/math_random.h)
//   guild::sim::BuildingType_MapActionToCategory   (sim/building_type.h)
#include "guild/common/types.h"
#include "sim/person_relations.h"   // PersonRelEntry, PersonRelCtx

namespace guild::world {

using guild::sim::Person;
using guild::sim::PersonRelEntry;

// ---------------------------------------------------------------------------
// gilde.exe 0x47fdfc — VIBE_Office_TallyCategoryCounts (this == out histogram).
//   Walks the 768-slot person array (word_12CE910, stride 536). For each person
//   record whose kind byte (+2) is in [2,7], reads the held-office byte (+359):
//     office == 0           -> bucket 0
//     office in [1,0x24]    -> bucket = OfficeDefBookCat(office)   (def +2 byte)
//     office >= 0x25        -> bucket = OfficeDefBookCat(0)
//   and increments histogram[bucket]. Returns the 37-entry histogram (dword_B59820,
//   zeroed each call). `out` must hold >= 256 entries (bucket is a u8 index).
// ---------------------------------------------------------------------------
int* OfficeTallyCategoryCounts(int* out);

// Convenience overload using the module-owned histogram (dword_B59820).
int* OfficeTallyCategoryCounts();

// ---------------------------------------------------------------------------
// gilde.exe 0x555d5c — VIBE_Office_CollectCategoryMatchedCandidates
//   (__usercall: eax=refRec a1, edx=capacity a2, ecx=filter a3, ebx=out a4).
// Resolves the held office's category-rank list (OfficeCollectCategoryRankList,
// up to 4 codes) for refRec's office (+358). If capacity==0, COUNTS the persons
// whose +360 office byte matches any list code (no write). Otherwise scans the
// 768 person slots per list code, writing each matching live person into a
// PersonRelEntry (person, relClass=list code, status flags, eligibility), up to
// `capacity` entries. Returns the number written (count mode: the count).
// `categoryRankList` supplies the up-to-4 codes; pass the codes + length so the
// scan is independent of the office-def table wiring.
u32 OfficeCollectCategoryMatchedCandidates(Person* refRec, u32 capacity,
                                           int filter, PersonRelEntry* out,
                                           const u8* categoryRankList,
                                           int rankListLen);

// ---------------------------------------------------------------------------
// gilde.exe 0x555ba8 — VIBE_Office_CollectGuildSuccessorCandidates
//   (__usercall: eax=refRec a1, edx=capacity a2, ecx=filter a3, ebx=out a4).
// If refRec's +360 office byte is 0, returns 0. Calls OfficeCollectSuccessorCandidates
// (rank=+360, max 6) for the office-pool successors, then scans the 768 person
// slots for up to 3 OTHER live persons sharing refRec's office (+360) and not
// equal to refRec. If capacity==0, returns (poolCount + scanCount). Otherwise
// fills out[] first from the office-pool ids (relClass 0) up to min(capacity,
// poolCount), then from the scanned same-office persons (relClass 1), each with
// status flags + eligibility, returning the number written.
// The office-successor pool is resolved through the reconstructed
// OfficeCollectSuccessorCandidates; the caller supplies its `people` pool (the
// original walks an internal office-holder array). Pass {nullptr,0} for the
// pure same-office scan path.
struct OfficePerson;  // fwd (defined in world/office.h)
u32 OfficeCollectGuildSuccessorCandidates(Person* refRec, u32 capacity,
                                          int filter, PersonRelEntry* out,
                                          const OfficePerson* pool,
                                          int poolCount);

// ---------------------------------------------------------------------------
// gilde.exe 0x555378 — VIBE_Office_CollectNearestCandidatesByDistance
//   (__usercall: eax=refRec a1, edx=capacity a2, ecx=filter a3, ebx=out a4).
// If capacity==0, COUNTS live persons (kind +2 in [2,7], != refRec) whose AI
// favorability toward refRec is <= 33.0 (or whose +524 dword equals refRec's id
// dword at +4, treated as favor 0). Otherwise keeps the `capacity` LOWEST-favor
// candidates via an insertion sort (favor stored truncated in entry.relClass),
// then resolves status flags + eligibility for each kept entry. Returns the kept
// count (clamped to capacity). Same-faction persons (+524==refId) score 0.
int OfficeCollectNearestCandidatesByDistance(Person* refRec, u32 capacity,
                                             int filter, PersonRelEntry* out);

// ---------------------------------------------------------------------------
// gilde.exe 0x556ba0 — VIBE_Office_ApplyCandidateRatingBars
//   (__usercall: eax=refPersonRec a1, edx=count a2, ebx=entries a3).
// For each of `count` 14-dword candidate slots (`entries` is the raw a4 buffer:
// slot i at entries + 56*i, *(slot)=person ptr, slot[2]=rating-bar widget): if the
// candidate person is live (marker != 0xFFFF) and has a bar widget, compute the
// 0..100 bar value. If BOTH refPerson.kind and candidate.kind are 6 or 7
// (council/court office holders) the bar is a flat 100; otherwise it is the AI
// favorability of refPerson toward the candidate, truncated. Returns the last
// SetValueOrText return (low byte). Routed through CourtCouncilHooks for the
// widget set and favorability so the rule is testable. `entries` slots use the
// RatingSlot view below.
struct RatingSlot {
    Person* person;     // dword 0   candidate person record (== *v5)
    i32     pad1;       // dword 1
    i32     barWidget;  // dword 2   rating-bar widget handle (== v5[2]; -1 == none)
    i32     pad[11];    // dwords 3..13  (full 14-dword / 56-byte stride)
};
char OfficeApplyCandidateRatingBars(Person* refPerson, u32 count,
                                    RatingSlot* entries);

// ---------------------------------------------------------------------------
// Court-trial verdict scorer.
// ---------------------------------------------------------------------------
// One live building/object slot as the court scorer consumes it (the originals
// read the 169-byte object record + its 589-byte type-def record).
struct CourtBuildingSlot {
    bool alive;        // object +0 (the kind/alive byte, != 0)
    u16  ownerWord;    // object +39 (owner/player id; 0xFFFF == none)
    u8   defKind;      // def +0 (reqCode tested against the 0x744180 mask)
    u8   defSecurity;  // def +583 (support weight added per supporter)
};

// The verdict the court scorer emits (the original writes a 21-record at a3).
struct CourtVerdict {
    bool    issued;     // true == a non-zero (54) verdict was produced
    int     category;   // verdict category (v24 + 1, 1..7)
    int     direction;  // verdict direction (v34: -1 / 0 / +1)
};

// Installable cross-cluster leaves for the court scorer + rating bars. Null
// members install inert defaults (defined in court_council2.cpp).
struct CourtCouncilHooks {
    // Number of live building/object slots to scan (default 0 == none). The
    // original scans 256 slots; supply the live ones here.
    int (*buildingSlotCount)() = nullptr;
    // Fetch slot `i` (0-based). Return false to skip. Default: no slots.
    bool (*buildingSlot)(int i, CourtBuildingSlot* out) = nullptr;
    // VIBE_AiNeeds_ComputeWeights(0x47936c) gate per category: non-zero ==
    // category participates. Default 0 (no category participates).
    int (*aiNeedsComputeWeights)(int category) = nullptr;
    // Per-category rating float read out of the AiNeeds scratch (v43[40*c+37]).
    // Default 1.0f (== the 1065353216 threshold, a neutral rating).
    float (*categoryRating)(int category) = nullptr;
    // VIBE_Amt_GetGuildEligibility(0x481bcc) on the accused: 1 == eligible.
    // Default 0 (not eligible -> the scorer returns 0 early).
    int (*guildEligibility)(Person* accused) = nullptr;
    // SetValueOrText widget write (used by the rating bars). Default returns 1.
    char (*setBarValue)(int widget, int value) = nullptr;
    // VIBE_Math_RandomModulo(4) tie-break for the ambiguous-verdict case. The
    // shipping game wires this to util::RandomModulo(4); the inert default
    // returns 0 (so the ambiguous case resolves to direction -1) keeping the
    // scorer deterministic for testing.
    int (*randomMod4)() = nullptr;
    // Rating-bar widget for entry slot i (== entries[i].barWidget). Default
    // returns the slot's own widget handle.
    // (No hook needed; read directly off the slot.)
};
void SetCourtCouncilHooks(const CourtCouncilHooks* hooks);
const CourtCouncilHooks& GetCourtCouncilHooks();

// ---------------------------------------------------------------------------
// gilde.exe 0x4747dc — VIBE_NpcAction_EvaluateCourtTrial
//   (__userpurge: al=a1, edx=accused a2, ecx=outVerdict a3, bl=a4, a5).
// Returns 0 immediately if a1 != 0, a4 != 0, or the accused is not guild-eligible.
// Otherwise tallies, over the live building slots whose def-kind is in the
// 0x744180 mask and whose category (MapActionToCategory of def-kind) is in [1,7]:
//   support[cat]  += def.security  when the slot owner == the accused's id
//   total[cat]    += def.security  (every qualifying slot)
//   topBuilding[cat] tracks the highest-security building (tie -> first)
// Per category (1..7) it clamps support/total to 15, and when total>0 and the
// AiNeeds gate fires computes a verdict score
//   score = (total + 1 - support) / total * categoryRating(cat)
//         * (16.0 - support) * 0.0666...
// then picks the worst-rated (v24) and best-rated (v28) categories. With both
// found and distinct, it decides a direction from the two categories' ratings vs
// 1.0 and support/total thresholds (an RNG break for the ambiguous case), and on
// a decision writes {category=v24+1, direction} into the verdict and returns 54.
// Returns 0 when no actionable verdict results.
char EvaluateCourtTrial(int a1, Person* accused, CourtVerdict* outVerdict,
                        int a4);

} // namespace guild::world
