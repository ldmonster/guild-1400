#include "sim/command.h"
#include "sim/command_builders.h"
#include "sim/he.h"
#include "test.h"

#include <cstring>

using namespace guild;
using namespace guild::sim;

// ---------------------------------------------------------------------------
// command_builders — golden-vector tests for the VIBE_Command_Queue* / Request*
// builders in command_builders.cpp (opcodes 29/33/49/53/56/57/73/88/91/93).
// Layouts pinned 1:1 from the binary via IDA MCP:
//   0x4949c4 Entity29     : id@+0x10, dwords@+0x14/+0x18/+0x1C, word@+0x20,
//                           appt@+0x22/+0x26/+0x2A, word@+0x2E, dwords@+0x30/
//                           +0x34/+0x38, word@+0x3C, a1@+0x3E, NUL@+0x3F.
//   0x494b04 Pair33       : a1@+0x10, a2@+0x14.
//   0x494e4c Single49     : a1@+0x10.
//   0x495098 Quad56       : a1@+0x10, a2@+0x14, a4@+0x18 (a3 off-wire).
//   0x495100 Pair57       : a2@+0x10, a1@+0x14 (SWAPPED).
//   0x495554 Op73Str      : a1@+0x14, a2@+0x17, a4@+0x1B, a3@+0x1F, a5@+0x20,
//                           a6@+0x21, name@+0x27 (HE_NULL 2-byte-stride copy).
//   0x494f0c NamedObject53: a1@+0x10, a2@+0x14, a4@+0x18, a5@+0x1C,
//                           obj@+0x1D (stride copy), name@+0x3D (StrNCopyPad,31).
//   0x495ae0/95b7c/95bd0 Op88/91/93: a1@+0x10 [+ a2@+0x14, a4@+0x18; a3 off-wire].
// ---------------------------------------------------------------------------

namespace {

u32 g32(const CommandPacket& p, u32 off) { return p.get32(off); }
u16 g16(const CommandPacket& p, u32 off) { return p.get16(off); }

} // namespace

TEST(SimCmdBuilders, Entity29SnapshotsRecordSlice) {
    CommandQueue q; q.Init();

    // Raw record bytes: write distinct dwords/words at the source offsets the
    // builder reads (4,68,72,76,80, 82,86,90,94, 96,100,104,108).
    u8 rec[256]; std::memset(rec, 0, sizeof(rec));
    auto wr32 = [&](u32 off, u32 v) { std::memcpy(rec + off, &v, 4); };
    auto wr16 = [&](u32 off, u16 v) { std::memcpy(rec + off, &v, 2); };
    wr32(4,   0x11111111);  // id
    wr32(68,  0x22222222);
    wr32(72,  0x33333333);
    wr32(76,  0x44444444);
    wr16(80,  0x5555);
    wr32(82,  0x66666666);
    wr32(86,  0x77777777);
    wr32(90,  0x88888888);
    wr16(94,  0x9999);
    wr32(96,  0xAAAAAAAA);
    wr32(100, 0xBBBBBBBB);
    wr32(104, 0xCCCCCCCC);
    wr16(108, 0xDDDD);

    HeRecord* h = reinterpret_cast<HeRecord*>(rec);
    i32 r = QueueRequestEntity29(q, (i8)2, h);
    CHECK_EQ(r, 1);
    const CommandPacket& p = q.ring_slot(r);
    CHECK_EQ((int)p.opcode(), 29);
    CHECK_EQ(g32(p, 0x10), 0x11111111u);
    CHECK_EQ(g32(p, 0x14), 0x22222222u);
    CHECK_EQ(g32(p, 0x18), 0x33333333u);
    CHECK_EQ(g32(p, 0x1C), 0x44444444u);
    CHECK_EQ((int)g16(p, 0x20), 0x5555);
    CHECK_EQ(g32(p, 0x22), 0x66666666u);
    CHECK_EQ(g32(p, 0x26), 0x77777777u);
    CHECK_EQ(g32(p, 0x2A), 0x88888888u);
    CHECK_EQ((int)g16(p, 0x2E), 0x9999);
    CHECK_EQ(g32(p, 0x30), 0xAAAAAAAAu);
    CHECK_EQ(g32(p, 0x34), 0xBBBBBBBBu);
    CHECK_EQ(g32(p, 0x38), 0xCCCCCCCCu);
    CHECK_EQ((int)g16(p, 0x3C), 0xDDDD);
    CHECK_EQ((int)p.bytes[0x3E], 2);    // a1
    CHECK_EQ((int)p.bytes[0x3F], 0);    // HE_NULL terminator
}

TEST(SimCmdBuilders, Pair33AndSingle49) {
    CommandQueue q; q.Init();

    i32 r33 = QueueRequestPair33(q, 0x0A0B0C0D, -2);
    {
        const CommandPacket& p = q.ring_slot(r33);
        CHECK_EQ((int)p.opcode(), 33);
        CHECK_EQ(g32(p, 0x10), 0x0A0B0C0Du);
        CHECK_EQ((i32)g32(p, 0x14), -2);
    }
    i32 r49 = QueueRequestSingle49(q, 0x7EADBEEF);
    {
        const CommandPacket& p = q.ring_slot(r49);
        CHECK_EQ((int)p.opcode(), 49);
        CHECK_EQ(g32(p, 0x10), 0x7EADBEEFu);
        CHECK_EQ(g32(p, 0x14), 0u);
    }
    CHECK_EQ(r33, 1);
    CHECK_EQ(r49, 2);
}

