#include "world/trial_session.h"

// Faithful 1:1 port of the PHASE-SEQUENCING shell of VIBE_Office_RunCourtTrial
// (gilde.exe 0x4a0eb8). The deterministic verdict/scoring is delegated to the
// already-recovered trial.{h,cpp} rules core; here we reconstruct the cutscene
// state-machine structure: the phase order, the per-phase transitions and the
// per-phase rules-core call + cutscene/voice/GUI invocation points (deferred
// leaves, fired through TrialSessionLeaves so the call sequence is testable).
//
// Phase semantics verified against the binary (hardening pass):
//   * PROZESS_3 branches on the DEFENDANT's panel value == 1 (the plea):
//     `mov ecx, dword_11AB094[edx]` @0x4a1adb, `cmp ecx,1` @0x4a1b24. The
//     favorability fine-adjust (flt_61CCF4/61CCF8) runs INSIDE the plea==guilty
//     branch (fmul @0x4a1b3e), not at sentencing.
//   * Torture is entered from the plea!=guilty branch when the torturer person
//     (ctx+72) resolves AND the evidence count > 2 (`if (v327 && v314 > 2)
//     v279[0] = 1`).
//   * When torture proceeds, the wealth-score-B fine (v292/3, three
//     QueueRequest16) is committed immediately, BEFORE the instrument scene.
//   * The jury tally (v313 = sum of the three panel rows; >= 2 -> acquit)
//     happens at PROZESS_6_ABSTIMMUNG, and the PROZESS_7 sentence fine uses
//     wealth-score-A (v291/3) — NOT the evidence score.

namespace guild::world {

// ---------------------------------------------------------------------------
// gilde.exe @0x49D658 — torture-instrument .esc scene table (16-byte stride,
// 7 entries). Byte-verified via get_bytes (hardening pass).
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

    // --- kAccusation (PROZESS_2 VORWURF): present evidence; the rules core sums
    //     the evidence score (penalty * wanted-weight) over the collected crimes
    //     (the v318 accumulation loop; v280 = v318 @0x4a15f6 seeds the running
    //     score).
    case TrialPhase::kAccusation:
        s.score = TrialComputeEvidenceScore(st.evidence, st.evidenceCount,
                                            st.maxWantedLevel,
                                            s.lawLookup, s.lawCtx);
        s.runningScore = s.score.score;
        FireVoice(leaves, kSbfAccusation);
        s.phase = TrialPhase::kPlea;
        break;

    // --- kPlea (PROZESS_3): the DEFENDANT's plea, read from his panel row
    //     (dword_11AB094[215*row] @0x4a1adb; `cmp ecx,1` @0x4a1b27).
    //     Guilty plea  -> PROZESS_3_SCHULDIG + the favorability fine-adjust
    //                     (v280 = v318 - fav*(v318*0.2f)*0.01f @0x4a1b3e..6e).
    //     Not guilty   -> PROZESS_3_NICHT_SCHULDIG; torture is entered only
    //                     when the torturer (ctx+72) resolves and the evidence
    //                     count exceeds 2 (`if (v327 && v314 > 2) v279[0]=1`).
    case TrialPhase::kPlea:
        if (st.defendantPleadsGuilty) {
            FireVoice(leaves, kSbfGuilty3);
            s.runningScore = TrialApplyGuiltyFine(s.runningScore,
                                                  st.judgeFavorability);
            s.phase = TrialPhase::kVoteAnnounce;
        } else {
            FireVoice(leaves, kSbfNotGuilty3);
            if (st.torturerPresent && st.evidenceCount > 2)
                s.phase = TrialPhase::kTorture;
            else
                s.phase = TrialPhase::kVoteAnnounce;
        }
        break;

    // --- kTorture (v279[0]==1 block): the torture-cost fine (wealth-score-B / 3
    //     to each of the three court seats, the QueueRequest16 triple right
    //     after the table setup) is committed BEFORE the instrument scene; then
    //     the torture intro, the chosen instrument .esc, and the result; the
    //     running score is scaled by the confess/deny multiplier
    //     (v164 = v280 * flt_61CCFC / flt_61CD00, applied under `if (v297)`).
    case TrialPhase::kTorture: {
        s.tortureFine = TrialCommitFine(st.defendantObj, st.judgeObj,
                                        st.assessorAObj, st.assessorBObj,
                                        st.wealthScoreB, st.currency);
        FireForm(leaves, "BuildTortureChoiceForm");      // torture-choice panel
        FireVoice(leaves, kSbfFolter1);
        // Instrument index: the original reads (panel & 0xF) then clamps
        // `<= 0 -> 0`, `>= 6 -> 6` (the v330 clamp).
        int inst = st.tortureInstrument;
        if (inst <= 0)
            inst = 0;
        else if (inst >= kTrialTortureInstrumentCount - 1)
            inst = kTrialTortureInstrumentCount - 1;
        FireTorture(leaves, kTrialTortureEsc[inst]);     // PROZESS_5_FOLTER2_%s
        FireVoice(leaves, kSbfFolter2Fmt);
        s.runningScore = TrialApplyTortureFine(s.runningScore,
                                               st.tortureConfessed);
        FireVoice(leaves, kSbfFolterErgebnis);
        s.phase = TrialPhase::kVoteAnnounce;
        break;
    }

    // --- kVoteAnnounce (PROZESS_6 ANKUENDIGUNG + ABSTIMMUNG): announce, then
    //     tally the jury votes — v313 sums the judge's and (present) assessors'
    //     panel rows; `if (v313 >= 2)` -> NICHT_SCHULDIG (the acquit branch of
    //     PROZESS_7).
    case TrialPhase::kVoteAnnounce:
        FireForm(leaves, "BuildElectionForm");           // vote panel (deferred)
        FireVoice(leaves, kSbfAnnounce);
        FireVoice(leaves, kSbfVoteResults);
        s.verdict = TrialTallyVerdict(st.juryVotes, st.juryCount, &s.voteTotal);
        s.phase = TrialPhase::kSentence;
        break;

    // --- kSentence (PROZESS_7): convicted -> PROZESS_7_SCHULDIG + the sentence
    //     fine (wealth-score-A / 3 to each seat: `v246 = v291 / 3` + three
    //     QueueRequest16) + PROZESS_7_STRAFEN; acquitted -> NICHT_SCHULDIG, no
    //     fine. Always close with PROZESS_8.
    case TrialPhase::kSentence:
        if (s.verdict == TrialVerdict::kConvicted) {
            FireVoice(leaves, kSbfGuilty7);
            s.perSeatFine = TrialCommitFine(st.defendantObj, st.judgeObj,
                                            st.assessorAObj, st.assessorBObj,
                                            st.wealthScoreA, st.currency);
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
