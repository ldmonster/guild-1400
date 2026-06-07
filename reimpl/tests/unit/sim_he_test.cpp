#include "sim/handler_entry.h"
#include "sim/he.h"
#include "sim/command.h"
#include "sim/command_builders.h"
#include "test.h"

#include <cstring>

using namespace guild;
using namespace guild::sim;

// The command layer forward-declares netglue::SendPacket; the single definition
// for the test binary lives in the e2e TU (sim_he_e2e_test.cpp) so the two TUs
// link without a duplicate symbol.

// ---------------------------------------------------------------------------
// Helpers: build an alloc descriptor (HeRecord-shaped) for the handler table.
// ---------------------------------------------------------------------------
namespace {

HeRecord MakeDesc(u8 kind, i32 personId, i32 cityId, u8 flags) {
    HeRecord d;
    std::memset(&d, 0, sizeof(d));
    u8* b = reinterpret_cast<u8*>(&d);
    b[4] = kind;                                              // +4 kind
    *reinterpret_cast<i32*>(b + 8) = personId;               // +8 personId
    *reinterpret_cast<i32*>(b + 12) = cityId;                // +12 cityId
    b[54] = flags;                                           // +54 flags
    return d;
}

int g_initRuns = 0;
int g_runCalls = 0;
void TestInit(HandlerRecord*) { ++g_initRuns; }
i32  TestRun(HandlerRecord*) { ++g_runCalls; return 7; }

} // namespace

// ===========================================================================
// He_SumPlayerHandlerValues — score table + per-player accumulation.
// ===========================================================================
TEST(SimHe, ScoreTableColumn12RecoveredValues) {
    // Hand-computed v6[3] (column +12) for the recovered 26-entry table.
    static const i32 kExpect[26] = {
        0,0,2,2,4, 0,0,0,0,0, 0,0,0,3,0, 3,3,3,3,2, 5,4,5,5,5, 5
    };
    for (int i = 0; i < 26; ++i)
        CHECK_EQ(kHeScoreTable[i][3], kExpect[i]);
}

TEST(SimHe, SumPlayerHandlerValuesAccumulates) {
    HeEntityTable t;
    std::memset(&t, 0, sizeof(t));
    // Player 42 owns entries 0,1,2 with actions 4,13,20 and state 1.
    // action 4 -> v6[3]=4 ; action 13 -> 3 ; action 20 -> 5.  Total = 12.
    t.set_owner(0, 42); t.set_action(0, 4);  t.set_state(0, 1);
    t.set_owner(1, 42); t.set_action(1, 13); t.set_state(1, 1);
    t.set_owner(2, 42); t.set_action(2, 20); t.set_state(2, 1);
    // Player 42 entry with state != 1 is ignored.
    t.set_owner(3, 42); t.set_action(3, 20); t.set_state(3, 0);
    // A different player is ignored.
    t.set_owner(4, 99); t.set_action(4, 20); t.set_state(4, 1);
    CHECK_EQ(He_SumPlayerHandlerValues(t, 42), 12);
    CHECK_EQ(He_SumPlayerHandlerValues(t, 99), 5);
    CHECK_EQ(He_SumPlayerHandlerValues(t, 7), 0);
}

TEST(SimHe, SumActionOutOfRangeReusesPreviousRow) {
    // Faithful quirk: when action >= 26 the original reuses the previous match's
    // v6 frame. So an out-of-range action after a valid one adds that value again.
    HeEntityTable t;
    std::memset(&t, 0, sizeof(t));
    t.set_owner(0, 5); t.set_action(0, 20); t.set_state(0, 1);  // 5
    t.set_owner(1, 5); t.set_action(1, 200); t.set_state(1, 1); // reuse -> 5
    CHECK_EQ(He_SumPlayerHandlerValues(t, 5), 10);
}

