// Unit tests for the court-trial & council-voting CUTSCENE STATE MACHINES and the
// election/vote FORM FSM (trial_session / council_session / election_form). These
// wrap the already-recovered rules cores (trial / council); the cutscene/GUI/voice
// leaves are mocked and the call SEQUENCE + per-phase rules-core invocation are
// verified.
#include <cmath>
#include <cstring>
#include <string>
#include <vector>

#include "tests/framework/test.h"

#include "world/trial_session.h"
#include "world/council_session.h"
#include "world/election_form.h"

using namespace guild;
using namespace guild::world;

namespace {

bool approx(float a, float b) { return std::fabs(a - b) < 1e-3f; }

CrimeRecord MakeCrime(i32 id, u8 lawType) {
    CrimeRecord c; std::memset(&c, 0, sizeof(c));
    c.id = id; c.location = lawType;
    return c;
}

bool LawLookup(u8 lawType, LawRecord* out, void* ctx) {
    const int* table = static_cast<const int*>(ctx);
    if (table[lawType] < 0) return false;
    std::memset(out, 0, sizeof(*out));
    out->penalty = static_cast<i16>(table[lawType]);
    return true;
}

// --- Leaf recorders: capture the playback/build call sequence. ------------
struct TrialRec {
    std::vector<std::string> log;
};
void TR_Load(const char* s, float, void* c) {
    static_cast<TrialRec*>(c)->log.push_back(std::string("load:") + s);
}
void TR_Voice(const char* s, void* c) {
    static_cast<TrialRec*>(c)->log.push_back(std::string("voice:") + s);
}
void TR_Torture(const char* s, void* c) {
    static_cast<TrialRec*>(c)->log.push_back(std::string("esc:") + s);
}
void TR_Form(const char* s, void* c) {
    static_cast<TrialRec*>(c)->log.push_back(std::string("form:") + s);
}
TrialSessionLeaves MakeTrialLeaves(TrialRec* r) {
    TrialSessionLeaves l;
    l.loadScene = &TR_Load; l.playVoice = &TR_Voice;
    l.playTortureEsc = &TR_Torture; l.buildForm = &TR_Form; l.ctx = r;
    return l;
}

struct FineCap { int calls = 0; i32 sum = 0; };
void FineHook(i32, i32, i32 amt, u8, void* c) {
    auto* f = static_cast<FineCap*>(c); ++f->calls; f->sum += amt;
}

} // namespace

