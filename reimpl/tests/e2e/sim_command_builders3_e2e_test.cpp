#include "sim/command.h"
#include "sim/command_builders3.h"
#include "test.h"

#include <cstring>

using namespace guild;
using namespace guild::sim;

// End-to-end: drive a sequence of RequestBuildOp* builders through the real
// CommandQueue in standalone mode. Each builder enqueues onto the pending-send
// list; FlushSendQueue (standalone) applies each packet locally by moving it onto
// the received list via StoreReceivedPacket. We then walk the received list and
// confirm opcode order, the EnqueuePacket-stamped headers (monotonic Count, ring
// cmdId), and that payloads survive the build -> ring -> flush -> received path
// byte-for-byte.

TEST(SimCmdBuilders3E2E, BuildFlushReceiveRoundtrip) {
    CommandQueue q; q.Init();

    // Build a mixed batch of packets.
    i32 a[5] = { 11, 22, 33, 44, 55 };
    u8  blob40[40]; for (int i = 0; i < 40; ++i) blob40[i] = (u8)(i + 1);

    i32 idx66 = RequestBuildOp66(q, 0xAABBCCDD);
    i32 idx71 = RequestBuildOp71(q, 1, 2, 999 /*dropped*/, 4);
    i32 idx83 = RequestBuildOp83(q, a);
    i32 idx86 = RequestBuildOp86Blob(q, blob40);
    i32 idx38 = RequestSendCutInfo(q, 7, 8, 1234);

    // Ring indices are assigned 1,2,3,4,5 in order.
    CHECK_EQ(idx66, 1);
    CHECK_EQ(idx71, 2);
    CHECK_EQ(idx83, 3);
    CHECK_EQ(idx86, 4);
    CHECK_EQ(idx38, 5);
    CHECK_EQ((int)q.send_count(), 5);

    // Five packets are queued for send.
    CHECK(q.pending_head() != nullptr);

    // Standalone flush -> apply locally onto the received list.
    CHECK_EQ(q.FlushSendQueue(), 0);
    CHECK(q.pending_head() == nullptr);

    // Walk the received list; StoreReceivedPacket appends in flush order, so the
    // list order matches enqueue order (opcodes 66,71,83,86,38).
    const u8 expectOps[5] = { 66, 71, 83, 86, 38 };
    CommandPacket* node = q.received_head();
    int n = 0;
    while (node) {
        CHECK(n < 5);
        CHECK_EQ(node->opcode(), expectOps[n]);
        // Count is monotonic 1..5 as stamped at enqueue time.
        CHECK_EQ((int)node->count(), n + 1);
        // cmdId is the ring slot index (1..5).
        CHECK_EQ((int)node->cmd_id(), n + 1);
        // len was recomputed by ComputePacketSize and must be > 0.
        CHECK(node->len() > 0);

        switch (node->opcode()) {
            case 66:
                CHECK_EQ(node->get32(0x10), 0xAABBCCDDu);
                CHECK_EQ((int)node->len(), 20);
                break;
            case 71:
                CHECK_EQ((int)node->get32(0x10), 1);
                CHECK_EQ((int)node->get32(0x14), 2);
                CHECK_EQ((int)node->get32(0x18), 4);   // a4, not the dropped a3
                CHECK_EQ((int)node->len(), 28);
                break;
            case 83:
                for (int i = 0; i < 5; ++i)
                    CHECK_EQ((int)node->get32(0x10 + 4 * i), (i + 1) * 11);
                CHECK_EQ((int)node->len(), 36);
                break;
            case 86:
                for (int i = 0; i < 40; ++i)
                    CHECK_EQ((int)node->bytes[0x10 + i], i + 1);
                CHECK_EQ((int)node->len(), 56);
                break;
            case 38:
                CHECK_EQ((int)node->get32(0x10), 7);
                CHECK_EQ((int)node->get32(0x14), 8);
                CHECK_EQ((int)node->len(), 24);
                break;
            default: CHECK(false); break;
        }

        // Recycle the node so the next iteration's link is read before unlinking.
        CommandPacket* next = nullptr;
        // received list is threaded via the pool links; UnlinkReceivedPacket reads
        // them, so capture by re-reading head after unlink instead.
        (void)next;
        q.UnlinkReceivedPacket(node);
        node = q.received_head();
        ++n;
    }
    CHECK_EQ(n, 5);
    CHECK(q.received_head() == nullptr);
}

TEST(SimCmdBuilders3E2E, DisconnectedRejectsEnqueue) {
    CommandQueue q; q.Init();
    q.set_disconnected(true);
    // All builders early-out through EnqueuePacket's disconnected latch (-1).
    CHECK_EQ(RequestBuildOp66(q, 1), -1);
    CHECK_EQ(RequestBuildOp87(q, 2), -1);
    i32 src[5] = {0,0,0,0,0};
    CHECK_EQ(RequestBuildOp83(q, src), -1);
    CHECK_EQ((int)q.send_count(), 0);
    CHECK(q.pending_head() == nullptr);
}