// ===========================================================================
// HandlerTable — alloc / find-by-filter / free / counts.
// ===========================================================================
TEST(SimHe, AllocStampsRecordAndCounts) {
    HandlerTable tbl;
    tbl.Init();
    tbl.RegisterHandlerByType(13, &TestInit, &TestRun);
    g_initRuns = 0;

    HeRecord d = MakeDesc(/*kind*/13, /*personId*/-1, /*cityId*/0x1234, /*flags*/0);
    HandlerRecord* r = tbl.AllocHandlerEntry(&d);
    CHECK(r != nullptr);
    CHECK_EQ((int)HrKind(r), 13);
    CHECK_EQ(HrOrdinal(r), 0);           // first ordinal
    CHECK_EQ(HrField16(r), (i32)0x1234); // cityId mirror (+16)
    CHECK_EQ(HrId(r), (i32)-1);          // personId == -1 path
    CHECK_EQ((int)HrIndex(r), 0xFFFF);
    CHECK_EQ(tbl.live_count(), 1);
    CHECK_EQ(tbl.next_ordinal(), 1);
    CHECK_EQ(g_initRuns, 1);             // per-type init ran

    // Second alloc takes the next slot and a fresh ordinal.
    HeRecord d2 = MakeDesc(13, -1, 0x5678, 0);
    HandlerRecord* r2 = tbl.AllocHandlerEntry(&d2);
    CHECK(r2 != nullptr);
    CHECK_EQ(HrOrdinal(r2), 1);
    CHECK_EQ(tbl.live_count(), 2);
    CHECK(r2 != r);
}

TEST(SimHe, AllocRejectsUnregisteredOrBadType) {
    HandlerTable tbl;
    tbl.Init();
    // type 50 not registered -> nullptr
    HeRecord d = MakeDesc(50, -1, 0, 0);
    CHECK(tbl.AllocHandlerEntry(&d) == nullptr);
    // type >= 0x88 -> nullptr
    HeRecord d2 = MakeDesc(0x88, -1, 0, 0);
    CHECK(tbl.AllocHandlerEntry(&d2) == nullptr);
    CHECK_EQ(tbl.live_count(), 0);
}

TEST(SimHe, AllocResolvesPersonMirror) {
    HandlerTable tbl;
    tbl.Init();
    tbl.RegisterHandlerByType(6, &TestInit, &TestRun);
    // synthetic Person record: +0 marker word, +4 id dword.
    static u8 person[16];
    std::memset(person, 0, sizeof(person));
    *reinterpret_cast<u16*>(person + 0) = 0xBEEF;
    *reinterpret_cast<i32*>(person + 4) = 0x00C0FFEE;
    tbl.set_person_find([](i32 id) -> const void* {
        return id == 555 ? person : nullptr;
    });
    HeRecord d = MakeDesc(6, /*personId*/555, 0, 0);
    HandlerRecord* r = tbl.AllocHandlerEntry(&d);
    CHECK(r != nullptr);
    CHECK_EQ(HrId(r), (i32)0x00C0FFEE);
    CHECK_EQ((int)HrIndex(r), 0xBEEF);
    // person not found aborts alloc
    HeRecord d2 = MakeDesc(6, /*personId*/999, 0, 0);
    CHECK(tbl.AllocHandlerEntry(&d2) == nullptr);
}

TEST(SimHe, FindFirstByFilterMatchesKindAndId) {
    HandlerTable tbl;
    tbl.Init();
    tbl.RegisterHandlerByType(13, &TestInit, &TestRun);
    tbl.RegisterHandlerByType(20, &TestInit, &TestRun);

    HeRecord a = MakeDesc(13, -1, 100, 0);  HandlerRecord* ra = tbl.AllocHandlerEntry(&a);
    HeRecord b = MakeDesc(20, -1, 200, 0);  HandlerRecord* rb = tbl.AllocHandlerEntry(&b);
    HeRecord c = MakeDesc(13, -1, 300, 0);  HandlerRecord* rc = tbl.AllocHandlerEntry(&c);
    (void)ra; (void)rc;

    // Filter by kind 20 (selector 0): exactly one match (rb).
    HandlerRecord* m = tbl.FindFirstHandlerByFilter(1, 0, 20);
    CHECK_EQ(m, rb);
    CHECK(tbl.FindNextMatchingHandler() == nullptr);

    // Filter by ordinal id (selector 1) == 2 -> rc (third alloc).
    HandlerRecord* m2 = tbl.FindFirstHandlerByFilter(1, 1, 2);
    CHECK_EQ(m2, rc);

    // Filter by field@+16 (selector 3) == 100 -> ra.
    HandlerRecord* m3 = tbl.FindFirstHandlerByFilter(1, 3, 100);
    CHECK_EQ(m3, ra);

    // Filter by kind 13: two matches in alloc order (ra, rc).
    HandlerRecord* k = tbl.FindFirstHandlerByFilter(1, 0, 13);
    CHECK_EQ(k, ra);
    CHECK_EQ(tbl.FindNextMatchingHandler(), rc);
    CHECK(tbl.FindNextMatchingHandler() == nullptr);
}