// ===========================================================================
// Trial session FSM: intro -> accusation -> verdict -> outcome.
// ===========================================================================
TEST(WorldCourtSession, TrialPhaseSequenceConvict) {
    int penalties[8] = {-1, -1, -1, -1, -1, 30, 50, 20};
    CrimeRecord ev[4] = {MakeCrime(200, 5), MakeCrime(201, 5),
                         MakeCrime(202, 6), MakeCrime(203, 7)};
    int votes[3] = {0, 0, 1};   // total 1 < 2 -> convicted

    TrialSetup st;
    st.chargedLawId = 5; st.defendantObj = 200; st.judgeObj = 1;
    st.assessorAObj = 2; st.assessorBObj = 3; st.currency = 0;
    st.maxWantedLevel = 2;      // weight 1.0
    st.evidence = ev; st.evidenceCount = 4;
    st.juryVotes = votes; st.juryCount = 3;
    st.judgeFavorability = 60.0;
    st.defendantPleadsGuilty = true;  // panel==1 -> PROZESS_3_SCHULDIG branch
    st.wealthScoreA = 114;            // v291 -> sentence fine 114/3 = 38

    TrialSession s;
    TrialSessionInit(s, st, &LawLookup, penalties);

    FineCap fc; TrialSetFineHook(&FineHook, &fc);
    TrialRec rec; TrialSessionLeaves leaves = MakeTrialLeaves(&rec);

    // Step through and verify the phase order.
    CHECK(TrialSessionStep(s, leaves) == TrialPhase::kIntro);
    CHECK(s.phase == TrialPhase::kAccusation);
    CHECK(TrialSessionStep(s, leaves) == TrialPhase::kAccusation);
    // Evidence-score core ran: (30+30+50+20)*1.0 = 130; 3 distinct law types.
    CHECK(approx(s.score.score, 130.0f));
    CHECK_EQ(s.score.uniqueCount, 3);
    CHECK(s.phase == TrialPhase::kPlea);
    CHECK(TrialSessionStep(s, leaves) == TrialPhase::kPlea);
    // Guilty plea: favorability adjust runs HERE (0x4a1b3e):
    // 130 - 60*(130*0.2)*0.01 = 114.4.
    CHECK(approx(s.runningScore, 114.4f));
    // Guilty plea never enters torture -> straight to vote-announce.
    CHECK(s.phase == TrialPhase::kVoteAnnounce);
    CHECK(TrialSessionStep(s, leaves) == TrialPhase::kVoteAnnounce);
    // The jury tally happens at PROZESS_6_ABSTIMMUNG (v313).
    CHECK_EQ(s.voteTotal, 1);
    CHECK(s.verdict == TrialVerdict::kConvicted);
    CHECK(s.phase == TrialPhase::kSentence);
    CHECK(TrialSessionStep(s, leaves) == TrialPhase::kSentence);
    CHECK(s.phase == TrialPhase::kDone);

    // Sentence fine = wealth-score-A / 3 per seat (v291/3), NOT the evidence
    // score: 114/3 = 38 to each of the three seats.
    CHECK_EQ(s.perSeatFine, 38);
    CHECK_EQ(fc.calls, 3);
    CHECK_EQ(fc.sum, 114);

    // Leaf call sequence: load, accusation voice, guilty3 voice, announce form +
    // 2 voices, guilty7 + strafen + close.
    CHECK_EQ(rec.log[0], std::string("load:Gericht.ed3"));
    CHECK_EQ(rec.log[1], std::string("voice:PROZESS_2_VORWURF_KOMMENTARE.sbf"));
    CHECK_EQ(rec.log[2], std::string("voice:PROZESS_3_SCHULDIG.sbf"));
    CHECK_EQ(rec.log[3], std::string("form:BuildElectionForm"));
    CHECK_EQ(rec.log[4], std::string("voice:PROZESS_6_ANKUENDIGUNG.sbf"));
    CHECK_EQ(rec.log[5], std::string("voice:PROZESS_6_ABSTIMMUNG_ERGEBNISSE.sbf"));
    CHECK_EQ(rec.log[6], std::string("voice:PROZESS_7_SCHULDIG.sbf"));
    CHECK_EQ(rec.log[7], std::string("voice:PROZESS_7_STRAFEN.sbf"));
    CHECK_EQ(rec.log[8], std::string("voice:PROZESS_8.sbf"));
    TrialSetFineHook(nullptr, nullptr);
}

TEST(WorldCourtSession, TrialAcquitNoFine) {
    int penalties[8] = {-1, 40, -1, -1, -1, -1, -1, -1};
    CrimeRecord ev[1] = {MakeCrime(300, 1)};
    int votes[2] = {1, 1};   // total 2 >= 2 -> acquitted

    TrialSetup st;
    st.evidence = ev; st.evidenceCount = 1; st.maxWantedLevel = 0;  // weight 1.4
    st.juryVotes = votes; st.juryCount = 2;

    TrialSession s; TrialSessionInit(s, st, &LawLookup, penalties);
    FineCap fc; TrialSetFineHook(&FineHook, &fc);
    TrialRec rec; TrialSessionLeaves leaves = MakeTrialLeaves(&rec);

    TrialVerdict v = TrialSessionRun(s, leaves);
    CHECK(v == TrialVerdict::kAcquitted);
    CHECK(approx(s.score.score, 56.0f));    // 40 * 1.4
    CHECK_EQ(s.perSeatFine, 0);             // no fine on acquit
    CHECK_EQ(fc.calls, 0);
    // Acquit voice present, no guilty/strafen voice.
    bool sawNicht = false, sawStrafen = false;
    for (auto& e : rec.log) {
        if (e == "voice:PROZESS_7_NICHT_SCHULDIG.sbf") sawNicht = true;
        if (e == "voice:PROZESS_7_STRAFEN.sbf") sawStrafen = true;
    }
    CHECK(sawNicht);
    CHECK(!sawStrafen);
    TrialSetFineHook(nullptr, nullptr);
}

