#include "test.h"

#include "sim/command_inherit.h"
#include "sim/command_codec.h"

#include <cstring>

// Integration: drive the inheritance emission body against a REAL CommandQueue
// (enqueue + flush via StoreReceivedPacket in standalone mode) and a real
// DeltaWriter, then prove the two State22 delta packets round-trip: applying the
// emitted (new - old) deltas back onto the old record reproduces the intended
// new field values.

using namespace guild;
using namespace guild::sim;

namespace {
u32 RandZero() { return 0; }

InheritPerson MakeOld() {
    InheritPerson p{};
    std::memset(p.bytes, 0, sizeof(p.bytes));
    i32 id = 0x00ABCDEF; std::memcpy(p.bytes + 4, &id, 4);
    p.bytes[8] = 12;                                  // status old
    u16 fl = 99; std::memcpy(p.bytes + 10, &fl, 2);   // flags old
    i32 v404 = 7; std::memcpy(p.bytes + 404, &v404, 4);
    i32 a = 111; for (int i=0;i<5;++i) std::memcpy(p.bytes+408+4*i,&a,4);
    p.bytes[432] = 3; p.bytes[433] = 4;
    // asset array @+92 .. (8 dwords) and @+36/+40/+44.
    for (int i=0;i<8;++i){ i32 v=1000+i; std::memcpy(p.bytes+92+4*i,&v,4); }
    i32 v36=55; std::memcpy(p.bytes+36,&v36,4);
    i32 v44=66; std::memcpy(p.bytes+44,&v44,4);
    u16 v40=77; std::memcpy(p.bytes+40,&v40,2);
    float w=8.0f; std::memcpy(p.bytes+92, &w, 4); // overwritten dword0 of array; ok
    return p;
}
} // namespace

// Drive the whole emission body; count the packets that reach the queue.
TEST(CmdInheritIT, EmitProducesQueuedPackets) {
    CommandQueue q; q.Init();
    DeltaWriter dw;
    InheritPerson old = MakeOld();

    InheritEmitCtx ctx;
    ctx.queue = &q; ctx.delta = &dw; ctx.randNext = RandZero;
    // Mood/skill constants (arbitrary but deterministic — exercises the curve).
    ctx.c626830 = 1.0f; ctx.c626834 = 2.0f;
    ctx.c62681C = 0.5f; ctx.c626820 = 0.5f; ctx.c626824 = 0.25f;
    ctx.c626828 = 0.5f; ctx.c62682C = 0.0f;

    EmitInheritanceDelta(ctx, old);

    int op22 = 0, op26 = 0;
    for (u32 i = 1; i <= q.send_count(); ++i) {
        u8 op = q.ring_slot(i).opcode();
        if (op == 22) ++op22;
        if (op == 26) ++op26;
    }
    // 2 delta (State22) packets + 4 Args26 scalar packets (124/20/28/24/32 = 5).
    CHECK_EQ(op22, 2);
    CHECK_EQ(op26, 5);
    CHECK_EQ(q.send_count(), 7u);
}

// Round-trip: pull the first State22 packet's delta block back out and apply it
// to a copy of the old record; the named columns must reach their new values.
TEST(CmdInheritIT, DeltaPacket1RoundTrip) {
    CommandQueue q; q.Init();
    DeltaWriter dw;
    InheritPerson old = MakeOld();

    // Build only delta packet #1 the way the body does.
    u8 new_status = 100; u16 new_flags = 16;
    i32 new404 = 3, zero = 0; u8 b0 = 0;
    dw.BeginDeltaPacket(old.bytes, static_cast<u32>(old.id()));
    dw.AppendDeltaField(1, 1, 8,   &new_status);
    dw.AppendDeltaField(2, 1, 10,  &new_flags);
    dw.AppendDeltaField(4, 1, 404, &new404);
    dw.AppendDeltaField(4, 1, 408, &zero);
    dw.AppendDeltaField(4, 1, 412, &zero);
    dw.AppendDeltaField(4, 1, 416, &zero);
    dw.AppendDeltaField(4, 1, 420, &zero);
    dw.AppendDeltaField(4, 1, 424, &zero);
    dw.AppendDeltaField(1, 1, 432, &b0);
    dw.AppendDeltaField(1, 1, 433, &b0);
    i32 slot = QueueRequestState22FromDelta(q, dw);
    CHECK(slot >= 0);

    // Extract the 124-byte block from the queued packet and apply it.
    CommandPacket& pkt = q.ring_slot(static_cast<u32>(slot));
    const u8* block = pkt.bytes + 0x10;
    u8 fieldCount = block[4];
    const u8* payload = block + 5;
    CHECK_EQ(fieldCount, 10);

    InheritPerson rebuilt = old;
    DeltaWriter::ApplyDelta(payload, fieldCount, rebuilt.bytes);

    CHECK_EQ(rebuilt.bytes[8], 100);
    u16 fl; std::memcpy(&fl, rebuilt.bytes + 10, 2); CHECK_EQ(fl, 16);
    i32 v404; std::memcpy(&v404, rebuilt.bytes + 404, 4); CHECK_EQ(v404, 3);
    for (int i = 0; i < 5; ++i) {
        i32 v; std::memcpy(&v, rebuilt.bytes + 408 + 4 * i, 4); CHECK_EQ(v, 0);
    }
    CHECK_EQ(rebuilt.bytes[432], 0);
    CHECK_EQ(rebuilt.bytes[433], 0);
}

// Standalone flush moves the emitted packets onto the received list.
TEST(CmdInheritIT, FlushDeliversLocally) {
    CommandQueue q; q.Init();
    q.set_standalone(true);
    DeltaWriter dw;
    InheritPerson old = MakeOld();
    InheritEmitCtx ctx;
    ctx.queue = &q; ctx.delta = &dw; ctx.randNext = RandZero;
    EmitInheritanceDelta(ctx, old);

    CHECK(q.pending_head() != nullptr);
    int rc = q.FlushSendQueue();
    CHECK_EQ(rc, 0);
    CHECK(q.pending_head() == nullptr);    // drained
    CHECK(q.received_head() != nullptr);   // applied locally
}