TEST(SimHe, FreeClearsSlotAndDecrementsCount) {
    HandlerTable tbl;
    tbl.Init();
    tbl.RegisterHandlerByType(13, &TestInit, &TestRun);
    HeRecord a = MakeDesc(13, -1, 100, 0);  HandlerRecord* ra = tbl.AllocHandlerEntry(&a);
    HeRecord b = MakeDesc(13, -1, 200, 0);  HandlerRecord* rb = tbl.AllocHandlerEntry(&b);
    CHECK_EQ(tbl.live_count(), 2);
    CHECK_EQ(tbl.high_water(), 1);

    tbl.FreeHandlerEntry(rb);    // free the top -> high-water walks back to 0
    CHECK_EQ(tbl.live_count(), 1);
    CHECK_EQ((int)HrKind(rb), 0);    // slot zeroed
    CHECK_EQ(tbl.high_water(), 0);

    // ra is still findable; rb no longer matches.
    HandlerRecord* m = tbl.FindFirstHandlerByFilter(1, 0, 13);
    CHECK_EQ(m, ra);
    CHECK(tbl.FindNextMatchingHandler() == nullptr);

    CHECK_EQ(tbl.FreeHandlerEntry(nullptr), 0);  // null-safe
}

TEST(SimHe, CountMatchingHandlersByIndex) {
    HandlerTable tbl;
    tbl.Init();
    tbl.RegisterHandlerByType(6, &TestInit, &TestRun);
    static u8 p1[16]; static u8 p2[16];
    std::memset(p1, 0, sizeof(p1)); std::memset(p2, 0, sizeof(p2));
    *reinterpret_cast<u16*>(p1) = 11; *reinterpret_cast<i32*>(p1 + 4) = 1;
    *reinterpret_cast<u16*>(p2) = 22; *reinterpret_cast<i32*>(p2 + 4) = 2;
    tbl.set_person_find([](i32 id) -> const void* {
        return id == 1 ? p1 : id == 2 ? p2 : nullptr;
    });
    // two records with index 11, one with index 22
    HeRecord a = MakeDesc(6, 1, 0, 0); tbl.AllocHandlerEntry(&a);
    HeRecord b = MakeDesc(6, 1, 0, 0); tbl.AllocHandlerEntry(&b);
    HeRecord c = MakeDesc(6, 2, 0, 0); tbl.AllocHandlerEntry(&c);

    u16 want11 = 11, want22 = 22;
    CHECK_EQ(tbl.CountMatchingHandlers(&want11), 2);
    CHECK_EQ(tbl.CountMatchingHandlers(&want22), 1);
    u16 missing = 99;
    CHECK_EQ(tbl.CountMatchingHandlers(&missing), 0);
}

