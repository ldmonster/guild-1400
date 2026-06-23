// Unit tests for the MeisterAi economy/intrigue director (guild::ai):
//   AiPlayer record accessors, the economy/mood rule cores, intrigue selection
//   (seeded RNG), and the card-game AI move selection.
#include "tests/framework/test.h"

#include "ai/aiplayer.h"
#include "ai/cardgame.h"
#include "ai/intrigue.h"
#include "ai/meisterai.h"
#include "crt/rand.h"
#include "util/math_random.h"
#include "util/math_rng_float.h"

#include <cstring>

using namespace guild;

// ---------------------------------------------------------------------------
// AiPlayer 589-byte record accessors at the recovered offsets.
// ---------------------------------------------------------------------------
TEST(AiMeister, AiPlayerRecordAccessors) {
    // Build a 3-type table (3 * 589 bytes) and write known fields.
    std::vector<u8> buf(3 * ai::kAiPlayerStride, 0);
    ai::AiPlayerTable tbl{buf.data(), true};

    // type 2: class=Bank(5), multiplier=3, guard target hi byte = 0x2A.
    buf[ai::kAiPlayerStride * 2 + 0]   = ai::kAiClassBank;
    buf[ai::kAiPlayerStride * 2 + 583] = 3;
    buf[ai::kAiPlayerStride * 2 + 544 + 3] = 0x2A; // HIBYTE of the +544 dword

    CHECK_EQ(tbl.ai_class(2), (u8)ai::kAiClassBank);
    CHECK_EQ(tbl.multiplier(2), (u8)3);
    CHECK_EQ(tbl.guard_target(2), (u8)0x2A);
    CHECK_EQ((int)(tbl.record(2) - buf.data()), ai::kAiPlayerStride * 2);
}

// ---------------------------------------------------------------------------
// EvaluateMeister dispatch classifier.
// ---------------------------------------------------------------------------
TEST(AiMeister, MeisterRoutineDispatch) {
    // category 1/4 + craft class -> craft production; + other -> production.
    CHECK(ai::ClassifyMeisterRoutine(1, ai::kAiClassCraftA) == ai::MeisterRoutine::kCraftProduction);
    CHECK(ai::ClassifyMeisterRoutine(4, 14) == ai::MeisterRoutine::kCraftProduction);
    CHECK(ai::ClassifyMeisterRoutine(1, 9) == ai::MeisterRoutine::kProduction);
    CHECK(ai::ClassifyMeisterRoutine(2, 9) == ai::MeisterRoutine::kFarming);
    CHECK(ai::ClassifyMeisterRoutine(0, ai::kAiClassWache) == ai::MeisterRoutine::kWache);
    CHECK(ai::ClassifyMeisterRoutine(0, ai::kAiClassDiebe) == ai::MeisterRoutine::kDiebe);
    CHECK(ai::ClassifyMeisterRoutine(0, ai::kAiClassAmbush) == ai::MeisterRoutine::kAmbush);
    CHECK(ai::ClassifyMeisterRoutine(0, ai::kAiClassBank) == ai::MeisterRoutine::kBank);
    CHECK(ai::ClassifyMeisterRoutine(0, ai::kAiClassProduction9) == ai::MeisterRoutine::kPlanProduction);
    CHECK(ai::ClassifyMeisterRoutine(0, 99) == ai::MeisterRoutine::kNone);
}

// ---------------------------------------------------------------------------
// Wave-20 (rule 13): EvaluateMeister is the dispatch-tail bind that connects
// ClassifyMeisterRoutine to guild::sim::DispatchMeisterCalc along the original's
// 0x4533a8 call edge. It must (a) return the classified routine and (b) dispatch
// without crashing under the inert/null Meister leaves (the original's null path).
// With meisterRec == nullptr the dispatched Calc takes the null-record early-out.
// ---------------------------------------------------------------------------
TEST(AiMeister, EvaluateMeisterReturnsClassifiedRoutine) {
    // A zeroed 536-byte meister record: every rdptr column reads 0 -> null handle,
    // so the dispatched Calc takes its null-record early-out (no leaves installed).
    unsigned char rec[536] = {0};
    CHECK(ai::EvaluateMeister(2, 9, rec, 0) == ai::MeisterRoutine::kFarming);
    CHECK(ai::EvaluateMeister(0, ai::kAiClassWache, rec, 0) == ai::MeisterRoutine::kWache);
    CHECK(ai::EvaluateMeister(0, ai::kAiClassDiebe, rec, 0) == ai::MeisterRoutine::kDiebe);
    CHECK(ai::EvaluateMeister(0, ai::kAiClassAmbush, rec, 0) == ai::MeisterRoutine::kAmbush);
    // Production / Bank / PlanProduction classify but DispatchMeisterCalc no-ops
    // them (their own planner module owns them); EvaluateMeister still returns the
    // classified routine.
    CHECK(ai::EvaluateMeister(1, 9, rec, 0) == ai::MeisterRoutine::kProduction);
    CHECK(ai::EvaluateMeister(0, ai::kAiClassBank, rec, 0) == ai::MeisterRoutine::kBank);
    CHECK(ai::EvaluateMeister(0, 99, rec, 0) == ai::MeisterRoutine::kNone);
}

