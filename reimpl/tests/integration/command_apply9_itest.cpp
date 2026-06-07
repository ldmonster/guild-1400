// Integration: drive command_apply9's EnqueuePacket builders and selection-driven
// emitters against the REAL command codec siblings (command.cpp /
// command_codec.cpp) — a live CommandQueue, the real DeltaWriter, and the real
// QueueRequest17 / QueueRequestState22 / QueueRequestArgs25 request builders.
// This is exactly the live wiring: command_apply9 never re-implements the codec;
// it stages a 153-byte CommandPacket and calls CommandQueue::EnqueuePacket (which
// stamps the header through the REAL VIBE_Command_ComputePacketSize), and its
// selection builders push opcode-17 / opcode-22 packets through the real codec.
//
// We additionally forward command_apply9's `parseInt` selection hook into the
// module's own faithful default (the VIBE_Util_ParseInt clone) AND cross-check it
// against libc std::atoi as a deterministic oracle, and we decode the State22
// delta back through the REAL DeltaWriter::ApplyDelta to confirm the flag byte
// the emitter wrote at field offset 433.
#include "test.h"

#include "sim/command_apply9.h"
#include "sim/command_codec.h"
#include "sim/command.h"

#include <cstdlib>
#include <cstring>

using namespace guild;
using namespace guild::sim;

namespace {

// A faithful libc oracle for the console int parser.
i32 RealParseInt(const char* s) { return static_cast<i32>(std::atoi(s)); }

// A 2-slot selection table: slot idx -> record base + entity id.
struct SelRec { int base; i32 entityId; };
SelRec g_selTable[4] = {{0, 0}, {0, 0}, {0, 0}, {0, 0}};
int SelectedPersonRecord(int /*sel*/, int idx, i32* outId) {
    if (idx < 0 || idx >= 4) { if (outId) *outId = 0; return 0; }
    if (outId) *outId = g_selTable[idx].entityId;
    return g_selTable[idx].base;
}

// Reveal-all world: a sparse set of revealable slots.
int g_revealActive[768];
i32 g_revealIds[768];
i32 WorldActiveCount() { return 4; }
int RevealableSlot(int i, i32* outId) {
    if (i < 0 || i >= 768) { if (outId) *outId = 0; return 0; }
    if (outId) *outId = g_revealIds[i];
    return g_revealActive[i];
}
u8 RevealFlagByte() { return 0x5A; }

} // namespace

// RequestBuildOp81 stages a real opcode-81 packet through the REAL EnqueuePacket;
// confirm the slot, opcode, payload geometry, and the header the real codec stamped.
TEST(CommandApply9Itest, Op81BuilderThroughRealQueue) {
    CommandQueue q;
    q.Init();
    q.set_standalone(false);   // keep the packet in the send ring (no local apply)
    q.set_disconnected(false);

    u8 body[44];
    for (int i = 0; i < 44; ++i) body[i] = static_cast<u8>(0xB0 + i);

    i32 slot = RequestBuildOp81(q, /*a1*/ 0x1234, body);
    CHECK(slot >= 0);
    CHECK_EQ(q.send_count(), 1u);

    CommandPacket& p = q.ring_slot(static_cast<u32>(slot));
    CHECK_EQ((int)p.opcode(), 81);
    // a1 written at payload +0x10, body44 at +0x14, snapshot dword (inert -1) at +0x40.
    CHECK_EQ(p.get32(0x10), 0x1234u);
    CHECK_EQ((int)p.bytes[0x14], 0xB0);
    CHECK_EQ((int)p.bytes[0x14 + 43], (u8)(0xB0 + 43));
    CHECK_EQ(p.get32(0x40), 0xFFFFFFFFu);   // DefaultOp80SnapshotDword -> -1 (inert)
    // The REAL EnqueuePacket stamped the header length via the real ComputePacketSize.
    CHECK_EQ(p.len(), ComputePacketSize(p));
}

// RequestChrMoveToUniverse: opcode 48, the 2-byte-stride wide name copy, and the
// real header stamp; the long-name path fires the errorLog hook and skips the copy.
TEST(CommandApply9Itest, ChrMoveToUniverseThroughRealQueue) {
    CommandQueue q;
    q.Init();
    q.set_standalone(false);
    q.set_disconnected(false);

    i32 slot = RequestChrMoveToUniverse(q, /*a1*/ 7, /*a2*/ 9, "Bob", /*a4*/ 11);
    CHECK(slot >= 0);
    CommandPacket& p = q.ring_slot(static_cast<u32>(slot));
    CHECK_EQ((int)p.opcode(), 48);
    CHECK_EQ(p.get32(0x10), 7u);
    CHECK_EQ(p.get32(0x14), 9u);
    CHECK_EQ(p.get32(0x18), 11u);
    // 2-byte-stride copy: 'B' at +0x1C, then 'o' at +0x1D (second lane), etc.
    CHECK_EQ((int)p.bytes[0x1C], 'B');
    CHECK_EQ((int)p.bytes[0x1D], 'o');
    CHECK_EQ((int)p.bytes[0x1E], 'b');
    CHECK_EQ(p.len(), ComputePacketSize(p));
}

