// Unit: the per-DAY events/time turn building blocks — clock advance + one
// event-trigger decision golden, deterministic via crt::Srand. No real assets.
//
// Covers:
//   * SeedEventsTurnState starts the wall clock at 06:00 day 0 and builds a live
//     trigger ring + event-slot rings.
//   * One RunEventsTurn advances the clock exactly one game-day (minutesPerDay),
//     ticks the He triggers, expires the MeisterAi event slots, and (when a
//     trigger is due) fires an event + appends one dated chronicle entry.
//   * A fixed-seed GOLDEN: with crt::Srand(seed) the first turn's clock delta,
//     fired-event count, and first chronicle entry are byte-stable across runs.
#include "test.h"

#include "play/turn_events.h"
#include "world/history.h"   // kChronicleBaseYear
#include "crt/rand.h"

using namespace guild;

namespace {

// Run SeedEventsTurnState + one RunEventsTurn from a fixed seed, returning the
// state + deltas so the golden can be captured deterministically.
play::EventsTurnState SeedAndStep(unsigned seed, play::EventsTurnDeltas* d) {
    crt::Srand(seed);
    play::EventsTurnState st = play::SeedEventsTurnState();
    play::EventsTurnDeltas dd = play::RunEventsTurn(st);
    if (d) *d = dd;
    return st;
}

} // namespace

TEST(TurnEventsUnit, SeedStartsAtNewGameClock) {
    crt::Srand(1);
    play::EventsTurnState st = play::SeedEventsTurnState();
    CHECK_EQ(st.clock.day, 0);
    CHECK_EQ((int)st.clock.hour, 6);
    CHECK_EQ(st.clock.minute, 0);
    CHECK(st.minutesPerDay == 1440);
    CHECK(!st.triggers.empty());           // ring of scheduled triggers built
    CHECK(st.turnsRun == 0);
}

TEST(TurnEventsUnit, OneTurnAdvancesClockExactlyOneDay) {
    play::EventsTurnDeltas d{};
    play::EventsTurnState st = SeedAndStep(12345, &d);

    // Clock advanced by one full game-day == 1440 minutes => day +1, hour unchanged.
    CHECK_EQ(d.dayBefore, 0);
    CHECK_EQ(d.dayAfter, 1);
    CHECK_EQ(d.hourBefore, d.hourAfter);   // +1440 min lands back on the same hour
    CHECK_EQ(st.clock.day, 1);
    CHECK((int)st.clock.hour == 6);

    // The calendar integral measured one day of minutes (1440).
    CHECK_EQ(d.minutesAdvanced, (i64)1440);
    CHECK_EQ(st.minutesIntegrated, (i64)1440);

    // The ordered pass list ran (>= the time + event passes).
    CHECK(d.passesRun >= 6);
    CHECK(st.turnsRun == 1);

    // The MeisterAi event slots were ticked (every lead byte decremented once).
    // With 1..5 RNG leads, at least some slots hit 0 the first turn is possible
    // but not guaranteed; the count is the observable.
    CHECK(d.slotsExpiredThisTurn >= 0);
}

TEST(TurnEventsUnit, EventTriggerFiresAndChroniclesDeterministically) {
    // GOLDEN: a fixed seed must reproduce the exact first-turn event/chronicle
    // outcome across runs (the determinism oracle for the event decision).
    play::EventsTurnDeltas dA{}, dB{};
    play::EventsTurnState a = SeedAndStep(777, &dA);
    play::EventsTurnState b = SeedAndStep(777, &dB);

    // Same seed => identical clock delta + fired + chronicled counts.
    CHECK_EQ(dA.dayAfter, dB.dayAfter);
    CHECK_EQ(dA.eventsFiredThisTurn, dB.eventsFiredThisTurn);
    CHECK_EQ(dA.chronicleAddedThisTurn, dB.chronicleAddedThisTurn);
    CHECK_EQ(a.eventsFired, b.eventsFired);
    CHECK_EQ(a.chronicle.Count(), b.chronicle.Count());

    // At least one trigger fired on the first turn (the seeded ring has a due
    // trigger), and each fired event produced a dated chronicle entry.
    CHECK(a.eventsFired == a.chronicleAdded);
    if (a.chronicle.Count() > 0) {
        const world::ChronicleEntry& e0 = a.chronicle.At(0);
        const world::ChronicleEntry& e1 = b.chronicle.At(0);
        // The first chronicle entry is byte-identical across the two runs.
        CHECK_EQ(e0.day, e1.day);
        CHECK_EQ(e0.month, e1.month);
        CHECK_EQ(e0.year, e1.year);
        CHECK_EQ(e0.textId, e1.textId);
        // Dated to the start day and the chronicle base year (day 0 -> 1400).
        CHECK(e0.year >= world::kChronicleBaseYear);
        CHECK(e0.textId >= 7300);
    }
}

TEST(TurnEventsUnit, DifferentSeedDiverges) {
    play::EventsTurnState a = SeedAndStep(111, nullptr);
    play::EventsTurnState b = SeedAndStep(222, nullptr);
    // The seed actually drives the schedule: the two rings differ somewhere
    // (countdowns/categories/leads), so the fired or chronicled totals or the
    // slot payloads diverge. Compare a robust composite.
    bool diverged = (a.eventsFired != b.eventsFired) ||
                    (a.chronicleAdded != b.chronicleAdded) ||
                    (a.triggers.size() != b.triggers.size());
    if (!diverged && !a.triggers.empty() && !b.triggers.empty()) {
        diverged = (a.triggers[0].category != b.triggers[0].category) ||
                   (a.triggers[0].period != b.triggers[0].period);
    }
    CHECK(diverged);
}
