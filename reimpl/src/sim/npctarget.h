#pragma once
// NpcTarget — the NPC combat/movement target-selection spatial queries for the
// Guild simulation (gilde.exe, VIBE_NpcTarget_* family). These feed the combat /
// office-succession behaviour: given an NPC "person" record they pick an enemy to
// attack, or a city/office "direction" to move toward, by scoring candidate
// persons with VIBE_Ai_ComputePersonFavorability and shuffling candidate slots
// through the CRT RNG (VIBE_Math_RandomModulo) exactly as the originals do.
//
// The control flow, the RNG draws (count + order), the office-category derivation
// (BYTE2(officeDef)-1)/3, the candidate-slot permutations, and the float scoring
// thresholds are translated 1:1. The leaf calls that cross into the Office / Ai /
// ObjectSearch / Person-array clusters are routed through NpcTargetHooks so the
// queries are exercisable against a synthetic scene in isolation.
//
// Recovered float constants (0x61A680 block, IEEE-754):
//   flt_61A680 = 8.0    flt_61A684 = 52.0   (FindNearestEnemy reject curve)
//   flt_61A688 = 66.0   (PickDirectionSeqA accept threshold, "<")
//   flt_61A68C = 33.0   (PickDirectionSeqB accept threshold, ">")
#include "guild/common/types.h"

namespace guild::sim {

// ---------------------------------------------------------------------------
// An opaque "person record" handle. The originals pass a raw `__int16*`/`_BYTE*`
// into the 768-slot Person array (word_12CE910, stride 536). This module never
// dereferences it directly; it reads the few fields it needs (the kind/id at
// +0/+4, the office-category bytes at +358..+360) through the hooks below, which
// the host backs with the real Person-array accessors and tests back with a mock.
// ---------------------------------------------------------------------------
using PersonHandle = void*;

// The action descriptor the combat dispatcher fills in (the 24-byte result frame
// the AiMethod planner consumes). Only the four fields the originals write are
// modeled; the rest of the frame is left to the caller.
//   verb   : action verb byte (+0): 7 = attack person, 22 = move toward direction.
//   target : target id / direction value (+4 dword).
//   mode   : mode/category dword (+16): 1 = attack target, 4 = move direction.
struct NpcActionDesc {
    u8  verb;     // +0
    i32 target;   // +4
    i32 mode;     // +16
};

// ---------------------------------------------------------------------------
// Leaf hooks — the Office / Ai / ObjectSearch leaves the queries call. Tests
// install a recording/synthetic mock; nullptr installs an inert default (queries
// behave as "no candidates / no office", i.e. the functions return 0 / null).
// ---------------------------------------------------------------------------
struct NpcTargetHooks {
    // VIBE_Person record field reads (raw byte offsets off the record base).
    //   personKind: *(_BYTE*)(p+0)  — the Person kind/marker low byte.
    //   personId:   *(_DWORD*)(p+1) (== record id used by Person_FindRecordById).
    //   officeCat{A,B,C}: *(_BYTE*)(p+358/+359/+360) office-category selectors.
    u8  (*personKind)(PersonHandle p);
    i32 (*personId)(PersonHandle p);
    u8  (*officeCatA)(PersonHandle p);
    u8  (*officeCatB)(PersonHandle p);
    u8  (*officeCatC)(PersonHandle p);
    //   subMethod:  *(_BYTE*)(p+361) — combat/move kind (30/31/32/33).
    u8  (*subMethod)(PersonHandle p);

    // VIBE_Office_GetDefinition(catId, outDef[3]) — fills a 3-dword office def;
    // returns nonzero on success. The behaviour only needs BYTE2(outDef[0]) (the
    // office "rank/level" byte) and, for FindNearestEnemy, the same field of the
    // holder entry. We expose just that byte to keep the model small.
    //   returns: 0 == no such office; else the rank byte (BYTE2 of def word 0).
    //   *ok set to true on success.
    u8  (*officeRank)(u8 catId, bool* ok);

    // VIBE_Office_GetHolderEntryByCity(person, catId) — does `person` hold office
    // `catId` in its city? Returns the holder's rank byte and sets *ok; the
    // original gates on (rank > 1) to skip high officials.
    u8  (*officeHolderRank)(PersonHandle person, u8 catId, bool* ok);

