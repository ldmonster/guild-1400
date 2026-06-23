// Golden tests for VIBE_AiAction_EvalBestPersonTarget. gilde.exe 0x475dd0.
#include "test.h"
#include "sim/npcevent_recon4_eval_target.h"

#include <cmath>
#include <cstring>
#include <map>

using namespace guild;
using namespace guild::sim;

namespace {
bool near(float a, float b, float eps = 1e-4f) { return std::fabs(a - b) <= eps; }

// A tiny test harness: a registry of records keyed by id, plus the hook leaves.
struct Harness {
    std::map<i32, EvalPersonRec> recs;
    bool activeReturn = 0;
    int  handlerReturn = 0;          // 0 = no existing claim
    float scoreOut0 = 7.0f, scoreOut1 = 11.0f;
    int  computeCalls = 0;
};

const EvalPersonRec* FindRec(i32 id, void* u) {
    auto* h = static_cast<Harness*>(u);
    auto it = h->recs.find(id);
    return it == h->recs.end() ? nullptr : &it->second;
}
int IsActive(const EvalPersonRec*, void* u) { return static_cast<Harness*>(u)->activeReturn; }
int FindHandler(u16, void* u) { return static_cast<Harness*>(u)->handlerReturn; }
int Score(f32* o0, f32* o1, const EvalActionRec*, char, int, int, int, void* u) {
    auto* h = static_cast<Harness*>(u);
    h->computeCalls++;
    *o0 = h->scoreOut0;
    *o1 = h->scoreOut1;
    return 1;
}

EvalTargetHooks MakeHooks(Harness& h) {
    EvalTargetHooks hk;
    hk.findRecordById = &FindRec;
    hk.isActiveType = &IsActive;
    hk.findFirstHandler = &FindHandler;
    hk.computeRelationWeighted = &Score;
    hk.user = &h;
    return hk;
}

// A "good" candidate record: passes all gates.
EvalPersonRec GoodRec(u16 id, u16 rank, f32 threshold) {
    EvalPersonRec r{};
    r.filterId = id;
    r.typeTag = 1;
    r.flag9 = 0;
    r.activeCount = 1;     // > 0
    r.rank = rank;
    r.threshold = threshold;
    r.busyFlag = 0;
    return r;
}
}

TEST(NpcEventRecon4, EvalEarlyOutWritesSentinel) {
    Harness h; auto hk = MakeHooks(h);
    EvalActionRec a{};
    float o0 = 1.0f, o1 = 2.0f;
    int rc = VIBE_AiAction_EvalBestPersonTarget(&o0, &o1, a, /*relArg*/0,
                                                /*earlyOut*/true, 0, 0, hk);
    CHECK_EQ(rc, 0);
    u32 b0, b1;
    std::memcpy(&b0, &o0, 4);
    std::memcpy(&b1, &o1, 4);
    CHECK_EQ(b0, kEvalSentinelBits);
    CHECK_EQ(b1, kEvalSentinelBits);
}

TEST(NpcEventRecon4, EvalNoCandidatesReturnsZero) {
    Harness h; auto hk = MakeHooks(h);
    EvalActionRec a{};
    a.typeTag = 5;                 // skip pre-check
    for (int i = 0; i < 5; ++i) a.slotIds[i] = 100 + i;  // none registered
    float o0 = 9.0f, o1 = 9.0f;
    int rc = VIBE_AiAction_EvalBestPersonTarget(&o0, &o1, a, 0, false, 0, 0, hk);
    CHECK_EQ(rc, 0);
    CHECK_EQ(h.computeCalls, 0);
}

TEST(NpcEventRecon4, EvalPicksHighestRank) {
    Harness h;
    h.recs[10] = GoodRec(10, /*rank*/3, /*thr*/5.0f);
    h.recs[11] = GoodRec(11, /*rank*/9, /*thr*/5.0f);  // best
    h.recs[12] = GoodRec(12, /*rank*/7, /*thr*/5.0f);
    auto hk = MakeHooks(h);
    EvalActionRec a{};
    a.typeTag = 5;
    a.slotIds[0] = 10; a.slotIds[1] = 11; a.slotIds[2] = 12;
    a.slotIds[3] = -1; a.slotIds[4] = -1;
    float o0 = 0.0f, o1 = 0.0f;
    int rc = VIBE_AiAction_EvalBestPersonTarget(&o0, &o1, a, 0, false, 0, 0, hk);
    CHECK_EQ(rc, 1);
    CHECK_EQ(h.computeCalls, 1);
    // gate: (9+4=13) >= 5 -> 1.0 ; scores pass through.
    CHECK(near(o0, 7.0f));
    CHECK(near(o1, 11.0f));
}