// ---------------------------------------------------------------------------
// Security-heat decrement (golden, hand-computed).
// ---------------------------------------------------------------------------
TEST(AiMeister, SecurityHeatDecrement) {
    CHECK_EQ(ai::SecurityHeatDecrement(10, 2), (u8)6);   // 10 - 4
    CHECK_EQ(ai::SecurityHeatDecrement(10, 5), (u8)0);   // 10 - 10 = 0
    CHECK_EQ(ai::SecurityHeatDecrement(3, 5), (u8)0);    // -7 -> clamp 0
    // 200-2 = 198 = 0xC6; the result byte is treated as signed (bit7 set) so it
    // clamps to 0 — this is the intentional byte-truncating arithmetic.
    CHECK_EQ(ai::SecurityHeatDecrement(200, 1), (u8)0);
}

TEST(AiMeister, SecurityHeatSignedClamp) {
    // value 0xC6 (198) has bit7 set => signed-negative => 0.
    CHECK_EQ(ai::SecurityHeatDecrement(200, 1), (u8)0);
    // 100 - 2*10 = 80 (0x50, positive) => 80.
    CHECK_EQ(ai::SecurityHeatDecrement(100, 10), (u8)80);
    // 127 stays (0x7F positive); 128 (0x80) negative.
    CHECK_EQ(ai::SecurityHeatDecrement(127, 0), (u8)127);
    CHECK_EQ(ai::SecurityHeatDecrement(128, 0), (u8)0);
}

// ---------------------------------------------------------------------------
// Mood/relation delta (golden, hand-computed from the /3 and /5 rules).
// ---------------------------------------------------------------------------
TEST(AiMeister, MoodRelationDelta) {
    // attitudeA<100, relation>-120: delta = -((100-A)/3, min 1).
    //   A=70 -> (30/3)=10 -> -10.  attitudeB=100 (>=100) and relation<100? no
    //   (relation=0<100 yes but attitudeB>100 false) => part2 no change.
    CHECK_EQ((int)ai::MoodRelationDelta(70, 100, 0), -10);
    // A=99 -> (1/3)=0 -> min 1 -> -1.
    CHECK_EQ((int)ai::MoodRelationDelta(99, 100, 0), -1);
    // A>100, relation<100: delta = (A-100)/3 min 1.  A=130 -> 10.
    CHECK_EQ((int)ai::MoodRelationDelta(130, 100, 0), 10);
    // attitudeB<100, relation>-120: part2 adds (B-100)/5 (negative) -> clamp 1 -> +1.
    //   A=100 (>=100 & relation 0 -> part1: A<=100 true so skip) ; B=80 -> +1.
    CHECK_EQ((int)ai::MoodRelationDelta(100, 80, 0), 1);
    // Both extremes: A=130 (+10) then B=80 (+1) => +11.
    CHECK_EQ((int)ai::MoodRelationDelta(130, 80, 0), 11);
    // relation <= -120 forces part1 into the inner branch.
    //   A=130, relation=-200: part1 inner: A>100 & relation>=100? no(rel<100) ->
    //   compute (A-100)/3=10. part2: B=100,rel<=-120 -> inner: B>100? no -> 0.
    CHECK_EQ((int)ai::MoodRelationDelta(130, 100, -200), 10);
}

// ---------------------------------------------------------------------------
// Banker tax payout.
// ---------------------------------------------------------------------------
TEST(AiMeister, TaxPayout) {
    CHECK_EQ(ai::TaxPayout(0), 0);
    CHECK_EQ(ai::TaxPayout(1), 16000);
    CHECK_EQ(ai::TaxPayout(3), 48000);
}

