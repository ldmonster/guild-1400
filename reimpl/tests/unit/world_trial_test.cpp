// Unit tests for the political/social rules cores: trial verdict/scoring,
// council voting, election candidate scoring, tutorial progression, guild
// eligibility, plus the extended History/Statistics/Stammbaum/Privilege rules.
#include <cmath>
#include <cstring>

#include "tests/framework/test.h"

#include "world/trial.h"
#include "world/trial_session.h"   // FSM phase / participant edge tests
#include "world/council.h"
#include "world/election.h"
#include "world/tutorial.h"
#include "world/guild.h"
#include "world/history.h"
#include "world/statistics.h"
#include "world/stammbaum.h"
#include "world/privilege.h"

using namespace guild;
using namespace guild::world;

namespace {

bool approx(float a, float b) { return std::fabs(a - b) < 1e-3f; }

// Build a 45-byte CrimeRecord with a given law-type byte (offset 28) and id.
CrimeRecord MakeCrime(i32 id, u8 lawType) {
    CrimeRecord c;
    std::memset(&c, 0, sizeof(c));
    c.id = id;
    c.location = lawType;   // offset +28 used as the law-table key in the trial
    return c;
}

// Law lookup: penalties keyed by law type (LawRecord::penalty at +16).
bool TestLawLookup(u8 lawType, LawRecord* out, void* ctx) {
    const int* table = static_cast<const int*>(ctx); // [lawType] -> penalty, -1 absent
    int pen = table[lawType];
    if (pen < 0)
        return false;
    std::memset(out, 0, sizeof(*out));
    out->penalty = static_cast<i16>(pen);
    return true;
}

} // namespace

// ===========================================================================
// Trial
// ===========================================================================
TEST(WorldTrial, WantedWeightTableAndClamp) {
    CHECK(approx(TrialWantedWeight(0), 1.4f));
    CHECK(approx(TrialWantedWeight(1), 1.2f));
    CHECK(approx(TrialWantedWeight(2), 1.0f));
    CHECK(approx(TrialWantedWeight(3), 0.8f));
    CHECK(approx(TrialWantedWeight(4), 0.6f));
    CHECK(approx(TrialWantedWeight(-5), 1.4f)); // clamp low
    CHECK(approx(TrialWantedWeight(99), 0.6f)); // clamp high
}

TEST(WorldTrial, EvidenceScoreAndDedup) {
    // 3 crimes: law-types 1,1,2; penalties law1=10, law2=20; wantedLevel=1 (w=1.2).
    int penalties[8] = {-1, 10, 20, -1, -1, -1, -1, -1};
    CrimeRecord crimes[3] = {MakeCrime(100, 1), MakeCrime(101, 1), MakeCrime(102, 2)};

    TrialScore s = TrialComputeEvidenceScore(crimes, 3, /*wantedLevel*/ 1,
                                             &TestLawLookup, penalties);
    // score = 10*1.2 + 10*1.2 + 20*1.2 = 48 (golden, python-checked f32 == 48.0).
    CHECK(approx(s.score, 48.0f));
    CHECK_EQ(s.totalCount, 3);
    CHECK_EQ(s.uniqueCount, 2);   // distinct law types {1,2}
}

TEST(WorldTrial, JuryVerdictThreshold) {
    int total = 0;
    // votes summing to >= 2 -> acquitted.
    int v1[3] = {1, 1, 0};
    CHECK(TrialTallyVerdict(v1, 3, &total) == TrialVerdict::kAcquitted);
    CHECK_EQ(total, 2);
    // votes summing to < 2 -> convicted.
    int v2[3] = {0, 1, 0};
    CHECK(TrialTallyVerdict(v2, 3, &total) == TrialVerdict::kConvicted);
    CHECK_EQ(total, 1);
    // exactly 0 -> convicted.
    int v3[1] = {0};
    CHECK(TrialTallyVerdict(v3, 1, &total) == TrialVerdict::kConvicted);
}

