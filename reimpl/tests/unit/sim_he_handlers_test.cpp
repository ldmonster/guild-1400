// Unit tests for the per-type He handler step functions (he_handlers.{h,cpp}).
// Each test drives a translated step with a synthetic He record, a seeded clock,
// and recording leaf hooks, checking the verdict, the abort/free gates, the cmd29
// re-arm, the byte countdown, and the profession-ordinal scan against a reference
// derived from the IDA decompilation.
#include "test.h"

#include <cstdint>
#include <vector>

#include "sim/he_handlers.h"
#include "sim/npcaction.h"   // NpcClock
#include "sim/he.h"

using namespace guild;
using namespace guild::sim;

namespace {

HeRecord* MakeHe(std::vector<uint8_t>& buf) {
    buf.assign(400, 0);
    return reinterpret_cast<HeRecord*>(buf.data());
}

// Recording leaves.
int s_freed = 0;
i32 FreeHook(HeRecord*) { s_freed++; return -555; }
int s_dispatched = 0;
HeRecord* s_dispatchArg = nullptr;
i32 DispatchHook(HeRecord* sub) { s_dispatched++; s_dispatchArg = sub; return 7; }
i32 s_pktStatus = 0; i32 s_pktAsked = -1;
i32 PacketHook(i32 handle) { s_pktAsked = handle; return s_pktStatus; }
int s_queued = 0; int s_queueArg = -99; i32 s_queueRet = 0x4321;
i32 QueueHook(int arg, HeRecord*) { s_queued++; s_queueArg = arg; return s_queueRet; }
int s_ca = 0, s_ev = 0, s_bd = 0;
i32 CaHook() { return s_ca; }
i32 EvHook() { return s_ev; }
i32 BdHook() { return s_bd; }
int s_tick = 0;
i32 TickHook() { s_tick++; return 0; }

HeHandlerHooks MakeHooks() {
    HeHandlerHooks hk{};
    hk.freeHandlerEntry = FreeHook;
    hk.npcActionDispatch = DispatchHook;
    hk.packetStatus = PacketHook;
    hk.queueRequestEntity29 = QueueHook;
    hk.charActionTick = CaHook;
    hk.eventTick = EvHook;
    hk.buildingTick = BdHook;
    return hk;
}

void ResetTrace() {
    s_freed = s_dispatched = s_queued = s_tick = 0;
    s_dispatchArg = nullptr; s_pktAsked = -1; s_queueArg = -99;
    s_ca = s_ev = s_bd = 0;
}

} // namespace

// ===========================================================================
// NpcActionHandler — dispatch then free (or free immediately on abort).
// ===========================================================================
TEST(SimHeH, NpcActionHandler) {
    auto hk = MakeHooks(); SetHeHandlerHooks(&hk);
    std::vector<uint8_t> buf; HeRecord* h = MakeHe(buf);

    // abort sentinel (-2) -> free, no dispatch
    ResetTrace(); He_State(h) = -2;
    CHECK_EQ(He_NpcActionHandler(h), -555);
    CHECK_EQ(s_freed, 1);
    CHECK_EQ(s_dispatched, 0);

    // normal -> dispatch(h+172) then free
    ResetTrace(); He_State(h) = 0;
    CHECK_EQ(He_NpcActionHandler(h), -555);
    CHECK_EQ(s_dispatched, 1);
    CHECK_EQ(s_freed, 1);
    CHECK(reinterpret_cast<uint8_t*>(s_dispatchArg) ==
          reinterpret_cast<uint8_t*>(h) + 172);
    SetHeHandlerHooks(nullptr);
}

// ===========================================================================
// CounterWaitHandler — byte countdown at +172.
// ===========================================================================
TEST(SimHeH, CounterWaitHandler) {
    auto hk = MakeHooks(); SetHeHandlerHooks(&hk);
    std::vector<uint8_t> buf; HeRecord* h = MakeHe(buf);
    u8* counter = reinterpret_cast<uint8_t*>(h) + 172;

    // state == -2 -> free
    ResetTrace(); He_State(h) = -2;
    CHECK_EQ(He_CounterWaitHandler(h), -555);
    CHECK_EQ(s_freed, 1);

    // state < -2 -> passthrough (returns record, no free)
    ResetTrace(); He_State(h) = -5;
    i32 r = He_CounterWaitHandler(h);
    CHECK_EQ(s_freed, 0);
    CHECK(r == static_cast<i32>(reinterpret_cast<intptr_t>(h)));

    // state > 0 -> passthrough, counter untouched
    ResetTrace(); He_State(h) = 3; *counter = 9;
    He_CounterWaitHandler(h);
    CHECK_EQ((int)*counter, 9);
    CHECK_EQ(s_freed, 0);

    // state == 0, counter > 0 -> decrement
    ResetTrace(); He_State(h) = 0; *counter = 4;
    He_CounterWaitHandler(h);
    CHECK_EQ((int)*counter, 3);
    CHECK_EQ(s_freed, 0);

    // state == 0, counter == 0 -> free
    ResetTrace(); He_State(h) = 0; *counter = 0;
    CHECK_EQ(He_CounterWaitHandler(h), -555);
    CHECK_EQ(s_freed, 1);
    SetHeHandlerHooks(nullptr);
}