    // VIBE_Office_CollectSuccessorCandidates(catId, max, out[]) — fills out[] with
    // up to `max` candidate PersonHandles; returns the count.
    int (*collectSuccessors)(u8 catId, int max, PersonHandle* out);

    // VIBE_Office_CollectByCategoryResolved(catId, max, out[]) — fills out[] with
    // up to `max` candidate person ids (dwords; -1 == empty); returns the count.
    int (*collectByCategory)(u8 catId, int max, i32* outIds);

    // VIBE_Person_FindRecordById(id) — id -> PersonHandle (null if absent).
    PersonHandle (*findPersonById)(i32 id);

    // VIBE_Ai_ComputePersonFavorability(aKind, bKind, mode) — the scalar score the
    // selection minimizes/averages. The originals pass the two persons' kind words
    // (*(_WORD*)record) and a mode flag (always 1 here).
    double (*favorability)(u16 aKind, u16 bKind, int mode);

    // VIBE_ObjectSearch_FindMatchingColors — the fallback nearest-visible-enemy
    // scan FindNearestEnemy uses when no office candidate wins. Returns nonzero and
    // writes the winning person *array index* into *outIndex; 0 == none found.
    int (*findNearestVisible)(PersonHandle origin, double minR, double maxR,
                              i32* outIndex);

    // word_12CE910 + 268*index — resolve a Person array index to a PersonHandle
    // (the originals index the raw person array by 268 words == 536 bytes).
    PersonHandle (*personByIndex)(i32 index);
};

void SetNpcTargetHooks(const NpcTargetHooks* hooks);
const NpcTargetHooks& GetNpcTargetHooks();

// ===========================================================================
// Translated queries.
// ===========================================================================

// gilde.exe 0x474dd4 — VIBE_NpcTarget_FindNearestEnemy(person@eax).
//   If the NPC holds no office of category A (+358 == 0): walk the office-holder
//   ring, keep the candidate with the lowest favorability (an "enemy"); the
//   ObjectSearch fallback below covers the no-office-candidate case. Otherwise
//   collect up to 5 succession candidates for office (+360), keep the lowest, and
//   reject it if (count*8 + 52) < bestScore. Either way, if nothing was chosen,
//   fall back to the nearest *visible* enemy within [0,25]. Returns the chosen
//   PersonHandle, or null.
PersonHandle NpcTarget_FindNearestEnemy(PersonHandle person);

// gilde.exe 0x474fe8 — VIBE_NpcTarget_PickDirectionSeqA(person@eax, seed@dl).
// gilde.exe 0x475274 — VIBE_NpcTarget_PickDirectionSeqB(person@eax, seed@dl).
//   Build a 1..6 candidate-direction array, optionally permute it by the office
//   "tier" ((rank-1)/3 == 1 -> rotate-by-4, == 2 -> reverse), shuffle the two
//   halves with 6 rounds of RandomModulo(3) swaps, then for each of the first 5
//   directions collect the office candidates for that category and average their
//   favorability. SeqA accepts the first direction whose average is < 66.0; SeqB
//   accepts the first whose *pairwise* average is > 33.0. Returns the chosen
//   direction byte (1..6), or 0 if none qualifies / no office.
u8 NpcTarget_PickDirectionSeqA(PersonHandle person, u8 seed);
u8 NpcTarget_PickDirectionSeqB(PersonHandle person, u8 seed);

// gilde.exe 0x475638 — VIBE_NpcTarget_EvalCombatOrMoveAction.
//   The combat/move action selector. Gated on (!busy && !blocked && guildRank==1).
//   Branches on the NPC sub-method byte (+361):
//     30        -> FindNearestEnemy; on hit emit verb 7 (attack), mode 1, returns 56.
//     32        -> PickDirectionSeqA; on hit emit verb 22 (move), mode 4, returns 56.
//     31 / 33   -> PickDirectionSeqB; same move emit.
//   `busy`/`blocked` are the two byte flags the caller passes (al / bl); `rank` is
//   the guild-rank level (the original calls VIBE_Amt_CheckGuildRankLevel3). On any
//   reject path returns 0; on success fills *out and returns 56.
int NpcTarget_EvalCombatOrMoveAction(PersonHandle person, bool busy, bool blocked,
                                     int guildRank, NpcActionDesc* out);

} // namespace guild::sim