// ---------------------------------------------------------------------------
// Mood-decay roll consumes RNG deterministically (seeded).
// ---------------------------------------------------------------------------
TEST(AiMeister, MoodDecayRollSeeded) {
    // Find the RNG behavior for a fixed seed by replaying RandomModulo.
    crt::Srand(12345);
    int r100 = util::RandomModulo(0x64);
    crt::Srand(12345);
    int decay = ai::MoodDecayRoll(0 /*kind*/, false /*flag*/);
    if (r100 <= 0x1E) {
        CHECK_EQ(decay, 0); // roll did not fire; only one RNG draw consumed
    } else {
        // fired: a second draw (RandomModulo(3)) gives the magnitude.
        crt::Srand(12345);
        util::RandomModulo(0x64);
        int mag = util::RandomModulo(3);
        CHECK_EQ(decay, -(mag + 2));
    }
    // Excluded kinds never decay regardless of the roll.
    crt::Srand(777);
    CHECK_EQ(ai::MoodDecayRoll(11, false), 0);
    crt::Srand(777);
    CHECK_EQ(ai::MoodDecayRoll(0, true), 0); // flagBit0 set => excluded
}

// ---------------------------------------------------------------------------
// Intrigue: single-attack acceptance + best-target tie-break (seeded RNG).
// ---------------------------------------------------------------------------
TEST(AiMeister, TrySingleAttackAccept) {
    // actionDepth > 3 always rejects.
    crt::Srand(1);
    CHECK(!ai::TrySingleAttackAccept(4));
    // actionDepth 0: gate = 0 => RandomFloatScaled() >= 0 is always true.
    crt::Srand(1);
    CHECK(ai::TrySingleAttackAccept(0));
}

TEST(AiMeister, PickBestTargetUnique) {
    ai::TargetCandidate c[4] = {};
    c[0] = {true, 1.0f, 100};
    c[1] = {true, 5.0f, 200}; // unique max
    c[2] = {false, 9.0f, 300};
    c[3] = {true, 2.0f, 400};
    CHECK_EQ(ai::PickBestTarget(c), (i32)200);

    ai::TargetCandidate none[4] = {};
    CHECK_EQ(ai::PickBestTarget(none), (i32)-1);
}

TEST(AiMeister, PickBestTargetTieSeeded) {
    ai::TargetCandidate c[4] = {};
    c[0] = {true, 5.0f, 100};
    c[1] = {true, 5.0f, 200};
    c[2] = {true, 5.0f, 300};
    c[3] = {true, 5.0f, 400};
    // All tied at 5.0: result is one of the four, chosen via RandomModulo(4) start.
    crt::Srand(42);
    i32 r = ai::PickBestTarget(c);
    CHECK(r == 100 || r == 200 || r == 300 || r == 400);
    // Deterministic for the seed: re-derive the start index.
    crt::Srand(42);
    int start = util::RandomModulo(4);
    CHECK_EQ(r, c[start].targetId);
}

// ---------------------------------------------------------------------------
// AiScore relation-weighted scorer over a synthetic method entry.
// ---------------------------------------------------------------------------
TEST(AiMeister, ComputeRelationWeighted) {
    u8 entry[148] = {};
    ai::PersonScoreScratch p; // value-initialized (bytes[] zeroed)

    // person method class = 2 (high byte of dword @+301).
    p.bytes[301 + 3] = 2;
    p.bytes[532] = 0; // no visited bits => not eligible by gate
    // gate mask at entry+142 (dword >> 16): set to 0 so the AND is 0.
    // sub-entry 0: the class id is the HIGH BYTE of the dword @+45, which is the
    //   SAME byte as the enable/sign byte @+48 (they overlap in the packed int).
    //   So class 2 (positive => counted) is written once at entry[48].
    entry[48] = 2;               // class 2 AND enable/sign (>= 0 => counted)
    float w = 1.0f;
    std::memcpy(&entry[52], &w, 4); // weight (float index 13)
    // relation weight for class 2: person+144+12*2 = +168 -> 4.0.
    float rel = 4.0f;
    std::memcpy(&p.bytes[144 + 12 * 2], &rel, 4);

    // The divisors come from the GATE (entry+142 & visited), not the pre-scan.
    // Gate is false here (entry+142 == 0), so divNear=50, divFar=100. The
    // pre-scan still sets the `eligible` return flag (matching positive sub-entry).
    ai::RelationScore s = ai::ComputeRelationWeighted(entry, p);
    CHECK(s.eligible);
    // class matches person class => divNear (50): a = w*rel/50 = 1*4/50 = 0.08.
    CHECK(s.a > 0.0799f && s.a < 0.0801f);
    CHECK(s.b == 0.0f);

    // Now make the gate fire: entry+142 dword >>16 AND visited != 0 => divNear 40.
    // (dword@142 >> 16) & visited: set entry[144]=1 (bit16 of the dword) and
    // visited bit 1 -> AND = 1 != 0.
    entry[144] = 1;     // bit 16 of the dword at entry+142
    p.bytes[532] = 1;   // visited bits
    ai::RelationScore s2 = ai::ComputeRelationWeighted(entry, p);
    CHECK(s2.eligible);
    // a = 1*4/40 = 0.1.
    CHECK(s2.a > 0.0999f && s2.a < 0.1001f);
}