TEST(WorldCourtSession, TrialTorturePhase) {
    int penalties[8] = {-1, 100, -1, -1, -1, -1, -1, -1};
    // Torture requires evidenceCount > 2 (the `v314 > 2` gate at PROZESS_3).
    CrimeRecord ev[3] = {MakeCrime(400, 1), MakeCrime(401, 1), MakeCrime(402, 1)};
    int votes[1] = {0};   // tally 0 < 2 -> convicted at PROZESS_6

    TrialSetup st;
    st.evidence = ev; st.evidenceCount = 3; st.maxWantedLevel = 2;  // weight 1.0
    st.juryVotes = votes; st.juryCount = 1;
    st.defendantPleadsGuilty = false;   // NICHT_SCHULDIG plea -> torture gate
    st.torturerPresent = true;          // ctx+72 resolves (v327 != 0)
    st.tortureConfessed = true; st.tortureInstrument = 2; // peitsche
    st.judgeFavorability = 0.0;
    st.wealthScoreB = 90;               // v292 -> torture-cost fine 90/3 = 30
    st.wealthScoreA = 60;               // v291 -> sentence fine 60/3 = 20

    TrialSession s; TrialSessionInit(s, st, &LawLookup, penalties);
    FineCap fc; TrialSetFineHook(&FineHook, &fc);
    TrialRec rec; TrialSessionLeaves leaves = MakeTrialLeaves(&rec);

    // intro, accusation, plea (not guilty + torturer + evidence>2) -> torture.
    TrialSessionStep(s, leaves); // intro
    TrialSessionStep(s, leaves); // accusation -> score 300
    CHECK(approx(s.score.score, 300.0f));
    TrialSessionStep(s, leaves); // plea (not guilty)
    CHECK(s.phase == TrialPhase::kTorture);
    TrialSessionStep(s, leaves); // torture
    // Torture-cost fine committed immediately: wealthScoreB/3 = 30 per seat.
    CHECK_EQ(s.tortureFine, 30);
    CHECK_EQ(fc.calls, 3);
    CHECK_EQ(fc.sum, 90);
    // confessed -> score * 1.1 = 330.
    CHECK(approx(s.runningScore, 330.0f));
    CHECK(s.phase == TrialPhase::kVoteAnnounce);
    // The torture instrument esc was played.
    bool sawEsc = false;
    for (auto& e : rec.log) if (e == "esc:peitsche.esc") sawEsc = true;
    CHECK(sawEsc);
    // Run to completion: convicted -> sentence fine wealthScoreA/3 = 20.
    TrialVerdict v = TrialSessionRun(s, leaves);
    CHECK(v == TrialVerdict::kConvicted);
    CHECK_EQ(s.perSeatFine, 20);
    CHECK_EQ(fc.calls, 6);
    CHECK_EQ(fc.sum, 150);
    TrialSetFineHook(nullptr, nullptr);
}

// ===========================================================================
// Council session FSM: intro -> mode -> collect -> tally -> result.
// ===========================================================================
namespace {
struct CouncilRec { std::vector<std::string> log; };
void CR_Load(const char* s, float, void* c) {
    static_cast<CouncilRec*>(c)->log.push_back(std::string("load:") + s);
}
void CR_Voice(const char* s, void* c) {
    static_cast<CouncilRec*>(c)->log.push_back(std::string("voice:") + s);
}
void CR_Scene(const char* s, void* c) {
    static_cast<CouncilRec*>(c)->log.push_back(std::string("scene:") + s);
}
void CR_Panel(bool init, void* c) {
    static_cast<CouncilRec*>(c)->log.push_back(init ? "panel:init" : "panel:mark");
}
CouncilSessionLeaves MakeCouncilLeaves(CouncilRec* r) {
    CouncilSessionLeaves l;
    l.loadScene = &CR_Load; l.playVoice = &CR_Voice; l.playScene = &CR_Scene;
    l.buildVotePanel = &CR_Panel; l.ctx = r;
    return l;
}
struct RelCap { int calls = 0; int total = 0; };
void RelHook(i32, i32, int d, void* c) {
    auto* r = static_cast<RelCap*>(c); ++r->calls; r->total += d;
}
} // namespace