TEST(WorldTrial, GuiltyAndTortureFines) {
    // golden: score 48, favorability 50 -> 48 - 50*(48*0.2)*0.01 = 43.2.
    CHECK(approx(TrialApplyGuiltyFine(48.0f, 50.0), 43.2f));
    CHECK(approx(TrialApplyTortureFine(48.0f, /*confessed*/ true), 52.8f));
    CHECK(approx(TrialApplyTortureFine(48.0f, /*confessed*/ false), 38.4f));
}

namespace {
struct FineCapture { i32 last = 0; int calls = 0; i32 sum = 0; };
void FineHook(i32, i32, i32 amount, u8, void* ctx) {
    auto* c = static_cast<FineCapture*>(ctx);
    c->last = amount; ++c->calls; c->sum += amount;
}
} // namespace

TEST(WorldTrial, CommitFineSplitsThreeWays) {
    FineCapture cap;
    TrialSetFineHook(&FineHook, &cap);
    i32 per = TrialCommitFine(/*defendant*/ 1, 2, 3, 4, /*wealth*/ 100, /*cur*/ 0);
    CHECK_EQ(per, 33);          // 100 / 3
    CHECK_EQ(cap.calls, 3);     // judge + two assessors
    CHECK_EQ(cap.sum, 99);
    TrialSetFineHook(nullptr, nullptr);
}

// ===========================================================================
// Council
// ===========================================================================
TEST(WorldCouncil, RemovalTallyAndThreshold) {
    // 3 remove, 1 keep, 1 abstain -> removed (yes > no).
    CouncilVote votes[5] = {CouncilVote::kRemove, CouncilVote::kRemove,
                            CouncilVote::kRemove, CouncilVote::kKeep,
                            CouncilVote::kAbstain};
    CouncilVoteTally t = CouncilTallyRemoval(votes, 5);
    CHECK_EQ(t.yes, 3);
    CHECK_EQ(t.no, 1);
    CHECK_EQ(t.abstain, 1);
    CHECK(t.removed);

    // tie (2 vs 2) keeps the holder (yes <= no).
    CouncilVote tied[4] = {CouncilVote::kRemove, CouncilVote::kRemove,
                           CouncilVote::kKeep, CouncilVote::kKeep};
    CouncilVoteTally t2 = CouncilTallyRemoval(tied, 4);
    CHECK(!t2.removed);
}

TEST(WorldCouncil, RelationDeltas) {
    // removed branch: JA -40, NEIN +10, ABSTAIN -20.
    CHECK_EQ(CouncilRelationDelta(CouncilVote::kRemove, true), -40);
    CHECK_EQ(CouncilRelationDelta(CouncilVote::kKeep, true), 10);
    CHECK_EQ(CouncilRelationDelta(CouncilVote::kAbstain, true), -20);
    // kept branch: JA -40, NEIN +10, ABSTAIN +5.
    CHECK_EQ(CouncilRelationDelta(CouncilVote::kRemove, false), -40);
    CHECK_EQ(CouncilRelationDelta(CouncilVote::kKeep, false), 10);
    CHECK_EQ(CouncilRelationDelta(CouncilVote::kAbstain, false), 5);
}

namespace {
// Deterministic RNG mock for the election tiebreak: returns a fixed first value.
u32 FixedRng(u32 range, void* ctx) {
    u32 v = *static_cast<u32*>(ctx);
    return range ? v % range : 0;
}
} // namespace

TEST(WorldCouncil, ElectionWinnerByBallots) {
    // candidate 1 gets 3 votes, candidate 0 gets 1, candidate 2 gets 0.
    int ballots[5] = {1, 1, 1, 0, -1}; // -1 skipped
    u32 seed = 0;
    ElectionResult r = CouncilElectWinner(ballots, 5, 3, &FixedRng, &seed);
    CHECK_EQ(r.winner, 1);
    CHECK_EQ(r.winnerVotes, 3);
    CHECK(!r.tie);
}

