#pragma once
// Council session — the VOTING rules core of the cutscene-driven
// VIBE_Office_RunCouncilSession (gilde.exe 0x49dd8c, ~8.8 KB). The original is a
// cutscene state machine (Sitzung_Wohnsitz.ed3, cutscenes\sitzungen\*.esc,
// AMTSWAHL.sbf / ABSETZUNG.sbf voice banks, the vote-marker GUI). That shell is
// DEFERRED (see the module report). What is recovered here, byte-for-byte, is the
// two deterministic decision cores the session runs:
//
//   1. ABSETZUNG (remove-from-office): each present councillor casts one of three
//      votes — JA (remove), NEIN (keep), ENTHALTUNG (abstain). The session tallies
//      yes/no/abstain and removes the holder iff yes > no. Each councillor's vote
//      then feeds a relation delta toward the deposed holder (different deltas for
//      the removed vs kept outcome).
//   2. AMTSWAHL (election): candidates are the present councillors whose held rank
//      is the next rank in the office book (Office_IsNextRankInCategory). Each
//      voter casts a ballot for a candidate index; the winner is the candidate
//      with the most ballots, ties broken by a seeded RNG pick among the leaders.
//
// Mutations (the office-table transfer, the relation deltas) route through a
// settable command hook (mock), matching amt.cpp.
#include "guild/common/types.h"

namespace guild::world {

// ===========================================================================
// ABSETZUNG — remove-from-office vote.
// ===========================================================================
// Vote codes (the dword_11AB094 panel result; also _ABSETZEN_* scene labels):
enum class CouncilVote : int {
    kRemove = 0,   // _ABSETZEN_JA      (yes, depose)
    kKeep   = 1,   // _ABSETZEN_NEIN    (no)
    kAbstain = 2,  // _ABSETZEN_ENTHALTUNG
};

struct CouncilVoteTally {
    int yes     = 0;   // v205 (remove)
    int no      = 0;   // v206 (keep)
    int abstain = 0;   // v207[0]
    bool removed = false; // outcome: yes > no  (original: NOT removed iff yes<=no)
};

// gilde.exe 0x49dd8c — tally the removal vote and decide the outcome.
//   ++*(&v205 + vote)  for each councillor;  removed iff v205 > v206.
// (The original keeps the holder when v205 <= v206, i.e. ties keep the holder.)
CouncilVoteTally CouncilTallyRemoval(const CouncilVote* votes, int count);

// Per-councillor relation delta toward the targeted holder, applied after the
// vote. Recovered from the QueueRequestCoord27 calls. The original's branch is
//   if ( v205 <= v206 )  // yes<=no  => holder KEPT
//        JA(0)-> -40, NEIN(1)-> +10, ABSTAIN(2)-> +5      (kept-branch deltas)
//   else                 // yes>no   => holder REMOVED
//        JA(0)-> -40, NEIN(1)-> +10, ABSTAIN(2)-> -20     (removed-branch deltas)
// (A deposed holder resents abstainers; a kept holder is neutral-positive to them.)
int CouncilRelationDelta(CouncilVote vote, bool removed);

// ===========================================================================
// AMTSWAHL — election (winner selection).
// ===========================================================================
// Each ballot names a candidate index (0..candidateCount-1) or -1 (no vote /
// not-applicable, the dword_11AB094 == -1 skip). Votes are tallied into per-
// candidate counters (v204[]). The winner is the highest-count candidate;
// equal-max counts set a tie flag and the winner is then chosen by a seeded RNG
// scan among the maxima (the VIBE_Cutscene_RandInt tiebreak).
struct ElectionResult {
    int winner    = -1;  // candidate index, or -1 if no candidates voted
    int winnerVotes = 0; // v167 (max count)
    bool tie      = false; // v233 (>=2 candidates share the max)
};

// gilde.exe 0x49dd8c — tally ballots into per-candidate counts and pick the
// winner. `ballots`/`ballotCount` are the cast votes (candidate indices, -1 to
// skip). `candidateCount` is the number of candidate slots (<= the counter
// array). `rng` is the seeded generator used only to break a tie; it advances
// exactly as the original's RandInt(candidateCount) tiebreak loop does.
//
// The original's winner scan (the v167/v169/v233 loop): walk candidates 0..N-1,
// track the running max count v167 and its index v169; on a strictly greater
// count, reset (v233=0); on an equal count, set v233=1 (tie). After the scan, if
// v233 (tie) or no leader, pick a starting index = RandInt(N) and advance forward
// (mod N) to the first candidate whose count equals the max.
using ElectionRng = u32 (*)(u32 range, void* ctx);  // mirrors Cutscene RandInt
ElectionResult CouncilElectWinner(const int* ballots, int ballotCount,
                                  int candidateCount,
                                  ElectionRng rng, void* rngCtx);

// ===========================================================================
// Mutation (command) hooks — mock.
// ===========================================================================
// Relation delta toward the holder (VIBE_Command_QueueRequestCoord27(voterObj,
// holderObj, delta)). Office transfer / new holder (VIBE_Office_AddTableEntry).
using CouncilRelationHook = void (*)(i32 voterObj, i32 holderObj, int delta,
                                     void* ctx);
using CouncilTransferHook = void (*)(int officeType, i32 newHolderObj, int mode,
                                     void* ctx);
void CouncilSetHooks(CouncilRelationHook relHook, CouncilTransferHook xferHook,
                     void* ctx);

// gilde.exe 0x49dd8c — apply the relation deltas for a removal vote (one hook
// call per councillor whose vote object differs from the holder). `voterObjs`
// are the voters' object ids; `holderObj` is the targeted holder.
void CouncilApplyRemovalRelations(const CouncilVote* votes,
                                  const i32* voterObjs, int count,
                                  i32 holderObj, bool removed);

} // namespace guild::world
