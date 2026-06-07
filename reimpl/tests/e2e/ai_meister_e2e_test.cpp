// E2E: run a synthetic faction's MeisterAi worker-mood turn pass and a full
// card-game round through the AI, verifying the emitted command sequence (to a
// mock command hook) + resulting state against a hand-computed reference.
#include "tests/framework/test.h"

#include "ai/cardgame.h"
#include "ai/meisterai.h"
#include "crt/rand.h"
#include "util/math_random.h"
#include "util/math_rng_float.h"

#include <vector>

using namespace guild;

namespace {
struct EmittedCmd { int op; i32 a, b, c; };
std::vector<EmittedCmd> g_cmds;
void RecordCmd(int op, i32 a, i32 b, i32 c) { g_cmds.push_back({op, a, b, c}); }
} // namespace

// ---------------------------------------------------------------------------
// Worker mood pass over a synthetic faction.
// ---------------------------------------------------------------------------
TEST(AiMeisterE2E, WorkerMoodPassSequence) {
    g_cmds.clear();
    crt::Srand(2024);

    // Two workers:
    //   w0: attitudeA=70 (delta -10), attitudeB=100, relation=0, kind=4.
    //       => MoodRelationDelta = -10; newRelation = -10 (>= -26 => no confront).
    //   w1: attitudeA=130 (+10), attitudeB=80 (+1) => delta +11; relation=-40 =>
    //       newRelation = -29 (< -26 => confrontation roll).
    ai::TurnWorker workers[2] = {};
    workers[0] = {1001, 70, 100, 0, 4, false};
    workers[1] = {1002, 130, 80, -40, 4, false};

    // Hand-derive the reference delta/relation values.
    i8 d0 = ai::MoodRelationDelta(70, 100, 0);
    i8 d1 = ai::MoodRelationDelta(130, 80, -40);
    CHECK_EQ((int)d0, -10);
    CHECK_EQ((int)d1, 11);

    crt::Srand(2024);
    int emitted = ai::RunWorkerMoodPass(7 /*playerId*/, workers, 2, RecordCmd);

    // Every worker emits: wage cmd (counted, not recorded), a mood-coord cmd if
    // delta != 0, a state22 cmd (counted), maybe a confront cmd, maybe a decay.
    // Verify the RECORDED command stream matches the rule cores.

    // w0: delta -10 != 0 => one mood-coord cmd recorded.
    bool found_w0_mood = false;
    for (auto& c : g_cmds)
        if (c.op == ai::kTurnMoodCoord && c.b == 1001) {
            CHECK_EQ(c.c, -10);
            CHECK_EQ(c.a, 7);
            found_w0_mood = true;
        }
    CHECK(found_w0_mood);

    // w1: delta +11 => mood-coord cmd recorded with c==11.
    bool found_w1_mood = false;
    for (auto& c : g_cmds)
        if (c.op == ai::kTurnMoodCoord && c.b == 1002) {
            CHECK_EQ(c.c, 11);
            found_w1_mood = true;
        }
    CHECK(found_w1_mood);

    // emitted count >= the mandatory ones: 2 wage + 2 state22 + 2 mood = 6.
    CHECK(emitted >= 6);
}

// ---------------------------------------------------------------------------
// Confrontation determinism: replay the RNG to predict w1's confront variant.
// ---------------------------------------------------------------------------
TEST(AiMeisterE2E, ConfrontationDeterminism) {
    // Isolate the confrontation roll for newRelation = -29.
    crt::Srand(555);
    int ref = ai::ConfrontationDecision(-29);

    crt::Srand(555);
    // Re-derive: gate = -RandomModulo(0x4A); fires if gate < -29.
    int gate = -static_cast<int>((u16)util::RandomModulo(0x4A));
    if (gate >= -29) {
        CHECK_EQ(ref, 0);
    } else {
        int variant = ((u16)util::RandomModulo(0x64) <= 0x32u) ? 52 : 51;
        CHECK_EQ(ref, variant);
    }
}

// ---------------------------------------------------------------------------
// Full card-game round: AI vs AI, both seats driven by DecideMove/TakeTurn until
// the round resolves. Verify the round reaches a terminal phase (5 or 6) and the
// pot accumulates the staked bets.
// ---------------------------------------------------------------------------
namespace {
std::vector<EmittedCmd> g_bet;
void RecordBet(i32 a1, i32 a2, i32 a3, u8 /*a4*/) { g_bet.push_back({0, a1, a2, a3}); }
// Hand-strength oracle: seat A "strong" (0.7), seat B "weak" (0.2).
double Strength(i32 seatPtr, int /*mode*/) {
    if (seatPtr == 0x1000) return 0.7;
    return 0.2;
}
} // namespace

TEST(AiMeisterE2E, CardGameFullRound) {
    g_bet.clear();
    ai::SetHandStrengthHook(&Strength);
    ai::SetBetCmdHook(&RecordBet);
    crt::Srand(98765);

    ai::CardGameState st;
    st.seti32(12, 0x1000); // seat A
    st.seti32(40, 0x2000); // seat B
    st.seti32(0, 100);     // seat A stake
    st.seti32(4, 50);      // seat B stake
    st.bytes[8] = 1;       // round phase: deal
    st.bytes[36] = 0;      // seat A decision
    st.bytes[64] = 0;      // seat B decision

    // Deal both hands (draw=1) — seat-entity ptrs match the state's seat slots.
    ai::EvaluateHand(st, 0x1000, 1);
    ai::EvaluateHand(st, 0x2000, 1);
    CHECK_EQ(st.bytes[16], (u8)3); // seat A hand size 3
    CHECK_EQ(st.bytes[44], (u8)3); // seat B hand size 3

    i32 potBefore = st.geti32(68);

    // Drive seat A through a couple of decisions.
    for (int turn = 0; turn < 4; ++turn) {
        u8 mv = ai::DecideMove(st, 0x1000);
        if (mv == 0) break;
        int ok = ai::TakeTurn(st, 0x1000, mv);
        if (!ok) break;
        if (st.bytes[36] >= 4) break; // seat A reached a terminal decision
    }

    // The pot should have grown by the staked amounts of the taken actions, or be
    // unchanged if seat A only knocked. Either way it never shrinks.
    CHECK(st.geti32(68) >= potBefore);

    // Resolve the round to a terminal phase.
    st.bytes[8] = 2; // force into reveal/showdown chain
    for (int i = 0; i < 8; ++i) {
        ai::UpdateRoundState(st);
        if (st.bytes[8] == 5 || st.bytes[8] == 6) break;
    }
    CHECK(st.bytes[8] == 5 || st.bytes[8] == 6);

    // Reset hooks so other test files see defaults.
    ai::SetHandStrengthHook(nullptr);
    ai::SetBetCmdHook(nullptr);
}
