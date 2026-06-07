// Integration: RunGameDay composes the three sub-turns over a seeded live world.
// Over K days the FULL world EVOLVES — the clock advances exactly K days, the
// economy moves (treasury / prices), the AI mutates live Person records — and
// play::HashFullWorld() changes across the run AND is byte-identical across two
// reruns with the same seed (full determinism). A different seed diverges.
#include "test.h"

#include "play/game_day.h"
#include "play/world_digest.h"   // HashFullWorld
#include "play/determinism.h"    // SnapshotWorld / CompareSnapshots

#include "crt/rand.h"
#include "sim/entity.h"
#include "sim/person.h"
#include "sim/types.h"
#include "sim/building_lifecycle.h"

#include <cstdint>
#include <vector>

using namespace guild;

namespace {

// Seed a small deterministic synthetic world of live "worker" persons (faction 0)
// so the AI sub-turn of the day has live actors to drive (mood/relation + group
// broadcast + event tick). Mirrors the turn_ai_itest seeding. RNG rooted at Srand.
void SeedWorkers(std::uint32_t seed) {
    sim::ResetEntityArrays();
    sim::ResetBuildingPersons();
    crt::Srand(seed);
    const int kWorkers = 12;
    for (int i = 0; i < kWorkers; ++i) {
        sim::Person& p = sim::g_persons[i];
        p.marker = 4; p.isPlayer = 1; p.id = 1000 + i; p.ownerPlayer = 0;
        u8 kinds[] = {5, 5, 3, 6, 5, 3, 5, 6, 5, 5, 3, 6};
        p.kind = kinds[i % (int)sizeof(kinds)];
        sim::PersonSetByte(&p, 61, (u8)(70 + (crt::RandNext() % 60)));
        sim::PersonSetByte(&p, 65, (u8)(70 + (crt::RandNext() % 60)));
        sim::PersonSetByte(&p, sim::kPfReputation, (u8)(crt::RandNext() % 40));
        sim::PersonSetDword(&p, sim::kPfTurnBits, (i32)0xFFFFFFFFu);
        sim::PersonSetDword(&p, sim::kPfStatusFlag, 0);
    }
}

// Run K full game days over a freshly-seeded synthetic world from root seed S.
// Fills `deltas` (per-day) and returns the final HashFullWorld().
std::uint64_t RunDays(std::uint32_t seed, int K,
                      std::vector<play::GameDayDeltas>* deltas = nullptr) {
    // Live entity arrays carry the AI workers; the day state carries clock+economy.
    SeedWorkers(seed);                       // resets + seeds the live persons
    crt::Srand(seed);                        // re-root for SeedGameDay's draws
    play::GameDayState st = play::SeedGameDay(seed);
    std::vector<play::GameDayDeltas> d = play::RunGameDays(seed, K, st);
    if (deltas) *deltas = d;
    return play::HashFullWorld();
}

} // namespace

// ---- one full day advances clock + economy + AI, and moves the world hash -----
TEST(GameDayItest, OneDayAdvancesAllThreeSubsystems) {
    SeedWorkers(4242);
    crt::Srand(4242);
    play::GameDayState st = play::SeedGameDay(4242);

    std::uint64_t hBefore = play::HashFullWorld();
    play::GameDayDeltas d = play::RunGameDay(4242, st);
    std::uint64_t hAfter = play::HashFullWorld();

    // EVENTS: the clock advanced exactly one day.
    CHECK_EQ(d.events.dayAfter - d.events.dayBefore, 1);
    CHECK_EQ(st.events.clock.day, 1);
    CHECK(d.events.minutesAdvanced > 0);

    // ECONOMY: production ran, prices moved, all six Amt passes ran.
    CHECK(d.economy.workMinutes > 0);
    CHECK(d.economy.priceAfter != d.economy.priceBefore);
    CHECK_EQ(d.economy.passesRun, 6);
    CHECK(d.economy.treasuryAfter != d.economy.treasuryBefore);

    // AI: the cascade ran and mutated live records (sweep + mood pass).
    CHECK(d.ai.passesRun > 0);
    CHECK(d.ai.workersEvaluated >= 1);
    CHECK(d.ai.npcFlagsCleared >= 1);
    CHECK(d.ai.moodDeltasApplied >= 1);

    // COMPOSITION: all recovered steps replayed and the full-world hash moved.
    CHECK_EQ(d.stepsRun, play::GameDayStepCount());
    CHECK(hBefore != 0u);
    CHECK(hAfter != hBefore);
    CHECK_EQ(d.hashBefore, hBefore);
    CHECK_EQ(d.hashAfter, hAfter);
}

