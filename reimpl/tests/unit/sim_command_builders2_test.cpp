#include "sim/command.h"
#include "sim/command_builders2.h"
#include "sim/command_pending.h"
#include "test.h"

#include <cstring>

using namespace guild;
using namespace guild::sim;

// ---------------------------------------------------------------------------
// command_builders2 — golden-vector tests for the VIBE_Command_Queue* request
// builders. Each builder stages a 153-byte packet, writes its opcode at +0 and
// its register-arg payload starting at +0x10, then calls EnqueuePacket, which
// stamps the header (len = ComputePacketSize, cmdId = ring slot, count = seq).
//
// Layout/opcode/size pinned 1:1 from the binary via IDA MCP (wave-22 W22-CMDQUEUE):
//   0x494848 Args26   op26 size 28  : a1@+0x10 a2@+0x14 a3@+0x18
//   0x494b74 Pair35   op35 size 24  : v4=a1@+0x10 v5=a2@+0x14            (mov bl,0x23)
//   0x494ca4 Pair42   op42 size 24  : v4=a1@+0x10 v5=a2@+0x14            (mov bl,0x2a)
//   0x494d68 Quad46   op46 size 32  : v6=a1@+0x10 v7=a2@+0x14 v8=a4@+0x18 v9=a3@+0x1C
//   0x495070 Quad54   op54 size 28  : v6=a1@+0x10 v7=a2@+0x14 v8=a4@+0x18; a3->ebp-4 off-wire
//   0x495124 Quad60   op60 size 32  : v6=a1@+0x10 v7=a2@+0x14 v8=a4@+0x18 v9=a3@+0x1C
//   0x494910 Buffer28 op28 size 20  : Person_FindRecordById guard -> stage 248+body block
// (ComputePacketSize @0x493034: op28->20, op35/op42->24, op46/op60->32, op54->28.)
// ---------------------------------------------------------------------------

namespace {

u32 g32(const CommandPacket& p, u32 off) { return p.get32(off); }

// The header EnqueuePacket always stamps for a fresh queue: the first packet
// lands at ring index 1 (= (sendCount+1)&0x7FFF with sendCount=0), with
// cmdId == count == ring index, flag == extra == 0.
void check_header(const CommandPacket& p, u8 op, u16 len, i32 ringIdx) {
    CHECK_EQ((int)p.opcode(), (int)op);
    CHECK_EQ((int)p.len(), (int)len);
    CHECK_EQ((int)p.flag(), 0);
    CHECK_EQ((int)p.cmd_id(), ringIdx);
    CHECK_EQ((int)p.count(), ringIdx);
    CHECK_EQ((int)p.extra(), 0);
}

} // namespace

TEST(SimCmdBuilders2, Args26ThreeDwords) {
    CommandQueue q; q.Init();
    i32 r = QueueRequestArgs26(q, 0x11111111, 0x22222222, 0x33333333);
    CHECK_EQ(r, 1);
    const CommandPacket& p = q.ring_slot(r);
    check_header(p, 26, 28, r);
    CHECK_EQ(g32(p, 0x10), 0x11111111u);
    CHECK_EQ(g32(p, 0x14), 0x22222222u);
    CHECK_EQ(g32(p, 0x18), 0x33333333u);
    // a3 is the last on-wire field; +0x1C must remain zero.
    CHECK_EQ(g32(p, 0x1C), 0u);
}

TEST(SimCmdBuilders2, Pair35AndPair42) {
    CommandQueue q; q.Init();

    i32 r35 = QueueRequestPair35(q, 0x0A0B0C0D, -1);
    {
        const CommandPacket& p = q.ring_slot(r35);
        check_header(p, 35, 24, r35);   // op 0x23 -> 24
        CHECK_EQ(g32(p, 0x10), 0x0A0B0C0Du);
        CHECK_EQ((i32)g32(p, 0x14), -1);
        CHECK_EQ(g32(p, 0x18), 0u);     // pair: no third field
    }

    i32 r42 = QueueRequestPair42(q, 7, 0x7FABCDEF);
    {
        const CommandPacket& p = q.ring_slot(r42);
        check_header(p, 42, 24, r42);   // op 0x2A -> 24
        CHECK_EQ((int)g32(p, 0x10), 7);
        CHECK_EQ(g32(p, 0x14), 0x7FABCDEFu);
        CHECK_EQ(g32(p, 0x18), 0u);
    }

    // Distinct ring slots in send order.
    CHECK_EQ(r35, 1);
    CHECK_EQ(r42, 2);
}