TEST(WorldCouncil, ElectionTieResolvedByRng) {
    // candidates 0 and 2 tie at 2 votes each; RNG seed 2 -> start idx 2 % 3 = 2.
    int ballots[4] = {0, 0, 2, 2};
    u32 seed = 2;
    ElectionResult r = CouncilElectWinner(ballots, 4, 3, &FixedRng, &seed);
    CHECK(r.tie);
    CHECK_EQ(r.winnerVotes, 2);
    CHECK_EQ(r.winner, 2);   // RandInt(3)=2 -> counts[2]==max -> winner 2
}

// ===========================================================================
// Election (guild master, wealth scoring)
// ===========================================================================
namespace {
ElectionCandidate Cand(i32 id, u8 officeType, u16 employer, bool flagged, i32 wealth) {
    ElectionCandidate c; c.personId = id; c.officeType = officeType;
    c.employer = employer; c.flagged = flagged; c.totalWealth = wealth;
    return c;
}
} // namespace

TEST(WorldElection, WealthiestEligibleWins) {
    // 4 people: three are eligible guild candidates (officeType 13..18), wealthiest
    // is id 30 with 5000. Two extra members make the quorum (>=3 members).
    ElectionCandidate pool[5] = {
        Cand(10, 13, 1, false, 1000),  // candidate
        Cand(20, 15, 2, false, 3000),  // candidate
        Cand(30, 18, 3, false, 5000),  // candidate (wealthiest)
        Cand(40, 99, 4, false, 9000),  // member, NOT candidate (officeType out)
        Cand(50, 14, 0xFFFF, false, 8000), // not employed -> not member/candidate
    };
    ElectionOutcome o = ElectionRunGuildMaster(pool, 5, /*incumbent*/ -1);
    CHECK_EQ(o.candidateCount, 3);
    CHECK_EQ(o.memberCount, 4);   // ids 10,20,30,40 (50 unemployed excluded)
    CHECK(o.quorumMet);
    CHECK_EQ(o.winnerId, 30);
    CHECK_EQ(o.winnerWealth, 5000);
    CHECK(o.install);             // winner != incumbent(-1)
}

TEST(WorldElection, IncumbentWinnerNotReinstalled) {
    ElectionCandidate pool[3] = {
        Cand(10, 13, 1, false, 1000),
        Cand(20, 15, 2, false, 3000),
        Cand(30, 18, 3, false, 5000),
    };
    ElectionOutcome o = ElectionRunGuildMaster(pool, 3, /*incumbent*/ 30);
    CHECK_EQ(o.winnerId, 30);
    CHECK(!o.install);            // winner == incumbent -> no install
}

TEST(WorldElection, QuorumNotMet) {
    ElectionCandidate pool[2] = {
        Cand(10, 13, 1, false, 1000),
        Cand(20, 15, 2, false, 3000),
    };
    ElectionOutcome o = ElectionRunGuildMaster(pool, 2, -1);
    CHECK(!o.quorumMet);          // only 2 members < 3
    CHECK(!o.install);
}

// WAVE-16 fix: the winner loop @0x4812ff SEEDS the running-best wealth with the
// INCUMBENT's ComputeTotalWealth (or 0 when no incumbent record). The winner
// pointer (v10) starts NULL and is replaced only when a candidate's wealth is
// STRICTLY greater than the running best, so a candidate must OUT-EARN the
// incumbent to win; if none does there is NO winner and NO install. (The prior
// reconstruction wrongly seeded with candidate[0] and always produced a winner.)
TEST(WorldElection, IncumbentWealthSeedNoCandidateOutEarns) {
    // 3 candidates, richest is 5000; incumbent (id 30, also a candidate) is worth
    // 6000 — nobody out-earns the incumbent -> no winner, no install.
    ElectionCandidate pool[3] = {
        Cand(10, 13, 1, false, 1000),
        Cand(20, 15, 2, false, 3000),
        Cand(30, 18, 3, false, 5000),
    };
    ElectionOutcome o = ElectionRunGuildMaster(pool, 3, /*incumbent*/ 30,
                                               /*incumbentWealth*/ 6000,
                                               /*incumbentValid*/ true);
    CHECK(o.quorumMet);
    CHECK_EQ(o.winnerIndex, -1);  // v10 stayed NULL
    CHECK_EQ(o.winnerId, -1);
    CHECK(!o.install);
}

