#include "tests/framework/test.h"
#include "sim/command_apply7.h"
#include "sim/command.h"

#include <cstring>
#include <vector>

using namespace guild;
using namespace guild::sim;

// Helper: after EnqueuePacket the staged bytes land in ring_slot(ret); the
// header is stamped (opcode +0, len +1, cmdId +4, count +8) and payload bytes
// are preserved. We assert opcode, computed length, and selected payload bytes.

namespace {
CommandPacket& slot(CommandQueue& q, i32 idx) { return q.ring_slot(static_cast<u32>(idx)); }

u32 ple32(const CommandPacket& p, u32 off) { return p.get32(off); }
u16 ple16(const CommandPacket& p, u32 off) { return p.get16(off); }
}

TEST(CmdApply7, FlagBlob32Layout) {
    CommandQueue q;
    std::vector<u8> blob(124);
    for (u32 i = 0; i < 124; ++i) blob[i] = static_cast<u8>(i * 7 + 3);
    i32 idx = QueueRequestFlagBlob32(q, 5, blob.data());
    CHECK(idx >= 0);
    CommandPacket& p = slot(q, idx);
    CHECK_EQ(p.opcode(), 32u);
    // opcode 0x20, flag != 14 -> wire length 141.
    CHECK_EQ(p.len(), 141u);
    CHECK_EQ(p.bytes[0x10], 5u);
    for (u32 i = 0; i < 124; ++i) CHECK_EQ(p.bytes[0x11 + i], blob[i]);
}

TEST(CmdApply7, FlagBlob32SyncMarkerShortLen) {
    CommandQueue q;
    // QueueRequestFlagBlob32 faithfully copies 124 bytes from `blob` (the
    // original's qmemcpy(v4, a2, 124)), so the source MUST be at least 124 bytes.
    // (Previously this passed a 4-byte i32, reading 120 bytes off the stack — a
    // test bug ASAN flags; the function's contract is a >=124-byte blob.)
    u8 blob[124] = {0};
    // flag == 14 makes byte[+16]==14 -> short sync variant (length 17).
    i32 idx = QueueRequestFlagBlob32(q, 14, blob);
    CHECK(idx >= 0);
    CHECK_EQ(slot(q, idx).len(), 17u);
}

TEST(CmdApply7, FlagBlob32NullBlobNoCopy) {
    CommandQueue q;
    i32 idx = QueueRequestFlagBlob32(q, 7, nullptr);
    CHECK(idx >= 0);
    CommandPacket& p = slot(q, idx);
    CHECK_EQ(p.bytes[0x10], 7u);
    // No blob copied; payload bytes stay zero.
    CHECK_EQ(p.bytes[0x11], 0u);
    CHECK_EQ(p.bytes[0x11 + 123], 0u);
}

TEST(CmdApply7, KeepAliveMagic) {
    CommandQueue q;
    i32 idx = EnqueueKeepAlive(q);
    CHECK(idx >= 0);
    CommandPacket& p = slot(q, idx);
    CHECK_EQ(p.opcode(), 3u);
    CHECK_EQ(p.len(), 20u);
    CHECK_EQ(ple32(p, 0x10), 0xB336A7DDu); // -1288263715 reinterpreted
}

TEST(CmdApply7, BuildingActionEndNamePad) {
    CommandQueue q;
    i32 idx = EnqueueBuildingActionEnd(q, "Town Hall");
    CHECK(idx >= 0);
    CommandPacket& p = slot(q, idx);
    CHECK_EQ(p.opcode(), 6u);
    CHECK_EQ(p.len(), 145u);
    CHECK(std::memcmp(&p.bytes[0x10], "Town Hall", 9) == 0);
    CHECK_EQ(p.bytes[0x10 + 9], 0u); // NUL after the name
}

TEST(CmdApply7, TargetedActionZeroKindRejects) {
    CommandQueue q;
    i32 idx = EnqueueTargetedAction(q, 0, 123, 4, nullptr);
    CHECK_EQ(idx, -1);
}

