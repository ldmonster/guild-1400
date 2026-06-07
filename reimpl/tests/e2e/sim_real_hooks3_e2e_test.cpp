// e2e: a full NPC appointment routine driven end-to-end with ALL real hooks
// installed (no mocks on the wired paths). Build a real handler entry + a He
// record, run an action routine, and verify real command packets were enqueued
// onto the real CommandQueue, the handler was freed back to the real He pool, and
// the record's appointment state advanced exactly as a hand-computed reference.
#include "sim/real_hooks.h"
#include "sim/real_hooks2.h"
#include "sim/real_hooks3.h"

#include "sim/command.h"
#include "sim/handler_entry.h"
#include "sim/he.h"
#include "sim/gametime.h"
#include "sim/npcaction.h"
#include "sim/npcaction2.h"

#include "test.h"

#include <cstring>

using namespace guild;
using namespace guild::sim;

namespace {

struct HeBuf { HeRecord rec; };

HandlerRecord* AllocRealHandler(HandlerTable& he, u8 kind) {
    he.RegisterHandlerByType(kind, [](HandlerRecord*) {}, [](HandlerRecord*) -> i32 { return 0; });
    HeBuf d{};
    std::memset(&d, 0, sizeof(d));
    u8* a1 = reinterpret_cast<u8*>(&d.rec);
    a1[4] = kind;
    *reinterpret_cast<i32*>(a1 + 8) = -1;
    return he.AllocHandlerEntry(&d.rec);
}

} // namespace

// Full appointment lifecycle: stamp+request (real opcode-29) -> reset-to-state0
// (advance +2 days) -> teardown free (real He pool), all through real hooks.
TEST(SimRealHooks3E2E, AppointmentRoutineAllRealHooks) {
    // Install all three waves; no mocks on the wired paths.
    InstallRealSimHooks();
    InstallRealSimHooks2();
    InstallRealSimHooks3();

    CommandQueue* q = RealCommandQueue();
    HandlerTable* he = RealHandlerTable();
    CHECK(q != nullptr);
    CHECK(he != nullptr);
    he->Init();   // clean pool

    // A known wall clock the NPC routine stamps from.
    GameTime clock{};
    clock.day = 10; clock.hour = 9; clock.minute = 30; clock.second = 0;
    SetNpcClock(clock);

    // 1) Allocate a REAL handler-entry in the real He pool.
    HandlerRecord* pool = AllocRealHandler(*he, /*kind=*/4);
    CHECK(pool != nullptr);
    CHECK_EQ(he->live_count(), 1);

    // 2) Drive the action routine on a full-size He record. The record requests a
    //    cmd29 entity packet (flag 0x02), then resets to state 0 (+2 days).
    HeBuf b{};
    std::memset(&b, 0, sizeof(b));
    He_Id(&b.rec) = 0x1234;
    He_Flags(&b.rec) = kHeNeedsCmd29;        // 0x02 -> emit entity29 on stamp

    u32 sendBefore = q->send_count();

    NpcAction_StampTimeAndRequestEntity(&b.rec);  // -> real QueueRequestEntity29
    int retHour = NpcAction_ResetToState0(&b.rec);  // -> +2 days, state 0

    // 3) Verify a REAL opcode-29 packet was enqueued onto the REAL queue.
    CHECK_EQ(q->send_count(), sendBefore + 1);
    CHECK(q->pending_head() != nullptr);
    CommandPacket& slot = q->ring_slot((sendBefore + 1) & 0x7FFF);
    CHECK_EQ((int)slot.opcode(), 29);
    CHECK_EQ((int)slot.get32(0x10), 0x1234);   // He id (+4) snapshotted to payload

    // 4) Verify the appointment STATE advanced exactly like a reference computation
    //    (stamp clock, then +2 days; state cleared to 0).
    GameTime ref = clock;
    int refHour = GameTimeAdvance(&ref, /*days=*/2, 0, 0);
    CHECK_EQ(retHour, refHour);
    CHECK_EQ(GameTimeCompare(&He_ApptTime(&b.rec), &ref), 0);
    CHECK_EQ((int)He_State(&b.rec), 0);

    // 5) Teardown: free the real handler entry through the wired hook -> real He
    //    pool. Live count returns to 0 and the pool slot's kind byte is cleared.
    const NpcLeafHooks& h = GetNpcLeafHooks();
    CHECK(h.freeHandlerEntry != nullptr);
    h.freeHandlerEntry(reinterpret_cast<HeRecord*>(pool));
    CHECK_EQ(he->live_count(), 0);
    CHECK_EQ((int)HrKind(pool), 0);
}

// A second routine: the plague teardown path frees the He pool via the real hook
// after the spread members have been emitted, confirming npcaction2's wired
// freeHandlerEntry composes with the same shared pool.
TEST(SimRealHooks3E2E, PlagueTeardownFreesRealHePool) {
    InstallRealSimHooks();
    InstallRealSimHooks2();
    InstallRealSimHooks3();

    HandlerTable* he = RealHandlerTable();
    he->Init();
    HandlerRecord* a = AllocRealHandler(*he, /*kind=*/5);
    HandlerRecord* bRec = AllocRealHandler(*he, /*kind=*/6);
    CHECK(a != nullptr);
    CHECK(bRec != nullptr);
    CHECK_EQ(he->live_count(), 2);

    // npcaction2's freeHandlerEntry is wired to the same real HandlerTable.
    const NpcAction2Hooks& h2 = GetNpcAction2Hooks();
    CHECK(h2.freeHandlerEntry != nullptr);
    h2.freeHandlerEntry(reinterpret_cast<HeRecord*>(a));
    CHECK_EQ(he->live_count(), 1);
    CHECK_EQ((int)HrKind(a), 0);
    h2.freeHandlerEntry(reinterpret_cast<HeRecord*>(bRec));
    CHECK_EQ(he->live_count(), 0);
    CHECK_EQ((int)HrKind(bRec), 0);
}
