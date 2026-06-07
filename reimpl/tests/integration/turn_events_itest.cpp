// Integration: RunEventsTurn drives the per-DAY events/time pass over the live
// world. Asserts the clock advances K days, events fire, and HashFullWorld
// changes across the run AND is identical across two reruns with the same seed.
// No real assets — the live entity/world globals are reset + seeded headlessly.
#include "test.h"

#include "play/turn_events.h"
#include "play/world_digest.h"   // HashFullWorld
#include "crt/rand.h"
#include "sim/entity.h"          // ResetEntityArrays

#include <cstdint>

using namespace guild;

namespace {

// Run K events/time turns over the live world from seed S. Returns the final
// full-world hash; fills firstHash with the pre-run hash and out-params for
// reporting.
std::uint64_t RunEvents(unsigned seed, int K, std::uint64_t* firstHash,
                        int* finalDay, int* eventsFired, int* chronicled) {
    sim::ResetEntityArrays();
    crt::Srand(seed);
    play::EventsTurnState st = play::SeedEventsTurnState();
    if (firstHash) *firstHash = play::HashFullWorld();
    for (int day = 0; day < K; ++day)
        play::RunEventsTurn(st);
    if (finalDay)    *finalDay = st.clock.day;
    if (eventsFired) *eventsFired = st.eventsFired;
    if (chronicled)  *chronicled = st.chronicleAdded;
    return play::HashFullWorld();
}

} // namespace

TEST(TurnEventsITest, ClockAdvancesEventsFireHashEvolves) {
    const int K = 8;
    std::uint64_t h0 = 0;
    int day = 0, fired = 0, chron = 0;
    std::uint64_t hRun = RunEvents(/*seed=*/4242, K, &h0, &day, &fired, &chron);

    // The clock advanced exactly K game-days.
    CHECK_EQ(day, K);

    // Events fired over the run, each producing a chronicle entry.
    CHECK(fired > 0);
    CHECK_EQ(fired, chron);

    // The world EVOLVED: the events turn consumed the seeded RNG (event picks +
    // schedule), so the full-world hash moved off its pre-run value.
    CHECK(h0 != 0u);
    CHECK(hRun != h0);

    std::printf("  [info] events: %d turns -> day %d, %d events fired / %d "
                "chronicled, hash %016llx -> %016llx\n",
                K, day, fired, chron,
                (unsigned long long)h0, (unsigned long long)hRun);
}

TEST(TurnEventsITest, DeterministicAcrossReruns) {
    const int K = 8;
    std::uint64_t hA = RunEvents(/*seed=*/4242, K, nullptr, nullptr, nullptr, nullptr);
    std::uint64_t hB = RunEvents(/*seed=*/4242, K, nullptr, nullptr, nullptr, nullptr);
    // Same seed -> byte-identical final full-world hash.
    CHECK_EQ(hA, hB);

    // A different seed diverges (the seed actually drives the world).
    std::uint64_t hC = RunEvents(/*seed=*/9999, K, nullptr, nullptr, nullptr, nullptr);
    CHECK(hA != hC);
}
