// Verifies InstallRealAiWiring() binds the faithfully-reconstructed engine leaves
// into the live AiRecon5Hooks bridge consumed by the recon5/recon2 AI decision
// math (previously every coupling fell back to the all-null inert defaults), and
// that the bound leaves run end-to-end through a real AI scorer without crashing.
#include "tests/framework/test.h"

#include "sim/wire_ai.h"
#include "sim/ai_recon5_decisions.h"
#include "crt/rand.h"

using namespace guild;
using namespace guild::sim;

// (a) Install binds exactly the three faithfully-bindable leaves; the unbound
//     couplings stay null so the scorers keep the inert default for them.
TEST(WireAi, InstallBindsFaithfulLeavesOnly) {
    InstallRealAiWiring();
    const AiRecon5Hooks& h = GetRealAiRecon5Hooks();

    // Bound leaves (faithful 1:1 reconstructions with matching shape).
    CHECK(h.randNext       != nullptr);   // 0x5cb8bc
    CHECK(h.moneyToDisplay != nullptr);   // 0x58f14c
    CHECK(h.moneyMulByRate != nullptr);   // 0x58f19c

    // Intentionally inert (full form unreachable at this layer).
    CHECK(h.personTotalWealth == nullptr);  // 0x591f7c
    CHECK(h.selectMoodColor   == nullptr);  // 0x4664d8
    CHECK(h.ratingCurveA      == nullptr);  // 0x58a6e8
}

// (b) Install is idempotent — re-running does not change the bound table.
TEST(WireAi, InstallIsIdempotent) {
    InstallRealAiWiring();
    const AiRecon5Hooks& a = GetRealAiRecon5Hooks();
    auto rn = a.randNext;
    auto md = a.moneyToDisplay;
    auto mr = a.moneyMulByRate;
    InstallRealAiWiring();
    const AiRecon5Hooks& b = GetRealAiRecon5Hooks();
    CHECK(b.randNext       == rn);
    CHECK(b.moneyToDisplay == md);
    CHECK(b.moneyMulByRate == mr);
}

// (c) The bound randNext drives the shared CRT LCG: with a known seed it returns
//     the exact 0x5cb8bc draw (state = state*1103515245 + 12345; (>>16)&0x7FFF).
TEST(WireAi, BoundRandNextMatchesCrtLcgGolden) {
    InstallRealAiWiring();
    const AiRecon5Hooks& h = GetRealAiRecon5Hooks();

    crt::Srand(1u);
    // First two draws of the ANSI LCG seeded with 1 (bits 16..30):
    //   s = 1*1103515245 + 12345 = 1103527590; (>>16)&0x7FFF = 0x41C6 = 16838
    //   s = s*1103515245 + 12345;               (>>16)&0x7FFF =          5758
    CHECK(h.randNext() == 16838);
    CHECK(h.randNext() == 5758);
}

// (d) The bound money leaves are identity until the session installs the rate
//     tables (rate 1). moneyToDisplay rounds-to-nearest (amount/1 + 0.5 trunc).
TEST(WireAi, BoundMoneyLeavesIdentityWithoutRateTables) {
    InstallRealAiWiring();
    const AiRecon5Hooks& h = GetRealAiRecon5Hooks();
    CHECK_EQ(h.moneyToDisplay(1000, 0), 1000);  // 1000/1 + 0.5 -> 1000
    CHECK_EQ(h.moneyMulByRate(1000, 0), 1000);  // 1000 * 1
}

// (e) End-to-end: a real recon5 scorer (AiCardGame_PlaceBet) runs over the wired
//     bridge without crashing and produces a defined result. The bound randNext /
//     money leaves now feed the wager math instead of returning 0.
TEST(WireAi, WiredBridgeDrivesCardGameScorer) {
    InstallRealAiWiring();
    const AiRecon5Hooks& h = GetRealAiRecon5Hooks();

    crt::Srand(12345u);
    // personId!=0, personTypeByte==6, betMult==2, familyAgg=0.5f -> the "ok" path.
    CardBet bet = AiCardGame_PlaceBet(h, /*personId*/ 7, /*personTypeByte*/ 6,
                                      /*betMult*/ 2, /*familyAggIn*/ 0.5f);
    CHECK(bet.ok == true);
    // Wager terms are >= 0 (display wealth was 0 -> v23 clamps to 10000 floor).
    CHECK(bet.stakeA >= 0);
    CHECK(bet.stakeB >= 0);
}
