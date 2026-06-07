#include "test.h"

#include "sim/command_inherit.h"

#include <cstring>

using namespace guild;
using namespace guild::sim;

namespace {

// Build a deterministic 536-byte "old" person record matching the Python oracle.
InheritPerson MakeOldPerson() {
    InheritPerson p{};
    std::memset(p.bytes, 0, sizeof(p.bytes));
    i32 id = 0x11223344;
    std::memcpy(p.bytes + 4, &id, 4);
    p.bytes[8] = 30;                                  // status (old)
    u16 flags = 5; std::memcpy(p.bytes + 10, &flags, 2);
    i32 v404 = 1; std::memcpy(p.bytes + 404, &v404, 4);
    i32 vals[] = {10, 20, 30, 40, 50};
    for (int i = 0; i < 5; ++i) std::memcpy(p.bytes + 408 + 4 * i, &vals[i], 4);
    p.bytes[432] = 7; p.bytes[433] = 9;
    float wealth = 12.5f; std::memcpy(p.bytes + 92, &wealth, 4);
    return p;
}

u32 RandStub() { return 0; }  // makes the skill-reroll curve deterministic

} // namespace

// --- Golden vector: delta packet #1 exact wire image ------------------------
TEST(CmdInherit, DeltaPacket1GoldenVector) {
    InheritPerson old = MakeOldPerson();
    DeltaWriter dw;

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

    CHECK_EQ(dw.field_count(), 10);
    CHECK_EQ(dw.cursor(), 69u);

    static const u8 kGolden[] = {
        0x01,0x01,0x08,0x00,0x46,
        0x02,0x01,0x0a,0x00,0x0b,0x00,
        0x04,0x01,0x94,0x01,0x02,0x00,0x00,0x00,
        0x04,0x01,0x98,0x01,0xf6,0xff,0xff,0xff,
        0x04,0x01,0x9c,0x01,0xec,0xff,0xff,0xff,
        0x04,0x01,0xa0,0x01,0xe2,0xff,0xff,0xff,
        0x04,0x01,0xa4,0x01,0xd8,0xff,0xff,0xff,
        0x04,0x01,0xa8,0x01,0xce,0xff,0xff,0xff,
        0x01,0x01,0xb0,0x01,0xf9,
        0x01,0x01,0xb1,0x01,0xf7,
    };
    CHECK_EQ(sizeof(kGolden), 69u);
    CHECK(std::memcmp(dw.payload(), kGolden, sizeof(kGolden)) == 0);
}

// --- QueueRequestState22 real builder: 124-byte block at +0x10 --------------
TEST(CmdInherit, State22BlockLayout) {
    InheritPerson old = MakeOldPerson();
    DeltaWriter dw;
    u8 ns = 100; dw.BeginDeltaPacket(old.bytes, static_cast<u32>(old.id()));
    dw.AppendDeltaField(1, 1, 8, &ns);

    CommandQueue q; q.Init();
    i32 slot = QueueRequestState22FromDelta(q, dw);
    CHECK(slot >= 0);
    CommandPacket& pkt = q.ring_slot(static_cast<u32>(slot));
    CHECK_EQ(pkt.opcode(), 22);

    // +0x10: id (4) | field_count (1) | payload triplet.
    static const u8 kHead[] = {0x44,0x33,0x22,0x11, 0x01, 0x01,0x01,0x08,0x00};
    CHECK(std::memcmp(pkt.bytes + 0x10, kHead, sizeof(kHead)) == 0);
    // delta value for status 100-30 = 70 = 0x46.
    CHECK_EQ(pkt.bytes[0x10 + 9], 0x46);
}

// --- EnqueueCmd15 (opcode 15) wire layout -----------------------------------
TEST(CmdInherit, EnqueueCmd15Layout) {
    CommandQueue q; q.Init();
    i32 slot = EnqueueCmd15(q, 0x0A0B0C0D, -1, 1500, 7);
    CHECK(slot >= 0);
    CommandPacket& p = q.ring_slot(static_cast<u32>(slot));
    CHECK_EQ(p.opcode(), 15);
    CHECK_EQ(p.get32(0x10), 0x0A0B0C0Du);   // a1 payer
    CHECK_EQ(p.get32(0x14), 0xFFFFFFFFu);   // a2 = -1
    CHECK_EQ(p.bytes[0x1C], 7);             // a4 currency byte
    CHECK_EQ(p.get32(0x1D), 1500u);         // a3 amount
}

// --- QueueRequestSlotReset28 (opcode 28 + 248-byte StagePendingBlock) -------
TEST(CmdInherit, SlotReset28StagesBlock) {
    CommandQueue q; q.Init();
    PendingState pending;
    SlotResetScratch scratch{};
    // word 14 currently 0 -> must become -1 after the builder runs.
    scratch.words[14] = 0;
    i32 slot = QueueRequestSlotReset28(q, pending, scratch, 5);
    CHECK(slot >= 0);
    CHECK_EQ(q.ring_slot(static_cast<u32>(slot)).opcode(), 28);
    CHECK_EQ(scratch.words[14], 0xFFFFFFFFu);
    // StagePendingBlock staged 248 + 2 bytes.
    CHECK_EQ(pending.staged, 250u);
    CHECK_EQ(pending.block[0], 0xF8);  // length lo (248)
    CHECK_EQ(pending.block[1], 0x00);

    // A non-zero word 14 is left untouched on a second packet (block already
    // staged -> StagePendingBlock no-ops, but the -1 guard still applies).
    SlotResetScratch s2{};
    s2.words[14] = 0x55;
    QueueRequestSlotReset28(q, pending, s2, 0);
    CHECK_EQ(s2.words[14], 0x55u);   // untouched (was non-zero)
}

// --- Args26 negated-wealth field bits ---------------------------------------
TEST(CmdInherit, EmitArgs26WealthBits) {
    InheritPerson old = MakeOldPerson();
    DeltaWriter dw;
    CommandQueue q; q.Init();
    InheritEmitCtx ctx;
    ctx.queue = &q; ctx.delta = &dw; ctx.randNext = RandStub;
    EmitInheritanceDelta(ctx, old);

    // Walk the ring for the first opcode-26 packet; its field id at +0x14 must be
    // 124 and value bits at +0x18 must be the float bits of -12.5 (0xC1480000).
    bool found = false;
    for (u32 i = 1; i <= q.send_count(); ++i) {
        CommandPacket& p = q.ring_slot(i);
        if (p.opcode() == 26 && p.get32(0x14) == 124) {
            CHECK_EQ(p.get32(0x18), 0xC1480000u);
            found = true; break;
        }
    }
    CHECK(found);
}

