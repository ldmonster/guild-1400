#include "tests/framework/test.h"

#include "sim/command.h"
#include "sim/command_pending.h"
#include "sim/command_receive.h"
#include "sim/command_builders2.h"
#include "net/transport.h"
#include "shim/INetSocket.h"

#include <cstring>
#include <deque>
#include <vector>

using namespace guild;
using namespace guild::sim;

// ---------------------------------------------------------------------------
// Mock loopback socket: bytes written with send() are read back by recv() in
// order, byte-for-byte, honoring the non-blocking would-block contract (recv of
// an empty queue returns 0). This lets the real NetTransport reassemble frames.
// ---------------------------------------------------------------------------
namespace {
class LoopSocket : public guild::shim::INetSocket {
public:
    bool connect(const char*, std::uint16_t) override { open_ = true; return true; }
    void close() override { open_ = false; }
    bool connected() const override { return open_; }
    int send(const void* data, std::size_t n) override {
        const u8* p = static_cast<const u8*>(data);
        for (std::size_t i = 0; i < n; ++i) q_.push_back(p[i]);
        return static_cast<int>(n);
    }
    int recv(void* dst, std::size_t n) override {
        if (q_.empty()) return 0;              // would-block
        std::size_t got = 0;
        u8* d = static_cast<u8*>(dst);
        while (got < n && !q_.empty()) { d[got++] = q_.front(); q_.pop_front(); }
        return static_cast<int>(got);
    }
private:
    bool open_ = false;
    std::deque<u8> q_;
};
} // namespace

// ---------------------------------------------------------------------------
// A tiny "world": one record whose fields the apply handlers mutate. The verify
// compares the world built by the network round-trip against a world built by a
// direct apply of the same logical commands.
// ---------------------------------------------------------------------------
namespace {
struct World {
    i32  pair_a = 0, pair_b = 0;   // opcode-35 pair lands here
    i32  single = 0;               // opcode-58 single
    u8   speech[512] = {0};        // opcode-28 reassembled speech buffer
    u32  speech_len = 0;
    int  group_begin_seen = 0;
    int  group_cmd_seen = 0;
    int  group_end_seen = 0;
};
World* g_world = nullptr;
PendingState* g_apply_pending = nullptr;  // the driver's pending buffer (for op 28)

void H_Pair35(CommandPacket& pkt, AckEntry*) {
    g_world->pair_a = static_cast<i32>(pkt.get32(0x10));
    g_world->pair_b = static_cast<i32>(pkt.get32(0x14));
}
void H_Single58(CommandPacket& pkt, AckEntry*) {
    g_world->single = static_cast<i32>(pkt.get32(0x10));
}
// opcode 28: a large speech command whose payload was carried by fragments and
// reassembled into the driver's pending buffer. The handler reads the reassembled
// bytes (mirrors ExSendCutInfo reading unk_1077B60).
void H_Buffer28(CommandPacket&, AckEntry*) {
    g_world->speech_len = g_apply_pending->reasm_len;
    std::memcpy(g_world->speech, g_apply_pending->reasm,
                (g_apply_pending->reasm_len < sizeof(g_world->speech))
                    ? g_apply_pending->reasm_len : sizeof(g_world->speech));
}
// group frames (opcode 1 = accepted begin/end after ExecCommandGroup; 0x21 = the
// framed command). We count them to prove the group dispatched.
void H_GroupBegin1(CommandPacket&, AckEntry*) { g_world->group_begin_seen++; }
void H_GroupCmd(CommandPacket&, AckEntry*)    { g_world->group_cmd_seen++; }

void InstallHandlers(ReceiveDriver& d) {
    d.set_handler(35, &H_Pair35);
    d.set_handler(58, &H_Single58);
    d.set_handler(28, &H_Buffer28);
    d.set_handler(1,  &H_GroupBegin1);   // accepted begin+end frames become op 1
    d.set_handler(0x21, &H_GroupCmd);    // the framed command
}
} // namespace

// Frame a 153-byte packet onto the wire through the transport (partial-send aware).
static void SendFramed(guild::net::NetTransport& net, CommandPacket& pkt) {
    // Ensure the on-wire length word is set (ComputePacketSize) before sending.
    pkt.set_len(ComputePacketSize(pkt));
    net.SetSendBuffer(pkt.bytes);
    // Pump until the whole frame is flushed.
    for (int guard = 0; guard < 1000; ++guard) {
        net.SendPacket();
        if (net.CompletedThisCall()) break;
    }
}