TEST(SimHe, RunMessageBoxHandlersRunsIconRecords) {
    HandlerTable tbl;
    tbl.Init();
    tbl.RegisterHandlerByType(13, &TestInit, &TestRun);
    g_runCalls = 0;
    // flag 0x08 => linked into the icon array.
    HeRecord a = MakeDesc(13, -1, 0, kHfHasIcon); HandlerRecord* ra = tbl.AllocHandlerEntry(&a);
    HeRecord b = MakeDesc(13, -1, 0, kHfHasIcon); HandlerRecord* rb = tbl.AllocHandlerEntry(&b);
    CHECK(ra && rb);
    CHECK_EQ(tbl.icon_slot(0), ra);
    CHECK_EQ(tbl.icon_slot(1), rb);
    CHECK_EQ(tbl.icon_high_water(), 1);

    i32 r = tbl.RunMessageBoxHandlers();
    CHECK_EQ(g_runCalls, 2);  // both icon records ran their per-type callback
    CHECK_EQ(r, 7);           // last run result
}

// ===========================================================================
// Command builders — each emits a packet with the expected opcode + payload.
// Decode the staged ring slot back and verify the bytes.
// ===========================================================================
namespace {
// Pull the just-enqueued packet from the ring by its returned slot id.
CommandPacket& Enqueued(CommandQueue& q, i32 slot) { return q.ring_slot((u32)slot); }
u32 rd32(const u8* p) {
    return (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24);
}
u16 rd16(const u8* p) { return (u16)(p[0] | (p[1] << 8)); }
} // namespace

TEST(SimHe, BuilderPair33) {
    CommandQueue q; q.Init();
    i32 s = QueueRequestPair33(q, 0x11223344, 0x55667788);
    CommandPacket& p = Enqueued(q, s);
    CHECK_EQ(p.opcode(), (u8)33);
    CHECK_EQ(rd32(p.bytes + 0x10), (u32)0x11223344);
    CHECK_EQ(rd32(p.bytes + 0x14), (u32)0x55667788);
}

TEST(SimHe, BuilderSingle49AndOp88) {
    CommandQueue q; q.Init();
    i32 s = QueueRequestSingle49(q, 0x0BADBEEF);
    CommandPacket& p = Enqueued(q, s);
    CHECK_EQ(p.opcode(), (u8)49);
    CHECK_EQ(rd32(p.bytes + 0x10), (u32)0x0BADBEEF);

    i32 s2 = RequestBuildOp88(q, 0x12345678);
    CommandPacket& p2 = Enqueued(q, s2);
    CHECK_EQ(p2.opcode(), (u8)88);
    CHECK_EQ(rd32(p2.bytes + 0x10), (u32)0x12345678);
}

TEST(SimHe, BuilderQuad56DropsThirdArg) {
    CommandQueue q; q.Init();
    i32 s = QueueRequestQuad56(q, 0xAAAAAAAA, 0xBBBBBBBB, 0xCCCCCCCC, 0xDDDDDDDD);
    CommandPacket& p = Enqueued(q, s);
    CHECK_EQ(p.opcode(), (u8)56);
    CHECK_EQ(rd32(p.bytes + 0x10), (u32)0xAAAAAAAA); // a1
    CHECK_EQ(rd32(p.bytes + 0x14), (u32)0xBBBBBBBB); // a2
    CHECK_EQ(rd32(p.bytes + 0x18), (u32)0xDDDDDDDD); // a4 (a3 dropped)
}

TEST(SimHe, BuilderPair57Swaps) {
    CommandQueue q; q.Init();
    i32 s = QueueRequestPair57(q, 0x01010101 /*a1*/, 0x02020202 /*a2*/);
    CommandPacket& p = Enqueued(q, s);
    CHECK_EQ(p.opcode(), (u8)57);
    CHECK_EQ(rd32(p.bytes + 0x10), (u32)0x02020202); // a2 first
    CHECK_EQ(rd32(p.bytes + 0x14), (u32)0x01010101); // a1 second
}

