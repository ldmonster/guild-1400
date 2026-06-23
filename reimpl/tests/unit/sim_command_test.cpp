#include "sim/command.h"
#include "sim/command_codec.h"
#include "test.h"

#include <cstddef>
#include <cstring>

using namespace guild;
using namespace guild::sim;

// ---------------------------------------------------------------------------
// Golden opcode -> wire-size table, recovered byte-for-byte from
// VIBE_Command_ComputePacketSize @0x493034. Variable opcodes (0x16,0x17,0x18)
// and the sync opcode (0x20) depend on payload, so their entries are 0 here and
// are exercised separately below.
// ---------------------------------------------------------------------------
static const u16 kGoldenFixed[96] = {
    145, 145, 145, 20, 17, 145, 145, 145, 20, 145, 80, 42,
    73, 24, 145, 33, 145, 39, 30, 28, 22, 57, 0, 0,
    0, 36, 28, 40, 20, 95, 30, 145, 0, 24, 61, 24,
    24, 28, 24, 24, 20, 24, 24, 28, 33, 31, 32, 60,
    60, 20, 48, 24, 28, 93, 28, 69, 28, 24, 24, 20,
    32, 39, 52, 145, 47, 74, 20, 20, 23, 31, 25, 28,
    21, 55, 20, 144, 53, 20, 64, 52, 68, 68, 28, 36,
    28, 97, 56, 20, 20, 145, 24, 28, 26, 28, 52, 145,
};

// ---------------------------------------------------------------------------
// Packet GEOMETRY pin (wave-13 1:1 audit). The Command lockstep codec's wire
// layout is determinism-critical: every constant below is a recovered 1:1 value
// from command.h's provenance (the qmemcpy(...,0x99) stride in EnqueuePacket /
// StoreReceivedPacket, the (seq & 0x7FFF) ring index, the 10-byte ACK entry
// stride byte_B5FB60, the 0x77 Append* cursor guard, the 96-entry jump table
// funcs_4941F4 @0x631298, the +0x91/+0x95 intrusive links, and the 0x20/14 sync
// discriminator). Pinning them at runtime guards against a silent drift of the
// header constants away from their documented binary origins.
// ---------------------------------------------------------------------------
TEST(SimCommand, PacketGeometryConstantsGolden) {
    // Fixed wire/staging record stride — qmemcpy(...,0x99).
    CHECK_EQ(kPacketStride, (u32)153);
    CHECK_EQ(kPacketStride, (u32)0x99);
    CHECK_EQ(sizeof(CommandPacket), (std::size_t)153);

    // Send-ring capacity and sequence mask (ring index = seq & 0x7FFF).
    CHECK_EQ(kSendRingSlots, (u32)0x8000);
    CHECK_EQ(kSendRingSlots, (u32)32768);
    CHECK_EQ(kSeqMask, (u32)0x7FFF);

    // ACK/status table: 10-byte entries, 32768 of them (byte_B5FB60).
    CHECK_EQ(kAckEntryBytes, (u32)10);
    CHECK_EQ(sizeof(AckEntry), (std::size_t)10);
    CHECK_EQ(kAckTableBytes, (u32)327680);
    CHECK_EQ(kAckTableBytes, kAckEntryBytes * kSendRingSlots);

    // Append* payload cursor guard (0x77 == 119).
    CHECK_EQ(kMaxPayload, (u32)0x77);
    CHECK_EQ(kMaxPayload, (u32)119);

    // Dispatch jump-table slot count (funcs_4941F4 @0x631298 has 96 entries).
    CHECK_EQ(kNumOpcodes, (u32)96);

    // Header field offsets (EnqueuePacket store sites).
    CHECK_EQ((u32)kFOpcode, (u32)0x00);
    CHECK_EQ((u32)kFLen,    (u32)0x01);
    CHECK_EQ((u32)kFFlag,   (u32)0x03);
    CHECK_EQ((u32)kFCmdId,  (u32)0x04);
    CHECK_EQ((u32)kFCount,  (u32)0x08);
    CHECK_EQ((u32)kFExtra,  (u32)0x0C);
    CHECK_EQ((u32)kFSync,   (u32)0x10);

    // Intrusive doubly-linked-list link fields (+0x91 / +0x95).
    CHECK_EQ((u32)kLPrev, (u32)145);
    CHECK_EQ((u32)kLPrev, (u32)0x91);
    CHECK_EQ((u32)kLNext, (u32)149);
    CHECK_EQ((u32)kLNext, (u32)0x95);

    // Sync packet discriminator: opcode 0x20 && byte[+16] == 14.
    CHECK_EQ((u32)kSyncType,   (u32)0x20);
    CHECK_EQ((u32)kSyncMarker, (u32)14);
}

