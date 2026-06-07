#include "sim/command.h"
#include "sim/command_codec.h"
#include "test.h"

#include <cstring>
#include <vector>

using namespace guild;
using namespace guild::sim;

// ---------------------------------------------------------------------------
// Mock transport. The Command layer hands finished frames to
// guild::sim::netglue::SendPacket (forward-declared, weak default in
// command.cpp). We provide a strong override that captures each 153-byte frame
// into a global queue, modelling "the bytes that go on the wire". The receiving
// peer then feeds those frames back into a second CommandQueue.
// ---------------------------------------------------------------------------
namespace {
std::vector<CommandPacket>* g_wire = nullptr;
}
namespace guild::sim { namespace netglue {
void SendPacket(const CommandPacket& pkt) {
    if (g_wire) g_wire->push_back(pkt);
}
} } // namespace guild::sim::netglue

namespace {

// Decoded view of a command as seen on the receive side, for verification.
struct Decoded {
    u8  opcode;
    u16 len;
    u32 cmdId;
    u32 count;
    u8  payload[153];
};

std::vector<Decoded>* g_decoded = nullptr;

// Receive-side handler: records the decoded command. Registered for every
// opcode we send so ExecCommands routes through it in sequence order.
void RecordHandler(CommandQueue&, CommandPacket& pkt, AckEntry*) {
    Decoded d{};
    d.opcode = pkt.opcode();
    d.len    = pkt.len();
    d.cmdId  = pkt.cmd_id();
    d.count  = pkt.count();
    std::memcpy(d.payload, pkt.bytes, sizeof(d.payload));
    if (g_decoded) g_decoded->push_back(d);
}

// Build a burst of varied commands on the sender and return the wire frames.
std::vector<CommandPacket> BuildBurst() {
    std::vector<CommandPacket> wire;
    g_wire = &wire;

    CommandQueue tx;
    tx.set_standalone(false); // route through the mock transport

    QueueRequest16(tx, 0x01020304, 0x05060708, 0x090A0B0C, 0x7F);
    QueueRequest17(tx, 0x11111111, 0x22222222, 0x33333333, (i16)0x4444, 0x55, 0x66666666);
    QueueRequestArgs25(tx, 1, 2, 3, 4, 5);
    EnqueueObjectInteraction(tx, 0xAB, 0xDEADBEEF, (i16)0x1234, 0x0BADF00D,
                             0x0C0FFEE0, 0x01, 0x02, 0x03);
    QueueRequestCoord27(tx, 100, 200, 300, 0x40, 0x80);

    tx.FlushSendQueue(); // hands all five frames to the mock transport

    g_wire = nullptr;
    return wire;
}

} // namespace

// ---------------------------------------------------------------------------
// Full flow: build varied commands, enqueue, flush through the mock transport,
// parse them back on the receiving peer into command structs, and verify they
// arrive identical and in sequence order.
// ---------------------------------------------------------------------------
TEST(SimCommandE2E, BurstRoundtripInSequenceOrder) {
    std::vector<CommandPacket> wire = BuildBurst();
    CHECK_EQ((int)wire.size(), 5);

    // Sender stamped sequential Counts 1..5 and ring slots 1..5.
    for (size_t i = 0; i < wire.size(); ++i) {
        CHECK_EQ(wire[i].count(), (u32)(i + 1));
        CHECK_EQ(wire[i].cmd_id(), (u32)(i + 1));
    }

    // Receiving peer parses the frames back.
    std::vector<Decoded> decoded;
    g_decoded = &decoded;
    CommandQueue rx;
    rx.set_standalone(false);
    for (int op = 0; op < (int)kNumOpcodes; ++op)
        rx.set_handler((u8)op, &RecordHandler);

    for (const auto& frame : wire)
        CHECK_EQ(rx.StoreReceivedPacket(frame), 0);
    rx.ExecCommands();
    g_decoded = nullptr;

    CHECK_EQ((int)decoded.size(), 5);

    // Sequence order preserved: Counts strictly 1,2,3,4,5.
    for (size_t i = 0; i < decoded.size(); ++i)
        CHECK_EQ(decoded[i].count, (u32)(i + 1));

    // Opcodes arrive in the order they were built.
    CHECK_EQ(decoded[0].opcode, (u8)16);
    CHECK_EQ(decoded[1].opcode, (u8)17);
    CHECK_EQ(decoded[2].opcode, (u8)25);
    CHECK_EQ(decoded[3].opcode, (u8)11);
    CHECK_EQ(decoded[4].opcode, (u8)27);

    // Field-level: opcode 16 payload round-trips bit-exact.
    const Decoded& d16 = decoded[0];
    auto rd32 = [](const u8* p) {
        return (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24);
    };
    CHECK_EQ(rd32(d16.payload + 0x10), (u32)0x01020304);
    CHECK_EQ(rd32(d16.payload + 0x14), (u32)0x05060708);
    CHECK_EQ(d16.payload[0x1C], (u8)0x7F);
    CHECK_EQ(rd32(d16.payload + 0x1D), (u32)0x090A0B0C);

    // opcode 11 object interaction fields.
    const Decoded& d11 = decoded[3];
    CHECK_EQ(d11.payload[0x14], (u8)0xAB);
    CHECK_EQ(rd32(d11.payload + 0x15), (u32)0xDEADBEEF);
    CHECK_EQ((u16)(d11.payload[0x1D] | (d11.payload[0x1E] << 8)), (u16)0x1234);
    CHECK_EQ(d11.payload[0x23], (u8)0x01);
    CHECK_EQ(d11.payload[0x25], (u8)0x03);

    // The receiver advanced its last-requested Count in order to 5.
    CHECK_EQ(rx.last_req_count(), (u32)5);

    // Computed wire lengths match the size table.
    CHECK_EQ(decoded[0].len, ComputePacketSizeFixed(16));
    CHECK_EQ(decoded[3].len, ComputePacketSizeFixed(11));
}

