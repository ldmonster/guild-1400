#include "world/trial_session.h"

// Faithful 1:1 port of the PHASE-SEQUENCING shell of VIBE_Office_RunCourtTrial
// (gilde.exe 0x4a0eb8). The deterministic verdict/scoring is delegated to the
// already-recovered trial.{h,cpp} rules core; here we reconstruct the cutscene
// state-machine structure: the phase order, the per-phase transitions and the
// per-phase rules-core call + cutscene/voice/GUI invocation points (deferred
// leaves, fired through TrialSessionLeaves so the call sequence is testable).

namespace guild::world {

// ---------------------------------------------------------------------------
// gilde.exe @0x49D658 — torture-instrument .esc scene table (16-byte stride,
// 7 entries). Recovered via get_string.
// ---------------------------------------------------------------------------
const char* const kTrialTortureEsc[kTrialTortureInstrumentCount] = {
    "dschraube.esc",   // +0x00  Daumenschraube (thumbscrew)
    "stiefel.esc",     // +0x10  Spanish boot
    "peitsche.esc",    // +0x20  whip
    "brandeisen.esc",  // +0x30  branding iron
    "eistropfer.esc",  // +0x40  ice-drip
    "kaefig.esc",      // +0x50  cage
    "streckbank.esc",  // +0x60  rack
};

// Phase voice banks (the PROZESS_* .sbf banks), in the order the original plays
// them. Recovered via get_string from the data refs inside the function.
namespace {
constexpr const char* kSceneGericht       = "Gericht.ed3";
constexpr const char* kSbfAccusation      = "PROZESS_2_VORWURF_KOMMENTARE.sbf";
constexpr const char* kSbfGuilty3         = "PROZESS_3_SCHULDIG.sbf";
constexpr const char* kSbfNotGuilty3      = "PROZESS_3_NICHT_SCHULDIG.sbf";
constexpr const char* kSbfFolter1         = "PROZESS_4_FOLTER_1.sbf";
constexpr const char* kSbfFolter2Fmt      = "PROZESS_5_FOLTER2_%s.sbf";
constexpr const char* kSbfFolterErgebnis  = "PROZESS_6_FOLTERERGEBNIS.sbf";
constexpr const char* kSbfAnnounce        = "PROZESS_6_ANKUENDIGUNG.sbf";
constexpr const char* kSbfVoteResults     = "PROZESS_6_ABSTIMMUNG_ERGEBNISSE.sbf";
constexpr const char* kSbfGuilty7         = "PROZESS_7_SCHULDIG.sbf";
constexpr const char* kSbfPenalty7        = "PROZESS_7_STRAFEN.sbf";
constexpr const char* kSbfNotGuilty7      = "PROZESS_7_NICHT_SCHULDIG.sbf";
constexpr const char* kSbfClose8          = "PROZESS_8.sbf";

// The locally-built cutscene slot ready-time / scene scale (447A0000h == 1000.0f
// loaded into esi at 0x4a0f86 and passed to LoadScene).
constexpr float kGerichtSceneScale = 1000.0f;

void FireLoad(const TrialSessionLeaves& l, const char* scene, float scale) {
    if (l.loadScene) l.loadScene(scene, scale, l.ctx);
}
void FireVoice(const TrialSessionLeaves& l, const char* sbf) {
    if (l.playVoice) l.playVoice(sbf, l.ctx);
}
void FireTorture(const TrialSessionLeaves& l, const char* esc) {
    if (l.playTortureEsc) l.playTortureEsc(esc, l.ctx);
}
void FireForm(const TrialSessionLeaves& l, const char* which) {
    if (l.buildForm) l.buildForm(which, l.ctx);
}
} // namespace

// gilde.exe 0x4a0eb8 (prologue) — initialize a trial session.
void TrialSessionInit(TrialSession& s, const TrialSetup& setup,
                      TrialLawLookup lawLookup, void* lawCtx) {
    s = TrialSession{};
    s.setup     = setup;
    s.phase     = TrialPhase::kIntro;
    s.lawLookup = lawLookup;
    s.lawCtx    = lawCtx;
}

// gilde.exe 0x4a0eb8 — execute one phase and advance the FSM cursor.
TrialPhase TrialSessionStep(TrialSession& s, const TrialSessionLeaves& leaves) {
    const TrialPhase executing = s.phase;
    const TrialSetup& st = s.setup;

    switch (s.phase) {
    // --- kIntro (0x4a0f81): load the courtroom scene, build the cutscene slot.
    case TrialPhase::kIntro:
        FireLoad(leaves, kSceneGericht, kGerichtSceneScale);
        s.phase = TrialPhase::kAccusation;
        break;

    // --- kAccusation (0x4a1853): present evidence; the rules core sums the
    //     evidence score (penalty * wanted-weight) over the collected crimes.
    case TrialPhase::kAccusation:
        s.score = TrialComputeEvidenceScore(st.evidence, st.evidenceCount,
                                            st.maxWantedLevel,
                                            s.lawLookup, s.lawCtx);
        s.runningScore = s.score.score;
        FireVoice(leaves, kSbfAccusation);
        s.phase = TrialPhase::kJuryVerdict;
        break;

    // --- kJuryVerdict (0x4a1d3b / 0x4a1de4): tally juror votes -> the
    //     SCHULDIG / NICHT_SCHULDIG branch (voice bank picked by verdict).
    case TrialPhase::kJuryVerdict:
        s.verdict = TrialTallyVerdict(st.juryVotes, st.juryCount, &s.voteTotal);
        FireVoice(leaves, s.verdict == TrialVerdict::kConvicted ? kSbfGuilty3
                                                                : kSbfNotGuilty3);
        // Torture is only reached on the convicted path that elects to torture.
        if (s.verdict == TrialVerdict::kConvicted && st.torture)
            s.phase = TrialPhase::kTorture;
        else
            s.phase = TrialPhase::kVoteAnnounce;
        break;

    // --- kTorture (0x4a21ff / 0x4a2392 / 0x4a2e2b): play the torture intro, the
    //     chosen instrument .esc, then the result; scale the running score by
    //     the confess/deny multiplier. The choice GUI is a deferred leaf.
    case TrialPhase::kTorture: {
        FireForm(leaves, "BuildTortureChoiceForm");      // 0x4a3dc8 (deferred)
        FireVoice(leaves, kSbfFolter1);
        int inst = st.tortureInstrument;
        if (inst < 0) inst = 0;
        if (inst >= kTrialTortureInstrumentCount)
            inst = kTrialTortureInstrumentCount - 1;
        FireTorture(leaves, kTrialTortureEsc[inst]);     // PROZESS_5_FOLTER2_%s
        FireVoice(leaves, kSbfFolter2Fmt);
        s.runningScore = TrialApplyTortureFine(s.runningScore,
                                               st.tortureConfessed);
        FireVoice(leaves, kSbfFolterErgebnis);
        s.phase = TrialPhase::kVoteAnnounce;
        break;
    }

    // --- kVoteAnnounce (0x4a2ec4 / 0x4a3346): announce + play the vote-result
    //     voice (the ABSTIMMUNG_ERGEBNISSE bank). No rules-core mutation.
    case TrialPhase::kVoteAnnounce:
        FireForm(leaves, "BuildElectionForm");           // 0x4a0610 (deferred)
        FireVoice(leaves, kSbfAnnounce);
        FireVoice(leaves, kSbfVoteResults);
        s.phase = TrialPhase::kSentence;
        break;

    // --- kSentence (0x4a34cd / _STRAFEN / _NICHT_SCHULDIG / PROZESS_8):
    //     convicted -> favorability-weighted guilty fine + three-way commit;
    //     acquitted -> the NICHT_SCHULDIG voice, no fine. Always close (PROZESS_8).
    case TrialPhase::kSentence:
        if (s.verdict == TrialVerdict::kConvicted) {
            FireVoice(leaves, kSbfGuilty7);
            s.runningScore = TrialApplyGuiltyFine(s.runningScore,
                                                  st.judgeFavorability);
            s.perSeatFine = TrialCommitFine(st.defendantObj, st.judgeObj,
                                            st.assessorAObj, st.assessorBObj,
                                            static_cast<i32>(s.runningScore),
                                            st.currency);
            FireVoice(leaves, kSbfPenalty7);
        } else {
            FireVoice(leaves, kSbfNotGuilty7);
        }
        FireVoice(leaves, kSbfClose8);                   // PROZESS_8 closing
        s.phase    = TrialPhase::kDone;
        s.finished = true;
        break;

    case TrialPhase::kDone:
        s.finished = true;
        break;
    }
    return executing;
}

// gilde.exe 0x4a0eb8 — run the whole trial inline (the original is straight-line).
TrialVerdict TrialSessionRun(TrialSession& s, const TrialSessionLeaves& leaves) {
    int guard = 64;  // the FSM is bounded; guard against a stuck state.
    while (s.phase != TrialPhase::kDone && guard-- > 0)
        TrialSessionStep(s, leaves);
    TrialSessionStep(s, leaves);  // execute kDone (sets finished).
    return s.verdict;
}

} // namespace guild::world