// ---------------------------------------------------------------------------
// e2e: fragmented command + a command group cross a mock transport, get
// reassembled + dispatched on the far side, and the resulting world equals a
// direct apply of the same commands.
// ---------------------------------------------------------------------------
TEST(SimCmdRecvE2E, FragmentedStreamAndGroup) {
    // --- sender side: build packets (pair, single, fragmented speech, group) ---
    CommandQueue sendQ;
    sendQ.set_standalone(false);
    PendingState sendPending;

    // Speech payload to fragment (250 bytes -> several opcode-7 fragments).
    u8 speech[250];
    for (int i = 0; i < 250; ++i) speech[i] = static_cast<u8>((i * 13 + 5) & 0xFF);

    // Opcode-28 header struct (248 bytes) — content is irrelevant to the test, but
    // the reassembled buffer reconstructs [header(248) || speech(250)] minus the
    // 248 header in front... actually the staged block is header||speech; the
    // reassembled bytes equal that whole block. We capture the block to compare.
    u8 header28[0xF8];
    for (int i = 0; i < 0xF8; ++i) header28[i] = static_cast<u8>(0xF8 - i);

    // Enqueue the opcode-28 header (links to its fragment chain) and the fragments.
    i32 h28 = QueueRequestBuffer28(sendQ, sendPending, header28, speech, 250,
                                   /*personFound=*/true);
    CHECK(h28 >= 0);
    // The expected reassembled block = header28 || speech, total 248+250 = 498.
    std::vector<u8> expectedBlock;
    expectedBlock.insert(expectedBlock.end(), header28, header28 + 0xF8);
    expectedBlock.insert(expectedBlock.end(), speech, speech + 250);
    CHECK_EQ(expectedBlock.size(), 498u);

    // Two simple commands.
    QueueRequestPair35(sendQ, 0x1234, 0x5678);
    QueueRequestSingle58(sendQ, 0x0BADF00D);

    // A command group: begin(5), one framed command(0x21), end(6).
    CommandPacket gBegin{}; gBegin.opcode() = 5;
    CommandPacket gCmd{};   gCmd.opcode()   = 0x21;
    CommandPacket gEnd{};   gEnd.opcode()   = 6;
    sendQ.EnqueuePacket(gBegin);
    sendQ.EnqueuePacket(gCmd);
    sendQ.EnqueuePacket(gEnd);

    // Collect everything on the pending-send list in order. The CommandQueue does
    // not expose ring_next; instead we know the Counts are contiguous from 1, so
    // walk the ring by Count. send_count() is the highest Count assigned.
    u32 n = sendQ.send_count();
    std::vector<CommandPacket> wire;
    for (u32 c = 1; c <= n; ++c) {
        CommandPacket& slot = sendQ.ring_slot(c & kSeqMask);
        wire.push_back(slot);
    }
    CHECK(wire.size() == n);

    // --- transport: a connected loopback pair (one socket; send == recv queue) ---
    LoopSocket sock;
    guild::net::NetTransport net(&sock);
    CHECK(net.ConnectToServer("loop", 1));

    // Send every framed packet.
    for (auto& w : wire) SendFramed(net, w);

    // --- receiver side: pull frames, reassemble + dispatch ---
    ReceiveDriver drv;
    InstallHandlers(drv);
    g_apply_pending = &drv.pending();

    // Drain the transport into the received list.
    u8 rxBuf[256];
    for (int guard = 0; guard < 10000; ++guard) {
        int rc = drv.ReceiveAndQueue(net, rxBuf, sizeof(rxBuf));
        (void)rc;
        // stop when nothing more is pending on the wire
        // (ReceiveAndQueue returns after the wire drains; check completion)
        // We loop until received_count stabilizes AND the socket queue is empty.
        if (!net.CompletedThisCall()) {
            // One more attempt to be sure the wire is empty.
            break;
        }
    }

    // Network-applied world.
    World netWorld;
    g_world = &netWorld;
    drv.ExecReceivedCommands();

    // --- direct-apply reference world (no transport, no fragmentation) ---
    World directWorld;
    g_world = &directWorld;
    // pair / single: apply the same field semantics directly.
    {
        CommandPacket p{}; p.opcode() = 35; p.put32(0x10, 0x1234); p.put32(0x14, 0x5678);
        H_Pair35(p, nullptr);
    }
    {
        CommandPacket p{}; p.opcode() = 58; p.put32(0x10, 0x0BADF00D);
        H_Single58(p, nullptr);
    }
    // speech: the reassembled buffer equals header28||speech; set it directly.
    directWorld.speech_len = static_cast<u32>(expectedBlock.size());
    std::memcpy(directWorld.speech, expectedBlock.data(), expectedBlock.size());
    // group: ExecCommandGroup stamps begin+end opcode 1. Per the binary
    // (gilde.exe 0x494088, edx-gate disasm 0x494166: ExecCommandGroup returns 1 so
    // edx stays 1), on group close BOTH the reprocessed BEGIN frame (now op 1) and
    // the END frame (op 1) dispatch, plus the framed command (0x21). The begin and
    // end frames share opcode 1 => H_GroupBegin1 fires twice => group_begin_seen==2.
    directWorld.group_begin_seen = 2; // begin frame (op1) + end frame (op1)
    directWorld.group_cmd_seen   = 1;

    // --- verify the network round-trip matches the direct apply ---
    CHECK_EQ(netWorld.pair_a, directWorld.pair_a);
    CHECK_EQ(netWorld.pair_b, directWorld.pair_b);
    CHECK_EQ(netWorld.single, directWorld.single);
    CHECK_EQ(netWorld.speech_len, directWorld.speech_len);
    bool speechOk = (netWorld.speech_len == directWorld.speech_len);
    for (u32 i = 0; speechOk && i < netWorld.speech_len; ++i)
        if (netWorld.speech[i] != directWorld.speech[i]) speechOk = false;
    CHECK(speechOk);
    CHECK_EQ(netWorld.group_begin_seen, directWorld.group_begin_seen);
    CHECK_EQ(netWorld.group_cmd_seen, directWorld.group_cmd_seen);
}
