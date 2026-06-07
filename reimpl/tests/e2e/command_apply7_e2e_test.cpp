#include "tests/framework/test.h"
#include "sim/command_apply7.h"
#include "sim/command.h"

#include <cstring>
#include <vector>

using namespace guild;
using namespace guild::sim;

// End-to-end: build packets through the command_apply7 builders, drain the
// pending-send list (standalone mode applies locally into the received list),
// then ExecCommands dispatches each opcode to an installed handler. The handler
// snapshots the delivered payload bytes so we can prove a builder -> ring ->
// flush -> received -> exec roundtrip preserves the wire layout exactly.

namespace {
struct Captured {
    u8  opcode;
    u16 len;
    u8  payload[guild::sim::kPacketStride];
};
std::vector<Captured>* g_sink = nullptr;

void CaptureHandler(CommandQueue&, CommandPacket& pkt, AckEntry*) {
    if (!g_sink) return;
    Captured c{};
    c.opcode = pkt.opcode();
    c.len    = pkt.len();
    std::memcpy(c.payload, pkt.bytes, guild::sim::kPacketStride);
    g_sink->push_back(c);
}
}

TEST(CmdApply7E2E, BuilderFlushExecRoundtrip) {
    CommandQueue q;
    q.Init();
    q.set_standalone(true);

    // Route opcodes used below to the capture handler.
    for (u8 op : {13u, 21u, 47u, 78u, 90u})
        q.set_handler(op, &CaptureHandler);

    std::vector<u8> blob(31);
    for (u32 i = 0; i < 31; ++i) blob[i] = static_cast<u8>(i * 3 + 1);

    EnqueueCmd13(q, 0xCAFEBABE, 0x12345678);
    QueueRequestBlob21(q, 0x0BADF00D, static_cast<i16>(0x1357), blob.data());
    QueueRequestString47(q, 0x10, 0x20, "Marketplace", 0x30);
    RequestBuildOp78DualStr(q, "Alpha", 0x1111, "Beta", 0x2222);
    RequestBuildOp90(q, 0x777, 0x888);

    // 5 packets staged onto the pending list.
    CHECK_EQ(q.send_count(), 5u);
    CHECK(q.pending_head() != nullptr);

    // Standalone flush applies each packet locally into the received list.
    int rc = q.FlushSendQueue();
    CHECK_EQ(rc, 0);
    CHECK(q.pending_head() == nullptr);
    CHECK(q.received_head() != nullptr);

    std::vector<Captured> sink;
    g_sink = &sink;
    q.ExecCommands();
    g_sink = nullptr;

    CHECK_EQ(sink.size(), static_cast<std::size_t>(5));

    // Packet 0: opcode 13.
    CHECK_EQ(sink[0].opcode, 13u);
    CHECK_EQ(sink[0].len, 24u);
    CHECK_EQ(static_cast<u32>(sink[0].payload[0x10] | (sink[0].payload[0x11] << 8)
            | (sink[0].payload[0x12] << 16) | (sink[0].payload[0x13] << 24)), 0xCAFEBABEu);

    // Packet 1: opcode 21 blob preserved.
    CHECK_EQ(sink[1].opcode, 21u);
    CHECK_EQ(sink[1].len, 57u);
    for (u32 i = 0; i < 31; ++i) CHECK_EQ(sink[1].payload[0x16 + i], blob[i]);

    // Packet 2: opcode 47 string preserved.
    CHECK_EQ(sink[2].opcode, 47u);
    CHECK_EQ(sink[2].len, 60u);
    CHECK(std::memcmp(&sink[2].payload[0x1C], "Marketplace", 12) == 0);

    // Packet 3: opcode 78 dual strings + ints preserved.
    CHECK_EQ(sink[3].opcode, 78u);
    CHECK(std::memcmp(&sink[3].payload[0x10], "Alpha", 6) == 0);
    CHECK(std::memcmp(&sink[3].payload[0x30], "Beta", 5) == 0);

    // Packet 4: opcode 90.
    CHECK_EQ(sink[4].opcode, 90u);
    CHECK_EQ(sink[4].len, 24u);
}

TEST(CmdApply7E2E, GameSpeedFlowEnqueuesOpcode32) {
    g_gameSpeed = 1;
    CommandQueue q;
    q.Init();
    q.set_standalone(true);

    std::vector<Captured> sink;
    g_sink = &sink;
    q.set_handler(32, &CaptureHandler);

    IncreaseGameSpeed(q);   // {2}
    SetGameSpeed(q, 4);     // {4}
    DecreaseGameSpeed(q);   // {0}

    CHECK_EQ(q.send_count(), 3u);
    q.FlushSendQueue();
    q.ExecCommands();
    g_sink = nullptr;

    CHECK_EQ(sink.size(), static_cast<std::size_t>(3));
    for (const auto& c : sink) {
        CHECK_EQ(c.opcode, 32u);
        CHECK_EQ(c.payload[0x10], 18u);  // flag arg
    }
    // Encoded levels 2, 4, 0 (a3 dropped where applicable).
    CHECK_EQ(static_cast<u32>(sink[0].payload[0x11]), 2u);
    CHECK_EQ(static_cast<u32>(sink[1].payload[0x11]), 4u);
    CHECK_EQ(static_cast<u32>(sink[2].payload[0x11]), 0u);
    g_gameSpeed = 0;
}