// AckEntry field layout + the three documented status codes (0 pending /
// 1 free-init / 2 applied-acked). Verifies the +0/+1/+2/+6 byte placement that
// EnqueuePacket / ExecCommands / GetPacketSeqById read and write.
TEST(SimCommand, AckEntryFieldLayoutGolden) {
    AckEntry e{};
    e.status = 2;
    e.slot   = 0x11;
    e.ring   = -1;
    e.seq    = 0x44332211;
    const u8* raw = reinterpret_cast<const u8*>(&e);
    CHECK_EQ(raw[0], (u8)2);        // +0 status
    CHECK_EQ(raw[1], (u8)0x11);     // +1 slot
    // +2 ring (i32, little-endian) == -1
    i32 ring;
    std::memcpy(&ring, raw + 2, sizeof(ring));
    CHECK_EQ(ring, (i32)-1);
    // +6 seq (i32, little-endian)
    i32 seq;
    std::memcpy(&seq, raw + 6, sizeof(seq));
    CHECK_EQ(seq, (i32)0x44332211);
}

TEST(SimCommand, OpcodeSizeTableGolden) {
    for (int op = 0; op < 96; ++op) {
        if (op == 0x16 || op == 0x17 || op == 0x18 || op == 0x20)
            continue; // variable / sync — covered separately
        CommandPacket p{};
        p.opcode() = static_cast<u8>(op);
        CHECK_EQ(ComputePacketSize(p), kGoldenFixed[op]);
        CHECK_EQ(ComputePacketSizeFixed(static_cast<u8>(op)), kGoldenFixed[op]);
    }
}

TEST(SimCommand, OpcodeSizeSyncShortAndLong) {
    CommandPacket lng{};
    lng.opcode() = 0x20;
    lng.bytes[16] = 0; // not the short marker
    CHECK_EQ(ComputePacketSize(lng), (u16)141);

    CommandPacket shrt{};
    shrt.opcode() = 0x20;
    shrt.bytes[16] = 14; // short sync variant
    CHECK_EQ(ComputePacketSize(shrt), (u16)17);
}

TEST(SimCommand, OpcodeSizeVariableFieldBatch) {
    // Opcode 0x16/0x17: count byte at +20, then per-field stride*count+4.
    // Build two fields: (width=2,count=3) -> 2*3+4=10; (width=4,count=1) -> 8.
    // Total = 21 + 10 + 8 = 39.
    CommandPacket p{};
    p.opcode() = 0x16;
    p.bytes[20] = 2;         // field count
    p.bytes[21] = 2;         // field0 width  (v3[0])
    p.bytes[22] = 3;         // field0 count  (v3[1])
    // field1 starts at 21 + (2*3+4)=10 -> offset 31
    p.bytes[31] = 4;         // field1 width
    p.bytes[32] = 1;         // field1 count
    CHECK_EQ(ComputePacketSize(p), (u16)39);

    // Opcode 0x18: 5 * count + 21.
    CommandPacket q{};
    q.opcode() = 0x18;
    q.bytes[20] = 4;
    CHECK_EQ(ComputePacketSize(q), (u16)(5 * 4 + 21));
}

