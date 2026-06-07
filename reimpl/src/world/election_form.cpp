#include "world/election_form.h"

// Faithful 1:1 port of the election/vote FORM state machine wrapping the
// VIBE_Office_Build{ElectionForm,VotePanel,TortureChoiceForm,SuccessorDialogA/B}
// and _AddVoteMarker GUI leaves (gilde.exe 0x4a0610 / 0x49dc18 / 0x49dbe8 /
// 0x4a3dc8 / 0x4a003c / 0x4a01a4). The window-build bodies are deferred; the
// vote-panel column tally and the form phase sequence are recovered here.

namespace guild::world {

// gilde.exe 0x49dc18 (a1 == 0 path) — place a marker in column `code` and bump
// the column counter (the dword_11B4E48/4C/50 increments).
bool VotePanel::Mark(int code, int* outX, int* outY) {
    if (code < 0 || code > 2)
        return false;
    int x = kVotePanelColumnX[code];
    int y = VotePanelMarkerY(count[code]);
    ++count[code];
    if (outX) *outX = x;
    if (outY) *outY = y;
    return true;
}

namespace {
void FireBuild(const ElectionFormLeaves& l) {
    if (l.buildElectionForm) l.buildElectionForm(l.ctx);
}
void FireMarker(const ElectionFormLeaves& l, int x, int y) {
    if (l.addVoteMarker) l.addVoteMarker(x, y, kVoteMarkerIcon, l.ctx);
}
void FireSuccessor(const ElectionFormLeaves& l, char which) {
    if (l.buildSuccessorDialog) l.buildSuccessorDialog(which, l.ctx);
}
} // namespace

void ElectionFormInit(ElectionForm& f, const ElectionFormSetup& setup) {
    f = ElectionForm{};
    f.setup = setup;
    f.phase = ElectionFormPhase::kOpen;
}

// gilde.exe — advance the election-form FSM by one phase.
ElectionFormPhase ElectionFormStep(ElectionForm& f,
                                   const ElectionFormLeaves& leaves) {
    const ElectionFormPhase executing = f.phase;
    const ElectionFormSetup& st = f.setup;

    switch (f.phase) {
    // --- kOpen (0x4a0610): build the candidate speech packets (deferred leaf).
    case ElectionFormPhase::kOpen:
        FireBuild(leaves);
        f.panel.Init();      // BuildVotePanel(init=1) clears the columns
        f.phase = ElectionFormPhase::kCollect;
        break;

    // --- kCollect (0x49dbe8): for each cast ballot, place a vote marker. The
    //     marker x steps left by 10 per marker (AddVoteMarker geometry).
    case ElectionFormPhase::kCollect: {
        int markerCount = 0;
        if (st.ballots) {
            for (int i = 0; i < st.ballotCount; ++i) {
                if (st.ballots[i] < 0)
                    continue;                    // dword_11AB094 == -1 skip
                FireMarker(leaves, VoteMarkerX(markerCount), kVoteMarkerY);
                ++markerCount;
            }
        }
        f.phase = ElectionFormPhase::kTally;
        break;
    }

    // --- kTally: pick the winner via the council election rules core.
    case ElectionFormPhase::kTally:
        f.result = CouncilElectWinner(st.ballots, st.ballotCount,
                                      st.candidateCount, st.rng, st.rngCtx);
        if (f.result.winner >= 0 && st.candidateObjs)
            f.winnerObj = st.candidateObjs[f.result.winner];
        f.phase = ElectionFormPhase::kAnnounce;
        break;

    // --- kAnnounce: optionally route to a successor-choice dialog (deferred).
    case ElectionFormPhase::kAnnounce:
        if (st.offerSuccessor)
            FireSuccessor(leaves, 'A');   // BuildSuccessorDialogA (0x4a003c)
        f.phase    = ElectionFormPhase::kDone;
        f.finished = true;
        break;

    case ElectionFormPhase::kDone:
        f.finished = true;
        break;
    }
    return executing;
}

i32 ElectionFormRun(ElectionForm& f, const ElectionFormLeaves& leaves) {
    int guard = 32;
    while (f.phase != ElectionFormPhase::kDone && guard-- > 0)
        ElectionFormStep(f, leaves);
    ElectionFormStep(f, leaves);  // kDone
    return f.winnerObj;
}

} // namespace guild::world