TEST(SimHe, BuilderOp91AndOp93) {
    CommandQueue q; q.Init();
    i32 s = RequestBuildOp91(q, 1, 2, 3, 4);
    CommandPacket& p = Enqueued(q, s);
    CHECK_EQ(p.opcode(), (u8)91);
    CHECK_EQ(rd32(p.bytes + 0x10), (u32)1);
    CHECK_EQ(rd32(p.bytes + 0x14), (u32)2);
    CHECK_EQ(rd32(p.bytes + 0x18), (u32)4);  // a3==3 dropped

    i32 s2 = RequestBuildOp93(q, 10, 20, 30, 40);
    CommandPacket& p2 = Enqueued(q, s2);
    CHECK_EQ(p2.opcode(), (u8)93);
    CHECK_EQ(rd32(p2.bytes + 0x10), (u32)10);
    CHECK_EQ(rd32(p2.bytes + 0x14), (u32)20);
    CHECK_EQ(rd32(p2.bytes + 0x18), (u32)40); // a3==30 dropped
}

TEST(SimHe, BuilderOp73Str) {
    CommandQueue q; q.Init();
    i32 s = RequestBuildOp73Str(q, /*a1*/0x7, /*a2*/0x44332211, /*a3*/0x3,
                                /*a4*/0x88776655, /*a5*/0x9, /*a6*/(i16)0x1234, "PEST");
    CommandPacket& p = Enqueued(q, s);
    CHECK_EQ(p.opcode(), (u8)73);
    CHECK_EQ(p.bytes[0x14], (u8)0x7);
    CHECK_EQ(rd32(p.bytes + 0x17), (u32)0x44332211);
    CHECK_EQ(rd32(p.bytes + 0x1B), (u32)0x88776655);
    CHECK_EQ(p.bytes[0x1F], (u8)0x3);
    CHECK_EQ(p.bytes[0x20], (u8)0x9);
    CHECK_EQ(rd16(p.bytes + 0x21), (u16)0x1234);
    CHECK_EQ(std::memcmp(p.bytes + 0x27, "PEST", 5), 0);
}

TEST(SimHe, BuilderNamedObject53) {
    CommandQueue q; q.Init();
    i32 s = QueueRequestNamedObject53(q, /*a1*/0x100, /*a2*/0x200, /*obj*/"OBJ",
                                      /*a4*/0x300, /*a5*/0x1, /*name*/"Pest");
    CommandPacket& p = Enqueued(q, s);
    CHECK_EQ(p.opcode(), (u8)53);
    CHECK_EQ(rd32(p.bytes + 0x10), (u32)0x100);
    CHECK_EQ(rd32(p.bytes + 0x14), (u32)0x200);
    CHECK_EQ(rd32(p.bytes + 0x18), (u32)0x300);
    CHECK_EQ(p.bytes[0x1C], (u8)0x1);
    CHECK_EQ(std::memcmp(p.bytes + 0x1D, "OBJ", 4), 0);
    CHECK_EQ(std::memcmp(p.bytes + 0x3D, "Pest", 5), 0);

    // personStamped=false short-circuits with -1.
    CHECK_EQ(QueueRequestNamedObject53(q, 1, 2, "x", 3, 0, "y", /*stamped*/false), -1);
}

TEST(SimHe, BuilderEntity29Snapshot) {
    CommandQueue q; q.Init();
    HeRecord h;
    std::memset(&h, 0, sizeof(h));
    u8* b = reinterpret_cast<u8*>(&h);
    *reinterpret_cast<i32*>(b + 4)  = 0x0A0B0C0D;   // id -> +0x10
    *reinterpret_cast<i32*>(b + 68) = 0x11111111;   // -> +0x14
    *reinterpret_cast<i32*>(b + 82) = 0x22222222;   // appt day -> +0x22
    *reinterpret_cast<u16*>(b + 80) = 0x3333;       // -> +0x20
    i32 s = QueueRequestEntity29(q, /*a1*/2, &h);
    CommandPacket& p = Enqueued(q, s);
    CHECK_EQ(p.opcode(), (u8)29);
    CHECK_EQ(rd32(p.bytes + 0x10), (u32)0x0A0B0C0D);
    CHECK_EQ(rd32(p.bytes + 0x14), (u32)0x11111111);
    CHECK_EQ(rd16(p.bytes + 0x20), (u16)0x3333);
    CHECK_EQ(rd32(p.bytes + 0x22), (u32)0x22222222);
    CHECK_EQ(p.bytes[0x3E], (u8)2);   // a1
}