TEST(CmdApply7, TargetedActionWithBlob) {
    CommandQueue q;
    std::vector<u8> blob(48);
    for (u32 i = 0; i < 48; ++i) blob[i] = static_cast<u8>(0xA0 + i);
    i32 idx = EnqueueTargetedAction(q, 2, 0x11223344, 0x55, blob.data());
    CHECK(idx >= 0);
    CommandPacket& p = slot(q, idx);
    CHECK_EQ(p.opcode(), 10u);
    CHECK_EQ(p.len(), 80u);
    CHECK_EQ(ple16(p, 0x14), 2u);
    CHECK_EQ(ple32(p, 0x16), 0x11223344u);
    CHECK_EQ(ple16(p, 0x1A), 0x55u);
    CHECK_EQ(ple32(p, 0x1C), 1u); // present flag
    for (u32 i = 0; i < 48; ++i) CHECK_EQ(p.bytes[0x20 + i], blob[i]);
}

TEST(CmdApply7, TargetedActionNoBlobClears) {
    CommandQueue q;
    i32 idx = EnqueueTargetedAction(q, 3, 9, 1, nullptr);
    CHECK(idx >= 0);
    CommandPacket& p = slot(q, idx);
    CHECK_EQ(ple32(p, 0x1C), 0u); // present flag 0
    for (u32 i = 0; i < 48; ++i) CHECK_EQ(p.bytes[0x20 + i], 0u);
}

TEST(CmdApply7, TradeRequestLayout) {
    CommandQueue q;
    i32 idx = EnqueueTradeRequest(q, 0x01020304, 0x0A0B0C0D, 0x11, 0x2233,
                                  0x44556677, 0x55, 0x66, "Seller", "Buyer");
    CHECK(idx >= 0);
    CommandPacket& p = slot(q, idx);
    CHECK_EQ(p.opcode(), 12u);
    CHECK_EQ(p.len(), 73u);
    CHECK_EQ(ple32(p, 0x14), 0x01020304u);
    CHECK_EQ(ple32(p, 0x18), 0x0A0B0C0Du);
    CHECK_EQ(ple16(p, 0x1C), 0x2233u);
    CHECK_EQ(p.bytes[0x1E], 0x11u);
    CHECK_EQ(ple32(p, 0x1F), 0x44556677u);
    CHECK_EQ(p.bytes[0x23], 0x55u);
    CHECK_EQ(p.bytes[0x24], 0x66u);
    CHECK(std::memcmp(&p.bytes[0x25], "Seller", 7) == 0);
    CHECK(std::memcmp(&p.bytes[0x35], "Buyer", 6) == 0);
}

TEST(CmdApply7, Cmd13And14) {
    CommandQueue q;
    i32 a = EnqueueCmd13(q, 0xDEAD, 0xBEEF);
    CommandPacket& pa = slot(q, a);
    CHECK_EQ(pa.opcode(), 13u);
    CHECK_EQ(pa.len(), 24u);
    CHECK_EQ(ple32(pa, 0x10), 0xDEADu);
    CHECK_EQ(ple32(pa, 0x14), 0xBEEFu);

    i32 b = EnqueueCmd14(q, static_cast<i16>(0x1234));
    CommandPacket& pb = slot(q, b);
    CHECK_EQ(pb.opcode(), 14u);
    CHECK_EQ(pb.len(), 145u);
    CHECK_EQ(ple16(pb, 0x10), 0x1234u);
}

TEST(CmdApply7, Request18DropsA3) {
    CommandQueue q;
    i32 idx = QueueRequest18(q, 0x11111111, static_cast<i16>(0x2222),
                             0x33333333 /*a3 dropped*/, 0x44444444);
    CommandPacket& p = slot(q, idx);
    CHECK_EQ(p.opcode(), 18u);
    CHECK_EQ(p.len(), 30u);
    CHECK_EQ(ple32(p, 0x10), 0x11111111u);
    CHECK_EQ(ple16(p, 0x14), 0x2222u);
    CHECK_EQ(ple32(p, 0x16), 0x44444444u);
    // Verify a3 (0x33333333) does NOT appear anywhere in the payload region.
    bool found = false;
    for (u32 o = 0x10; o + 4 <= guild::sim::kPacketStride; ++o)
        if (p.get32(o) == 0x33333333u) found = true;
    CHECK(!found);
}

