#include "sim/command.h"
#include "sim/command_builders3.h"
#include "test.h"

#include <cstring>

using namespace guild;
using namespace guild::sim;

// ---------------------------------------------------------------------------
// command_builders3 — golden-vector tests for the RequestBuildOp* packet
// builders (opcodes 65..95 + 76/38/89). Each builder stages a 153-byte packet,
// writes its opcode at +0 and payload from +0x10, and enqueues it; EnqueuePacket
// stamps the header (len=ComputePacketSize, cmdId=ringIdx, count=seq). We assert
// the exact bytes the original would place on the wire.
// ---------------------------------------------------------------------------

namespace {

// Enqueue one builder result, return the ring slot it landed in. The first
// packet always lands at ring index 1 (= (sendCount+1)&0x7FFF with sendCount=0).
CommandPacket& slot_of(CommandQueue& q, i32 idx) { return q.ring_slot(idx); }

// Read a little-endian dword from a packet at offset.
u32 g32(const CommandPacket& p, u32 off) { return p.get32(off); }
u16 g16(const CommandPacket& p, u32 off) { return p.get16(off); }

// Verify the header fields EnqueuePacket always stamps for the first packet.
void check_header(const CommandPacket& p, u8 op, u16 len, i32 ringIdx) {
    CHECK_EQ(p.opcode(), op);
    CHECK_EQ(p.len(), len);
    CHECK_EQ((int)p.flag(), 0);
    CHECK_EQ((int)p.cmd_id(), ringIdx);
    // For a fresh queue, ring index == sequence Count (both follow send order).
    CHECK_EQ((int)p.count(), ringIdx);
    CHECK_EQ((int)p.extra(), 0);
}

} // namespace

TEST(SimCmdBuilders3, ScalarBuildersHeaderAndPayload) {
    CommandQueue q; q.Init();

    i32 r66 = RequestBuildOp66(q, 0x11223344);
    CHECK_EQ(r66, 1);
    {
        const CommandPacket& p = slot_of(q, r66);
        check_header(p, 66, 20, r66);
        CHECK_EQ(g32(p, 0x10), 0x11223344u);
    }

    i32 r67 = RequestBuildOp67(q, -7);
    {
        const CommandPacket& p = slot_of(q, r67);
        check_header(p, 67, 20, r67);
        CHECK_EQ((i32)g32(p, 0x10), -7);
    }

    i32 r74 = RequestBuildOp74(q, 0x0BADF00D);
    {
        const CommandPacket& p = slot_of(q, r74);
        check_header(p, 74, 20, r74);
        CHECK_EQ(g32(p, 0x10), 0x0BADF00Du);
    }

    i32 r87 = RequestBuildOp87(q, 999);
    {
        const CommandPacket& p = slot_of(q, r87);
        check_header(p, 87, 20, r87);
        CHECK_EQ((int)g32(p, 0x10), 999);
    }
}

TEST(SimCmdBuilders3, Op71QuadDropsThirdArg) {
    CommandQueue q; q.Init();
    i32 r = RequestBuildOp71(q, 10, 20, 30 /*a3 dropped*/, 40);
    const CommandPacket& p = slot_of(q, r);
    check_header(p, 71, 28, r);
    CHECK_EQ((int)g32(p, 0x10), 10);
    CHECK_EQ((int)g32(p, 0x14), 20);
    CHECK_EQ((int)g32(p, 0x18), 40);   // a4, NOT a3
    // a3 (30) must not appear in the payload region.
    CHECK_EQ((int)g32(p, 0x1C), 0);
}

TEST(SimCmdBuilders3, Op72DwordThenByte) {
    CommandQueue q; q.Init();
    i32 r = RequestBuildOp72(q, 0x01020304, (i8)0x5A);
    const CommandPacket& p = slot_of(q, r);
    check_header(p, 72, 21, r);
    CHECK_EQ(g32(p, 0x10), 0x01020304u);
    CHECK_EQ((int)p.bytes[0x14], 0x5A);
}

TEST(SimCmdBuilders3, Op83FiveDwords) {
    CommandQueue q; q.Init();
    i32 src[5] = { 1, 2, 3, 4, 5 };
    i32 r = RequestBuildOp83(q, src);
    const CommandPacket& p = slot_of(q, r);
    check_header(p, 83, 36, r);
    for (int i = 0; i < 5; ++i)
        CHECK_EQ((int)g32(p, 0x10 + 4 * i), i + 1);
}