TEST(WorldElection, IncumbentWealthSeedChallengerOutEarns) {
    // A challenger (id 20) worth 8000 beats the incumbent's 6000 -> installs.
    ElectionCandidate pool[3] = {
        Cand(10, 13, 1, false, 1000),
        Cand(20, 15, 2, false, 8000),  // out-earns the incumbent
        Cand(30, 18, 3, false, 5000),
    };
    ElectionOutcome o = ElectionRunGuildMaster(pool, 3, /*incumbent*/ 30,
                                               /*incumbentWealth*/ 6000,
                                               /*incumbentValid*/ true);
    CHECK_EQ(o.winnerId, 20);
    CHECK_EQ(o.winnerWealth, 8000);
    CHECK(o.install);             // winner != incumbent
}

// ===========================================================================
// Tutorial
// ===========================================================================
TEST(WorldTutorial, IsInactiveAndSetChapter) {
    TutorialState st; std::memset(&st, 0, sizeof(st));
    st.active = false;
    CHECK(TutorialIsInactive(st));
    TutorialChapter ch; std::memset(&ch, 0, sizeof(ch));
    CHECK_EQ(TutorialSetActiveChapter(st, &ch), -4);  // inactive -> -4
    st.active = true;
    CHECK(!TutorialIsInactive(st));
    CHECK_EQ(TutorialSetActiveChapter(st, &ch), 0);
    CHECK(st.chapter == &ch);
}

TEST(WorldTutorial, StepProgression) {
    TutorialStep steps[3];
    std::memset(steps, 0, sizeof(steps));
    steps[0].phase = 1; steps[0].formPos = kTutPosLeft;   steps[0].textId = 100;
    steps[1].phase = 1; steps[1].formPos = kTutPosLeft;   steps[1].textId = 101;
    steps[2].phase = 2; steps[2].formPos = kTutPosBottom; steps[2].textId = 102;
    TutorialChapter ch; std::memset(&ch, 0, sizeof(ch));
    ch.stepCount = 3; ch.steps = steps; ch.next = nullptr;

    TutorialState st; std::memset(&st, 0, sizeof(st));
    st.active = true; st.chapter = &ch; st.stepIndex = 0; st.lastPhase = 0;

    // step 0: phase 1 != lastPhase 0, formPos<4 -> rebuild.
    CHECK(TutorialClassifyAdvance(st) == TutorialAdvance::kRebuildForm);
    CHECK_EQ(TutorialAdvanceReturn(TutorialClassifyAdvance(st)), 0);
    TutorialCommitStep(st, steps[0]);
    st.stepIndex = 1;
    // step 1: phase 1 == lastPhase 1 -> reuse.
    CHECK(TutorialClassifyAdvance(st) == TutorialAdvance::kReuseForm);
    TutorialCommitStep(st, steps[1]);
    st.stepIndex = 2;
    // step 2: phase 2 != 1 -> rebuild (bottom form).
    CHECK(TutorialClassifyAdvance(st) == TutorialAdvance::kRebuildForm);
    CHECK(std::strcmp(TutorialFormResource(steps[2].formPos), "tutorial\\bottom_form") == 0);
    TutorialCommitStep(st, steps[2]);
    st.stepIndex = 3;
    // past the end -> end chapter (return 0).
    CHECK(TutorialClassifyAdvance(st) == TutorialAdvance::kEndChapter);
}

