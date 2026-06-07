#include "test.h"

// Integration: drive charaction_steps5 step leaves against a REAL reconstructed
// sibling module — the "He" handler pool (handler_entry.cpp,
// VIBE_He_AllocHandlerEntry @0x4c5f40 / VIBE_He_FreeHandlerEntry @0x4c6144). No
// mock allocator: the shared NpcLeafHooks.freeHandlerEntry is forwarded into the
// genuine HandlerTable::FreeHandlerEntry exactly as the live engine wires the
// handler-pool free path. We allocate a real live record in the real pool, then
// let a charaction_steps5 leaf (RestorePosFinishAlt / RunFollowTarget /
// RunBuyObject) reach the free path; we assert the real pool's live_count and
// high-water bookkeeping change as the sibling dictates, end to end.
//
// The CharActionStep5Hooks.randomModulo / findPersonById etc. are local recorders
// so the control flow is steerable and observable; the LOAD-BEARING side effect
// (the actual handler free + the saved-pose -> appointment copy through the real
// GameTime sibling) is the reconstructed one.
#include "sim/charaction_steps5.h"
#include "sim/handler_entry.h"   // REAL HandlerTable (He pool, 0x4c5f40 / 0x4c6144)
#include "sim/npcaction.h"       // NpcLeafHooks / SetNpcLeafHooks / NpcClock
#include "sim/gametime.h"        // REAL GameTimeAdvance / GameTimeCompare

#include <cstring>

using namespace guild;
using namespace guild::sim;

namespace {

// The one live handler pool the charaction free path operates on. A HandlerRecord
// is 332 bytes; the He_*/Cas5_* accessors decode the same bytes via HeRecord.
HandlerTable* g_table = nullptr;

// --- NpcLeafHooks: freeHandlerEntry forwards into the REAL pool ----------------
i32 RealFree(HeRecord* h) {
    // The He record and the pool's HandlerRecord are the same 332-byte object at
    // the same address; forward verbatim into the reconstructed sibling.
    if (!g_table) return 0;
    return g_table->FreeHandlerEntry(reinterpret_cast<HandlerRecord*>(h));
}
i32 RealQueue29(int, HeRecord*) { return 0; }
i32 RealPacketStatus(i32)       { return 1; }

const NpcLeafHooks kNpcHooks = {
    /*queueRequestEntity29*/ RealQueue29,
    /*freeHandlerEntry*/     RealFree,
    /*packetStatus*/         RealPacketStatus,
    /*lawBaseTextId*/        nullptr,
    /*findInventorySlot*/    nullptr,
    /*shuffleDwords*/        nullptr,
    /*requestBuildOp93*/     nullptr,
};

// --- CharActionStep5Hooks recorders -------------------------------------------
int       g_rollValue = 0;
HeRecord* g_leader    = nullptr;
HeRecord* g_self      = nullptr;
int       g_changeActionCalls = 0;

int       RecRoll(int)                                  { return g_rollValue; }
HeRecord* RecFindPerson(i32)                            { return g_leader; }
HeRecord* RecPersonQuery(i32, int, int, i32)            { return g_self; }
HeRecord* RecObjectQuery(i32, int, int, i32)            { return nullptr; }
HeRecord* RecFindFirst(int, int, int, int)              { return nullptr; }
HeRecord* RecFindNext()                                 { return nullptr; }
void      RecChangeAction(HeRecord*, HeRecord*, HeRecord*, u16) { ++g_changeActionCalls; }

CharActionStep5Hooks MakeHooks() {
    CharActionStep5Hooks h;
    std::memset(&h, 0, sizeof h);
    h.randomModulo       = RecRoll;
    h.findPersonById     = RecFindPerson;
    h.personQueryBegin   = RecPersonQuery;
    h.objectQueryFind    = RecObjectQuery;
    h.findFirstByFilter  = RecFindFirst;
    h.findNextMatching   = RecFindNext;
    h.changePlayerAction = RecChangeAction;
    return h;
}

void Dummy(HandlerRecord*) {}
i32  DummyRun(HandlerRecord*) { return 0; }

// Allocate one live record (kind byte 5, no person) in the real pool, return it
// as a HeRecord* (same bytes). The caller drives a leaf that frees it.
HeRecord* AllocLive(HandlerTable& t) {
    t.RegisterHandlerByType(5, &Dummy, &DummyRun);
    // desc template: +4 kind byte, +8 personId (-1 = no person resolve needed).
    alignas(8) std::uint8_t desc[sizeof(HeRecord)];
    std::memset(desc, 0, sizeof desc);
    desc[4] = 5;                                                 // kind
    *reinterpret_cast<i32*>(desc + 8) = -1;                      // personId == -1
    HandlerRecord* r = t.AllocHandlerEntry(reinterpret_cast<HeRecord*>(desc));
    return reinterpret_cast<HeRecord*>(r);
}

} // namespace

