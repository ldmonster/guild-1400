// Integration test: the interaction3 approach/move-target functions wired against
// the REAL reconstructed relation scorer sibling guild::ai::ComputeRelationWeighted
// (ai/intrigue.cpp, gilde.exe 0x4796b0 — NOT a mock). This is exactly the live
// wiring: VIBE_Interaction_ComputeApproachOffset (0x46b654) and
// VIBE_Interaction_ComputeMoveTargetA (0x46dc80) both call AiScore_ComputeRelationWeighted
// through the Interaction3Hooks.relationWeighted leaf; production binds that leaf to
// the genuine scorer over the 148-byte method-table entry + person scratch. We
// forward the leaf into the real ai::ComputeRelationWeighted, feeding the same
// 148-byte entry/scratch the binary would, take its (a,b) accumulators into the
// (outX,outY) the register ABI returns, and assert the cross-module flow: the real
// scorer's weighted accumulation, then interaction3's ×10 / ×1 approach scaling.
#include "test.h"

#include "sim/interaction3.h"
#include "ai/intrigue.h"     // REAL scorer sibling: ai::ComputeRelationWeighted

#include <cstring>

using namespace guild;
using guild::sim::g_i3Hooks;
using guild::ai::PersonScoreScratch;
using guild::ai::RelationScore;

namespace {

// The single live method entry + person scratch the relation scorer reads. The hook
// is a plain C function pointer, so it forwards into these globals — the genuine
// reconstructed scorer, exercised end to end.
u8 g_entry[148];
PersonScoreScratch g_person;
bool g_scorerReached = false;
RelationScore g_lastScore;

// Helpers to lay out the entry/scratch the way the real scorer indexes them.
void SetF(u8* b, int o, float f) { std::memcpy(b + o, &f, sizeof(f)); }
void SetDW(u8* b, int o, u32 v)  { std::memcpy(b + o, &v, sizeof(v)); }

// Production binds relationWeighted to AiScore_ComputeRelationWeighted. The `record`
// the interaction functions pass is the 148-byte entry base; we forward it into the
// REAL scorer and unpack its accumulators into the register-ABI (outX,outY).
int RealRelationWeighted(float* outX, float* outY, int /*record*/, char /*relFlag*/,
                         int /*extra*/, int /*a5*/, int /*a6*/) {
    g_scorerReached = true;
    g_lastScore = guild::ai::ComputeRelationWeighted(g_entry, g_person);
    *outX = g_lastScore.a;
    *outY = g_lastScore.b;
    return g_lastScore.eligible ? 1 : 0;
}

bool g_factorTen = true;
bool HookFactorTen(int) { return g_factorTen; }
int  HookLawInactive(int) { return 0; }   // forces ComputeMoveTargetA onto weighted

void WireRealScorer() {
    guild::sim::ResetInteraction3Hooks();
    g_i3Hooks.relationWeighted = &RealRelationWeighted;
    g_i3Hooks.approachFactorIsTen = &HookFactorTen;
    g_i3Hooks.lawRecordActive = &HookLawInactive;
    g_scorerReached = false;
    g_factorTen = true;

    // Build a deterministic entry/scratch (see the golden computation below).
    std::memset(g_entry, 0, sizeof(g_entry));
    g_person = PersonScoreScratch{};
    SetDW(g_person.bytes, 301, 1u << 24);   // person method class id == 1
    SetF(g_person.bytes, 156, 2.0f);        // relation weight for class 1
    SetF(g_person.bytes, 144, 3.0f);        // relation weight for class 0
    g_person.bytes[532] = 0;                // no visited bits -> /50,/100 divisors
    SetDW(g_entry, 142, 0);                 // gate mask 0
    SetDW(g_entry, 45, 1u << 24);           // own sub-entry 0: class 1
    SetF(g_entry, 52, 4.0f);                // own sub-entry 0: weight 4 (>0 -> eligible)
    g_entry[48] = 0;                        // own sub-entry 0 enable byte (>=0 counted)
}

} // namespace

// The REAL scorer deterministically accumulates a single class-1 own sub-entry into
// `a` (and 0 into `b`); interaction3's ×10 approach scaling must lift exactly that
// real value by 10. We bind the golden to the REAL sibling's own output (the source
// of truth) and assert interaction3 composed its ×10 scaling on top of it.
TEST(Interaction3Itest, ApproachOffsetScalesRealScorerByTen) {
    WireRealScorer();
    g_factorTen = true;

    // The real scorer's raw accumulator for this entry/scratch (computed by the
    // genuine sibling, not a mock).
    RelationScore golden = guild::ai::ComputeRelationWeighted(g_entry, g_person);
    CHECK(golden.a > 0.0f);                 // the crafted entry produces a real score

    float x = 0, y = 0;
    int r = guild::sim::ComputeApproachOffset(&x, &y, /*record=*/0, /*relFlag=*/0,
                                              /*skip=*/0, 0, 0);
    CHECK_EQ(r, 1);
    CHECK(g_scorerReached);                 // the REAL scorer was reached
    CHECK_EQ(x, golden.a * 10.0f);          // ×10 of the genuine accumulator
    CHECK_EQ(y, golden.b * 10.0f);
}

// Factor-1 path leaves the real scorer's raw accumulator unscaled (==1× golden).
TEST(Interaction3Itest, ApproachOffsetFactorOnePassesRealScorerThrough) {
    WireRealScorer();
    g_factorTen = false;

    RelationScore golden = guild::ai::ComputeRelationWeighted(g_entry, g_person);

    float x = 0, y = 0;
    CHECK_EQ(guild::sim::ComputeApproachOffset(&x, &y, 0, 0, 0, 0, 0), 1);
    CHECK(g_scorerReached);
    CHECK_EQ(x, golden.a);                  // genuine accumulator, unscaled
    CHECK_EQ(y, golden.b);
}

// ComputeMoveTargetA (inactive law) routes the same real scorer into its weighted
// branch; the (outX,outY) carry the genuine accumulators with no extra scaling.
TEST(Interaction3Itest, MoveTargetAWeightedBranchUsesRealScorer) {
    WireRealScorer();

    RelationScore golden = guild::ai::ComputeRelationWeighted(g_entry, g_person);

    float x = 0, y = 0;
    int r = guild::sim::ComputeMoveTargetA(&x, &y, /*record=*/0, /*relFlag=*/0, 0, 0);
    CHECK(g_scorerReached);
    // result mirrors the real scorer's eligibility flag (0/1).
    CHECK_EQ(r, golden.eligible ? 1 : 0);
    CHECK_EQ(x, golden.a);
    CHECK_EQ(y, golden.b);
}

// The skip short-circuit must NOT reach the real scorer at all.
TEST(Interaction3Itest, SkipShortCircuitsBeforeRealScorer) {
    WireRealScorer();
    float x = 0, y = 0;
    CHECK_EQ(guild::sim::ComputeApproachOffset(&x, &y, 0, 0, /*skip=*/1, 0, 0), 0);
    CHECK(!g_scorerReached);                 // scorer never invoked
    CHECK_EQ(x, -1.0e30f);
}