TEST(WorldTutorial, EndSentinelAndNoChapter) {
    TutorialStep step; std::memset(&step, 0, sizeof(step));
    step.phase = 5; step.formPos = kTutorialPosEnd;  // >= 4 -> end
    TutorialChapter ch; std::memset(&ch, 0, sizeof(ch));
    ch.stepCount = 1; ch.steps = &step;
    TutorialState st; std::memset(&st, 0, sizeof(st));
    st.active = true; st.chapter = &ch; st.lastPhase = 0;
    CHECK(TutorialClassifyAdvance(st) == TutorialAdvance::kEndOfSteps);
    CHECK_EQ(TutorialAdvanceReturn(TutorialAdvance::kEndOfSteps), -4);
    CHECK(TutorialFormResource(kTutorialPosEnd) == nullptr);

    st.chapter = nullptr;
    CHECK(TutorialClassifyAdvance(st) == TutorialAdvance::kNoChapter);
}

TEST(WorldTutorial, ChainLength) {
    TutorialChapter a, b, c;
    std::memset(&a, 0, sizeof(a)); std::memset(&b, 0, sizeof(b)); std::memset(&c, 0, sizeof(c));
    a.next = &b; b.next = &c; c.next = nullptr;
    CHECK_EQ(TutorialChainLength(&a), 3);
    CHECK_EQ(TutorialChainLength(nullptr), 0);
}

// ===========================================================================
// Guild eligibility
// ===========================================================================
namespace {
bool MasterYes(int, void*) { return true; }
bool MasterNo(int, void*) { return false; }
} // namespace

TEST(WorldGuild, RankToCategory) {
    CHECK_EQ(GuildRankToQueryCategory(30), 23);
    CHECK_EQ(GuildRankToQueryCategory(31), 24);
    CHECK_EQ(GuildRankToQueryCategory(32), 25);
    CHECK_EQ(GuildRankToQueryCategory(33), 26);
    CHECK_EQ(GuildRankToQueryCategory(29), -1);
    CHECK_EQ(GuildRankToQueryCategory(34), -1);
}

TEST(WorldGuild, Eligibility) {
    GuildPlayer p; p.rank = 31; p.flags459 = 0; p.money = 10;
    // valid rank, master present, not in guild, enough money -> eligible.
    CHECK(GuildCheckRankLevel2(p, &MasterYes, nullptr) == GuildEligibility::kEligible);
    // no master -> not guild.
    CHECK(GuildCheckRankLevel2(p, &MasterNo, nullptr) == GuildEligibility::kNotGuild);
    // already in guild (flag bit 3).
    p.flags459 = kGuildFlagInGuild;
    CHECK(GuildCheckRankLevel2(p, &MasterYes, nullptr) == GuildEligibility::kAlreadyInGuild);
    // insufficient funds.
    p.flags459 = 0; p.money = 4;
    CHECK(GuildCheckRankLevel2(p, &MasterYes, nullptr) == GuildEligibility::kInsufficientFunds);
    // wrong rank.
    p.rank = 10; p.money = 100;
    CHECK(GuildCheckRankLevel2(p, &MasterYes, nullptr) == GuildEligibility::kNotGuild);
}

TEST(WorldGuild, Level2Dispatch) {
    CHECK(GuildLevel2Dispatch(GuildEligibility::kEligible) == GuildLevel2Action::kShowJoinDialog);
    CHECK(GuildLevel2Dispatch(GuildEligibility::kAlreadyInGuild) == GuildLevel2Action::kShowMessageBox);
    CHECK(GuildLevel2Dispatch(GuildEligibility::kInsufficientFunds) == GuildLevel2Action::kCheckSkill);
    CHECK(GuildLevel2Dispatch(GuildEligibility::kNotGuild) == GuildLevel2Action::kCheckSkill);
}

