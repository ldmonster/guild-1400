#include "test.h"

#include "sim/command_apply8.h"
#include "sim/command.h"
#include "sim/command_pending.h"

#include <cstring>

// e2e: drive the command_apply8 builders through a real CommandQueue and assert
// the send ring + sequence machinery evolve coherently across opcodes, then run a
// justice-console flow (resolve law record -> clamp -> emit) end to end.

using namespace guild;
using namespace guild::sim;

namespace {

struct World {
    i32 money = 0;
    // a tiny "law table": one law with a band and a current severity.
    bool hasLaw = false;
    CommandApply8Hooks::LawRecord rec{};
    int  current = 0;
    // capture of the last emitted gesetz apply
    int  lastLaw = -1, lastValue = 0;
    int  applyCount = 0;
};
World* g_w = nullptr;

i32  w_money() { return g_w->money; }
int  w_getrec(int, CommandApply8Hooks::LawRecord* o) { *o = g_w->rec; return g_w->hasLaw ? 1 : 0; }
void w_apply(int, int lt, int v) { g_w->lastLaw = lt; g_w->lastValue = v; g_w->applyCount++; }

void InstallWorld(World& w) {
    g_w = &w;
    CommandApply8Hooks h{};
    h.localPlayerMoney = &w_money;
    h.gesetzGetRecord = &w_getrec;
    h.gesetzRequestApply = &w_apply;
    SetCommandApply8Hooks(&h);
}

} // namespace

TEST(CommandApply8_E2E, BuilderSequenceThroughQueue) {
    World w; InstallWorld(w);
    w.money = 5000;
    CommandQueue q; q.Init();

    // Enqueue a heterogeneous sequence; each must land in a distinct ring slot
    // with a monotonically increasing sequence Count.
    i32 s1 = QueueRequest20(q, 42, (i16)7);
    i32 s2 = QueueRequestPair36(q, 100, 200);
    i32 s3 = EnqueueBuildingActionStart(q, "Smithy");
    i32 s4 = SendPlayerMoneyState(q);
    i32 s5 = RequestBuildOp90_Thunk(q, 9, 8);

    CHECK(s1 >= 0 && s2 >= 0 && s3 >= 0 && s4 >= 0 && s5 >= 0);
    // distinct slots
    CHECK(s1 != s2 && s2 != s3 && s3 != s4 && s4 != s5);

    CommandPacket& r1 = q.ring_slot(static_cast<u32>(s1));
    CommandPacket& r3 = q.ring_slot(static_cast<u32>(s3));
    CommandPacket& r4 = q.ring_slot(static_cast<u32>(s4));
    CommandPacket& r5 = q.ring_slot(static_cast<u32>(s5));

    CHECK_EQ(r1.opcode(), (u8)20);
    CHECK_EQ(r3.opcode(), (u8)5);
    CHECK(std::strcmp(reinterpret_cast<const char*>(r3.bytes + 0x10), "Smithy") == 0);
    CHECK_EQ(r4.opcode(), (u8)32);
    CHECK_EQ(r4.get32(0x11), (u32)5000);
    CHECK_EQ(r5.opcode(), (u8)90);
    CHECK_EQ(r5.get32(0x10), (u32)8); // thunk swapped
    CHECK_EQ(r5.get32(0x14), (u32)9);

    // The sequence Counts are strictly increasing in enqueue order.
    CHECK(r3.count() > r1.count());
    CHECK(r5.count() > r4.count());

    // The queue's send counter advanced by 5 commands.
    CHECK_EQ(q.send_count(), (u32)5);
    SetCommandApply8Hooks(nullptr);
}

TEST(CommandApply8_E2E, PendingBlockRequest40) {
    World w; InstallWorld(w);
    CommandQueue q; q.Init();
    PendingState pending;

    u8 payload[0x114];
    for (int i = 0; i < 0x114; ++i) payload[i] = static_cast<u8>(i * 3);
    i32 slot = QueueRequest40(q, pending, payload);
    CHECK(slot >= 0);
    CommandPacket& hdr = q.ring_slot(static_cast<u32>(slot));
    CHECK_EQ(hdr.opcode(), (u8)40);
    // The header's +12 link was set to the first fragment's Count by
    // EmitWithPendingBlock (nonzero when a block was staged).
    CHECK(hdr.extra() != 0);
    // Staging buffer was consumed.
    CHECK_EQ(pending.staged, (u32)0);
    SetCommandApply8Hooks(nullptr);
}

TEST(CommandApply8_E2E, JusticeConsoleFlow) {
    World w; InstallWorld(w);
    // Law id 4, band [2..18], adjustable, current severity 10.
    w.hasLaw = true; w.rec.min = 2; w.rec.max = 18; w.rec.adjustable = true; w.current = 10;

    // "set" to an in-band absolute value -> applied verbatim.
    CHECK_EQ(QueueSetJusticeSeverity(4, 12), 1);
    CHECK_EQ(w.lastLaw, 4);
    CHECK_EQ(w.lastValue, 12);

    // "set" out of band while adjustable -> rejected, no new emit.
    int before = w.applyCount;
    CHECK_EQ(QueueSetJusticeSeverity(4, 50), 0);
    CHECK_EQ(w.applyCount, before);

    // "adjust" +5 from current 10 -> 15 (in band).
    CHECK_EQ(QueueAdjustJusticeSeverity(4, +1, 5, w.current), 1);
    CHECK_EQ(w.lastValue, 15);

    // "adjust" +20 from current 10 -> clamp to 18 (max).
    CHECK_EQ(QueueAdjustJusticeSeverity(4, +1, 20, w.current), 1);
    CHECK_EQ(w.lastValue, 18);

    // unknown law -> reject.
    w.hasLaw = false;
    CHECK_EQ(QueueSetJusticeSeverity(99, 5), 0);
    SetCommandApply8Hooks(nullptr);
}
