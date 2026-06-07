#pragma once
// Election / vote FORM state machine — the phase-sequencing shell wrapping the
// election & vote GUI forms of the council/office system:
//   VIBE_Office_BuildElectionForm     (gilde.exe 0x4a0610, ~2.2 KB)
//   VIBE_Office_BuildVotePanel        (gilde.exe 0x49dc18)
//   VIBE_Office_AddVoteMarker         (gilde.exe 0x49dbe8)
//   VIBE_Office_BuildTortureChoiceForm(gilde.exe 0x4a3dc8, ~2.6 KB)
//   VIBE_Office_BuildSuccessorDialogA (gilde.exe 0x4a003c)
//   VIBE_Office_BuildSuccessorDialogB (gilde.exe 0x4a01a4)
//
// These forms are GUI/cutscene LEAVES (they build window objects, speech packets
// and send entity messages — VIBE_Object_AddToWindow / VIBE_Text_RenderFormatted
// Message / VIBE_Cutscene_BuildSpeechPacket / VIBE_He_SendEntityMessage). Their
// FULL bodies are DEFERRED (listed in the report). What is recovered here 1:1 is
// the deterministic substrate they sit on and the form FSM that sequences them:
//
//   * The vote-panel column tally (BuildVotePanel's dword_11B4E48/4C/50 counters,
//     and AddVoteMarker's per-marker column-index bump at +44 of the form rec).
//   * The election-form FSM phases: open -> collect ballots (vote-panel markers)
//     -> tally winner (CouncilElectWinner) -> announce.
//   * The torture/successor choice forms appear as deferred build leaves invoked
//     at the appropriate phase.
//
// The form FSM is intentionally small and testable: it threads the vote tally
// through the panel counters and the council election rules core, mocking the
// window-build leaves so the call sequence + final tally are verifiable.
#include "guild/common/types.h"
#include "world/council.h"     // CouncilElectWinner, ElectionResult, ElectionRng

namespace guild::world {

// ===========================================================================
// AddVoteMarker geometry — recovered byte-for-byte from VIBE_Office_AddVoteMarker
// (gilde.exe 0x49dbe8):
//   VIBE_Object_AddToWindow(win, 68 - 10 * markerCount, 140, 1162);
//   ++markerCount;     // the *(_WORD*)(formRec + 44) field
// The icon resource id is 1162; columns start at x=68 and step left by 10.
// ===========================================================================
constexpr int kVoteMarkerIcon   = 1162;  // marker object resource id
constexpr int kVoteMarkerBaseX  = 68;    // first marker x
constexpr int kVoteMarkerStepX  = 10;    // per-marker x step (leftward)
constexpr int kVoteMarkerY       = 140;  // marker y

// gilde.exe 0x49dbe8 — the marker x for the Nth marker (68 - 10*n).
inline int VoteMarkerX(int markerCount) {
    return kVoteMarkerBaseX - kVoteMarkerStepX * markerCount;
}

// ===========================================================================
// Vote-panel column tally — VIBE_Office_BuildVotePanel (gilde.exe 0x49dc18).
// Three column counters keyed by vote code (0 yes / 1 no / 2 abstain), the
// header text ids (3861/3862/3863), and column marker geometry
// (x = {32, 62, 47}, y = 10*count + 80, icon 1162).
// ===========================================================================
constexpr int kVotePanelHeaderTextId[3] = {3861, 3862, 3863};  // yes/no/abstain
constexpr int kVotePanelColumnX[3]       = {32, 62, 47};         // yes/no/abstain x
constexpr int kVotePanelMarkerIcon       = 1162;
inline int VotePanelMarkerY(int countInColumn) { return 10 * countInColumn + 80; }

struct VotePanel {
    int count[3] = {0, 0, 0};  // dword_11B4E48 (yes) / 4C (no) / 50 (abstain)
    // gilde.exe 0x49dc18 (a1 != 0): reset all three columns + render headers.
    void Init() { count[0] = count[1] = count[2] = 0; }
    // gilde.exe 0x49dc18 (a1 == 0): add a marker in column `code` (0..2),
    // returning the (x, y) of the placed marker; bumps the column counter.
    // `code` clamps out-of-range to no effect (the original only handles 0/1/2).
    bool Mark(int code, int* outX, int* outY);
};

// ===========================================================================
// Election-form FSM.
// ===========================================================================
enum class ElectionFormPhase : int {
    kOpen       = 0,  // BuildElectionForm: build candidate speech packets (leaf)
    kCollect    = 1,  // per-ballot vote markers (BuildVotePanel / AddVoteMarker)
    kTally      = 2,  // CouncilElectWinner over the cast ballots
    kAnnounce   = 3,  // announce winner (+ optional successor dialog leaf)
    kDone       = 4,
};

struct ElectionFormSetup {
    const int*  ballots = nullptr;     // per-voter candidate index (-1 = skip)
    int         ballotCount = 0;
    int         candidateCount = 0;     // candidate slots (<= panel columns)
    const i32*  candidateObjs = nullptr;// candidate person ids
    ElectionRng rng = nullptr;          // tie-break RNG
    void*       rngCtx = nullptr;
    bool        offerSuccessor = false; // route to BuildSuccessorDialog leaf?
};

// Deferred GUI/cutscene leaves for the election forms.
struct ElectionFormLeaves {
    // VIBE_Office_BuildElectionForm — build candidate speech packets.
    void (*buildElectionForm)(void* ctx) = nullptr;
    // VIBE_Office_AddVoteMarker — place one ballot marker (x derived from count).
    void (*addVoteMarker)(int x, int y, int icon, void* ctx) = nullptr;
    // VIBE_Office_BuildSuccessorDialogA / B — successor-choice dialogs.
    void (*buildSuccessorDialog)(char which, void* ctx) = nullptr;
    void* ctx = nullptr;
};

struct ElectionForm {
    ElectionFormSetup setup;
    ElectionFormPhase phase = ElectionFormPhase::kOpen;
    VotePanel         panel;
    ElectionResult    result;
    i32               winnerObj = -1;
    bool              finished = false;
};

void ElectionFormInit(ElectionForm& f, const ElectionFormSetup& setup);

// Advance the form FSM by one phase; fires the (mock) leaves.
ElectionFormPhase ElectionFormStep(ElectionForm& f,
                                   const ElectionFormLeaves& leaves);

// Run to kDone; returns the winning candidate object id (-1 if none).
i32 ElectionFormRun(ElectionForm& f, const ElectionFormLeaves& leaves);

} // namespace guild::world