TEST(CmdApply7, Request19DropsA3) {
    CommandQueue q;
    i32 idx = QueueRequest19(q, 0xAAAA0001, 0xAAAA0002, 0xBBBBBBBB, 0xAAAA0003);
    CommandPacket& p = slot(q, idx);
    CHECK_EQ(p.opcode(), 19u);
    CHECK_EQ(p.len(), 28u);
    CHECK_EQ(ple32(p, 0x10), 0xAAAA0001u);
    CHECK_EQ(ple32(p, 0x14), 0xAAAA0002u);
    CHECK_EQ(ple32(p, 0x18), 0xAAAA0003u);
}

TEST(CmdApply7, Blob21) {
    CommandQueue q;
    std::vector<u8> b(31);
    for (u32 i = 0; i < 31; ++i) b[i] = static_cast<u8>(i + 1);
    i32 idx = QueueRequestBlob21(q, 0x12345678, static_cast<i16>(0x4321), b.data());
    CommandPacket& p = slot(q, idx);
    CHECK_EQ(p.opcode(), 21u);
    CHECK_EQ(p.len(), 57u);
    CHECK_EQ(ple32(p, 0x10), 0x12345678u);
    CHECK_EQ(ple16(p, 0x14), 0x4321u);
    for (u32 i = 0; i < 31; ++i) CHECK_EQ(p.bytes[0x16 + i], b[i]);
}

TEST(CmdApply7, Request31Empty) {
    CommandQueue q;
    i32 idx = QueueRequest31(q);
    CommandPacket& p = slot(q, idx);
    CHECK_EQ(p.opcode(), 31u);
    CHECK_EQ(p.len(), 145u);
}

TEST(CmdApply7, Mixed44) {
    CommandQueue q;
    i32 idx = QueueRequestMixed44(q, 0x10203040, 0x55, static_cast<i16>(0x6677),
                                  0x44, 0x88, 0x99AABBCC);
    CommandPacket& p = slot(q, idx);
    CHECK_EQ(p.opcode(), 44u);
    CHECK_EQ(p.len(), 33u);
    CHECK_EQ(ple32(p, 0x14), 0x10203040u);
    CHECK_EQ(p.bytes[0x18], 0x55u);
    CHECK_EQ(p.bytes[0x19], 0x44u);
    CHECK_EQ(ple16(p, 0x1A), 0x6677u);
    CHECK_EQ(p.bytes[0x1C], 0x88u);
    CHECK_EQ(ple32(p, 0x1D), 0x99AABBCCu);
}

TEST(CmdApply7, Mixed45A3SplitWordByte) {
    CommandQueue q;
    i32 idx = QueueRequestMixed45(q, 0x01020304, 0x05060708, 0x00AB1234);
    CommandPacket& p = slot(q, idx);
    CHECK_EQ(p.opcode(), 45u);
    CHECK_EQ(p.len(), 31u);
    CHECK_EQ(ple32(p, 0x14), 0x01020304u);
    CHECK_EQ(ple32(p, 0x18), 0x05060708u);
    CHECK_EQ(ple16(p, 0x1C), 0x1234u);       // low word of a3
    CHECK_EQ(p.bytes[0x1E], 0xABu);          // BYTE2(a3)
}

TEST(CmdApply7, String47) {
    CommandQueue q;
    i32 idx = QueueRequestString47(q, 0x11, 0x22, "Wares", 0x33);
    CommandPacket& p = slot(q, idx);
    CHECK_EQ(p.opcode(), 47u);
    CHECK_EQ(p.len(), 60u);
    CHECK_EQ(ple32(p, 0x10), 0x11u);
    CHECK_EQ(ple32(p, 0x14), 0x22u);
    CHECK_EQ(ple32(p, 0x18), 0x33u);
    CHECK(std::memcmp(&p.bytes[0x1C], "Wares", 6) == 0);
}