TEST(WorldCourtSession, CouncilRemovalSession) {
    CouncilVote votes[5] = {CouncilVote::kRemove, CouncilVote::kRemove,
                            CouncilVote::kRemove, CouncilVote::kKeep,
                            CouncilVote::kAbstain};
    i32 voters[5] = {501, 502, 503, 504, 505};

    CouncilSetup st;
    st.mode = CouncilMode::kRemoval; st.holderObj = 500;
    st.votes = votes; st.voterObjs = voters; st.voterCount = 5;

    CouncilSession s; CouncilSessionInit(s, st);
    RelCap rc; CouncilSetHooks(&RelHook, nullptr, &rc);
    CouncilRec rec; CouncilSessionLeaves leaves = MakeCouncilLeaves(&rec);

    CHECK(CouncilSessionStep(s, leaves) == CouncilPhase::kIntro);
    CHECK(CouncilSessionStep(s, leaves) == CouncilPhase::kModeSelect);
    CHECK(CouncilSessionStep(s, leaves) == CouncilPhase::kCollectVotes);
    // Panel counters mirror the votes: 3 yes, 1 no, 1 abstain.
    CHECK_EQ(s.panel.yes, 3);
    CHECK_EQ(s.panel.no, 1);
    CHECK_EQ(s.panel.abstain, 1);
    CHECK(CouncilSessionStep(s, leaves) == CouncilPhase::kTally);
    CHECK(s.tally.removed);
    CHECK(CouncilSessionStep(s, leaves) == CouncilPhase::kResult);
    CHECK(s.phase == CouncilPhase::kDone);
    // Removed-branch relation deltas: 3*(-40)+10+(-20) = -130.
    CHECK_EQ(rc.calls, 5);
    CHECK_EQ(rc.total, -130);

    // Leaf sequence: scene load, ABSETZUNG voice, KAMMERN scene, panel init,
    // 5 vote scenes+marks, then ABGESETZT result.
    CHECK_EQ(rec.log[0], std::string("load:Sitzung_Wohnsitz.ed3"));
    CHECK_EQ(rec.log[1], std::string("voice:ABSETZUNG.sbf"));
    CHECK_EQ(rec.log[2], std::string("scene:_ABSETZEN_KAMMERN"));
    CHECK_EQ(rec.log[3], std::string("panel:init"));
    CHECK_EQ(rec.log[4], std::string("scene:_ABSETZEN_JA"));
    CHECK_EQ(rec.log[5], std::string("panel:mark"));
    CHECK_EQ(rec.log.back(), std::string("scene:_ABSETZEN_ERGEBNIS_ABGESETZT"));
    CouncilSetHooks(nullptr, nullptr, nullptr);
}

TEST(WorldCourtSession, CouncilElectionSession) {
    int ballots[5] = {1, 1, 1, 0, -1};
    i32 candObjs[3] = {700, 710, 720};
    u32 seed = 0;
    auto rng = [](u32 range, void* c) -> u32 {
        u32 v = *static_cast<u32*>(c); return range ? v % range : 0; };

    CouncilSetup st;
    st.mode = CouncilMode::kElection;
    st.ballots = ballots; st.ballotCount = 5; st.candidateCount = 3;
    st.candidateObjs = candObjs; st.rng = rng; st.rngCtx = &seed;

    CouncilSession s; CouncilSessionInit(s, st);
    CouncilRec rec; CouncilSessionLeaves leaves = MakeCouncilLeaves(&rec);

    CouncilSessionRun(s, leaves);
    CHECK_EQ(s.election.winner, 1);    // candidate 1 got 3 ballots
    CHECK_EQ(s.winnerObj, 710);
    CHECK_EQ(rec.log[1], std::string("voice:AMTSWAHL.sbf"));   // election mode
}

