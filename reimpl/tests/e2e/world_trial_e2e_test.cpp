// End-to-end flow across the political/social rules cores:
//   1. Stage a court trial (defendant + collected evidence + jury), compute the
//      evidence score, tally the jury verdict, and apply the resulting fine.
//   2. Run a council removal vote on an office holder and apply the relation
//      deltas through the (mock) command hook.
//   3. Run a guild-master election with seeded-RNG tiebreak.
// All outcomes are checked against a hand-computed reference.
#include <cmath>
#include <cstring>

#include "tests/framework/test.h"

#include "crt/rand.h"
#include "world/trial.h"
#include "world/council.h"
#include "world/election.h"

using namespace guild;
using namespace guild::world;

namespace {

bool approx(float a, float b) { return std::fabs(a - b) < 1e-3f; }

CrimeRecord MakeCrime(i32 id, u8 lawType) {
    CrimeRecord c; std::memset(&c, 0, sizeof(c));
    c.id = id; c.location = lawType;
    return c;
}

// Law penalties keyed by law type.
bool LawLookup(u8 lawType, LawRecord* out, void* ctx) {
    const int* table = static_cast<const int*>(ctx);
    if (table[lawType] < 0) return false;
    std::memset(out, 0, sizeof(*out));
    out->penalty = static_cast<i16>(table[lawType]);
    return true;
}

// --- e2e fine-commit capture --------------------------------------------
struct E2EFines { int calls = 0; i32 sum = 0; };
void E2EFineHook(i32, i32, i32 amount, u8, void* ctx) {
    auto* f = static_cast<E2EFines*>(ctx); ++f->calls; f->sum += amount;
}

// --- e2e relation-delta capture -----------------------------------------
struct E2ERelations { int calls = 0; int total = 0; };
void E2ERelHook(i32, i32, int delta, void* ctx) {
    auto* r = static_cast<E2ERelations*>(ctx); ++r->calls; r->total += delta;
}

// crt-seeded RNG adapter for the election tiebreak (uses VIBE_Util_RandNext).
u32 CrtRng(u32 range, void*) {
    int r = crt::RandNext();
    return range ? static_cast<u32>(r) % range : 0;
}

} // namespace

// ===========================================================================
// 1. Full court trial: convict + fine.
// ===========================================================================
TEST(WorldTrialE2E, ConvictAndFine) {
    // Defendant has 4 pieces of evidence over 3 distinct law types.
    //   law 5 (theft)   penalty 30, two counts
    //   law 6 (assault) penalty 50, one count
    //   law 7 (fraud)   penalty 20, one count
    int penalties[8] = {-1, -1, -1, -1, -1, 30, 50, 20};
    CrimeRecord evidence[4] = {
        MakeCrime(200, 5), MakeCrime(201, 5),
        MakeCrime(202, 6), MakeCrime(203, 7),
    };
    // Max-wanted level 2 -> weight 1.0 (so the score is just the penalty sum).
    TrialScore score = TrialComputeEvidenceScore(evidence, 4, /*wanted*/ 2,
                                                 &LawLookup, penalties);
    // Reference: (30 + 30 + 50 + 20) * 1.0 = 130; distinct law types = 3.
    CHECK(approx(score.score, 130.0f));
    CHECK_EQ(score.uniqueCount, 3);
    CHECK_EQ(score.totalCount, 4);

    // Jury: judge votes guilty(0), assessor A guilty(0), assessor B not-guilty(1).
    // Vote total = 1 < 2 -> CONVICTED.
    int votes[3] = {0, 0, 1};
    int total = 0;
    TrialVerdict v = TrialTallyVerdict(votes, 3, &total);
    CHECK_EQ(total, 1);
    CHECK(v == TrialVerdict::kConvicted);

    // Convicted: apply the favorability-weighted fine. Judge favorability 60.
    //   verdictScore = 130 - 60 * (130 * 0.2) * 0.01
    //                = 130 - 60 * 26 * 0.01 = 130 - 15.6 = 114.4
    float adjusted = TrialApplyGuiltyFine(score.score, /*favorability*/ 60.0);
    CHECK(approx(adjusted, 114.4f));

    // Commit the fine three ways (wealth score 114 -> per-seat 38, sum 114).
    E2EFines fines;
    TrialSetFineHook(&E2EFineHook, &fines);
    i32 per = TrialCommitFine(/*defendant*/ 200, /*judge*/ 1, /*aA*/ 2, /*aB*/ 3,
                              /*wealth*/ 114, /*currency*/ 0);
    CHECK_EQ(per, 38);          // 114 / 3
    CHECK_EQ(fines.calls, 3);
    CHECK_EQ(fines.sum, 114);
    TrialSetFineHook(nullptr, nullptr);
}