TEST(SimCmdBuilders2, Quad46AllFourOnWire) {
    CommandQueue q; q.Init();
    // a1,a2,a3,a4 -> +0x10=a1, +0x14=a2, +0x18=a4, +0x1C=a3.
    i32 r = QueueRequestQuad46(q, 0xA1, 0xA2, 0xA3 /*a3*/, 0xA4 /*a4*/);
    const CommandPacket& p = q.ring_slot(r);
    check_header(p, 46, 32, r);          // op 0x2E -> 32
    CHECK_EQ((int)g32(p, 0x10), 0xA1);
    CHECK_EQ((int)g32(p, 0x14), 0xA2);
    CHECK_EQ((int)g32(p, 0x18), 0xA4);   // a4 at +0x18
    CHECK_EQ((int)g32(p, 0x1C), 0xA3);   // a3 at +0x1C (last)
}

TEST(SimCmdBuilders2, Quad60AllFourOnWire) {
    CommandQueue q; q.Init();
    i32 r = QueueRequestQuad60(q, 0xB1, 0xB2, 0xB3 /*a3*/, 0xB4 /*a4*/);
    const CommandPacket& p = q.ring_slot(r);
    check_header(p, 60, 32, r);          // op 0x3C -> 32
    CHECK_EQ((int)g32(p, 0x10), 0xB1);
    CHECK_EQ((int)g32(p, 0x14), 0xB2);
    CHECK_EQ((int)g32(p, 0x18), 0xB4);   // a4 at +0x18
    CHECK_EQ((int)g32(p, 0x1C), 0xB3);   // a3 at +0x1C
}

TEST(SimCmdBuilders2, Quad54DropsThirdArg) {
    CommandQueue q; q.Init();
    // op54 stores a3 into ebp-4 OUTSIDE the packet -> it must NOT reach the wire.
    i32 r = QueueRequestQuad54(q, 0xC1, 0xC2, 0xDEADBEEF /*a3 dropped*/, 0xC4 /*a4*/);
    const CommandPacket& p = q.ring_slot(r);
    check_header(p, 54, 28, r);          // op 0x36 -> 28
    CHECK_EQ((int)g32(p, 0x10), 0xC1);   // a1
    CHECK_EQ((int)g32(p, 0x14), 0xC2);   // a2
    CHECK_EQ((int)g32(p, 0x18), 0xC4);   // a4 (NOT a3)
    // a3 (0xDEADBEEF) is off-wire; +0x1C stays zero.
    CHECK_EQ(g32(p, 0x1C), 0u);
}

// Differential: the only wire-level difference between the Quad3 family (54) and
// the Quad4 family (46/60) is whether a3 lands at +0x1C. Pin that contrast.
TEST(SimCmdBuilders2, QuadFamilyA3Contrast) {
    CommandQueue q; q.Init();
    i32 r54 = QueueRequestQuad54(q, 1, 2, 0x33, 4);
    i32 r46 = QueueRequestQuad46(q, 1, 2, 0x33, 4);
    CHECK_EQ(g32(q.ring_slot(r54), 0x1C), 0u);       // dropped
    CHECK_EQ((int)g32(q.ring_slot(r46), 0x1C), 0x33); // on wire
}

TEST(SimCmdBuilders2, Buffer28PersonGuardEarlyOut) {
    CommandQueue q; q.Init();
    PendingState pending;
    u8 header[0xF8]; std::memset(header, 0xAB, sizeof(header));
    u8 body[16];     std::memset(body, 0xCD, sizeof(body));
    // personFound == false reproduces !Person_FindRecordById(header->id): return -1,
    // nothing staged, nothing enqueued.
    i32 r = QueueRequestBuffer28(q, pending, header, body, 16, /*personFound*/ false);
    CHECK_EQ(r, -1);
    CHECK_EQ((int)pending.staged, 0);
    CHECK_EQ((int)q.send_count(), 0);   // no packet enqueued
}

TEST(SimCmdBuilders2, Buffer28StagesHeaderPlusBodyAndEnqueues) {
    CommandQueue q; q.Init();
    PendingState pending;
    u8 header[0xF8];
    for (int i = 0; i < 0xF8; ++i) header[i] = (u8)(i & 0xFF);
    u8 body[40];
    for (int i = 0; i < 40; ++i) body[i] = (u8)(0xF0 ^ i);

    i32 r = QueueRequestBuffer28(q, pending, header, body, 40, /*personFound*/ true);
    CHECK(r >= 1);

    // The header packet carries opcode 28 with wire size 20.
    const CommandPacket& p = q.ring_slot(r);
    CHECK_EQ((int)p.opcode(), 28);
    CHECK_EQ((int)p.len(), 20);

    // StagePendingBlock(bodyLen+248): the staging block holds the 248-byte header
    // struct immediately followed by the 40 body bytes (length word at block[0..1]
    // == 288, staged == 290). GeneratePendingPackets (run inside EmitWithPendingBlock)
    // consumes the block, so after enqueue `staged` is cleared back to 0.
    CHECK_EQ((int)pending.staged, 0);
    // The header packet's +12 link points at the first generated fragment's Count
    // (non-zero when a block was staged).
    CHECK(p.get32(0x0C) != 0);
}