// RestorePosFinishAlt: copies saved-pose(+68) -> appointment(+82) then, with a
// nonzero RandomModulo(2) draw, frees the handler via the shared hook. Here the
// free hook forwards into the REAL HandlerTable::FreeHandlerEntry: a record we
// allocated in the real pool is genuinely freed (live_count 1 -> 0).
TEST(CharactionSteps5Itest, RestorePosFinishFreesRealPoolRecord) {
    HandlerTable table;
    table.Init();
    g_table = &table;

    HeRecord* h = AllocLive(table);
    CHECK(h != nullptr);
    CHECK_EQ(table.live_count(), 1);   // the real sibling minted a live record

    if (h) {
        // Seed a recognizable saved-pose; assert it is copied into the appt slot
        // (the deterministic part of the leaf runs before the free).
        He_SavedTime(h).day  = 12;
        He_SavedTime(h).hour = 9;
        He_ApptTime(h).day   = 0;
    }

    const CharActionStep5Hooks hooks = MakeHooks();
    SetCharActionStep5Hooks(&hooks);
    SetNpcLeafHooks(&kNpcHooks);

    g_rollValue = 1;                   // nonzero -> the free branch is taken
    i32 r = RestorePosFinishAlt(h);

    CHECK_EQ(r, 0);                    // real FreeHandlerEntry returns 0
    CHECK_EQ(table.live_count(), 0);   // the REAL pool freed it (cross-module flow)
    CHECK_EQ(table.high_water(), 0);   // and walked the high-water back down

    SetCharActionStep5Hooks(nullptr);
    SetNpcLeafHooks(nullptr);
    g_table = nullptr;
}

// With a zero RandomModulo(2) draw the leaf returns the draw WITHOUT freeing: the
// real pool's live record survives (no spurious free into the sibling).
TEST(CharactionSteps5Itest, RestorePosFinishKeepsRecordOnZeroRoll) {
    HandlerTable table;
    table.Init();
    g_table = &table;

    HeRecord* h = AllocLive(table);
    CHECK(h != nullptr);
    CHECK_EQ(table.live_count(), 1);

    const CharActionStep5Hooks hooks = MakeHooks();
    SetCharActionStep5Hooks(&hooks);
    SetNpcLeafHooks(&kNpcHooks);

    g_rollValue = 0;                   // zero -> no free
    i32 r = RestorePosFinishAlt(h);

    CHECK_EQ(r, 0);                    // returns the (zero) draw
    CHECK_EQ(table.live_count(), 1);   // record still live in the real pool

    SetCharActionStep5Hooks(nullptr);
    SetNpcLeafHooks(nullptr);
    g_table = nullptr;
}

// RunFollowTarget frees the handler through the same real sibling when the leader
// is absent (findPersonById -> null). Drive that gate and assert the real pool
// actually frees the live record.
TEST(CharactionSteps5Itest, RunFollowTargetFreesViaRealPoolWhenLeaderAbsent) {
    HandlerTable table;
    table.Init();
    g_table = &table;

    HeRecord* h = AllocLive(table);
    CHECK(h != nullptr);
    CHECK_EQ(table.live_count(), 1);

    const CharActionStep5Hooks hooks = MakeHooks();
    SetCharActionStep5Hooks(&hooks);
    SetNpcLeafHooks(&kNpcHooks);

    g_leader = nullptr;                // leader resolve fails -> free path
    i32 r = RunFollowTarget(h);

    CHECK_EQ(r, 0);                    // FreeHandlerEntry's result
    CHECK_EQ(table.live_count(), 0);   // real pool freed the follower handler
    CHECK_EQ(g_changeActionCalls, 0);  // never reached the drive-action tail

    SetCharActionStep5Hooks(nullptr);
    SetNpcLeafHooks(nullptr);
    g_table = nullptr;
}

// The pure GameTime sibling (gametime.cpp) is also real: StateReset24Alt stamps
// the clock and advances +24h; assert it round-trips through the real
// GameTimeAdvance and leaves the appointment hour-of-day == the clock's hour.
TEST(CharactionSteps5Itest, StateReset24AltUsesRealGameTimeAdvance) {
    HandlerTable table;        // not strictly needed but keeps the wiring uniform
    table.Init();
    g_table = &table;

    GameTime clk{};
    clk.day = 3; clk.hour = 8; clk.minute = 30; clk.second = 0;
    SetNpcClock(clk);

    alignas(8) std::uint8_t buf[sizeof(HeRecord)];
    std::memset(buf, 0, sizeof buf);
    HeRecord* h = reinterpret_cast<HeRecord*>(buf);

    const CharActionStep5Hooks hooks = MakeHooks();
    SetCharActionStep5Hooks(&hooks);
    SetNpcLeafHooks(&kNpcHooks);

    i32 hourOfDay = StateReset24Alt(h);

    // +24h keeps the same hour-of-day; the real advance rolled the day forward.
    CHECK_EQ(hourOfDay, 8);
    CHECK_EQ(static_cast<int>(He_ApptTime(h).hour), 8);
    CHECK_EQ(He_ApptTime(h).day, 4);   // day 3 + 24h == day 4

    SetCharActionStep5Hooks(nullptr);
    SetNpcLeafHooks(nullptr);
    g_table = nullptr;
}