// ===========================================================================
// Entity29RequestHandler — packet-ack gate + cmd29 re-arm.
// ===========================================================================
TEST(SimHeH, Entity29RequestHandler) {
    auto hk = MakeHooks(); SetHeHandlerHooks(&hk);
    GameTime clk{}; clk.day = 70; clk.hour = 8; clk.minute = 58; SetNpcClock(clk);
    std::vector<uint8_t> buf; HeRecord* h = MakeHe(buf);

    // pending packet not yet acked -> passthrough (status 0), no state change
    ResetTrace(); He_ReqHandle(h) = 0x10; s_pktStatus = 0; He_State(h) = 0; He_Flags(h) = 2;
    CHECK_EQ(He_Entity29RequestHandler(h), 0);
    CHECK_EQ(s_pktAsked, 0x10);
    CHECK_EQ(He_ReqHandle(h), 0x10);    // unchanged
    CHECK_EQ(s_queued, 0);

    // packet acked, state 0, needs-cmd29 flag set -> stamp + advance + re-arm
    ResetTrace(); He_ReqHandle(h) = 0x10; s_pktStatus = 1;
    He_State(h) = 0; He_Flags(h) = kHeNeedsCmd29; s_queueRet = 0xABC;
    i32 r = He_Entity29RequestHandler(h);
    CHECK_EQ(r, 0xABC);
    CHECK_EQ(He_ReqHandle(h), 0xABC);
    CHECK_EQ(s_queueArg, -1);
    CHECK_EQ(He_ApptTime(h).day, 70);
    CHECK_EQ((int)He_ApptTime(h).hour, 9);   // 8:58 + 2min = 9:00
    CHECK_EQ(He_ApptTime(h).minute, 0);

    // packet acked, state == -2 -> free
    ResetTrace(); He_ReqHandle(h) = -1; He_State(h) = -2;
    CHECK_EQ(He_Entity29RequestHandler(h), -555);
    CHECK_EQ(s_freed, 1);

    // packet acked, state == -1 -> free
    ResetTrace(); He_ReqHandle(h) = -1; He_State(h) = -1;
    CHECK_EQ(He_Entity29RequestHandler(h), -555);

    // packet acked, state < -2 (e.g. -7) -> passthrough that state, no free
    ResetTrace(); He_ReqHandle(h) = -1; He_State(h) = -7;
    CHECK_EQ(He_Entity29RequestHandler(h), -7);
    CHECK_EQ(s_freed, 0);

    // packet acked, state 0 but flag not set -> no re-arm, returns 0
    ResetTrace(); He_ReqHandle(h) = -1; He_State(h) = 0; He_Flags(h) = 0;
    CHECK_EQ(He_Entity29RequestHandler(h), 0);
    CHECK_EQ(s_queued, 0);
    CHECK_EQ(He_ReqHandle(h), -1);
    SetHeHandlerHooks(nullptr);
}

// ===========================================================================
// FindEntityHandlerOrdinal — profession-6 ordinal scan.
// ===========================================================================
TEST(SimHeH, FindEntityHandlerOrdinal) {
    // 768-slot profession column; mark a few prof-6 records.
    std::vector<uint8_t> prof(768, 0);
    prof[2]  = 6;   // 1st prof-6 (ordinal 0)
    prof[5]  = 6;   // 2nd prof-6 (ordinal 1)
    prof[9]  = 6;   // 3rd prof-6 (ordinal 2)
    SetHeProfessionColumn(prof.data(), 768);

    CHECK_EQ(He_FindEntityHandlerOrdinal(2), 0);
    CHECK_EQ(He_FindEntityHandlerOrdinal(5), 1);
    CHECK_EQ(He_FindEntityHandlerOrdinal(9), 2);
    // a slot that is not profession 6 -> never matches `target` -> -1
    CHECK_EQ(He_FindEntityHandlerOrdinal(3), -1);
    // slot 0 (prof 0) -> -1
    CHECK_EQ(He_FindEntityHandlerOrdinal(0), -1);

    // no column installed -> no prof-6 found -> -1
    SetHeProfessionColumn(nullptr, 0);
    CHECK_EQ(He_FindEntityHandlerOrdinal(2), -1);
}

// ===========================================================================
// UpdateSubsystems — tick + subsystem short-circuit.
// ===========================================================================
TEST(SimHeH, UpdateSubsystems) {
    auto hk = MakeHooks(); SetHeHandlerHooks(&hk);

    // all stubs zero -> ticks the pool, returns 0
    ResetTrace();
    CHECK_EQ(He_UpdateSubsystems(TickHook), 0);
    CHECK_EQ(s_tick, 1);

    // charAction claims -> returns 1
    ResetTrace(); s_ca = 1;
    CHECK_EQ(He_UpdateSubsystems(TickHook), 1);

    // event claims -> returns 1
    ResetTrace(); s_ev = 1;
    CHECK_EQ(He_UpdateSubsystems(TickHook), 1);

    // building claims -> returns 1
    ResetTrace(); s_bd = 1;
    CHECK_EQ(He_UpdateSubsystems(TickHook), 1);
    SetHeHandlerHooks(nullptr);
}