TEST(SimCmdBuilders3, Op84ThreeDwords) {
    CommandQueue q; q.Init();
    i32 src[3] = { 0x10, 0x20, 0x30 };
    i32 r = RequestBuildOp84(q, src);
    const CommandPacket& p = slot_of(q, r);
    check_header(p, 84, 28, r);
    CHECK_EQ((int)g32(p, 0x10), 0x10);
    CHECK_EQ((int)g32(p, 0x14), 0x20);
    CHECK_EQ((int)g32(p, 0x18), 0x30);
}

TEST(SimCmdBuilders3, Op92TwoDwordsThenWord) {
    CommandQueue q; q.Init();
    // src is a byte buffer: dword@0, dword@4, word@8.
    u8 src[12] = {0};
    std::memcpy(src + 0, (const u8[]){0x44,0x33,0x22,0x11}, 4); // 0x11223344
    std::memcpy(src + 4, (const u8[]){0x78,0x56,0x34,0x12}, 4); // 0x12345678
    src[8] = 0xCD; src[9] = 0xAB;                               // 0xABCD
    i32 r = RequestBuildOp92(q, reinterpret_cast<const i32*>(src));
    const CommandPacket& p = slot_of(q, r);
    check_header(p, 92, 26, r);
    CHECK_EQ(g32(p, 0x10), 0x11223344u);
    CHECK_EQ(g32(p, 0x14), 0x12345678u);
    CHECK_EQ((int)g16(p, 0x18), 0xABCD);
}

TEST(SimCmdBuilders3, Op95SrcAndRecordId) {
    CommandQueue q; q.Init();
    i32 src[1] = { 0x0A0B0C0D };
    u8  rec[8] = { 0, 0, 0, 0, 0x99, 0x88, 0x77, 0x66 }; // *(rec+4) = 0x66778899
    i32 r = RequestBuildOp95(q, src, rec);
    const CommandPacket& p = slot_of(q, r);
    // opcode 95 has no entry in ComputePacketSize -> default size 145.
    check_header(p, 95, 145, r);
    CHECK_EQ(g32(p, 0x10), 0x0A0B0C0Du);
    CHECK_EQ(g32(p, 0x14), 0x66778899u);
}

TEST(SimCmdBuilders3, Op94HeaderWidth20) {
    CommandQueue q; q.Init();
    // base+1 must yield a dword; blob is 28 bytes.
    u8 base[5] = { 0xFF, 0x21, 0x43, 0x65, 0x87 }; // *(base+1) = 0x87654321
    u8 blob[28];
    for (int i = 0; i < 28; ++i) blob[i] = (u8)(0xC0 + i);
    i32 r = RequestBuildOp94(q, base, blob);
    const CommandPacket& p = slot_of(q, r);
    check_header(p, 94, 52, r);
    CHECK_EQ(g32(p, 0x14), 0x87654321u);
    for (int i = 0; i < 28; ++i)
        CHECK_EQ((int)p.bytes[0x18 + i], (int)(u8)(0xC0 + i));
}