// ===========================================================================
// History notify text-id selectors (extended)
// ===========================================================================
TEST(WorldHistory, NotifyTextIds) {
    CHECK_EQ(HistoryNotifyUseItemTextId(6), 7316);
    CHECK_EQ(HistoryNotifyUseItemTextId(7), 7316);
    CHECK_EQ(HistoryNotifyUseItemTextId(3), -1);
    CHECK_EQ(HistoryNotifyBuildingLinkRemovedTextId(6), 7318);
    CHECK_EQ(HistoryNotifyOfficeTransferTextId(7), 7317);
    CHECK_EQ(HistoryNotifyCrimeAddedTextId(6), 7328);
    CHECK_EQ(HistoryNotifyRivalEventTextId(), 4954);   // ungated
    CHECK_EQ(HistoryNotifyLawChangeTextId(/*target*/ 5, /*local*/ 9), 7327);
    CHECK_EQ(HistoryNotifyLawChangeTextId(/*target*/ 9, /*local*/ 9), -1);
    // OfficeSwap: party A kind 6 -> 6604.
    CHECK_EQ(HistoryNotifyOfficeSwapTextId(0, 6, 0), 6604);
    // party B kind 6 -> 6604.
    CHECK_EQ(HistoryNotifyOfficeSwapTextId(0, 0, 6), 6604);
    // neither party kind 6, subject not 6 -> 6605.
    CHECK_EQ(HistoryNotifyOfficeSwapTextId(0, 0, 0), 6605);
    // subject is kind 6 (and neither party) -> -1.
    CHECK_EQ(HistoryNotifyOfficeSwapTextId(6, 0, 0), -1);
}

// ===========================================================================
// Statistics tax-window query (extended)
// ===========================================================================
TEST(WorldStatistics, TaxRowRound) {
    // current round 100, 17-row window: row 0 -> 84, row 16 -> 100.
    CHECK_EQ(StatisticsTaxRowRound(100, 0), 84);
    CHECK_EQ(StatisticsTaxRowRound(100, 16), 100);
    CHECK_EQ(StatisticsTaxRowRound(50, 8), 42);
}

// ===========================================================================
// Stammbaum succession queries (extended)
// ===========================================================================
TEST(WorldStammbaum, HeirsAndAncestors) {
    FamilyRecord recs[4];
    std::memset(recs, 0, sizeof(recs));
    recs[0].id = 1; // grandparent
    for (int i = 0; i < kMaxChildren; ++i) recs[0].children[i] = kFamilyNone;
    recs[0].children[0] = 2; recs[0].father = kFamilyNone; recs[0].mother = kFamilyNone;
    recs[1].id = 2; // parent
    for (int i = 0; i < kMaxChildren; ++i) recs[1].children[i] = kFamilyNone;
    recs[1].children[0] = 3; recs[1].children[1] = 4;
    recs[1].father = 1; recs[1].mother = kFamilyNone;
    recs[2].id = 3; recs[2].father = 2; recs[2].mother = kFamilyNone;
    for (int i = 0; i < kMaxChildren; ++i) recs[2].children[i] = kFamilyNone;
    recs[3].id = 4; recs[3].father = 2; recs[3].mother = kFamilyNone;
    for (int i = 0; i < kMaxChildren; ++i) recs[3].children[i] = kFamilyNone;
    FamilyTree tree; tree.records = recs; tree.count = 4;

    i32 heirs[kMaxHeirs];
    int n = FamilyCollectHeirs(tree, 2, heirs, kMaxHeirs);
    CHECK_EQ(n, 2);
    CHECK_EQ(heirs[0], 3);
    CHECK_EQ(heirs[1], 4);

    CHECK(FamilyIsAncestorOf(tree, /*ancestor*/ 1, /*descendant*/ 3)); // grandparent
    CHECK(FamilyIsAncestorOf(tree, 2, 4));                              // parent
    CHECK(!FamilyIsAncestorOf(tree, 3, 1));                            // child not ancestor
    CHECK(!FamilyIsAncestorOf(tree, 4, 3));                            // siblings
}