// ===========================================================================
// 1b. Full court trial: acquit (no fine path).
// ===========================================================================
TEST(WorldTrialE2E, AcquitWhenJuryFavors) {
    int penalties[8] = {-1, 40, -1, -1, -1, -1, -1, -1};
    CrimeRecord evidence[1] = { MakeCrime(300, 1) };
    TrialScore score = TrialComputeEvidenceScore(evidence, 1, 0, &LawLookup, penalties);
    // weight[0] = 1.4 -> 40 * 1.4 = 56.
    CHECK(approx(score.score, 56.0f));

    // Jury: not-guilty(1), not-guilty(1) -> total 2 >= 2 -> ACQUITTED.
    int votes[2] = {1, 1};
    int total = 0;
    CHECK(TrialTallyVerdict(votes, 2, &total) == TrialVerdict::kAcquitted);
    CHECK_EQ(total, 2);
}

// ===========================================================================
// 2. Council removal vote on a law/office holder.
// ===========================================================================
TEST(WorldCouncilE2E, RemovalVoteAndRelations) {
    // 5 councillors vote on deposing holder (object id 500).
    //   remove, remove, remove, keep, abstain -> yes 3 > no 1 -> REMOVED.
    CouncilVote votes[5] = {CouncilVote::kRemove, CouncilVote::kRemove,
                            CouncilVote::kRemove, CouncilVote::kKeep,
                            CouncilVote::kAbstain};
    i32 voterObjs[5] = {501, 502, 503, 504, 505};

    CouncilVoteTally t = CouncilTallyRemoval(votes, 5);
    CHECK(t.removed);
    CHECK_EQ(t.yes, 3);
    CHECK_EQ(t.no, 1);
    CHECK_EQ(t.abstain, 1);

    // Apply removed-branch relation deltas:
    //   3x remove(-40) + keep(+10) + abstain(-20) = -120 + 10 - 20 = -130.
    E2ERelations rel;
    CouncilSetHooks(&E2ERelHook, nullptr, &rel);
    CouncilApplyRemovalRelations(votes, voterObjs, 5, /*holder*/ 500, t.removed);
    CHECK_EQ(rel.calls, 5);     // all 5 voters differ from holder 500
    CHECK_EQ(rel.total, -130);
    CouncilSetHooks(nullptr, nullptr, nullptr);
}

// ===========================================================================
// 3. Council election with seeded-RNG tiebreak (determinism).
// ===========================================================================
TEST(WorldCouncilE2E, ElectionDeterministicTiebreak) {
    // 4 ballots: candidate 0 and candidate 1 tie at 2 each. Seed the crt RNG so
    // the tiebreak is reproducible.
    int ballots[4] = {0, 0, 1, 1};
    crt::Srand(12345);
    ElectionResult r1 = CouncilElectWinner(ballots, 4, 2, &CrtRng, nullptr);
    crt::Srand(12345);
    ElectionResult r2 = CouncilElectWinner(ballots, 4, 2, &CrtRng, nullptr);
    CHECK(r1.tie);
    CHECK_EQ(r1.winnerVotes, 2);
    CHECK_EQ(r1.winner, r2.winner);    // same seed -> same winner
    CHECK(r1.winner == 0 || r1.winner == 1);
}

// ===========================================================================
// 3b. Guild-master election end-to-end (wealth scoring + install commit).
// ===========================================================================
namespace {
struct InstallCap { int calls = 0; i32 winner = -1; };
void InstallHook(int, i32 winnerId, i32, void* ctx) {
    auto* c = static_cast<InstallCap*>(ctx); ++c->calls; c->winner = winnerId;
}
ElectionCandidate Cand(i32 id, u8 ot, u16 emp, i32 wealth) {
    ElectionCandidate c; c.personId = id; c.officeType = ot; c.employer = emp;
    c.flagged = false; c.totalWealth = wealth; return c;
}
} // namespace

TEST(WorldElectionE2E, GuildMasterInstall) {
    ElectionCandidate pool[3] = {
        Cand(60, 13, 1, 2000),
        Cand(70, 16, 2, 8000),   // wealthiest -> winner
        Cand(80, 18, 3, 4000),
    };
    ElectionOutcome o = ElectionRunGuildMaster(pool, 3, /*incumbent*/ 60);
    CHECK(o.quorumMet);
    CHECK_EQ(o.winnerId, 70);
    CHECK(o.install);

    InstallCap cap;
    ElectionSetInstallHook(&InstallHook, &cap);
    bool committed = ElectionCommit(o, /*officeSlot*/ 9, /*incumbent*/ 60);
    CHECK(committed);
    CHECK_EQ(cap.calls, 1);
    CHECK_EQ(cap.winner, 70);
    ElectionSetInstallHook(nullptr, nullptr);
}
