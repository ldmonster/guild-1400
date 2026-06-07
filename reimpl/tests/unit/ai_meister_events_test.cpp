// Unit tests for the MeisterAi event-slot managers (gilde.exe 0x4c6f68..,
// 0x4c932c). Golden vectors computed independently in Python against a faithful
// model of the original slot arrays.
#include "ai/meister_events.h"

#include "test.h"

#include <cstring>

using namespace guild::ai;

namespace {
int g_logCount = 0;
void CountingLog(const char*) { ++g_logCount; }
} // namespace

TEST(AiMeisterEvents, ApRegisterFillsSlotFields) {
    ApEventPool pool{};
    int i = RegisterApEvent(pool, /*owner*/10, /*secondary*/100, /*primary*/5, "abc");
    CHECK_EQ(i, 0);
    CHECK_EQ(pool.slots[0].ttl, 2);
    CHECK_EQ(pool.slots[0].ownerKey, 10);
    CHECK_EQ(pool.slots[0].primary, 5);
    CHECK_EQ(pool.slots[0].secondary, 100);
    CHECK(std::strcmp(pool.slots[0].label, "abc") == 0);
    // label is NUL-padded to 63 bytes.
    CHECK_EQ(pool.slots[0].label[3], 0);
    CHECK_EQ(pool.slots[0].label[62], 0);
}

TEST(AiMeisterEvents, ApRegisterSequentialIndices) {
    ApEventPool pool{};
    CHECK_EQ(RegisterApEvent(pool, 10, 100, 5, "a"), 0);
    CHECK_EQ(RegisterApEvent(pool, 10, 200, 7, "b"), 1);
    CHECK_EQ(RegisterApEvent(pool, 20, 300, 11, "c"), 2);
}

TEST(AiMeisterEvents, ApSumByOwnerAndSecondary) {
    ApEventPool pool{};
    RegisterApEvent(pool, 10, 100, 5, "a");
    RegisterApEvent(pool, 10, 200, 7, "b");
    RegisterApEvent(pool, 20, 300, 11, "c");
    // primary: 5 + 7 = 12 for owner 10, 11 for owner 20.
    CHECK_EQ(SumApEventsByOwner(pool, 10), 12);
    CHECK_EQ(SumApEventsByOwner(pool, 20), 11);
    CHECK_EQ(SumApEventsByOwner(pool, 99), 0);
    // secondary: 100 + 200 = 300 for owner 10; 300 for owner 20.
    CHECK_EQ(SumApEventsBySecondary(pool, 10), 300);
    CHECK_EQ(SumApEventsBySecondary(pool, 20), 300);
}

TEST(AiMeisterEvents, ApExpireAgesThenFreesAfterTwoTicks) {
    ApEventPool pool{};
    RegisterApEvent(pool, 10, 100, 5, "a");
    RegisterApEvent(pool, 10, 200, 7, "b");
    RegisterApEvent(pool, 20, 300, 11, "c");
    // tick 1: ttl 2 -> 1, slots still live.
    ExpireApEventSlots(pool);
    CHECK_EQ(pool.slots[0].ttl, 1);
    CHECK_EQ(pool.slots[0].primary, 5);
    CHECK_EQ(SumApEventsByOwner(pool, 10), 12);
    // tick 2: ttl 1 -> 0 (<=0) -> freed (primary/secondary/label[0] cleared).
    ExpireApEventSlots(pool);
    CHECK_EQ(pool.slots[0].ttl, 0);
    CHECK_EQ(pool.slots[0].primary, 0);
    CHECK_EQ(pool.slots[0].secondary, 0);
    CHECK_EQ(pool.slots[0].label[0], 0);
    CHECK_EQ(SumApEventsByOwner(pool, 10), 0);
    CHECK_EQ(SumApEventsBySecondary(pool, 10), 0);
}

TEST(AiMeisterEvents, ApExpiredSlotIsReused) {
    ApEventPool pool{};
    RegisterApEvent(pool, 10, 100, 5, "a");
    ExpireApEventSlots(pool); // ttl 2->1
    ExpireApEventSlots(pool); // ttl 1->0, freed (slot 0 now empty)
    // The freed slot (primary/secondary/label[0] all zero) is the first empty
    // slot, so a fresh register reuses index 0.
    CHECK_EQ(RegisterApEvent(pool, 30, 9, 4, "z"), 0);
    CHECK_EQ(pool.slots[0].ownerKey, 30);
    CHECK_EQ(pool.slots[0].ttl, 2);
}

TEST(AiMeisterEvents, ApRegisterOverflowReportsAndFails) {
    ApEventPool pool{};
    g_logCount = 0;
    for (int k = 0; k < kApEventSlotCount; ++k)
        CHECK(RegisterApEvent(pool, 1, k + 1, k + 1, "f") == k);
    // pool full -> overflow path logs once and returns -1.
    CHECK_EQ(RegisterApEvent(pool, 1, 1, 1, "f", CountingLog), -1);
    CHECK_EQ(g_logCount, 1);
}

TEST(AiMeisterEvents, EventRegisterFillsSlotFields) {
    EventPool pool{};
    const unsigned char payload[] = {1, 2, 3};
    int i = RegisterEventSlot(pool, /*id*/42, /*pa*/7, /*pb*/8, "hello", payload,
                              sizeof(payload));
    CHECK_EQ(i, 0);
    CHECK_EQ(pool.slots[0].id, 42);
    CHECK_EQ(pool.slots[0].ttl, 2);
    CHECK_EQ(pool.slots[0].kind, 1);
    CHECK_EQ(pool.slots[0].payloadA, 7);
    CHECK_EQ(pool.slots[0].payloadB, 8);
    CHECK(std::strcmp(pool.slots[0].label, "hello") == 0);
    CHECK_EQ(pool.slots[0].payload[0], 1);
    CHECK_EQ(pool.slots[0].payload[1], 2);
    CHECK_EQ(pool.slots[0].payload[2], 3);
    CHECK_EQ(pool.slots[0].payload[3], 0);
}

TEST(AiMeisterEvents, EventRegisterClampsLabelAndPayload) {
    EventPool pool{};
    char longLabel[200];
    std::memset(longLabel, 'x', sizeof(longLabel) - 1);
    longLabel[sizeof(longLabel) - 1] = 0;
    unsigned char bigPayload[64];
    std::memset(bigPayload, 0xAB, sizeof(bigPayload));
    int i = RegisterEventSlot(pool, 99, 1, 2, longLabel, bigPayload,
                              sizeof(bigPayload));
    CHECK_EQ(i, 0);
    // label truncated to 127 chars, byte 127 is the NUL pad.
    CHECK_EQ(pool.slots[0].label[126], 'x');
    CHECK_EQ(pool.slots[0].label[127], 0);
    // payload clamped to the 14-byte region.
    CHECK_EQ(pool.slots[0].payload[13], 0xAB);
}

TEST(AiMeisterEvents, EventExpireFreesAfterTwoTicks) {
    EventPool pool{};
    RegisterEventSlot(pool, 42, 7, 8, "hello", nullptr, 0);
    ExpireEventSlots(pool); // ttl 2 -> 1
    CHECK_EQ(pool.slots[0].id, 42);
    CHECK_EQ(pool.slots[0].ttl, 1);
    ExpireEventSlots(pool); // ttl 1 -> 0 -> freed (id cleared)
    CHECK_EQ(pool.slots[0].id, 0);
    CHECK_EQ(pool.slots[0].ttl, 0);
}

TEST(AiMeisterEvents, NullTickDoesNothing) {
    NullTick(); // must compile and not crash.
    CHECK(true);
}
