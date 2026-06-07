// End-to-end flow for the per-type He handler step functions (he_handlers.{h,cpp}).
// Models a handler-pool tick across several record types with a recording command/
// free-handler bridge: a cmd29 entity request that waits for its packet to ack and
// then re-arms, a counter-wait record that counts down to a free, and an NpcAction
// handler that dispatches its embedded sub-record and frees. This mirrors how the
// scheduler runs each live record's per-type step once per simulation tick.
#include "test.h"

#include <cstdint>
#include <vector>

#include "sim/he_handlers.h"
#include "sim/npcaction.h"
#include "sim/he.h"

using namespace guild;
using namespace guild::sim;

namespace {

HeRecord* MakeHe(std::vector<uint8_t>& buf) {
    buf.assign(400, 0);
    return reinterpret_cast<HeRecord*>(buf.data());
}

// A mock command/packet bridge tracking the queue + ack lifecycle.
struct Bridge {
    int freed = 0;
    int dispatched = 0;
    int queued = 0;
    i32 nextHandle = 0x200;
    bool acked = false;
};
Bridge g_b;

i32 FreeLeaf(HeRecord*) { g_b.freed++; return -1; }
i32 DispatchLeaf(HeRecord*) { g_b.dispatched++; return 0; }
i32 StatusLeaf(i32 /*handle*/) { return g_b.acked ? 1 : 0; }
i32 QueueLeaf(int /*arg*/, HeRecord*) { g_b.queued++; return g_b.nextHandle++; }

} // namespace

TEST(SimHeHe2e, Entity29RequestRearmLifecycle) {
    g_b = Bridge{};
    HeHandlerHooks hk{};
    hk.freeHandlerEntry = FreeLeaf;
    hk.packetStatus = StatusLeaf;
    hk.queueRequestEntity29 = QueueLeaf;
    SetHeHandlerHooks(&hk);

    GameTime clk{}; clk.day = 12; clk.hour = 10; clk.minute = 0; SetNpcClock(clk);
    std::vector<uint8_t> buf; HeRecord* h = MakeHe(buf);

    // Arm: a record with a pending packet, state 0, needs-cmd29 set.
    He_State(h) = 0; He_Flags(h) = kHeNeedsCmd29; He_ReqHandle(h) = 0x100;

    // Tick 1: packet pending -> nothing happens.
    g_b.acked = false;
    CHECK_EQ(He_Entity29RequestHandler(h), 0);
    CHECK_EQ(g_b.queued, 0);
    CHECK_EQ(He_ReqHandle(h), 0x100);

    // Tick 2: packet acks -> re-arm a fresh request, appointment advanced +2min.
    g_b.acked = true;
    i32 newHandle = He_Entity29RequestHandler(h);
    CHECK_EQ(g_b.queued, 1);
    CHECK_EQ(He_ReqHandle(h), newHandle);
    CHECK_EQ(newHandle, 0x200);
    CHECK_EQ(He_ApptTime(h).minute, 2);   // 10:00 + 2min
    CHECK_EQ(g_b.freed, 0);

    // Later the record's state flips to the done sentinel -> next tick frees it.
    He_State(h) = -2; He_ReqHandle(h) = -1;
    CHECK_EQ(He_Entity29RequestHandler(h), -1);
    CHECK_EQ(g_b.freed, 1);
    SetHeHandlerHooks(nullptr);
}

TEST(SimHeHe2e, CounterWaitCountsDownToFree) {
    g_b = Bridge{};
    HeHandlerHooks hk{};
    hk.freeHandlerEntry = FreeLeaf;
    SetHeHandlerHooks(&hk);

    std::vector<uint8_t> buf; HeRecord* h = MakeHe(buf);
    u8* counter = reinterpret_cast<uint8_t*>(h) + 172;
    He_State(h) = 0; *counter = 3;

    // 3 ticks decrement 3 -> 0 (never frees while it was nonzero at entry).
    He_CounterWaitHandler(h); CHECK_EQ((int)*counter, 2);
    He_CounterWaitHandler(h); CHECK_EQ((int)*counter, 1);
    He_CounterWaitHandler(h); CHECK_EQ((int)*counter, 0);
    CHECK_EQ(g_b.freed, 0);
    // 4th tick: counter already 0 -> free.
    He_CounterWaitHandler(h);
    CHECK_EQ(g_b.freed, 1);
    SetHeHandlerHooks(nullptr);
}

TEST(SimHeHe2e, NpcActionDispatchAndFree) {
    g_b = Bridge{};
    HeHandlerHooks hk{};
    hk.freeHandlerEntry = FreeLeaf;
    hk.npcActionDispatch = DispatchLeaf;
    SetHeHandlerHooks(&hk);

    std::vector<uint8_t> buf; HeRecord* h = MakeHe(buf);
    He_State(h) = 5;            // not the abort sentinel
    He_NpcActionHandler(h);
    CHECK_EQ(g_b.dispatched, 1);
    CHECK_EQ(g_b.freed, 1);

    // abort sentinel -> free without dispatch
    g_b = Bridge{};
    He_State(h) = -2;
    He_NpcActionHandler(h);
    CHECK_EQ(g_b.dispatched, 0);
    CHECK_EQ(g_b.freed, 1);
    SetHeHandlerHooks(nullptr);
}