TEST(CmdApply7, Vectors50BothPresent) {
    CommandQueue q;
    i32 vA[3] = {0x100, 0x200, 0x300};
    i32 vB[3] = {0x400, 0x500, 0x600};
    i32 idx = QueueRequestVectors50(q, 0x77, vA, 0xDEAD /*dropped*/, vB);
    CommandPacket& p = slot(q, idx);
    CHECK_EQ(p.opcode(), 50u);
    CHECK_EQ(p.len(), 48u);
    CHECK_EQ(ple32(p, 0x10), 0x77u);
    CHECK_EQ(ple32(p, 0x14), 0x100u);
    CHECK_EQ(ple32(p, 0x18), 0x200u);
    CHECK_EQ(ple32(p, 0x1C), 0x300u);
    CHECK_EQ(ple32(p, 0x24), 0x400u);
    CHECK_EQ(ple32(p, 0x28), 0x500u);
    CHECK_EQ(ple32(p, 0x2C), 0x600u);
}

TEST(CmdApply7, Vectors50NullVectorsSkip) {
    CommandQueue q;
    i32 idx = QueueRequestVectors50(q, 0x42, nullptr, 0, nullptr);
    CommandPacket& p = slot(q, idx);
    CHECK_EQ(ple32(p, 0x10), 0x42u);
    CHECK_EQ(ple32(p, 0x14), 0u); // untouched
    CHECK_EQ(ple32(p, 0x24), 0u); // untouched
}

TEST(CmdApply7, String62) {
    CommandQueue q;
    i32 idx = QueueRequestString62(q, 0xABCDEF, "Beggar");
    CommandPacket& p = slot(q, idx);
    CHECK_EQ(p.opcode(), 62u);
    CHECK_EQ(p.len(), 52u);
    CHECK_EQ(ple32(p, 0x10), 0xABCDEFu);
    CHECK(std::memcmp(&p.bytes[0x14], "Beggar", 7) == 0);
}

TEST(CmdApply7, FlagBlob63) {
    CommandQueue q;
    std::vector<u8> b(128);
    for (u32 i = 0; i < 128; ++i) b[i] = static_cast<u8>(255 - i);
    i32 idx = QueueRequestFlagBlob63(q, b.data(), 9);
    CommandPacket& p = slot(q, idx);
    CHECK_EQ(p.opcode(), 63u);
    CHECK_EQ(p.len(), 145u);
    CHECK_EQ(p.bytes[0x10], 9u);
    for (u32 i = 0; i < 128; ++i) CHECK_EQ(p.bytes[0x11 + i], b[i]);
}

TEST(CmdApply7, BuildOps77_82_90) {
    CommandQueue q;
    i32 i77 = RequestBuildOp77(q, 0x1234);
    CHECK_EQ(slot(q, i77).opcode(), 77u);
    CHECK_EQ(slot(q, i77).len(), 20u);
    CHECK_EQ(slot(q, i77).get32(0x10), 0x1234u);

    i32 i82 = RequestBuildOp82(q, 0xA, 0xB, 0xCAFE /*dropped*/, 0xC);
    CHECK_EQ(slot(q, i82).opcode(), 82u);
    CHECK_EQ(slot(q, i82).len(), 28u);
    CHECK_EQ(slot(q, i82).get32(0x10), 0xAu);
    CHECK_EQ(slot(q, i82).get32(0x14), 0xBu);
    CHECK_EQ(slot(q, i82).get32(0x18), 0xCu);

    i32 i90 = RequestBuildOp90(q, 0x111, 0x222);
    CHECK_EQ(slot(q, i90).opcode(), 90u);
    CHECK_EQ(slot(q, i90).len(), 24u);
    CHECK_EQ(slot(q, i90).get32(0x10), 0x111u);
    CHECK_EQ(slot(q, i90).get32(0x14), 0x222u);
}

