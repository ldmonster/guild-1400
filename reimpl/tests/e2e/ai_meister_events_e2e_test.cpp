// End-to-end flow for the MeisterAi event-slot managers: a multi-turn lifecycle
// over the ApEvent and EventSlot pools (register -> query -> age -> expire ->
// reuse), exercising every translated function across many ticks. Deterministic;
// no external assets required.
#include "ai/meister_events.h"

#include "test.h"

#include <cstring>

using namespace guild::ai;

TEST(AiMeisterEventsE2E, ApEventLifecycleAcrossTurns) {
    ApEventPool pool{};

    // Turn 0: three owners register events (ttl=2 each).
    CHECK_EQ(RegisterApEvent(pool, /*owner*/1, /*sec*/50, /*pri*/3, "tax"), 0);
    CHECK_EQ(RegisterApEvent(pool, /*owner*/1, /*sec*/20, /*pri*/4, "fee"), 1);
    CHECK_EQ(RegisterApEvent(pool, /*owner*/2, /*sec*/10, /*pri*/9, "fine"), 2);

    // Aggregates reflect all three live events.
    CHECK_EQ(SumApEventsByOwner(pool, 1), 7);   // 3 + 4
    CHECK_EQ(SumApEventsByOwner(pool, 2), 9);
    CHECK_EQ(SumApEventsBySecondary(pool, 1), 70); // 50 + 20

    // Turn 1: age once. Everything still live (ttl 2 -> 1).
    ExpireApEventSlots(pool);
    CHECK_EQ(SumApEventsByOwner(pool, 1), 7);
    CHECK_EQ(SumApEventsByOwner(pool, 2), 9);

    // A new event registered mid-life lands in a fresh slot (3) with ttl 2.
    CHECK_EQ(RegisterApEvent(pool, /*owner*/2, /*sec*/5, /*pri*/2, "bribe"), 3);
    CHECK_EQ(SumApEventsByOwner(pool, 2), 11); // 9 + 2

    // Turn 2: age again. The three turn-0 events (ttl 1 -> 0) expire; the
    // turn-1 event (ttl 2 -> 1) survives.
    ExpireApEventSlots(pool);
    CHECK_EQ(SumApEventsByOwner(pool, 1), 0);   // both owner-1 events gone
    CHECK_EQ(SumApEventsByOwner(pool, 2), 2);   // only the bribe remains
    CHECK_EQ(SumApEventsBySecondary(pool, 1), 0);
    CHECK_EQ(SumApEventsBySecondary(pool, 2), 5);

    // Freed slots are reclaimed in order on the next register.
    CHECK_EQ(RegisterApEvent(pool, /*owner*/3, /*sec*/1, /*pri*/8, "new"), 0);
    CHECK_EQ(pool.slots[0].ownerKey, 3);
    CHECK_EQ(SumApEventsByOwner(pool, 3), 8);

    // Turn 3: age once more. The bribe (ttl 1 -> 0) expires; the just-added
    // event (ttl 2 -> 1) survives.
    ExpireApEventSlots(pool);
    CHECK_EQ(SumApEventsByOwner(pool, 2), 0);
    CHECK_EQ(SumApEventsByOwner(pool, 3), 8);
}

TEST(AiMeisterEventsE2E, EventSlotLifecycleAndPayload) {
    EventPool pool{};

    const unsigned char p0[] = {0x10, 0x20, 0x30, 0x40};
    const unsigned char p1[] = {0xFF};
    CHECK_EQ(RegisterEventSlot(pool, /*id*/1001, /*pa*/11, /*pb*/22, "alpha",
                               p0, sizeof(p0)), 0);
    CHECK_EQ(RegisterEventSlot(pool, /*id*/1002, /*pa*/33, /*pb*/44, "beta",
                               p1, sizeof(p1)), 1);

    CHECK_EQ(pool.slots[0].id, 1001);
    CHECK_EQ(pool.slots[0].payloadA, 11);
    CHECK_EQ(pool.slots[0].payload[2], 0x30);
    CHECK(std::strcmp(pool.slots[1].label, "beta") == 0);
    CHECK_EQ(pool.slots[1].payload[0], 0xFF);

    // Age twice: both slots (ttl 2 -> 1 -> 0) free on the second tick.
    ExpireEventSlots(pool);
    CHECK_EQ(pool.slots[0].id, 1001);
    CHECK_EQ(pool.slots[1].id, 1002);
    ExpireEventSlots(pool);
    CHECK_EQ(pool.slots[0].id, 0);
    CHECK_EQ(pool.slots[1].id, 0);

    // Pool is empty again -> next register reuses slot 0.
    CHECK_EQ(RegisterEventSlot(pool, 2001, 5, 6, "gamma", nullptr, 0), 0);
    CHECK_EQ(pool.slots[0].id, 2001);

    NullTick(); // installed in unused dispatch slots; no observable effect.
}
