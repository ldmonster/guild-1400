// E2E: multi-turn playable run. The driver installs the REAL sim hooks and runs
// N turns advancing game time through the real economy / production / event /
// time-advance cores. We prove it RUNS (state EVOLVES across turns) and is
// REPRODUCIBLE (same seed -> identical run; different seed -> different run).
// This is the self-consistency oracle (no original binary is available).
#include "test.h"

#include "play/turn_driver.h"
#include "sim/real_hooks.h"

using namespace guild;

namespace {
// True if at least `min` of the N snapshots differ from their predecessor in the
// given accessor (i.e. that counter is genuinely evolving, not stuck).
template <class F>
int countChanges(const std::vector<play::WorldSnapshot>& s, F get) {
    int n = 0;
    for (size_t i = 1; i < s.size(); ++i)
        if (get(s[i]) != get(s[i - 1])) ++n;
    return n;
}
}

// ---- a multi-turn run evolves the world state across turns --------------------
TEST(TurnDriverE2E, StateEvolvesAcrossTurns) {
    play::TurnDriver d;
    auto s = d.run(/*seed=*/2024, /*turns=*/8);
    CHECK_EQ(static_cast<int>(s.size()), 8);
    if (s.size() < 8) return;

    // Game time advances monotonically, one day per turn.
    for (size_t i = 0; i < s.size(); ++i)
        CHECK(s[i].day == static_cast<i32>(i) + 1);

    // The economy / treasury / fire counters change over the run (not inert).
    CHECK(countChanges(s, [](const play::WorldSnapshot& w){ return w.smoothedPrice; }) >= 1);
    CHECK(countChanges(s, [](const play::WorldSnapshot& w){ return w.treasury; }) >= 1);
    CHECK(countChanges(s, [](const play::WorldSnapshot& w){ return w.fireValue; }) >= 1);

    // The fire building burns down over the run (strictly decreasing until 0).
    CHECK(s.back().fireValue < s.front().fireValue);

    // The hooks were installed (live runtime de-inerted) and the shared queue
    // is reachable.
    CHECK(d.hooksInstalled());
    CHECK(sim::RealCommandQueue() != nullptr);
}

// ---- same seed -> identical run (reproducible) --------------------------------
TEST(TurnDriverE2E, SameSeedReproducible) {
    play::TurnDriver a, b;
    auto sa = a.run(/*seed=*/555, /*turns=*/10);
    auto sb = b.run(/*seed=*/555, /*turns=*/10);
    CHECK_EQ(sa.size(), sb.size());
    bool identical = sa.size() == sb.size();
    for (size_t i = 0; i < sa.size() && i < sb.size(); ++i)
        if (sa[i] != sb[i]) identical = false;
    CHECK(identical);
}

// ---- different seed -> different run (the seed actually drives the world) ------
TEST(TurnDriverE2E, DifferentSeedDiverges) {
    play::TurnDriver a, b;
    auto sa = a.run(/*seed=*/1, /*turns=*/10);
    auto sb = b.run(/*seed=*/2, /*turns=*/10);
    CHECK_EQ(sa.size(), sb.size());
    bool anyDiff = false;
    for (size_t i = 0; i < sa.size() && i < sb.size(); ++i)
        if (sa[i] != sb[i]) anyDiff = true;
    CHECK(anyDiff);
}

// ---- re-running on the SAME driver instance is still seed-reproducible --------
// (proves run() fully re-seeds the world each call; no cross-run leakage.)
TEST(TurnDriverE2E, ReseedOnSameInstance) {
    play::TurnDriver d;
    auto first  = d.run(/*seed=*/909, /*turns=*/6);
    auto other  = d.run(/*seed=*/101, /*turns=*/6);   // perturb global state
    (void)other;
    auto again  = d.run(/*seed=*/909, /*turns=*/6);   // must match `first`
    CHECK_EQ(first.size(), again.size());
    bool same = first.size() == again.size();
    for (size_t i = 0; i < first.size() && i < again.size(); ++i)
        if (first[i] != again[i]) same = false;
    CHECK(same);
}