// ---------------------------------------------------------------------------
// Intrigue action-label return codes.
// ---------------------------------------------------------------------------
TEST(AiMeister, IntrigueLabels) {
    CHECK_EQ(ai::EvalActionLabel(true, 99), (u8)39);
    CHECK_EQ(ai::EvalActionLabel(false, 99), (u8)99);
    CHECK_EQ(ai::EvalPamphlet(false, 5), (u8)0);
    CHECK_EQ(ai::EvalPamphlet(true, 5), (u8)41);
}

// ---------------------------------------------------------------------------
// Card-game move selection.
// ---------------------------------------------------------------------------
TEST(AiMeister, CardGameDecideMovePhase0) {
    ai::CardGameState st;
    st.seti32(12, 0x1000); // seat A entity ptr
    st.bytes[36] = 0;      // decision phase 0 -> deal
    CHECK_EQ(ai::DecideMove(st, 0x1000), (u8)1);
    // unknown seat -> 0.
    CHECK_EQ(ai::DecideMove(st, 0x9999), (u8)0);
}

TEST(AiMeister, CardGameDecideMoveHold) {
    ai::CardGameState st;
    st.seti32(12, 0x1000);
    st.bytes[36] = 1;            // decision 1 (act)
    st.bytes[16] = 2;            // hand size 2
    st.bytes[20] = 3; st.bytes[21] = 4; // hand sum 7; 17-7=10 >=6 => Hold(2)
    st.bytes[18] = 0;            // mood low
    CHECK_EQ(ai::DecideMove(st, 0x1000), (u8)2);

    // sum == 17 -> Draw(3).
    st.bytes[16] = 3;
    st.bytes[20] = 6; st.bytes[21] = 6; st.bytes[22] = 5; // 17
    CHECK_EQ(ai::DecideMove(st, 0x1000), (u8)3);
}

TEST(AiMeister, CardGameCanPlayAndPlay) {
    ai::CardGameState st;
    st.seti32(12, 0x1000);
    st.bytes[36] = 0;            // decision 0
    // gilde.exe dword_46637D/byte_466381: transition result == the played action.
    // (0,1) is a valid transition -> next 1 (byte_466381[0]=0x01).
    CHECK_EQ(ai::CanPlayCard(st, 0x1000, 1), 1);
    CHECK_EQ(ai::PlayCard(st, 0x1000, 1), 1);
    CHECK_EQ(st.bytes[36], (u8)1);
    // now decision 1: (1,2) -> next 2 (byte_466381[2]=0x02).
    CHECK_EQ(ai::CanPlayCard(st, 0x1000, 2), 1);
    CHECK_EQ(ai::PlayCard(st, 0x1000, 2), 1);
    CHECK_EQ(st.bytes[36], (u8)2);
    // (2,4) -> next 4 (byte_466381[12]=0x04).
    CHECK_EQ(ai::CanPlayCard(st, 0x1000, 4), 1);
    CHECK_EQ(ai::PlayCard(st, 0x1000, 4), 1);
    CHECK_EQ(st.bytes[36], (u8)4);
    // (4,*) has no transition; and an unknown action is rejected.
    CHECK_EQ(ai::CanPlayCard(st, 0x1000, 9), 0);
}

TEST(AiMeister, CardGameShouldRaiseDefault) {
    // With the default hand-strength hook (0.0): threshold = 0-0+0.1 = 0.1.
    // RandomFloatScaled() in [0,1]; for a seed where it exceeds 0.1, no raise.
    crt::Srand(1);
    double f = util::RandomFloatScaled();
    crt::Srand(1);
    bool raise = ai::ShouldRaise(0x1000, 0x2000);
    CHECK_EQ(raise, (f <= 0.1));
}

TEST(AiMeister, CardGameUpdateRoundState) {
    ai::CardGameState st;
    st.bytes[8] = 1;
    ai::UpdateRoundState(st);
    CHECK_EQ(st.bytes[8], (u8)2); // 1 -> 2 (reveal)

    // phase 2 with seat-A triple (all 6) and no seat-B triple -> A wins (5).
    st.bytes[8] = 2;
    st.bytes[20] = st.bytes[21] = st.bytes[22] = 6;
    st.bytes[48] = 1; st.bytes[49] = 2; st.bytes[50] = 3;
    ai::UpdateRoundState(st);
    CHECK_EQ(st.bytes[8], (u8)5);
}