TEST(SimCmdBuilders, Quad56DropsThirdArg) {
    CommandQueue q; q.Init();
    i32 r = QueueRequestQuad56(q, 0xC1, 0xC2, 0xDEADBEEF /*a3 dropped*/, 0xC4);
    const CommandPacket& p = q.ring_slot(r);
    CHECK_EQ((int)p.opcode(), 56);
    CHECK_EQ((int)g32(p, 0x10), 0xC1);
    CHECK_EQ((int)g32(p, 0x14), 0xC2);
    CHECK_EQ((int)g32(p, 0x18), 0xC4);   // a4, NOT a3
    CHECK_EQ(g32(p, 0x1C), 0u);          // a3 off-wire
}

TEST(SimCmdBuilders, Pair57SwapsArgs) {
    CommandQueue q; q.Init();
    // Original: v4=a2@+0x10, v5=a1@+0x14.
    i32 r = QueueRequestPair57(q, 0xAAAA0001, 0xBBBB0002);
    const CommandPacket& p = q.ring_slot(r);
    CHECK_EQ((int)p.opcode(), 57);
    CHECK_EQ(g32(p, 0x10), 0xBBBB0002u);  // a2
    CHECK_EQ(g32(p, 0x14), 0xAAAA0001u);  // a1
}

TEST(SimCmdBuilders, Op73StrLayout) {
    CommandQueue q; q.Init();
    i32 r = RequestBuildOp73Str(q, (i8)0x12, 0x33445566, (i8)0x78,
                                0x0A0B0C0D, (i8)0x5A, (i16)0x6B7C, "Pest");
    const CommandPacket& p = q.ring_slot(r);
    CHECK_EQ((int)p.opcode(), 73);
    CHECK_EQ((int)p.bytes[0x14], 0x12);          // a1 byte
    CHECK_EQ(g32(p, 0x17), 0x33445566u);         // a2
    CHECK_EQ(g32(p, 0x1B), 0x0A0B0C0Du);         // a4
    CHECK_EQ((int)p.bytes[0x1F], 0x78);          // a3 byte
    CHECK_EQ((int)p.bytes[0x20], 0x5A);          // a5 byte
    CHECK_EQ((int)g16(p, 0x21), 0x6B7C);         // a6 word
    CHECK_EQ(std::memcmp(p.bytes + 0x27, "Pest", 5), 0);
}

TEST(SimCmdBuilders, NamedObject53Layout) {
    CommandQueue q; q.Init();
    i32 r = QueueRequestNamedObject53(q, 0x1001, 0x2002, "Obj", 0x3003,
                                      (i8)0x44, "Name", /*personStamped*/ true);
    const CommandPacket& p = q.ring_slot(r);
    CHECK_EQ((int)p.opcode(), 53);
    CHECK_EQ(g32(p, 0x10), 0x1001u);             // a1
    CHECK_EQ(g32(p, 0x14), 0x2002u);             // a2
    CHECK_EQ(g32(p, 0x18), 0x3003u);             // a4
    CHECK_EQ((int)p.bytes[0x1C], 0x44);          // a5 byte
    CHECK_EQ(std::memcmp(p.bytes + 0x1D, "Obj", 4), 0);   // obj @+0x1D
    CHECK_EQ(std::memcmp(p.bytes + 0x3D, "Name", 5), 0);  // name @+0x3D
}

TEST(SimCmdBuilders, NamedObject53NotStampedEarlyOut) {
    CommandQueue q; q.Init();
    // FindRecordById(a1)->+8 == 0 => -1, nothing enqueued.
    i32 r = QueueRequestNamedObject53(q, 0x1001, 0x2002, "Obj", 0x3003,
                                      (i8)0x44, "Name", /*personStamped*/ false);
    CHECK_EQ(r, -1);
    CHECK_EQ((int)q.send_count(), 0);
}

TEST(SimCmdBuilders, Op88_91_93) {
    CommandQueue q; q.Init();

    i32 r88 = RequestBuildOp88(q, 0x1234);
    {
        const CommandPacket& p = q.ring_slot(r88);
        CHECK_EQ((int)p.opcode(), 88);
        CHECK_EQ((int)g32(p, 0x10), 0x1234);
    }
    i32 r91 = RequestBuildOp91(q, 0x91, 0x92, 0xDEAD /*a3 off-wire*/, 0x94);
    {
        const CommandPacket& p = q.ring_slot(r91);
        CHECK_EQ((int)p.opcode(), 91);
        CHECK_EQ((int)g32(p, 0x10), 0x91);
        CHECK_EQ((int)g32(p, 0x14), 0x92);
        CHECK_EQ((int)g32(p, 0x18), 0x94);   // a4
        CHECK_EQ(g32(p, 0x1C), 0u);          // a3 off-wire
    }
    i32 r93 = RequestBuildOp93(q, 0x93A, 0x93B, 0xBEEF /*a3 off-wire*/, 0x93D);
    {
        const CommandPacket& p = q.ring_slot(r93);
        CHECK_EQ((int)p.opcode(), 93);
        CHECK_EQ((int)g32(p, 0x10), 0x93A);
        CHECK_EQ((int)g32(p, 0x14), 0x93B);
        CHECK_EQ((int)g32(p, 0x18), 0x93D);  // a4
        CHECK_EQ(g32(p, 0x1C), 0u);          // a3 off-wire
    }
}