// ---------------------------------------------------------------------------
// Header encode -> decode roundtrip, every field bit-exact. EnqueuePacket is the
// canonical header writer; we enqueue a staged packet and read the ring slot.
// ---------------------------------------------------------------------------
TEST(SimCommand, HeaderEncodeDecodeRoundtrip) {
    CommandQueue q;
    q.set_standalone(false); // keep packets in the ring (don't apply locally)

    CommandPacket staged{};
    staged.opcode() = 0x11; // fixed size 39
    // payload junk that must survive the copy
    for (int i = 16; i < 39; ++i) staged.bytes[i] = static_cast<u8>(i * 7 + 1);

    i32 ring = q.EnqueuePacket(staged);
    CHECK_EQ(ring, (i32)1); // first enqueue lands at slot (0+1)&0x7FFF = 1

    CommandPacket& slot = q.ring_slot(static_cast<u32>(ring));
    CHECK_EQ(slot.opcode(), (u8)0x11);
    CHECK_EQ(slot.len(), (u16)39);            // ComputePacketSize stamped
    CHECK_EQ(slot.flag(), (u8)0);             // +3 cleared
    CHECK_EQ(slot.extra(), (u32)0);           // +12 cleared
    CHECK_EQ(slot.cmd_id(), (u32)1);          // +4 = ring slot index
    CHECK_EQ(slot.count(), (u32)1);           // +8 = sequence Count
    // payload preserved byte-exact
    for (int i = 16; i < 39; ++i)
        CHECK_EQ(slot.bytes[i], static_cast<u8>(i * 7 + 1));

    // ACK entry seeded.
    CHECK_EQ(q.GetPacketStatusById(static_cast<u32>(ring)), 0); // pending
    CHECK_EQ(q.ack(static_cast<u32>(ring)).ring, ring);
}

TEST(SimCommand, SyncPacketUpdatesLastSyncCount) {
    CommandQueue q;
    q.set_standalone(false);
    CommandPacket sync{};
    sync.opcode() = kSyncType;     // 0x20
    sync.bytes[16] = kSyncMarker;  // 14
    i32 ring = q.EnqueuePacket(sync);
    CHECK_EQ(q.ring_slot(ring).len(), (u16)17); // short sync
    CHECK_EQ(q.last_sync_count(), q.send_count());
}

// ---------------------------------------------------------------------------
// Send-ring sequence wraparound: seq & 0x7FFF over >32768 pushes. We must see
// the ring slot index wrap from 0x7FFF back to 0 while send_count keeps rising.
// ---------------------------------------------------------------------------
TEST(SimCommand, SendRingSequenceWraparound) {
    CommandQueue q;
    q.set_standalone(true); // each enqueue+flush applies+recycles, freeing the ring

    // Push and flush one at a time so the pending list never aliases a live slot.
    CommandPacket staged{};
    staged.opcode() = 4; // size 17

    i32 firstRing = -2;
    i32 wrapRing  = -2;
    u32 wrapCount = 0;
    // Need > 32768 pushes to wrap. The slot index = (count)&0x7FFF.
    for (u32 i = 1; i <= 0x8001; ++i) {
        i32 r = q.EnqueuePacket(staged);
        if (i == 1) firstRing = r;
        if (i == 0x8000) {
            // count 0x8000 -> slot (0x8000)&0x7FFF = 0
            wrapRing = r;
            wrapCount = q.send_count();
        }
        q.FlushSendQueue(); // applies locally, recycles received node
        q.ExecCommands();   // drain received list back to free pool
    }
    CHECK_EQ(firstRing, (i32)1);
    CHECK_EQ(wrapRing, (i32)0);          // slot wrapped to 0
    CHECK_EQ(wrapCount, (u32)0x8000);    // but the Count kept counting
    CHECK_EQ(q.send_count(), (u32)0x8001);
}

// ---------------------------------------------------------------------------
// EnqueuePacket disconnected latch: dword_764CF0 set => the encoder early-outs
// with -1 and touches no ring/send-count state (the first line of 0x49388c).
// ---------------------------------------------------------------------------
TEST(SimCommand, EnqueueRejectsWhenDisconnected) {
    CommandQueue q;
    q.set_standalone(false);
    q.set_disconnected(true);
    CommandPacket staged{};
    staged.opcode() = 4; // size 17
    CHECK_EQ(q.EnqueuePacket(staged), (i32)-1);
    CHECK_EQ(q.send_count(), (u32)0);       // counter untouched
    CHECK(q.pending_head() == nullptr);     // nothing linked
}