// ===========================================================================
// Privilege dispatch return codes (extended)
// ===========================================================================
TEST(WorldPrivilege, SimpleAndBuildCmdResults) {
    // SimpleCmd: subject kind 6 -> office path, immune -> -127.
    CHECK_EQ(PrivilegeSimpleCmdResult(6, /*immune*/ true, false), -127);
    CHECK_EQ(PrivilegeSimpleCmdResult(6, /*immune*/ false, false), 2);
    // SimpleCmd: ordinary subject -> 16 (target exists) / 96 (no target).
    CHECK_EQ(PrivilegeSimpleCmdResult(0, false, true), 16);
    CHECK_EQ(PrivilegeSimpleCmdResult(0, false, false), 96);
    // BuildCmd: office path -> 1 (shown) / 0 (cancel); ordinary -> 16/96.
    CHECK_EQ(PrivilegeBuildCmdResult(7, /*shown*/ true, false), 1);
    CHECK_EQ(PrivilegeBuildCmdResult(7, /*shown*/ false, false), 0);
    CHECK_EQ(PrivilegeBuildCmdResult(0, false, true), 16);
    CHECK_EQ(PrivilegeBuildCmdResult(0, false, false), 96);
}

// ===========================================================================
// HARDENING (wave-12): trial verdict/scoring + session FSM edge cases —
// 0/many participants, empty evidence, out-of-range torture instrument.
// Clean under -fsanitize=address,undefined.
// ===========================================================================
namespace {
bool TrialHardenLawLookup(u8 /*type*/, LawRecord* out, void* /*ctx*/) {
    if (out) { *out = LawRecord{}; out->penalty = 2; }
    return true;
}
} // namespace

TEST(TrialHarden, ComputeScoreNullAndEmpty) {
    // Null crimes / zero count -> empty score, no read.
    TrialScore a = TrialComputeEvidenceScore(nullptr, 0, 0, TrialHardenLawLookup, nullptr);
    CHECK_EQ(a.totalCount, 0);
    CHECK_EQ(a.uniqueCount, 0);
    CHECK(a.score == 0.0f);
    CrimeRecord none{};
    TrialScore b = TrialComputeEvidenceScore(&none, 0, 0, TrialHardenLawLookup, nullptr);
    CHECK_EQ(b.totalCount, 0);
    // Negative count is treated as empty.
    TrialScore c = TrialComputeEvidenceScore(&none, -5, 0, TrialHardenLawLookup, nullptr);
    CHECK_EQ(c.totalCount, -5);   // totalCount echoes the raw arg
    CHECK_EQ(c.uniqueCount, 0);
}

TEST(TrialHarden, ComputeScoreManyCrimesNoOverflow) {
    // More than the internal dedup mirror (512) — must not overrun consumed[512].
    std::vector<CrimeRecord> crimes(700);
    for (auto& c : crimes) c.location = 5;       // all the same law type
    TrialScore s = TrialComputeEvidenceScore(crimes.data(), 700, 0,
                                             TrialHardenLawLookup, nullptr);
    CHECK_EQ(s.totalCount, 700);
    // Dedup is bounded to the first 512; all share one law type -> 1 unique.
    CHECK_EQ(s.uniqueCount, 1);
}

TEST(TrialHarden, TallyVerdictZeroAndManyJurors) {
    int total = -1;
    // 0 jurors -> total 0 -> below threshold -> convicted.
    CHECK((int)TrialTallyVerdict(nullptr, 0, &total) == (int)TrialVerdict::kConvicted);
    CHECK_EQ(total, 0);
    // Null votes with positive count must not deref.
    total = -1;
    TrialTallyVerdict(nullptr, 5, &total);
    CHECK_EQ(total, 0);
    // Many jurors summing past threshold -> acquitted.
    std::vector<int> votes(100, 1);
    CHECK((int)TrialTallyVerdict(votes.data(), 100, &total) == (int)TrialVerdict::kAcquitted);
    CHECK_EQ(total, 100);
}

