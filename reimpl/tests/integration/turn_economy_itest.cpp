// Integration: RunEconomyTurn over a small seeded world. Over K days the economy
// EVOLVES — prices, production, treasury move AND play::HashWorldState() (which
// folds the live entity arrays + the CRT RNG state the passes consume) changes —
// and the whole run is IDENTICAL across two reruns with the same seed
// (determinism), while a different seed diverges.
#include "test.h"

#include "play/turn_economy.h"
#include "play/determinism.h"

#include "crt/rand.h"
#include "sim/entity.h"

using namespace guild;

namespace {

// One full K-day economy run from RNG seed S. Returns the final world hash and
// (out) the per-day deltas observed.
std::uint64_t RunDays(unsigned seed, int K,
                      std::vector<play::EconomyTurnDeltas>* out = nullptr) {
    sim::ResetEntityArrays();
    crt::Srand(seed);
    play::EconomyTurnState st = play::SeedEconomyTurnState();
    if (out) out->clear();
    for (int day = 0; day < K; ++day) {
        st.day = day;
        play::EconomyTurnDeltas d = play::RunEconomyTurn(st);
        if (out) out->push_back(d);
    }
    return play::HashWorldState();
}

template <class F>
int countChanges(const std::vector<play::EconomyTurnDeltas>& s, F get) {
    int n = 0;
    for (size_t i = 1; i < s.size(); ++i)
        if (get(s[i]) != get(s[i - 1])) ++n;
    return n;
}

} // namespace

// ---- the economy turn changes prices / production / treasury AND the hash ------
TEST(TurnEconomyIntegration, TurnMutatesWorldAndHash) {
    sim::ResetEntityArrays();
    crt::Srand(4242);
    std::uint64_t hBefore = play::HashWorldState();

    play::EconomyTurnState st = play::SeedEconomyTurnState();
    float priceBefore   = st.priceLevel;     // 0 before the first tick
    i64   treasuryBefore = st.treasury;

    play::EconomyTurnDeltas d = play::RunEconomyTurn(st);
    std::uint64_t hAfter = play::HashWorldState();

    // Economy state moved.
    CHECK(d.priceAfter != d.priceBefore);              // smoothed price moved
    CHECK(d.priceLevel != static_cast<int>(priceBefore)); // price level moved
    CHECK(d.cityMoneyAfter != d.cityMoneyBefore);      // g_cityTotalMoney moved
    CHECK(d.workMinutes > 0);                          // production ran
    CHECK(d.treasuryAfter != treasuryBefore);          // treasury changed
    CHECK_EQ(d.passesRun, 6);                          // all six Amt passes

    // HashWorldState changed (the passes consumed the seeded RNG -> RNG region
    // of the world digest advances).
    CHECK(hAfter != hBefore);
}

// ---- a K-day run evolves: prices and treasury keep moving day to day -----------
TEST(TurnEconomyIntegration, MultiDayEvolves) {
    std::vector<play::EconomyTurnDeltas> days;
    RunDays(/*seed=*/2024, /*K=*/8, &days);
    CHECK_EQ(static_cast<int>(days.size()), 8);
    if (days.size() < 8) return;

    // The price level changes on most days (genuinely evolving EMA + drift).
    CHECK(countChanges(days, [](const play::EconomyTurnDeltas& w){ return w.priceAfter; }) >= 4);
    // The treasury strictly grows (production + taxes dominate wages/interest).
    CHECK(days.back().treasuryAfter > days.front().treasuryAfter);
    // Production work-minutes vary by weekday (window table flt_6476FC/64770C).
    CHECK(countChanges(days, [](const play::EconomyTurnDeltas& w){ return w.workMinutes; }) >= 1);
    // Every day ran all six passes and wrote non-zero price deltas.
    for (const auto& w : days) {
        CHECK_EQ(w.passesRun, 6);
        CHECK(w.priceDeltaSum != 0.0);
    }
}

// ---- same seed -> identical run (hash) ; different seed -> different ------------
TEST(TurnEconomyIntegration, DeterministicAcrossReruns) {
    std::uint64_t a = RunDays(/*seed=*/555, /*K=*/10);
    std::uint64_t b = RunDays(/*seed=*/555, /*K=*/10);
    CHECK_EQ(a, b);                       // same seed -> identical final hash
    CHECK(a != 0u);

    std::uint64_t c = RunDays(/*seed=*/999, /*K=*/10);
    CHECK(a != c);                        // different seed -> different hash
}

// ---- the per-day delta sequence is byte-identical across two reruns ------------
TEST(TurnEconomyIntegration, DeltaSequenceReproducible) {
    std::vector<play::EconomyTurnDeltas> r1, r2;
    RunDays(/*seed=*/77, /*K=*/6, &r1);
    RunDays(/*seed=*/77, /*K=*/6, &r2);
    CHECK_EQ(r1.size(), r2.size());
    bool identical = r1.size() == r2.size();
    for (size_t i = 0; i < r1.size() && i < r2.size(); ++i) {
        if (r1[i].priceAfter   != r2[i].priceAfter)   identical = false;
        if (r1[i].treasuryAfter!= r2[i].treasuryAfter)identical = false;
        if (r1[i].priceLevel   != r2[i].priceLevel)   identical = false;
        if (r1[i].priceDeltaSum!= r2[i].priceDeltaSum)identical = false;
    }
    CHECK(identical);
}