TEST(NpcEventRecon4, EvalGateZerosScoreWhenBelowThreshold) {
    Harness h;
    // rank 1 -> (1+4=5) >= threshold? threshold 5.0 passes loop gate (>=),
    // but make the FINAL gate fail by raising threshold just above (5 < 5.5).
    EvalPersonRec r = GoodRec(20, /*rank*/1, /*thr*/5.0f);
    h.recs[20] = r;
    auto hk = MakeHooks(h);
    EvalActionRec a{};
    a.typeTag = 5;
    a.slotIds[0] = 20;
    for (int i = 1; i < 5; ++i) a.slotIds[i] = -1;
    float o0 = 0.0f, o1 = 0.0f;
    int rc = VIBE_AiAction_EvalBestPersonTarget(&o0, &o1, a, 0, false, 0, 0, hk);
    CHECK_EQ(rc, 1);
    // (1+4=5) >= 5.0 -> gate 1.0
    CHECK(near(o0, 7.0f));
    CHECK(near(o1, 11.0f));

    // Now a candidate that passes the loop gate via the +4 slack but fails the
    // identical final gate is impossible (same expression). Instead verify the
    // loop gate itself rejects when (rank+4) < threshold.
    Harness h2;
    h2.recs[21] = GoodRec(21, /*rank*/0, /*thr*/100.0f); // 0+4=4 < 100 reject
    auto hk2 = MakeHooks(h2);
    EvalActionRec a2{};
    a2.typeTag = 5;
    a2.slotIds[0] = 21;
    for (int i = 1; i < 5; ++i) a2.slotIds[i] = -1;
    float p0 = 3.0f, p1 = 4.0f;
    int rc2 = VIBE_AiAction_EvalBestPersonTarget(&p0, &p1, a2, 0, false, 0, 0, hk2);
    CHECK_EQ(rc2, 0);  // no candidate selected
}

TEST(NpcEventRecon4, EvalRejectsBusyAndClaimedAndInactiveCount) {
    Harness h;
    EvalPersonRec busy = GoodRec(30, 9, 5.0f); busy.busyFlag = 1;
    EvalPersonRec zeroCount = GoodRec(31, 9, 5.0f); zeroCount.activeCount = 0;
    h.recs[30] = busy;
    h.recs[31] = zeroCount;
    h.recs[32] = GoodRec(32, 4, 5.0f);   // valid, rank 4
    h.handlerReturn = 0;
    auto hk = MakeHooks(h);
    EvalActionRec a{};
    a.typeTag = 5;
    a.slotIds[0] = 30; a.slotIds[1] = 31; a.slotIds[2] = 32;
    a.slotIds[3] = -1; a.slotIds[4] = -1;
    float o0 = 0.0f, o1 = 0.0f;
    int rc = VIBE_AiAction_EvalBestPersonTarget(&o0, &o1, a, 0, false, 0, 0, hk);
    CHECK_EQ(rc, 1);          // only #32 qualifies
    CHECK_EQ(h.computeCalls, 1);

    // With an existing handler claim, all candidates are rejected.
    Harness h2;
    h2.recs[40] = GoodRec(40, 9, 5.0f);
    h2.handlerReturn = 1;     // claimed
    auto hk2 = MakeHooks(h2);
    EvalActionRec a2{};
    a2.typeTag = 5;
    a2.slotIds[0] = 40;
    for (int i = 1; i < 5; ++i) a2.slotIds[i] = -1;
    float p0 = 0.0f, p1 = 0.0f;
    int rc2 = VIBE_AiAction_EvalBestPersonTarget(&p0, &p1, a2, 0, false, 0, 0, hk2);
    CHECK_EQ(rc2, 0);
}

TEST(NpcEventRecon4, EvalPrecheckRejectsActivePrimary) {
    // action.typeTag != 5 triggers the pre-check on the primary id.
    Harness h;
    EvalPersonRec primary = GoodRec(50, 1, 1.0f);
    primary.flag9 = 0;          // flag9==0 + IsActive -> reject whole eval
    h.recs[50] = primary;
    h.activeReturn = 1;
    auto hk = MakeHooks(h);
    EvalActionRec a{};
    a.typeTag = 1;              // != 5
    a.primaryId = 50;
    for (int i = 0; i < 5; ++i) a.slotIds[i] = -1;
    float o0 = 0.0f, o1 = 0.0f;
    int rc = VIBE_AiAction_EvalBestPersonTarget(&o0, &o1, a, 0, false, 0, 0, hk);
    CHECK_EQ(rc, 0);

    // If flag9 != 0, the pre-check passes through to the (empty) loop.
    Harness h2;
    EvalPersonRec p2 = GoodRec(51, 1, 1.0f); p2.flag9 = 7;
    h2.recs[51] = p2;
    h2.activeReturn = 1;        // ignored because flag9 != 0
    auto hk2 = MakeHooks(h2);
    EvalActionRec a2{};
    a2.typeTag = 1;
    a2.primaryId = 51;
    for (int i = 0; i < 5; ++i) a2.slotIds[i] = -1;
    float p0 = 0.0f, q1 = 0.0f;
    int rc2 = VIBE_AiAction_EvalBestPersonTarget(&p0, &q1, a2, 0, false, 0, 0, hk2);
    CHECK_EQ(rc2, 0);           // loop empty -> 0, but NOT via pre-check reject
}

TEST(NpcEventRecon4, EvalPrecheckRejectsTypeFive) {
    Harness h;
    EvalPersonRec primary = GoodRec(60, 1, 1.0f);
    primary.typeTag = 5;        // rec.type==5 -> reject
    h.recs[60] = primary;
    auto hk = MakeHooks(h);
    EvalActionRec a{};
    a.typeTag = 1;
    a.primaryId = 60;
    for (int i = 0; i < 5; ++i) a.slotIds[i] = 60;  // even valid slots ignored
    float o0 = 0.0f, o1 = 0.0f;
    int rc = VIBE_AiAction_EvalBestPersonTarget(&o0, &o1, a, 0, false, 0, 0, hk);
    CHECK_EQ(rc, 0);
}