TEST(TrialHarden, SessionTortureInstrumentClamped) {
    int votes[3] = {0, 0, 0};            // convict
    CrimeRecord ev[1] = {};
    TrialSetup st;
    st.juryVotes = votes; st.juryCount = 3;
    st.evidence = ev; st.evidenceCount = 1;
    st.torture = true;
    // Run with an out-of-range and a negative instrument index; the FSM clamps
    // into [0, kTrialTortureInstrumentCount-1] before indexing the .esc table.
    for (int inst : {999, -3, 7, 6, 0}) {
        TrialSession s;
        st.tortureInstrument = inst;
        TrialSessionInit(s, st, TrialHardenLawLookup, nullptr);
        TrialSessionLeaves leaves{};      // all no-op
        TrialSessionRun(s, leaves);
        CHECK(s.finished);
        CHECK((int)s.phase == (int)TrialPhase::kDone);
    }
}

TEST(TrialHarden, SessionEmptyParticipantsRunsToDone) {
    TrialSetup st;                        // all defaults: no jury, no evidence
    TrialSession s;
    TrialSessionInit(s, st, TrialHardenLawLookup, nullptr);
    TrialSessionLeaves leaves{};
    TrialVerdict v = TrialSessionRun(s, leaves);
    CHECK(s.finished);
    (void)v;
}

// ===========================================================================
// Wave-14 1:1 value pins (W14-LAW).
// ===========================================================================

// Pin all 7 torture-instrument .esc scene names (gilde.exe @0x49D658, 16-byte
// stride). Values traced verbatim to kTrialTortureEsc in src/world/trial_session
// .cpp (recovered via get_string); the torture phase indexes this table by the
// clamped instrument selection.
TEST(WorldTrialW14, TortureEscTableGolden) {
    CHECK_EQ(kTrialTortureInstrumentCount, 7);
    const char* expect[7] = {
        "dschraube.esc",  "stiefel.esc",    "peitsche.esc",  "brandeisen.esc",
        "eistropfer.esc", "kaefig.esc",     "streckbank.esc",
    };
    for (int i = 0; i < kTrialTortureInstrumentCount; ++i)
        CHECK(std::strcmp(kTrialTortureEsc[i], expect[i]) == 0);
}

// Pin the wanted-weight table BYTES (dword_49D644[5]) exactly and the full clamp
// range, so the whole table is asserted (not just the endpoints).
TEST(WorldTrialW14, WantedWeightTableFullAndBits) {
    const float expect[5] = {1.4f, 1.2f, 1.0f, 0.8f, 0.6f};
    for (int i = 0; i < 5; ++i)
        CHECK(approx(kTrialWantedWeight[i], expect[i]));
    // clamp covers [<=0 -> 0] .. [>=4 -> 4] inclusive.
    CHECK(approx(TrialWantedWeight(2), 1.0f));   // exact mid (untested above)
    CHECK(approx(TrialWantedWeight(0), 1.4f));
    CHECK(approx(TrialWantedWeight(4), 0.6f));
}

// Pin the four trial-fine scaling constants and the acquit threshold to their
// recovered values (the float-bit identities documented in trial.h).
TEST(WorldTrialW14, FineConstantsAndThreshold) {
    CHECK(approx(kTrialFineBasisScale, 0.2f));        // flt_61CCF4 0x3E4CCCCD
    CHECK(approx(kTrialFavorScale, 0.01f));           // flt_61CCF8 0x3C23D70A
    CHECK(approx(kTrialTortureConfessScale, 1.1f));   // flt_61CCFC 0x3F8CCCCD
    CHECK(approx(kTrialTortureDenyScale, 0.8f));      // flt_61CD00 0x3F4CCCCD
    CHECK_EQ(kTrialAcquitThreshold, 2);
}