// ---------------------------------------------------------------------------
// ExecCommands sequence classification (the lost-command / resync branch of
// 0x494088). An in-order Count advances last_req_count by exactly 1; a gap
// (a Count that is neither last_req+1, nor last_sync, nor a sync packet) snaps
// last_req_count to the received Count ("Lost a Command" resync).
// ---------------------------------------------------------------------------
TEST(SimCommand, ExecLostCommandResyncsLastRequested) {
    CommandQueue q;
    q.set_standalone(false);

    // Receive a packet whose Count is 5 while last_req_count is still 0: this is
    // NOT last_req+1 (==1), not last_sync (==0 but opcode!=sync gate), and not a
    // sync frame, so the resync branch snaps last_req_count to 5.
    CommandPacket frame{};
    frame.opcode() = 4;              // a plain fixed opcode (size 17)
    frame.set_cmd_id(7);            // a tracked command (cmdId != -1)
    frame.set_count(5);            // out-of-order Count
    CHECK_EQ(q.StoreReceivedPacket(frame), 0);
    q.ExecCommands();
    CHECK_EQ(q.last_req_count(), (u32)5);     // resynced to the received Count
    // The ACK slot for cmdId 7 is stamped applied (status 2).
    CHECK_EQ(q.GetPacketStatusById(7), 2);
}

// ---------------------------------------------------------------------------
// Delta encoding: wire bytes equal new-minus-old, and applying the delta to old
// reproduces new. Covers widths 1/2/4.
// ---------------------------------------------------------------------------
TEST(SimCommand, DeltaEncodeNewMinusOld) {
    // Live entity memory (the "old" state).
    u8 entity[64];
    std::memset(entity, 0, sizeof(entity));
    // old values at the offsets we'll touch
    entity[8]  = 100;                    // width-1 field at offset 8
    *reinterpret_cast<u16*>(entity + 16) = 5000; // width-2 field at offset 16
    *reinterpret_cast<u32*>(entity + 24) = 0x11223344; // width-4 field at offset 24

    DeltaWriter dw;
    dw.BeginDeltaPacket(entity, /*entityId=*/0xCAFE);
    CHECK_EQ(dw.entity_id(), (u32)0xCAFE);

    u8  newB  = 130;                       // delta = 30
    u16 newW  = 4000;                      // delta = -1000 (u16 wrap)
    u32 newD  = 0x11223300;                // delta = -0x44

    CHECK_EQ(dw.AppendDeltaField(1, 1, 8,  &newB), 0);
    CHECK_EQ(dw.AppendDeltaField(2, 1, 16, &newW), 0);
    CHECK_EQ(dw.AppendDeltaField(4, 1, 24, &newD), 0);
    CHECK_EQ(dw.field_count(), (u8)3);

    // Inspect the wire bytes: each field is [w][c][off_lo][off_hi][delta...].
    const u8* p = dw.payload();
    // field 0
    CHECK_EQ(p[0], (u8)1); CHECK_EQ(p[1], (u8)1);
    CHECK_EQ(p[2], (u8)8); CHECK_EQ(p[3], (u8)0);
    CHECK_EQ(p[4], (u8)(newB - 100));      // 30
    // field 1 at offset 5
    const u8* f1 = p + 5;
    CHECK_EQ(f1[0], (u8)2); CHECK_EQ(f1[1], (u8)1);
    CHECK_EQ(f1[2], (u8)16); CHECK_EQ(f1[3], (u8)0);
    u16 d1 = static_cast<u16>(f1[4] | (f1[5] << 8));
    CHECK_EQ(d1, (u16)(4000 - 5000));      // wraps mod 2^16
    // field 2 at offset 5 + (2+4) = 11
    const u8* f2 = p + 11;
    CHECK_EQ(f2[0], (u8)4); CHECK_EQ(f2[1], (u8)1);
    CHECK_EQ(f2[2], (u8)24); CHECK_EQ(f2[3], (u8)0);
    u32 d2 = static_cast<u32>(f2[4]) | (static_cast<u32>(f2[5]) << 8)
           | (static_cast<u32>(f2[6]) << 16) | (static_cast<u32>(f2[7]) << 24);
    CHECK_EQ(d2, (u32)(0x11223300u - 0x11223344u));

    // Apply the delta back onto the old entity -> must reproduce new.
    DeltaWriter::ApplyDelta(dw.payload(), dw.field_count(), entity);
    CHECK_EQ(entity[8], newB);
    CHECK_EQ(*reinterpret_cast<u16*>(entity + 16), newW);
    CHECK_EQ(*reinterpret_cast<u32*>(entity + 24), newD);
}