// ---- a K-day run genuinely EVOLVES: clock, treasury, and hash keep moving ------
TEST(GameDayItest, MultiDayEvolves) {
    const int K = 10;
    std::vector<play::GameDayDeltas> days;
    RunDays(/*seed=*/2024, K, &days);
    CHECK_EQ((int)days.size(), K);
    if ((int)days.size() != K) return;

    // The clock advanced one day per day, ending at day K.
    CHECK_EQ(days.back().dayAfter, K);
    for (int i = 0; i < K; ++i)
        CHECK_EQ(days[i].dayAfter - days[i].dayBefore, 1);

    // Treasury strictly grows over the run (production + taxes dominate).
    CHECK(days.back().economy.treasuryAfter > days.front().economy.treasuryAfter);

    // The full-world hash changes across most days (not a fixed point).
    int hashChanges = 0;
    for (int i = 1; i < K; ++i)
        if (days[i].hashAfter != days[i - 1].hashAfter) ++hashChanges;
    CHECK(hashChanges >= K - 2);

    // Events fire and chronicle over the run.
    int firedTotal = 0;
    for (auto& w : days) firedTotal += w.events.eventsFiredThisTurn;
    CHECK(firedTotal > 0);

    // Every day ran the full composition.
    for (auto& w : days) {
        CHECK_EQ(w.stepsRun, play::GameDayStepCount());
        CHECK_EQ(w.economy.passesRun, 6);
    }
}

// ---- same seed -> byte-identical full-world hash (full determinism) ------------
TEST(GameDayItest, DeterministicAcrossReruns) {
    const int K = 10;
    std::uint64_t a = RunDays(/*seed=*/555, K);
    std::uint64_t b = RunDays(/*seed=*/555, K);
    CHECK_EQ(a, b);                 // same seed -> identical final full-world hash
    CHECK(a != 0u);

    std::uint64_t c = RunDays(/*seed=*/999, K);
    CHECK(a != c);                  // different seed -> different hash
}

// ---- the per-day delta sequence is reproducible across two reruns --------------
TEST(GameDayItest, DeltaSequenceReproducible) {
    std::vector<play::GameDayDeltas> r1, r2;
    RunDays(/*seed=*/77, /*K=*/8, &r1);
    RunDays(/*seed=*/77, /*K=*/8, &r2);
    CHECK_EQ(r1.size(), r2.size());
    bool identical = r1.size() == r2.size();
    for (size_t i = 0; i < r1.size() && i < r2.size(); ++i) {
        if (r1[i].hashAfter            != r2[i].hashAfter)            identical = false;
        if (r1[i].economy.treasuryAfter!= r2[i].economy.treasuryAfter)identical = false;
        if (r1[i].economy.priceAfter   != r2[i].economy.priceAfter)   identical = false;
        if (r1[i].events.eventsFiredThisTurn != r2[i].events.eventsFiredThisTurn)
            identical = false;
        if (r1[i].ai.moodDeltasApplied != r2[i].ai.moodDeltasApplied) identical = false;
        if (r1[i].ai.relationDeltaSum  != r2[i].ai.relationDeltaSum)  identical = false;
    }
    CHECK(identical);

    // And the final live-world snapshot is byte-identical region by region.
    play::WorldSnapshot snapA;
    {
        RunDays(/*seed=*/77, /*K=*/8);
        snapA = play::SnapshotWorld();
    }
    play::WorldSnapshot snapB;
    {
        RunDays(/*seed=*/77, /*K=*/8);
        snapB = play::SnapshotWorld();
    }
    std::string diff;
    CHECK(play::CompareSnapshots(snapA, snapB, &diff));
    CHECK(diff.empty());
}
