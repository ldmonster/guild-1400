// End-to-end: drive a full court-trial cutscene session (defendant + evidence +
// jury) through every phase tick-by-tick, verifying the phase sequence, the
// verdict, and the emitted (mock) commands against a hand-computed reference.
// Also runs a council removal session and an election-form vote tally end-to-end.
#include <cmath>
#include <cstring>
#include <string>
#include <vector>

#include "tests/framework/test.h"

#include "crt/rand.h"
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

struct Trace { std::vector<std::string> log; };
void T_Load(const char* s, float, void* c) {
    static_cast<Trace*>(c)->log.push_back(std::string("load:") + s);
}
void T_Voice(const char* s, void* c) {
    static_cast<Trace*>(c)->log.push_back(std::string("voice:") + s);
}
void T_Esc(const char* s, void* c) {
    static_cast<Trace*>(c)->log.push_back(std::string("esc:") + s);
}
void T_Form(const char* s, void* c) {
    static_cast<Trace*>(c)->log.push_back(std::string("form:") + s);
}

struct Fines { int calls = 0; i32 sum = 0; i32 per = 0; };
void FineHook(i32, i32, i32 amt, u8, void* c) {
    auto* f = static_cast<Fines*>(c); ++f->calls; f->sum += amt; f->per = amt;
}

u32 CrtRng(u32 range, void*) {
    int r = crt::RandNext();
    return range ? static_cast<u32>(r) % range : 0;
}

} // namespace

// ===========================================================================
// Full convict-and-fine trial, tick by tick, with a torture detour.
// ===========================================================================
TEST(WorldCourtSessionE2E, FullTrialConvictWithTorture) {
    // Hand-computed reference:
    //   evidence: law 5 (pen 30) x2, law 6 (pen 50) x1  -> 3 crimes, 2 law types
    //   maxWanted 1 -> weight 1.2
    //   score = (30 + 30 + 50) * 1.2 = 110 * 1.2 = 132.0
    //   convicted (jury total 0 < 2); torture, confessed -> 132 * 1.1 = 145.2
    //   guilty fine: 145.2 - favor(20) * (145.2*0.2) * 0.01
    //              = 145.2 - 20 * 29.04 * 0.01 = 145.2 - 5.808 = 139.392
    //   commit: floor(139 / 3) = 46 per seat, sum = 138, 3 calls
    int penalties[8] = {-1, -1, -1, -1, -1, 30, 50, -1};
    CrimeRecord ev[3] = {MakeCrime(200, 5), MakeCrime(201, 5), MakeCrime(202, 6)};
    int votes[3] = {0, 0, 0};   // all guilty -> total 0 -> convicted

    TrialSetup st;
    st.chargedLawId = 5; st.defendantObj = 200;
    st.judgeObj = 1; st.assessorAObj = 2; st.assessorBObj = 3; st.currency = 1;
    st.maxWantedLevel = 1;          // weight 1.2
    st.evidence = ev; st.evidenceCount = 3;
    st.juryVotes = votes; st.juryCount = 3;
    st.judgeFavorability = 20.0;
    st.torture = true; st.tortureConfessed = true; st.tortureInstrument = 0; // dschraube

    TrialSession s; TrialSessionInit(s, st, &LawLookup, penalties);
    Fines fines; TrialSetFineHook(&FineHook, &fines);
    Trace tr; TrialSessionLeaves leaves;
    leaves.loadScene = &T_Load; leaves.playVoice = &T_Voice;
    leaves.playTortureEsc = &T_Esc; leaves.buildForm = &T_Form; leaves.ctx = &tr;

    // Tick through every phase, asserting the exact phase ordering.
    TrialPhase order[] = {
        TrialPhase::kIntro, TrialPhase::kAccusation, TrialPhase::kJuryVerdict,
        TrialPhase::kTorture, TrialPhase::kVoteAnnounce, TrialPhase::kSentence,
        TrialPhase::kDone,
    };
    for (int i = 0; i < 7; ++i) {
        TrialPhase ran = TrialSessionStep(s, leaves);
        CHECK(ran == order[i]);
        if (i == 1) { CHECK(approx(s.score.score, 132.0f)); CHECK_EQ(s.score.uniqueCount, 2); }
        if (i == 2) { CHECK(s.verdict == TrialVerdict::kConvicted); CHECK_EQ(s.voteTotal, 0); }
        if (i == 3) { CHECK(approx(s.runningScore, 145.2f)); }
    }
    CHECK(s.finished);
    CHECK(approx(s.runningScore, 139.392f));
    CHECK_EQ(s.perSeatFine, 46);     // floor(139/3)
    CHECK_EQ(fines.calls, 3);
    CHECK_EQ(fines.sum, 138);

    // Verify the cutscene/voice invocation sequence against the reference.
    const char* expect[] = {
        "load:Gericht.ed3",
        "voice:PROZESS_2_VORWURF_KOMMENTARE.sbf",
        "voice:PROZESS_3_SCHULDIG.sbf",
        "form:BuildTortureChoiceForm",
        "voice:PROZESS_4_FOLTER_1.sbf",
        "esc:dschraube.esc",
        "voice:PROZESS_5_FOLTER2_%s.sbf",
        "voice:PROZESS_6_FOLTERERGEBNIS.sbf",
        "form:BuildElectionForm",
        "voice:PROZESS_6_ANKUENDIGUNG.sbf",
        "voice:PROZESS_6_ABSTIMMUNG_ERGEBNISSE.sbf",
        "voice:PROZESS_7_SCHULDIG.sbf",
        "voice:PROZESS_7_STRAFEN.sbf",
        "voice:PROZESS_8.sbf",
    };
    CHECK_EQ(tr.log.size(), static_cast<size_t>(14));
    for (size_t i = 0; i < tr.log.size(); ++i)
        CHECK_EQ(tr.log[i], std::string(expect[i]));
    TrialSetFineHook(nullptr, nullptr);
}