// ---------------------------------------------------------------------------
// DETERMINISM: run the whole encode sequence twice and assert byte-identical
// output across the entire burst.
// ---------------------------------------------------------------------------
TEST(SimCommandE2E, EncodeIsDeterministic) {
    std::vector<CommandPacket> a = BuildBurst();
    std::vector<CommandPacket> b = BuildBurst();
    CHECK_EQ(a.size(), b.size());
    bool identical = a.size() == b.size();
    for (size_t i = 0; i < a.size() && i < b.size(); ++i) {
        if (std::memcmp(a[i].bytes, b[i].bytes, kPacketStride) != 0)
            identical = false;
    }
    CHECK(identical);
}

// ---------------------------------------------------------------------------
// Delta packet round-trip through a state-request command: encode new-vs-old on
// the sender, ship it, and reproduce `new` from `old` on the receiver.
// ---------------------------------------------------------------------------
TEST(SimCommandE2E, DeltaStatePacketRoundtrip) {
    // Sender entity (old) and the new values to push.
    u8 oldEntity[64];
    std::memset(oldEntity, 0, sizeof(oldEntity));
    oldEntity[1]  = 50;
    *reinterpret_cast<u16*>(oldEntity + 4)  = 1000;
    *reinterpret_cast<u32*>(oldEntity + 8)  = 0x20000000;

    DeltaWriter dw;
    dw.BeginDeltaPacket(oldEntity, 0xABCD);
    u8  nb = 200;                 // +150
    u16 nw = 900;                 // -100
    u32 nd = 0x20000050;          // +0x50
    CHECK_EQ(dw.AppendDeltaField(1, 1, 1, &nb), 0);
    CHECK_EQ(dw.AppendDeltaField(2, 1, 4, &nw), 0);
    CHECK_EQ(dw.AppendDeltaField(4, 1, 8, &nd), 0);

    std::vector<CommandPacket> wire;
    g_wire = &wire;
    CommandQueue tx;
    tx.set_standalone(false);
    QueueRequestState22(tx, dw);
    tx.FlushSendQueue();
    g_wire = nullptr;
    CHECK_EQ((int)wire.size(), 1);

    // Receiver: extract the delta payload from the opcode-22 frame and apply it
    // to a fresh copy of the OLD entity, expecting it to become NEW.
    const CommandPacket& frame = wire[0];
    CHECK_EQ(frame.opcode(), (u8)22);
    u8 fieldCount = frame.bytes[0x10];
    CHECK_EQ(fieldCount, (u8)3);

    u8 rxEntity[64];
    std::memset(rxEntity, 0, sizeof(rxEntity));
    rxEntity[1]  = 50;
    *reinterpret_cast<u16*>(rxEntity + 4)  = 1000;
    *reinterpret_cast<u32*>(rxEntity + 8)  = 0x20000000;

    DeltaWriter::ApplyDelta(frame.bytes + 0x11, fieldCount, rxEntity);
    CHECK_EQ(rxEntity[1], nb);
    CHECK_EQ(*reinterpret_cast<u16*>(rxEntity + 4), nw);
    CHECK_EQ(*reinterpret_cast<u32*>(rxEntity + 8), nd);
}