TEST(SimCmdBuilders3, BlobBuilders65_68_69_70_75_86) {
    CommandQueue q; q.Init();

    // op65: 56 bytes @+0x10, 2 bytes @+0x48 (from src[56..57]).
    {
        u8 src[58];
        for (int i = 0; i < 58; ++i) src[i] = (u8)(i + 1);
        i32 r = RequestBuildOp65Blob(q, src);
        const CommandPacket& p = slot_of(q, r);
        check_header(p, 65, 74, r);
        for (int i = 0; i < 56; ++i) CHECK_EQ((int)p.bytes[0x10 + i], i + 1);
        CHECK_EQ((int)p.bytes[0x48], 57);
        CHECK_EQ((int)p.bytes[0x49], 58);
    }
    // op68: 4 bytes @+0x10, 3 bytes @+0x14.
    {
        u8 src[7] = { 1, 2, 3, 4, 5, 6, 7 };
        i32 r = RequestBuildOp68Blob(q, src);
        const CommandPacket& p = slot_of(q, r);
        check_header(p, 68, 23, r);
        for (int i = 0; i < 7; ++i) CHECK_EQ((int)p.bytes[0x10 + i], i + 1);
    }
    // op69: 12 bytes @+0x10, 3 bytes @+0x1C.
    {
        u8 src[15];
        for (int i = 0; i < 15; ++i) src[i] = (u8)(0x40 + i);
        i32 r = RequestBuildOp69Blob(q, src);
        const CommandPacket& p = slot_of(q, r);
        check_header(p, 69, 31, r);
        for (int i = 0; i < 12; ++i) CHECK_EQ((int)p.bytes[0x10 + i], 0x40 + i);
        for (int i = 0; i < 3; ++i)  CHECK_EQ((int)p.bytes[0x1C + i], 0x40 + 12 + i);
    }
    // op70: 8 bytes @+0x10, 1 byte @+0x18.
    {
        u8 src[9];
        for (int i = 0; i < 9; ++i) src[i] = (u8)(0x80 + i);
        i32 r = RequestBuildOp70Blob(q, src);
        const CommandPacket& p = slot_of(q, r);
        check_header(p, 70, 25, r);
        for (int i = 0; i < 8; ++i) CHECK_EQ((int)p.bytes[0x10 + i], 0x80 + i);
        CHECK_EQ((int)p.bytes[0x18], 0x80 + 8);
    }
    // op75: 128 bytes @+0x10. Note ring stride is 153 so +0x10+128 = 0x90 < 153.
    {
        u8 src[128];
        for (int i = 0; i < 128; ++i) src[i] = (u8)(i ^ 0x5A);
        i32 r = RequestBuildOp75Blob(q, src);
        const CommandPacket& p = slot_of(q, r);
        check_header(p, 75, 144, r);
        for (int i = 0; i < 128; ++i) CHECK_EQ((int)p.bytes[0x10 + i], (int)(u8)(i ^ 0x5A));
    }
    // op86: 40 bytes @+0x10.
    {
        u8 src[40];
        for (int i = 0; i < 40; ++i) src[i] = (u8)(0x30 + i);
        i32 r = RequestBuildOp86Blob(q, src);
        const CommandPacket& p = slot_of(q, r);
        check_header(p, 86, 56, r);
        for (int i = 0; i < 40; ++i) CHECK_EQ((int)p.bytes[0x10 + i], 0x30 + i);
    }
}

TEST(SimCmdBuilders3, CreateGebaeudeLayout) {
    CommandQueue q; q.Init();
    float pos[4] = { 1.0f, 2.0f, 3.0f, 4.0f };
    u8 meta[12]; for (int i = 0; i < 12; ++i) meta[i] = (u8)(0x70 + i);
    i32 r = RequestCreateGebaeude(q, 0x0BADCAFE, (i8)0x33, meta, pos);
    const CommandPacket& p = slot_of(q, r);
    check_header(p, 76, 53, r);
    CHECK_EQ(g32(p, 0x10), 0x0BADCAFEu);
    CHECK_EQ((int)p.bytes[0x18], 0x33);
    // pos (16 bytes) @ +0x19
    float fpos[4];
    std::memcpy(fpos, p.bytes + 0x19, 16);
    CHECK_EQ(fpos[0], 1.0f); CHECK_EQ(fpos[1], 2.0f);
    CHECK_EQ(fpos[2], 3.0f); CHECK_EQ(fpos[3], 4.0f);
    // meta (12 bytes) @ +0x29
    for (int i = 0; i < 12; ++i) CHECK_EQ((int)p.bytes[0x29 + i], 0x70 + i);
}

TEST(SimCmdBuilders3, SendCutInfoAndCutsceneReady) {
    CommandQueue q; q.Init();

    i32 r38 = RequestSendCutInfo(q, 7, 0x12345, /*gameTick*/ 42);
    {
        const CommandPacket& p = slot_of(q, r38);
        check_header(p, 38, 24, r38);
        CHECK_EQ((int)g32(p, 0x10), 7);
        CHECK_EQ((int)g32(p, 0x14), 0x12345);
    }

    u8 src[68];
    for (int i = 0; i < 68; ++i) src[i] = (u8)(i + 100);
    i32 r89 = RequestCutsceneReady(q, src);
    {
        const CommandPacket& p = slot_of(q, r89);
        // opcode 89 has no entry -> default size 145.
        check_header(p, 89, 145, r89);
        for (int i = 0; i < 68; ++i) CHECK_EQ((int)p.bytes[0x10 + i], (int)(u8)(i + 100));
    }
}
