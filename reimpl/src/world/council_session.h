#pragma once
// Council-voting CUTSCENE STATE MACHINE — the phase-sequencing shell of
// VIBE_Office_RunCouncilSession (gilde.exe 0x49dd8c, ~8.8 KB, 0x22b0 bytes).
//
// The original is a cutscene state machine that loads the council-chamber scene
// "Sitzung_Wohnsitz.ed3", selects one of two session MODES by voice bank
// (ABSETZUNG.sbf = remove-from-office vote, AMTSWAHL.sbf = election), then
// collects per-councillor votes (the _ABSETZEN_JA/_NEIN/_ENTHALTUNG scenes via
// VIBE_Office_BuildVotePanel / _AddVoteMarker), tallies them with the recovered
// VOTING rules core (council.{h,cpp}), and plays the result scene
// (_ABSETZEN_ERGEBNIS_ABGESETZT = removed / _NICHT_ABGESETZT = kept). For the
// AMTSWAHL mode it elects a winner (council.CouncilElectWinner) and shows the
// election form.
//
// This header recovers the FSM STRUCTURE 1:1: the phase enum, the mode branch,
// the per-councillor vote-collection loop, the tally phase calling the rules
// core, and the result phase. The scene playback / voice banks / GUI vote panel
// build are DEFERRED leaves (CouncilSessionLeaves).
//
// Phase map (recovered from the in-function string-ref ordering, see report):
//   kIntro        load "Sitzung_Wohnsitz.ed3"                     (0x49ddba)
//   kModeSelect   ABSETZUNG.sbf vs AMTSWAHL.sbf                   (0x49de5f / 0x49de6b)
//   kCollectVotes _ABSETZEN_KAMMERN + per-vote JA/NEIN/ENTHALTUNG (0x49eec6..0x49f03d)
//                 (also drives VIBE_Office_BuildVotePanel's 3 counters)
//   kTally        CouncilTallyRemoval / CouncilElectWinner        (rules core)
//   kResult       _ABSETZEN_ERGEBNIS_ABGESETZT / _NICHT_ABGESETZT (0x49f05b / 0x49f1d1)
//                 + apply relation deltas / office transfer (mock command hook)
//   kDone
#include "guild/common/types.h"
#include "world/council.h"     // CouncilVote, CouncilVoteTally, ElectionResult, rules

namespace guild::world {

// Session mode (selected by the voice bank at 0x49de5f / 0x49de6b).
enum class CouncilMode : int {
    kRemoval  = 0,  // ABSETZUNG.sbf  — remove-from-office vote
    kElection = 1,  // AMTSWAHL.sbf   — office election
};

enum class CouncilPhase : int {
    kIntro        = 0,  // LoadScene Sitzung_Wohnsitz.ed3
    kModeSelect   = 1,  // pick ABSETZUNG / AMTSWAHL voice bank
    kCollectVotes = 2,  // per-councillor vote scenes + vote-panel markers
    kTally        = 3,  // CouncilTallyRemoval / CouncilElectWinner
    kResult       = 4,  // result scene + relation deltas / transfer commit
    kDone         = 5,
};

// ===========================================================================
// Vote-panel counters — the deterministic substrate of VIBE_Office_BuildVotePanel
// (gilde.exe 0x49dc18). The panel keeps three running tallies in
// dword_11B4E48/4C/50 keyed by the vote code (0=yes/JA, 1=no/NEIN,
// 2=abstain/ENTHALTUNG); BuildVotePanel(init=1) clears them and renders the 3
// column headers (text 3861/3862/3863), then each AddVoteMarker/BuildVotePanel
// call bumps the column counter for the cast vote. The GUI add is the deferred
// leaf; the counters are pure logic we mirror here.
// ===========================================================================
struct CouncilVotePanel {
    int yes = 0;      // dword_11B4E48 (vote code 0)
    int no = 0;       // dword_11B4E4C (vote code 1)
    int abstain = 0;  // dword_11B4E50 (vote code 2)
    void Reset() { yes = no = abstain = 0; }                  // BuildVotePanel(init=1)
    void Mark(CouncilVote v);                                 // BuildVotePanel(code)
};

// ===========================================================================
// Council session setup.
// ===========================================================================
struct CouncilSetup {
    CouncilMode mode = CouncilMode::kRemoval;

    i32 sceneId = 0;          // cutscene slot id for the session
    int officeType = 0;       // office-table key being voted on
    i32 holderObj = -1;       // the targeted office holder (removal mode)

    // Removal-mode inputs: per-councillor votes + their object ids.
    const CouncilVote* votes = nullptr;
    const i32*         voterObjs = nullptr;
    int                voterCount = 0;

    // Election-mode inputs: per-voter ballots (candidate index, -1 = skip),
    // the candidate count, and the seeded RNG for tie-breaking.
    const int*  ballots = nullptr;
    int         ballotCount = 0;
    int         candidateCount = 0;
    const i32*  candidateObjs = nullptr;  // candidate person ids (winner -> obj)
    ElectionRng rng = nullptr;            // mirrors Cutscene RandInt
    void*       rngCtx = nullptr;
};

// ===========================================================================
// Deferred cutscene/voice/GUI leaves.
// ===========================================================================
struct CouncilSessionLeaves {
    void (*loadScene)(const char* scene, float scale, void* ctx) = nullptr;
    void (*playVoice)(const char* sbf, void* ctx) = nullptr;
    // A per-councillor vote scene label (_ABSETZEN_JA / _NEIN / _ENTHALTUNG) or
    // the chamber/result labels.
    void (*playScene)(const char* label, void* ctx) = nullptr;
    // VIBE_Office_BuildVotePanel(init) / _AddVoteMarker — GUI panel build.
    void (*buildVotePanel)(bool init, void* ctx) = nullptr;
    void* ctx = nullptr;
};

// ===========================================================================
// The session driver.
// ===========================================================================
struct CouncilSession {
    CouncilSetup     setup;
    CouncilPhase     phase = CouncilPhase::kIntro;
    CouncilVotePanel panel;             // mirrors the BuildVotePanel counters

    // Rules-core outputs:
    CouncilVoteTally tally;             // removal mode
    ElectionResult   election;          // election mode
    bool             removed = false;   // removal outcome
    i32              winnerObj = -1;    // election winner object id
    bool             finished = false;
};

void CouncilSessionInit(CouncilSession& s, const CouncilSetup& setup);

// Advance the FSM by one phase. Fires the (mock) leaves and, at kTally, calls
// the appropriate rules core; at kResult applies the mutations via the council
// command hooks (CouncilSetHooks, mock).
CouncilPhase CouncilSessionStep(CouncilSession& s,
                                const CouncilSessionLeaves& leaves);

// Run to kDone. Returns true if the holder was removed / a new holder installed.
bool CouncilSessionRun(CouncilSession& s, const CouncilSessionLeaves& leaves);

} // namespace guild::world
