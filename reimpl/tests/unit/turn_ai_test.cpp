// Unit: a single REAL AI decision pass over a synthetic seeded world is
// deterministic and produces the expected (golden) choice. The decisions are the
// reconstructed MeisterAi rule cores (ai::MoodRelationDelta / ConfrontationDecision
// / MoodDecayRoll) and the AiMethod group-state mask roll, exercised exactly as the
// per-turn AI driver invokes them — with the REAL CRT RNG seeded via crt::Srand.
#include "test.h"

#include "ai/meisterai.h"
#include "ai/meisterai3.h"
#include "ai/aimethod2.h"
#include "crt/rand.h"

using namespace guild;

// ---- golden DATA/RULES cores (pure; no RNG) -------------------------------
TEST(TurnAiUnit, MoodRelationDeltaGolden) {
    // attitudeA/B above 100 with neutral relation -> positive then small negative
    // accumulation: part1 (130-100)/3 = 10, part2 (100-130)/5 = -6 -> min-clamp 1
    // -> 10 - 1 = 9. (Recovered from the 0x53265f mood math.)
    CHECK_EQ((int)ai::MoodRelationDelta(130, 130, 0), 9);
    // Symmetric below-100 attitudes -> mirror negative delta.
    CHECK_EQ((int)ai::MoodRelationDelta(70, 70, 0), -9);
    // Exactly 100 attitudes -> no change.
    CHECK_EQ((int)ai::MoodRelationDelta(100, 100, 0), 0);
}

TEST(TurnAiUnit, TaxPayoutGolden) {
    // 16000 * multiplier tier (0x532e73).
    CHECK_EQ((int)ai::TaxPayout(1), 16000);
    CHECK_EQ((int)ai::TaxPayout(3), 48000);
}

// ---- RNG-driven decisions: deterministic given the seed -------------------
TEST(TurnAiUnit, MoodDecayRollIsSeededDeterministic) {
    // The decay roll consumes RandomModulo(100) then RandomModulo(3). Seeded at a
    // fixed value it must produce a fixed golden result and repeat across reseeds.
    crt::Srand(12345);
    int d1 = ai::MoodDecayRoll(5, /*flagBit0=*/false);
    crt::Srand(12345);
    int d2 = ai::MoodDecayRoll(5, /*flagBit0=*/false);
    CHECK_EQ(d1, d2);          // same seed -> same draw
    CHECK_EQ(d1, -3);          // golden (seed 12345)

    // A different seed yields a (possibly) different draw, but is itself stable.
    crt::Srand(777);
    int d3 = ai::MoodDecayRoll(5, false);
    crt::Srand(777);
    int d4 = ai::MoodDecayRoll(5, false);
    CHECK_EQ(d3, d4);
}

TEST(TurnAiUnit, ConfrontationDecisionIsSeededDeterministic) {
    // newRelation -100 -> the gate roll RandomModulo(0x4A); with seed 12345 the
    // gate fails -> no confrontation (golden 0). Reproducible across reseeds.
    crt::Srand(12345);
    int c1 = ai::ConfrontationDecision(-100);
    crt::Srand(12345);
    int c2 = ai::ConfrontationDecision(-100);
    CHECK_EQ(c1, c2);
    CHECK_EQ(c1, 0);

    // A relation >= -26 never confronts (no RNG draw at all).
    CHECK_EQ(ai::ConfrontationDecision(-10), 0);
}

TEST(TurnAiUnit, GroupStateMaskRollIsSeededDeterministic) {
    ai::MethodEnv env{};   // default leaves: RNG via the shared LCG.
    crt::Srand(999);
    int m1 = ai::RollGroupStateMask(env);
    crt::Srand(999);
    int m2 = ai::RollGroupStateMask(env);
    CHECK_EQ(m1, m2);          // same seed -> identical mask
    CHECK_EQ(m1, 1);           // golden (seed 999); bit0 always set
    CHECK((m1 & 1) != 0);      // the always-on leader bit
}

// ---- a full "one decision pass over a synthetic worker" -------------------
namespace {
int g_capN = 0;
int g_capOps[64] = {0};
void CaptureEmit(int op, i32, i32, i32) {
    if (g_capN < 64) g_capOps[g_capN++] = op;
}
} // namespace

TEST(TurnAiUnit, SingleWorkerMoodPassIsDeterministicAndGolden) {
    // One synthetic worker the MeisterAi turn director would process. The pass
    // emits the wage/stock + relation packets always, plus mood/confront/decay
    // commands conditionally. We capture the emitted op sequence and assert it is
    // identical across two seeded runs and matches the golden expectation.
    ai::TurnWorker w{};
    w.personId = 42;
    w.attitudeA = 130;   // -> MoodRelationDelta nonzero (+9)
    w.attitudeB = 130;
    w.relation  = 0;
    w.kind      = 5;
    w.flagBit0  = false;

    crt::Srand(2024);
    g_capN = 0;
    int e1 = ai::RunWorkerMoodPass(0, &w, 1, &CaptureEmit);
    int seq1[64]; int n1 = g_capN; for (int i = 0; i < n1; ++i) seq1[i] = g_capOps[i];

    crt::Srand(2024);
    g_capN = 0;
    int e2 = ai::RunWorkerMoodPass(0, &w, 1, &CaptureEmit);

    CHECK_EQ(e1, e2);                 // same emitted count
    CHECK(e1 >= 2);                   // always >= wage + relation packets
    CHECK_EQ(n1, g_capN);            // same number of hook emissions
    bool same = (n1 == g_capN);
    for (int i = 0; i < n1 && same; ++i) same = same && (seq1[i] == g_capOps[i]);
    CHECK(same);                      // identical op sequence (deterministic)

    // Golden: attitudeA=130 -> a mood-coord command (op kTurnMoodCoord==2) emits.
    bool sawMood = false;
    for (int i = 0; i < g_capN; ++i) if (g_capOps[i] == ai::kTurnMoodCoord) sawMood = true;
    CHECK(sawMood);
}