// ===========================================================================
// Election form FSM + vote-panel tally.
// ===========================================================================
namespace {
struct FormRec { std::vector<std::string> log; int markers = 0; int lastX = 0; };
void FR_Build(void* c) { static_cast<FormRec*>(c)->log.push_back("build"); }
void FR_Marker(int x, int, int, void* c) {
    auto* r = static_cast<FormRec*>(c); ++r->markers; r->lastX = x;
}
void FR_Succ(char w, void* c) {
    static_cast<FormRec*>(c)->log.push_back(std::string("succ:") + w);
}
} // namespace

TEST(WorldCourtSession, VotePanelColumnTally) {
    VotePanel p; p.Init();
    int x = 0, y = 0;
    CHECK(p.Mark(0, &x, &y));  // yes column
    CHECK_EQ(x, 32);           // kVotePanelColumnX[0]
    CHECK_EQ(y, 80);           // 10*0 + 80
    CHECK(p.Mark(0, &x, &y));
    CHECK_EQ(y, 90);           // 10*1 + 80
    CHECK(p.Mark(1, &x, &y));  // no column
    CHECK_EQ(x, 62);
    CHECK(p.Mark(2, &x, &y));  // abstain column
    CHECK_EQ(x, 47);
    CHECK(!p.Mark(3, &x, &y)); // out of range
    CHECK_EQ(p.count[0], 2);
    CHECK_EQ(p.count[1], 1);
    CHECK_EQ(p.count[2], 1);
}

TEST(WorldCourtSession, VoteMarkerGeometry) {
    // AddVoteMarker: x = 68 - 10*n.
    CHECK_EQ(VoteMarkerX(0), 68);
    CHECK_EQ(VoteMarkerX(1), 58);
    CHECK_EQ(VoteMarkerX(3), 38);
}

TEST(WorldCourtSession, ElectionFormPhasesAndTally) {
    int ballots[5] = {1, 1, 1, 0, -1};   // candidate 1 wins (3), -1 skipped
    i32 candObjs[3] = {700, 710, 720};
    u32 seed = 0;
    auto rng = [](u32 range, void* c) -> u32 {
        u32 v = *static_cast<u32*>(c); return range ? v % range : 0; };

    ElectionFormSetup st;
    st.ballots = ballots; st.ballotCount = 5; st.candidateCount = 3;
    st.candidateObjs = candObjs; st.rng = rng; st.rngCtx = &seed;
    st.offerSuccessor = true;

    ElectionForm f; ElectionFormInit(f, st);
    FormRec rec; ElectionFormLeaves leaves;
    leaves.buildElectionForm = &FR_Build; leaves.addVoteMarker = &FR_Marker;
    leaves.buildSuccessorDialog = &FR_Succ; leaves.ctx = &rec;

    CHECK(ElectionFormStep(f, leaves) == ElectionFormPhase::kOpen);
    CHECK(ElectionFormStep(f, leaves) == ElectionFormPhase::kCollect);
    // 4 valid ballots (one -1 skipped) -> 4 markers; last x = 68 - 10*3 = 38.
    CHECK_EQ(rec.markers, 4);
    CHECK_EQ(rec.lastX, 38);
    CHECK(ElectionFormStep(f, leaves) == ElectionFormPhase::kTally);
    CHECK_EQ(f.result.winner, 1);
    CHECK_EQ(f.winnerObj, 710);
    CHECK(ElectionFormStep(f, leaves) == ElectionFormPhase::kAnnounce);
    CHECK(f.phase == ElectionFormPhase::kDone);
    // Successor dialog offered.
    bool sawSucc = false;
    for (auto& e : rec.log) if (e == "succ:A") sawSucc = true;
    CHECK(sawSucc);
}