TEST(SimCommand, DeltaArrayCountAndRawCopied) {
    u8 entity[64];
    std::memset(entity, 0, sizeof(entity));
    u8 oldArr[4] = {10, 20, 30, 40};
    std::memcpy(entity + 4, oldArr, 4);

    DeltaWriter dw;
    dw.BeginDeltaPacket(entity, 1);
    u8 newArr[4] = {11, 19, 33, 36};   // deltas +1,-1,+3,-4
    CHECK_EQ(dw.AppendDeltaField(1, 4, 4, newArr), 0);
    const u8* p = dw.payload();
    CHECK_EQ(p[0], (u8)1); CHECK_EQ(p[1], (u8)4);
    CHECK_EQ(p[4], (u8)1); CHECK_EQ(p[5], (u8)0xFF);
    CHECK_EQ(p[6], (u8)3); CHECK_EQ(p[7], (u8)0xFC);
    DeltaWriter::ApplyDelta(p, dw.field_count(), entity);
    CHECK_EQ(std::memcmp(entity + 4, newArr, 4), 0);

    // Raw/Copied write absolute values (same wire bytes for both).
    DeltaWriter raw; raw.Reset(nullptr);
    DeltaWriter cop; cop.Reset(nullptr);
    u16 vals[3] = {0x1111, 0x2222, 0x3333};
    CHECK_EQ(raw.AppendRawField(2, 3, 12, vals), 0);
    CHECK_EQ(cop.AppendCopiedField(2, 3, 12, vals), 0);
    CHECK_EQ(raw.cursor(), cop.cursor());
    CHECK_EQ(std::memcmp(raw.payload(), cop.payload(), raw.cursor()), 0);
    // and equal to the absolute values
    CHECK_EQ(raw.payload()[4], (u8)0x11);
    CHECK_EQ(raw.payload()[5], (u8)0x11);
    CHECK_EQ(raw.payload()[8], (u8)0x33);
}

TEST(SimCommand, DeltaRejectsBadWidthAndOverflow) {
    u8 entity[8] = {0};
    DeltaWriter dw;
    dw.BeginDeltaPacket(entity, 0);
    u8 v = 1;
    CHECK_EQ(dw.AppendDeltaField(3, 1, 0, &v), 1); // bad width
    CHECK_EQ(dw.field_count(), (u8)0);
    // overflow: width 4 * count 30 = 120; 120 + 0 + 4 = 124 >= 0x77 -> reject
    u8 big[120] = {0};
    CHECK_EQ(dw.AppendDeltaField(4, 30, 0, big), 1);
}

TEST(SimCommand, AiMethodEntryEncoding) {
    AiMethodWriter aw;
    aw.Begin(0x1234);
    CHECK_EQ(aw.person_id(), (u32)0x1234);
    CHECK_EQ(aw.AppendEntry(7, -5), 0);
    CHECK_EQ(aw.AppendEntry(9, 1000), 0);
    CHECK_EQ(aw.entry_count(), (u8)2);
    const u8* p = aw.payload();
    CHECK_EQ(p[0], (u8)7);
    i32 d0 = static_cast<i32>(p[1] | (p[2] << 8) | (p[3] << 16) | (p[4] << 24));
    CHECK_EQ(d0, (i32)-5);
    CHECK_EQ(p[5], (u8)9);
}