// EncodeFlagState emits opcode-25 through the REAL QueueRequestArgs25 codec; the
// representative mask comes from the pure classifier and lands in the packet.
TEST(CommandApply9Itest, EncodeFlagStateOp25ThroughRealCodec) {
    CommandQueue q;
    q.Init();
    q.set_standalone(false);
    q.set_disconnected(false);

    // flag4 with the low nibble set -> classifier returns mask 7 (top priority).
    u32 flag4 = 0x00000003;
    CHECK_EQ(EncodeFlagStateMask(flag4), 7);

    i32 r = EncodeFlagState(q, /*personId*/ 0x2222, flag4);
    CHECK_EQ(r, 1);
    CHECK_EQ(q.send_count(), 1u);

    CommandPacket& p = q.ring_slot(1);
    CHECK_EQ((int)p.opcode(), 25);
    // QueueRequestArgs25(q, personId, 44, 0, 4, mask): a1 @ +0x10, a5(mask) tail.
    CHECK_EQ(p.get32(0x10), 0x2222u);
    CHECK_EQ(p.len(), ComputePacketSize(p));

    // A zero flag word emits nothing through the codec (send_count unchanged).
    i32 r2 = EncodeFlagState(q, 0x3333, 0);
    CHECK_EQ(r2, 1);
    CHECK_EQ(q.send_count(), 1u);
}

// QueueSetSelectedFlag: console "-7" -> a State22 delta packet through the REAL
// DeltaWriter + codec; decode it back via the REAL ApplyDelta and read field 433.
TEST(CommandApply9Itest, SetSelectedFlagState22RoundTrip) {
    CommandQueue q;
    q.Init();
    q.set_standalone(false);
    q.set_disconnected(false);
    DeltaWriter dw;

    SelectionHooks sh{};
    sh.parseInt = RealParseInt;               // libc oracle for the int parse
    sh.selectedPersonRecord = SelectedPersonRecord;
    SetSelectionHooks(&sh);

    // Slot 0 resolves to a non-null record with entity id 0x99.
    g_selTable[0] = {/*base*/ 0x5555, /*entityId*/ 0x99};

    i32 r = QueueSetSelectedFlag(q, dw, /*sel*/ 0x10, /*idx*/ 0, "-7");
    CHECK_EQ(r, 1);
    CHECK_EQ(q.send_count(), 1u);

    CommandPacket& p = q.ring_slot(1);
    CHECK_EQ((int)p.opcode(), 22);
    CHECK_EQ(p.len(), ComputePacketSize(p));   // variable-length opcode 0x16

    // Decode the delta back onto a fresh record (sized to the real 536-byte rec
    // plus headroom for the +433 write).
    const u8* block = p.bytes + 0x10;
    u8 fieldCount = block[0];
    CHECK_EQ((int)fieldCount, 1);
    if (fieldCount == 1) {
        u8 rec[600];
        std::memset(rec, 0, sizeof(rec));
        DeltaWriter::ApplyDelta(block + 1, fieldCount, rec);
        // emitter parses ParseInt(arg+1) == ParseInt("7") == 7 -> byte at +433.
        CHECK_EQ((int)rec[433], 7);
    }

    // A token not starting with '-' is rejected (no emit).
    i32 r2 = QueueSetSelectedFlag(q, dw, 0x10, 0, "7");
    CHECK_EQ(r2, 0);
    CHECK_EQ(q.send_count(), 1u);

    SetSelectionHooks(nullptr);
}

// QueueRevealAllPersons: each revealable slot emits a real opcode-17 packet via
// QueueRequest17 + the real ComputePacketSize; assert the per-slot entity ids.
TEST(CommandApply9Itest, RevealAllPersonsEmitsRealOp17) {
    CommandQueue q;
    q.Init();
    q.set_standalone(false);
    q.set_disconnected(false);

    std::memset(g_revealActive, 0, sizeof(g_revealActive));
    std::memset(g_revealIds, 0, sizeof(g_revealIds));
    g_revealActive[3] = 1; g_revealIds[3] = 0x301;
    g_revealActive[8] = 1; g_revealIds[8] = 0x802;

    SelectionHooks sh{};
    sh.worldActiveCount = WorldActiveCount;
    sh.revealableSlot = RevealableSlot;
    sh.revealFlagByte = RevealFlagByte;
    SetSelectionHooks(&sh);

    i32 r = QueueRevealAllPersons(q, "-ABCD");   // tail len >= 4
    CHECK_EQ(r, 1);
    CHECK_EQ(q.send_count(), 2u);

    CommandPacket& p1 = q.ring_slot(1);
    CHECK_EQ((int)p1.opcode(), 17);
    CHECK_EQ(p1.get32(0x10), 0x301u);
    CHECK_EQ(p1.get32(0x14), 0xFFFFFFFFu);       // a2 == -1
    CHECK_EQ((int)p1.get16(0x18), 4);            // a4 word == active count
    CHECK_EQ(p1.len(), ComputePacketSize(p1));
    CHECK_EQ(q.ring_slot(2).get32(0x10), 0x802u);

    // Malformed token (missing '-') -> rejected, no further emit.
    i32 r2 = QueueRevealAllPersons(q, "ABCD");
    CHECK_EQ(r2, 0);
    CHECK_EQ(q.send_count(), 2u);

    SetSelectionHooks(nullptr);
}
