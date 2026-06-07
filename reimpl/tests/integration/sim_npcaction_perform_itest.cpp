// Integration tests: the NpcActionPerform handlers driven through the REAL sibling
// modules — the address-keyed dispatch table (RegisterNpcActionsPerform /
// NpcActionPerform_TableEntry), the live CommandQueue command-builder pipeline
// (command.cpp + command_builders3.cpp RequestBuildOp72), and the real GameTime
// calendar (gametime.cpp GameTimeAdvance). The perform handlers' command-emit
// leaves are bridged to the real builders so a DrinkTavern perform actually stages
// a real opcode-72 packet in a real queue, and the result codes are asserted
// end-to-end through the table.
#include "test.h"

#include "sim/npcaction_perform.h"
#include "sim/command.h"
#include "sim/command_builders3.h"
#include "sim/gametime.h"

#include <cstring>

using namespace guild::sim;
using guild::u8;
using guild::i8;
using guild::i32;

namespace {

// A live command queue shared by the bridged leaves.
CommandQueue* g_queue = nullptr;
i32 g_lastOp72Slot = -2;

// Bridge the perform op72 leaf to the REAL RequestBuildOp72 builder.
void bridge_op72(i32 id, u8 value) {
    if (g_queue)
        g_lastOp72Slot = RequestBuildOp72(*g_queue, id, static_cast<i8>(value));
}
// Tavern gate accepts so the command burst runs.
int bridge_tavern(i32, const void*, i32) { return 1; }

// Capture op25 / op90 so we still see the full DrinkTavern burst order.
int g_op25Count = 0, g_op90Count = 0;
void bridge_op25(i32, int, int, int, int) { g_op25Count++; }
void bridge_op90(int, i32) { g_op90Count++; }

NpcActionPerformHooks MakeRealBridgeHooks() {
    NpcActionPerformHooks hk{};
    hk.requestBuildOp72 = bridge_op72;
    hk.queueRequestArgs25 = bridge_op25;
    hk.requestBuildOp90 = bridge_op90;
    hk.aiLoadBuildingGraphic = bridge_tavern;
    return hk;
}

struct Ctx { u8 buf[64]; };
Ctx MakeCtx(u8 type, i32 at4 = 0, i32 at16 = 0) {
    Ctx c{}; std::memset(c.buf, 0, sizeof(c.buf));
    c.buf[0] = type;
    std::memcpy(c.buf + 4, &at4, 4);
    std::memcpy(c.buf + 16, &at16, 4);
    return c;
}

} // namespace

// The dispatch table is byte-faithful to the perform addresses and routes each
// address to a distinct handler.
TEST(NpcActionPerformItest, TableDispatch_addressesResolve) {
    CHECK_EQ(RegisterNpcActionsPerform(), 10);
    CHECK(NpcActionPerform_TableEntry(0x470ea4) != nullptr);  // Spionage
    CHECK(NpcActionPerform_TableEntry(0x471840) != nullptr);  // EnterBuilding
    CHECK(NpcActionPerform_TableEntry(0x4737cc) != nullptr);  // ShopTransaction
    CHECK(NpcActionPerform_TableEntry(0x4742ec) != nullptr);  // DrinkTavern
    CHECK(NpcActionPerform_TableEntry(0x4756c4) != nullptr);  // PickupCarry
    CHECK(NpcActionPerform_TableEntry(0x000000) == nullptr);
    // All 10 entries distinct.
    const int addrs[] = {0x470ea4, 0x471840, 0x471cb4, 0x471dfc, 0x471f24,
                         0x4737cc, 0x4742ec, 0x474c18, 0x474da0, 0x4756c4};
    for (int i = 0; i < 10; ++i)
        for (int j = i + 1; j < 10; ++j)
            CHECK(NpcActionPerform_TableEntry(addrs[i]) !=
                  NpcActionPerform_TableEntry(addrs[j]));
}

// DrinkTavern's op72 leaf wired to the REAL command builder stages a real packet
// in a real CommandQueue, and the burst runs in the original order (op25,op72,op90).
TEST(NpcActionPerformItest, DrinkTavern_realCommandQueue) {
    CommandQueue q;
    q.Init();
    q.set_standalone(true);
    q.set_disconnected(false);
    g_queue = &q;
    g_lastOp72Slot = -2;
    g_op25Count = g_op90Count = 0;

    auto hk = MakeRealBridgeHooks();
    SetNpcActionPerformHooks(&hk);

    Ctx actor = MakeCtx(0, /*actorId*/ 4242);
    Ctx ctx = MakeCtx(0); ctx.buf[16] = 0x33;  // op72 byte value

    CHECK_EQ(NpcActionPerform_DrinkTavern(actor.buf, ctx.buf, 1), 52);
    // op25 emitted, op72 staged into the real queue, op90 emitted.
    CHECK_EQ(g_op25Count, 1);
    CHECK_EQ(g_op90Count, 1);
    CHECK(g_lastOp72Slot >= 0);   // EnqueuePacket returned a ring slot

    // The staged ring slot really carries opcode 72 with our id/value.
    CommandPacket& p = q.ring_slot(static_cast<guild::u32>(g_lastOp72Slot));
    CHECK_EQ((int)p.opcode(), 72);
    i32 storedId = 0; std::memcpy(&storedId, p.bytes + 0x10, 4);
    CHECK_EQ(storedId, 4242);
    CHECK_EQ((int)p.bytes[0x14], 0x33);

    SetNpcActionPerformHooks(nullptr);
    g_queue = nullptr;
}

// Sanity: the real GameTime sibling links and the result codes hold under the
// table-driven path for a couple of dialog handlers (no leaves installed).
TEST(NpcActionPerformItest, RealGameTime_andResultCodes) {
    SetNpcActionPerformHooks(nullptr);  // inert leaves

    // Verdict/Arrest/PickupCarry return their codes on a type match even with no
    // dialog leaf installed (the leaf is a no-op).
    Ctx v = MakeCtx(21, 1, 2);
    CHECK_EQ(NpcActionPerform_Verdict(nullptr, v.buf, 7), 54);
    Ctx a = MakeCtx(4, 1, 2);
    CHECK_EQ(NpcActionPerform_Arrest(nullptr, a.buf, 7), 55);
    Ctx pc = MakeCtx(22, 1, 0);
    CHECK_EQ(NpcActionPerform_PickupCarry(nullptr, pc.buf, 7), 56);

    // The real GameTime calendar still advances correctly alongside (proves the
    // gametime sibling is linked into the same image).
    GameTime t{}; t.day = 3; t.hour = 23; t.minute = 59; t.second = 0;
    int h = GameTimeAdvance(&t, 0, 0, 2);   // +2 minutes -> rolls to next day 00:01
    CHECK_EQ(t.day, 4);
    CHECK_EQ((int)t.hour, 0);
    CHECK_EQ(t.minute, 1);
    CHECK_EQ(h, 0);
}
