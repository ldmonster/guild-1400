#include "test.h"

// Integration: drive meisterai3's session-decision orchestrator against a REAL
// reconstructed sibling — the AI RNG VIBE_Math_RandomModulo (ai/method.cpp
// 0x58b89c), which itself bottoms out in the real CRT LCG (crt/rand.cpp). No mock
// RNG: EvaluateSessionDecision's random session types draw through
// MethodEnv.rng_mod, and here that installable leaf is forwarded into the genuine
// ai::RandomModulo, so the coin-flip / random-value verdicts are the real LCG
// sequence, end to end. A second test wires the same orchestrator into the real
// EvalPurchaseDesire -> real ClassifyWealthTier scorer (aimethod2.cpp), pinning
// the purchase-tier branch to the reconstructed wealth classifier.
//
// The id-match guards and scalar leaves are supplied inputs; the load-bearing
// arithmetic (the RNG draw, the wealth-tier switch) is the reconstructed sibling.
#include "ai/meisterai3.h"
#include "ai/method.h"      // REAL ai::RandomModulo (0x58b89c)
#include "ai/aimethod2.h"   // REAL ClassifyWealthTier (0x467d60), MethodEnv
#include "crt/rand.h"       // REAL crt::Srand / crt::RandNext (the LCG underneath)

#include <cstring>

using namespace guild;
using namespace guild::ai;

namespace {

// The MethodEnv.rng_mod leaf forwarded into the REAL reconstructed RNG sibling.
int g_rngCalls = 0;
int RealRngMod(u16 n) { ++g_rngCalls; return ai::RandomModulo(n); }   // REAL

MethodEnv MakeEnvRealRng() {
    MethodEnv e{};            // all other leaves left null -> Bind() inert defaults
    e.rng_mod = RealRngMod;   // <-- real wiring
    return e;
}

} // namespace

// Session type 6 (coin flip) draws RandomModulo(2) through the real RNG when the
// actor's id matches the relevant participant. Seed the REAL LCG, predict the
// draw from the real sequence, re-seed, run; the orchestrator's result must equal
// the real RandomModulo(2) verdict.
TEST(Meister3Itest, SessionCoinFlipUsesRealRandomModulo) {
    const u32 seed = 0xBEEF01u;

    // Predict from the real sibling: a single RandomModulo(2) draw.
    crt::Srand(seed);
    int expected = ai::RandomModulo(2);

    SessionInputs in{};
    in.sessionType = 6;
    in.match6      = true;     // actor.id == session+24 -> the draw is taken
    in.randModulus = 2;

    MethodEnv env = MakeEnvRealRng();
    g_rngCalls = 0;
    crt::Srand(seed);          // re-seed the REAL LCG identically
    int result = EvaluateSessionDecision(in, env);

    CHECK_EQ(result, expected);   // real RNG verdict, end to end
    CHECK_EQ(g_rngCalls, 1);      // the orchestrator drew exactly once

    // And when the id does NOT match, no draw happens; the type byte is returned.
    SessionInputs in2 = in;
    in2.match6 = false;
    g_rngCalls = 0;
    int result2 = EvaluateSessionDecision(in2, env);
    CHECK_EQ(result2, 6);         // falls through to the session-type byte
    CHECK_EQ(g_rngCalls, 0);      // real RNG untouched
}

// Session type 2 (random value) returns RandomModulo(randModulus) directly. With
// a modulus of 5 over the real LCG, the orchestrator's output equals the real
// sibling's draw for the same seed.
TEST(Meister3Itest, SessionRandomValueUsesRealRandomModulo) {
    const u32 seed = 0x5151AAu;

    crt::Srand(seed);
    int expected = ai::RandomModulo(5);

    SessionInputs in{};
    in.sessionType = 2;
    in.match2      = true;
    in.randModulus = 5;

    MethodEnv env = MakeEnvRealRng();
    g_rngCalls = 0;
    crt::Srand(seed);
    int result = EvaluateSessionDecision(in, env);

    CHECK_EQ(result, expected);
    CHECK_EQ(g_rngCalls, 1);
}

// Session type 5 (purchase) delegates to EvalPurchaseDesire, whose base tier is
// the REAL ClassifyWealthTier (aimethod2.cpp). Pin a wealth gauge to a known tier
// and assert the orchestrator returns it (favorability gate neutralized so the
// classifier verdict is the result).
TEST(Meister3Itest, SessionPurchaseUsesRealWealthClassifier) {
    // Sanity-pin the real classifier: gauge 11.0 -> tier index 10 -> class 4.
    CHECK_EQ(static_cast<int>(ClassifyWealthTier(11.0f)), 4);

    SessionInputs in{};
    in.sessionType    = 5;
    in.match5         = true;
    in.purWealthGauge = 11.0f;     // -> real ClassifyWealthTier == 4
    in.purLawCur      = 5;         // t = (5-5)*0.1 + 0.7 = 0.7
    // fav huge so neither desire-bump branch fires: tier stays the classifier's 4.
    in.purFav         = 1.0e9f;
    in.purByte358     = false;
    in.purByte13      = 0;

    MethodEnv env = MakeEnvRealRng();
    int result = EvaluateSessionDecision(in, env);

    CHECK_EQ(result, 4);           // the real wealth tier, undisturbed, flows out
}

// Inert-default Meister3Hooks path: with no command hooks installed, ApplyEatAction
// still returns its action code while the (real) inert default emitters are no-ops
// — exercising the module's reconstructed inert default end to end.
TEST(Meister3Itest, ApplyEatActionInertDefaultEmittersReturnCode) {
    SetMeister3Hooks(Meister3Hooks{});     // all-null -> inert defaults installed

    char code = ApplyEatAction(/*actorBuildId*/ 12, /*cost*/ 50, /*slotId*/ 7,
                               /*slotKind*/ 3);
    CHECK_EQ(static_cast<int>(code), 5);   // eat -> 5, no crash through inert hooks

    char dcode = ApplyDrinkAction(12, 50, 7, 3);
    CHECK_EQ(static_cast<int>(dcode), 4);  // drink -> 4

    SetMeister3Hooks(Meister3Hooks{});
}