TEST(CmdApply7, BuildOp78DualStr) {
    CommandQueue q;
    i32 idx = RequestBuildOp78DualStr(q, "FirstName", 0x1111, "SecondName", 0x2222);
    CommandPacket& p = slot(q, idx);
    CHECK_EQ(p.opcode(), 78u);
    CHECK_EQ(p.len(), 64u);
    CHECK(std::memcmp(&p.bytes[0x10], "FirstName", 10) == 0);
    CHECK_EQ(ple32(p, 0x28), 0x1111u);
    CHECK_EQ(ple32(p, 0x2C), 0x2222u);
    CHECK(std::memcmp(&p.bytes[0x30], "SecondName", 11) == 0);
}

TEST(CmdApply7, BuildOp78NullSecondString) {
    CommandQueue q;
    i32 idx = RequestBuildOp78DualStr(q, "Solo", 7, nullptr, 8);
    CommandPacket& p = slot(q, idx);
    CHECK(std::memcmp(&p.bytes[0x10], "Solo", 5) == 0);
    CHECK_EQ(p.bytes[0x30], 0u); // empty second string
}

// --- game-speed control -----------------------------------------------------

TEST(CmdApply7, SetGameSpeedClampAndEnqueue) {
    g_gameSpeed = 0;
    CommandQueue q;
    // level 2 != current 0 -> enqueue {2} via opcode 32.
    i32 r = SetGameSpeed(q, 2);
    CHECK_EQ(r, 2);
    CHECK(q.pending_head() != nullptr);
    // The enqueued packet is opcode 32; the level blob sits at +0x11.
    i32 ring = 0;
    // find the single enqueued slot (send_count == 1 -> ring 1)
    CHECK_EQ(q.send_count(), 1u);
    ring = 1;
    CommandPacket& p = slot(q, ring);
    CHECK_EQ(p.opcode(), 32u);
    CHECK_EQ(p.bytes[0x10], 18u);            // flag arg
    CHECK_EQ(p.get32(0x11), 2u);             // level blob
}

TEST(CmdApply7, SetGameSpeedClampHigh) {
    g_gameSpeed = 0;
    CommandQueue q;
    i32 r = SetGameSpeed(q, 99);
    CHECK_EQ(r, 4);                          // clamped to 4
    CHECK_EQ(slot(q, 1).get32(0x11), 4u);
}

TEST(CmdApply7, SetGameSpeedSameNoEnqueue) {
    g_gameSpeed = 3;
    CommandQueue q;
    i32 r = SetGameSpeed(q, 3);
    CHECK_EQ(r, 3);
    CHECK(q.pending_head() == nullptr);      // no enqueue
    CHECK_EQ(q.send_count(), 0u);
}

TEST(CmdApply7, SetGameSpeedAlreadyMaxNoEnqueue) {
    g_gameSpeed = 4;
    CommandQueue q;
    i32 r = SetGameSpeed(q, 7);
    CHECK_EQ(r, 4);
    CHECK(q.pending_head() == nullptr);
}

TEST(CmdApply7, IncreaseDecreaseGameSpeed) {
    g_gameSpeed = 1;
    CommandQueue q;
    IncreaseGameSpeed(q);                     // enqueue {2}
    CHECK_EQ(q.send_count(), 1u);
    CHECK_EQ(slot(q, 1).get32(0x11), 2u);
    DecreaseGameSpeed(q);                      // enqueue {0}
    CHECK_EQ(q.send_count(), 2u);
    CHECK_EQ(slot(q, 2).get32(0x11), 0u);
}

TEST(CmdApply7, IncreaseGameSpeedAtMaxNoEnqueue) {
    g_gameSpeed = 4;
    CommandQueue q;
    IncreaseGameSpeed(q);
    CHECK_EQ(q.send_count(), 0u);
}

TEST(CmdApply7, DecreaseGameSpeedAtZeroNoEnqueue) {
    g_gameSpeed = 0;
    CommandQueue q;
    DecreaseGameSpeed(q);
    CHECK_EQ(q.send_count(), 0u);
}

TEST(CmdApply7, GetGameSpeedReflectsGlobal) {
    g_gameSpeed = 3;
    CHECK_EQ(GetGameSpeed(), 3);
    g_gameSpeed = 0;
}