// ===========================================================================
// Council removal session end-to-end (deposed) with relation deltas.
// ===========================================================================
TEST(WorldCourtSessionE2E, CouncilRemovalEndToEnd) {
    // 4 councillors: remove, remove, keep, abstain -> yes 2 > no 1 -> REMOVED.
    // Removed-branch relation deltas: 2*(-40) + 10 + (-20) = -90 over 4 calls.
    CouncilVote votes[4] = {CouncilVote::kRemove, CouncilVote::kRemove,
                            CouncilVote::kKeep, CouncilVote::kAbstain};
    i32 voters[4] = {601, 602, 603, 604};

    CouncilSetup st;
    st.mode = CouncilMode::kRemoval; st.holderObj = 600;
    st.votes = votes; st.voterObjs = voters; st.voterCount = 4;

    CouncilSession s; CouncilSessionInit(s, st);
    struct Rel { int calls = 0; int total = 0; } rel;
    CouncilSetHooks([](i32, i32, int d, void* c) {
        auto* r = static_cast<Rel*>(c); ++r->calls; r->total += d;
    }, nullptr, &rel);
    Trace tr; CouncilSessionLeaves leaves;
    leaves.loadScene = &T_Load; leaves.playVoice = &T_Voice;
    leaves.playScene = [](const char* s, void* c) {
        static_cast<Trace*>(c)->log.push_back(std::string("scene:") + s); };
    leaves.buildVotePanel = [](bool, void*) {};
    leaves.ctx = &tr;

    bool removed = CouncilSessionRun(s, leaves);
    CHECK(removed);
    CHECK_EQ(s.tally.yes, 2);
    CHECK_EQ(s.tally.no, 1);
    CHECK_EQ(s.tally.abstain, 1);
    CHECK(s.panel.yes == 2 && s.panel.no == 1 && s.panel.abstain == 1);
    CHECK_EQ(rel.calls, 4);
    CHECK_EQ(rel.total, -90);
    // Result scene reached.
    bool sawResult = false;
    for (auto& e : tr.log)
        if (e == "scene:_ABSETZEN_ERGEBNIS_ABGESETZT") sawResult = true;
    CHECK(sawResult);
    CouncilSetHooks(nullptr, nullptr, nullptr);
}

// ===========================================================================
// Election-form vote tally end-to-end with a seeded-RNG tie-break (determinism).
// ===========================================================================
TEST(WorldCourtSessionE2E, ElectionFormTallyDeterministic) {
    // 4 ballots: candidates 0 and 1 tie at 2 each -> RNG tie-break.
    int ballots[4] = {0, 0, 1, 1};
    i32 candObjs[2] = {800, 810};

    ElectionFormSetup st;
    st.ballots = ballots; st.ballotCount = 4; st.candidateCount = 2;
    st.candidateObjs = candObjs; st.rng = &CrtRng; st.rngCtx = nullptr;

    ElectionForm f1; ElectionFormInit(f1, st);
    ElectionForm f2; ElectionFormInit(f2, st);
    ElectionFormLeaves leaves;  // all-null leaves (pure tally)

    crt::Srand(2024);
    i32 w1 = ElectionFormRun(f1, leaves);
    crt::Srand(2024);
    i32 w2 = ElectionFormRun(f2, leaves);

    CHECK(f1.result.tie);
    CHECK_EQ(f1.result.winnerVotes, 2);
    CHECK_EQ(w1, w2);                       // same seed -> same winner (determinism)
    CHECK(w1 == 800 || w1 == 810);
}
