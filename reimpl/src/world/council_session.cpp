#include "world/council_session.h"

// Faithful 1:1 port of the PHASE-SEQUENCING shell of VIBE_Office_RunCouncilSession
// (gilde.exe 0x49dd8c). The deterministic vote tally / election winner-pick is
// delegated to council.{h,cpp}; here we reconstruct the cutscene state machine:
// intro -> mode select -> vote collection (per-councillor scenes + the
// BuildVotePanel counters) -> tally (rules core) -> result (scene + mutations).

namespace guild::world {

namespace {
constexpr const char* kSceneSitzung   = "Sitzung_Wohnsitz.ed3";
constexpr const char* kSbfAbsetzung   = "ABSETZUNG.sbf";
constexpr const char* kSbfAmtswahl    = "AMTSWAHL.sbf";
constexpr const char* kScKammern      = "_ABSETZEN_KAMMERN";
constexpr const char* kScJa           = "_ABSETZEN_JA";
constexpr const char* kScNein         = "_ABSETZEN_NEIN";
constexpr const char* kScEnthaltung   = "_ABSETZEN_ENTHALTUNG";
constexpr const char* kScErgAbgesetzt = "_ABSETZEN_ERGEBNIS_ABGESETZT";
constexpr const char* kScErgNicht     = "_ABSETZEN_ERGEBNIS_NICHT_ABGESETZT";

// The council scene scale (mirrors the trial's 1000.0f scene scale).
constexpr float kSitzungSceneScale = 1000.0f;

void FireLoad(const CouncilSessionLeaves& l, const char* scene, float scale) {
    if (l.loadScene) l.loadScene(scene, scale, l.ctx);
}
void FireVoice(const CouncilSessionLeaves& l, const char* sbf) {
    if (l.playVoice) l.playVoice(sbf, l.ctx);
}
void FireScene(const CouncilSessionLeaves& l, const char* label) {
    if (l.playScene) l.playScene(label, l.ctx);
}
void FirePanel(const CouncilSessionLeaves& l, bool init) {
    if (l.buildVotePanel) l.buildVotePanel(init, l.ctx);
}

const char* VoteScene(CouncilVote v) {
    switch (v) {
        case CouncilVote::kRemove:  return kScJa;
        case CouncilVote::kKeep:    return kScNein;
        case CouncilVote::kAbstain: return kScEnthaltung;
    }
    return kScNein;
}
} // namespace

// gilde.exe 0x49dc18 — BuildVotePanel marker bump (the dword_11B4E48/4C/50
// counters keyed by vote code 0/1/2).
void CouncilVotePanel::Mark(CouncilVote v) {
    switch (v) {
        case CouncilVote::kRemove:  ++yes;     break;  // a3==0 -> dword_11B4E48
        case CouncilVote::kKeep:    ++no;      break;  // a3==1 -> dword_11B4E4C
        case CouncilVote::kAbstain: ++abstain; break;  // a3==2 -> dword_11B4E50
    }
}

void CouncilSessionInit(CouncilSession& s, const CouncilSetup& setup) {
    s = CouncilSession{};
    s.setup = setup;
    s.phase = CouncilPhase::kIntro;
}

// gilde.exe 0x49dd8c — execute one phase and advance the FSM cursor.
CouncilPhase CouncilSessionStep(CouncilSession& s,
                                const CouncilSessionLeaves& leaves) {
    const CouncilPhase executing = s.phase;
    const CouncilSetup& st = s.setup;

    switch (s.phase) {
    // --- kIntro (0x49ddba): load the council-chamber scene.
    case CouncilPhase::kIntro:
        FireLoad(leaves, kSceneSitzung, kSitzungSceneScale);
        s.phase = CouncilPhase::kModeSelect;
        break;

    // --- kModeSelect (0x49de5f / 0x49de6b): pick the session-mode voice bank.
    case CouncilPhase::kModeSelect:
        FireVoice(leaves, st.mode == CouncilMode::kRemoval ? kSbfAbsetzung
                                                           : kSbfAmtswahl);
        s.phase = CouncilPhase::kCollectVotes;
        break;

    // --- kCollectVotes (0x49eec6..0x49f03d): the chamber scene, then for each
    //     councillor play the per-vote scene and bump the vote-panel column.
    //     (For election mode the ballots feed the panel as a generic cast.)
    case CouncilPhase::kCollectVotes:
        FireScene(leaves, kScKammern);
        FirePanel(leaves, /*init*/ true);     // BuildVotePanel(init=1): reset+headers
        s.panel.Reset();
        if (st.mode == CouncilMode::kRemoval && st.votes) {
            for (int i = 0; i < st.voterCount; ++i) {
                FireScene(leaves, VoteScene(st.votes[i]));
                FirePanel(leaves, /*init*/ false);   // AddVoteMarker for the column
                s.panel.Mark(st.votes[i]);
            }
        }
        s.phase = CouncilPhase::kTally;
        break;

    // --- kTally: run the rules core for the active mode.
    case CouncilPhase::kTally:
        if (st.mode == CouncilMode::kRemoval) {
            s.tally   = CouncilTallyRemoval(st.votes, st.voterCount);
            s.removed = s.tally.removed;
        } else {
            s.election = CouncilElectWinner(st.ballots, st.ballotCount,
                                            st.candidateCount, st.rng, st.rngCtx);
            if (s.election.winner >= 0 && st.candidateObjs)
                s.winnerObj = st.candidateObjs[s.election.winner];
        }
        s.phase = CouncilPhase::kResult;
        break;

    // --- kResult (0x49f05b / 0x49f1d1): play the result scene and commit the
    //     mutations through the council command hooks (mock).
    case CouncilPhase::kResult:
        if (st.mode == CouncilMode::kRemoval) {
            FireScene(leaves, s.removed ? kScErgAbgesetzt : kScErgNicht);
            // Per-councillor relation deltas toward the holder (mock hook).
            CouncilApplyRemovalRelations(st.votes, st.voterObjs, st.voterCount,
                                         st.holderObj, s.removed);
            // (The office-table transfer on removal routes through the transfer
            //  hook in the original; left to the caller's installed hook.)
        } else {
            // Election: the new-holder install is the transfer hook's job; we
            // record the winner. The winner-announce scene reuses the result
            // label (the original plays the AMTSWAHL result through the same
            // ERGEBNIS path).
            FireScene(leaves, kScErgAbgesetzt);
        }
        s.phase    = CouncilPhase::kDone;
        s.finished = true;
        break;

    case CouncilPhase::kDone:
        s.finished = true;
        break;
    }
    return executing;
}

// gilde.exe 0x49dd8c — run the whole session inline.
bool CouncilSessionRun(CouncilSession& s, const CouncilSessionLeaves& leaves) {
    int guard = 64;
    while (s.phase != CouncilPhase::kDone && guard-- > 0)
        CouncilSessionStep(s, leaves);
    CouncilSessionStep(s, leaves);  // kDone
    return s.setup.mode == CouncilMode::kRemoval ? s.removed
                                                 : (s.winnerObj >= 0);
}

} // namespace guild::world
