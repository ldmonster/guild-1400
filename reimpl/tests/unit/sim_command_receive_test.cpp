#include "tests/framework/test.h"

#include "sim/command.h"
#include "sim/command_pending.h"
#include "sim/command_receive.h"
#include "sim/command_builders2.h"

#include <cstring>
#include <vector>

using namespace guild;
using namespace guild::sim;

// ---------------------------------------------------------------------------
// StagePendingBlock — block layout (length word + data), reject paths.
// ---------------------------------------------------------------------------
TEST(SimCmdRecv, StagePendingBlockLayout) {
    PendingState st;
    u8 data[100];
    for (int i = 0; i < 100; ++i) data[i] = static_cast<u8>(i + 1);

    CHECK_EQ(StagePendingBlock(st, 100, data), 1);
    CHECK_EQ(st.staged, 102u);                 // len + 2
    CHECK_EQ(st.block_len(), 100);             // word_1077F60
    CHECK_EQ(st.block[2], 1);                  // first data byte after the word
    CHECK_EQ(st.block[101], 100);              // last data byte

    // Already staged => reject.
    CHECK_EQ(StagePendingBlock(st, 10, data), 0);

    // Oversize => reject.
    PendingState st2;
    CHECK_EQ(StagePendingBlock(st2, 0x3FF, data), 0);
    CHECK_EQ(st2.staged, 0u);
}

// ---------------------------------------------------------------------------
// GeneratePendingPackets — fragment count, opcode 7, +12 Count chain.
// ---------------------------------------------------------------------------
TEST(SimCmdRecv, GeneratePendingFragments) {
    CommandQueue q;
    q.set_standalone(false);                   // keep packets on the pending list
    PendingState st;

    // 300 data bytes -> staged 302 -> ceil(302/128) = 3 fragments.
    u8 data[300];
    for (int i = 0; i < 300; ++i) data[i] = static_cast<u8>(i);
    StagePendingBlock(st, 300, data);
    u32 firstCount = GeneratePendingPackets(q, st);
    CHECK_EQ(st.staged, 0u);                   // cleared

    // 3 fragments were generated: their Counts are firstCount, +1, +2. We verify
    // them directly from the ring (the pending list is threaded via the queue's
    // private ring links, but Counts are contiguous from firstCount).
    CHECK(firstCount != 0);
    CommandPacket& f0 = q.ring_slot((firstCount) & kSeqMask);
    CommandPacket& f1 = q.ring_slot((firstCount + 1) & kSeqMask);
    CommandPacket& f2 = q.ring_slot((firstCount + 2) & kSeqMask);
    CHECK_EQ(f0.opcode(), kOpFragment);
    CHECK_EQ(f1.opcode(), kOpFragment);
    CHECK_EQ(f2.opcode(), kOpFragment);
    CHECK_EQ(f0.count(), firstCount);
    // +12 chain: f0 -> f1 -> f2 -> 0
    CHECK_EQ(f0.get32(12), f1.count());
    CHECK_EQ(f1.get32(12), f2.count());
    CHECK_EQ(f2.get32(12), 0u);
    // First fragment carries the length word at +16 and data at +18.
    CHECK_EQ(f0.get16(16), 300);
    CHECK_EQ(f0.bytes[18], data[0]);
}

// File-scope linked-list walker over a vector of packets (matches-by-pointer).
namespace {
std::vector<CommandPacket>* g_walk_list = nullptr;
CommandPacket* WalkNext(CommandPacket* p) {
    if (!g_walk_list) return nullptr;
    for (std::size_t i = 0; i + 1 < g_walk_list->size(); ++i)
        if (&(*g_walk_list)[i] == p) return &(*g_walk_list)[i + 1];
    return nullptr;
}
} // namespace

// ---------------------------------------------------------------------------
// ReassembleReceived — round-trip a staged block through fragments back to bytes.
// ---------------------------------------------------------------------------
TEST(SimCmdRecv, ReassembleRoundTrip) {
    CommandQueue q;
    q.set_standalone(false);
    PendingState gen;   // producer-side staging

    // 400 data bytes -> staged 402 -> 4 fragments.
    u8 data[400];
    for (int i = 0; i < 400; ++i) data[i] = static_cast<u8>(i * 7 + 3);
    StagePendingBlock(gen, 400, data);

    // Header packet that owns the block: enqueue it + link +12 to the fragments.
    CommandPacket header{};
    header.opcode() = 0x20; // any non-fragment opcode (here a sync-shaped opcode)
    i32 hslot = EmitWithPendingBlock(q, gen, header);
    CHECK(hslot >= 0);
    CommandPacket& hp = q.ring_slot(static_cast<u32>(hslot));
    u32 firstFragCount = hp.get32(12);
    CHECK(firstFragCount != 0);

    // Build a received-list of the fragment packets (as if they arrived). Order
    // them out of arrival order to prove the reassembler matches by Count.
    std::vector<CommandPacket> recv;
    for (u32 k = 0; k < 4; ++k)
        recv.push_back(q.ring_slot((firstFragCount + (3 - k)) & kSeqMask)); // reversed

    // Provide a simple singly-linked walk over `recv` for the reassembler.
    g_walk_list = &recv;

    PendingState reasm;
    int consumed = ReassembleReceived(reasm, hp, &recv[0], &WalkNext);
    CHECK_EQ(consumed, 4);
    CHECK_EQ(reasm.reasm_len, 400u);
    // reasm[0..399] must equal the original data.
    bool ok = true;
    for (int i = 0; i < 400; ++i)
        if (reasm.reasm[i] != data[i]) { ok = false; break; }
    CHECK(ok);
}

// ---------------------------------------------------------------------------
// CheckReassemblyComplete — complete vs missing-fragment.
// ---------------------------------------------------------------------------
TEST(SimCmdRecv, CheckReassemblyComplete) {
    CommandQueue q;
    q.set_standalone(false);
    PendingState gen;
    u8 data[300];
    for (int i = 0; i < 300; ++i) data[i] = static_cast<u8>(i);
    StagePendingBlock(gen, 300, data);
    CommandPacket header{};
    header.opcode() = 0x20;
    i32 hslot = EmitWithPendingBlock(q, gen, header);
    CommandPacket& hp = q.ring_slot(static_cast<u32>(hslot));
    u32 fc = hp.get32(12);

    std::vector<CommandPacket> full;
    for (u32 k = 0; k < 3; ++k) full.push_back(q.ring_slot((fc + k) & kSeqMask));

    g_walk_list = &full;
    CHECK_EQ(CheckReassemblyComplete(hp, &full[0], &WalkNext), 1);

    // Drop the last fragment -> incomplete.
    std::vector<CommandPacket> partial(full.begin(), full.end() - 1);
    g_walk_list = &partial;
    CHECK_EQ(CheckReassemblyComplete(hp, &partial[0], &WalkNext), 0);

    // A skip frame (opcode 7) is always "complete".
    CommandPacket skip{};
    skip.opcode() = 7;
    CHECK_EQ(CheckReassemblyComplete(skip, &full[0], &WalkNext), 1);
}

// ---------------------------------------------------------------------------
// ExecCommandGroup — accept (frames -> opcode 1) / reject (span -> opcode 2).
// ---------------------------------------------------------------------------
static int g_gate_reject = 0;
static int GateRejectAll(CommandPacket&) { return g_gate_reject; }

TEST(SimCmdRecv, ExecCommandGroupAccept) {
    ReceiveDriver d;
    d.set_group_gate(GateRejectAll);
    g_gate_reject = 0; // accept

    CommandPacket begin{}; begin.opcode() = 5;
    CommandPacket mid{};   mid.opcode()   = 0x21; // some command
    CommandPacket end{};   end.opcode()   = 6;
    d.DeliverPacket(begin);
    d.DeliverPacket(mid);
    d.DeliverPacket(end);

    CommandPacket* head = d.received_head();
    CHECK(head != nullptr);
    d.ExecCommandGroup(head);
    // begin and end frames become opcode 1; mid is untouched by the group stamper.
    CommandPacket* b = head;
    CommandPacket* m = ReceiveDriver::NextOf(b);
    CommandPacket* e = ReceiveDriver::NextOf(m);
    CHECK_EQ(b->opcode(), 1);
    CHECK_EQ(e->opcode(), 1);
    CHECK_EQ(m->opcode(), 0x21);
}

TEST(SimCmdRecv, ExecCommandGroupReject) {
    ReceiveDriver d;
    d.set_group_gate(GateRejectAll);
    g_gate_reject = 1; // reject

    CommandPacket begin{}; begin.opcode() = 5;
    CommandPacket mid{};   mid.opcode()   = 0x21;
    CommandPacket end{};   end.opcode()   = 6;
    d.DeliverPacket(begin);
    d.DeliverPacket(mid);
    d.DeliverPacket(end);

    CommandPacket* head = d.received_head();
    d.ExecCommandGroup(head);
    // Whole span [begin..end] stamped opcode 2.
    CommandPacket* b = head;
    CommandPacket* m = ReceiveDriver::NextOf(b);
    CommandPacket* e = ReceiveDriver::NextOf(m);
    CHECK_EQ(b->opcode(), 2);
    CHECK_EQ(m->opcode(), 2);
    CHECK_EQ(e->opcode(), 2);
}

// ---------------------------------------------------------------------------
// New builders — wire bytes + ComputePacketSize.
// ---------------------------------------------------------------------------
TEST(SimCmdRecv, Builders2WireBytes) {
    CommandQueue q;
    q.set_standalone(false);

    i32 s37 = QueueRequestQuad37(q, 0x11111111, 0x22222222, 0x33333333, 0x44444444);
    CommandPacket& p37 = q.ring_slot(static_cast<u32>(s37));
    CHECK_EQ(p37.opcode(), 37);
    CHECK_EQ(p37.get32(0x10), 0x11111111u); // a1
    CHECK_EQ(p37.get32(0x14), 0x22222222u); // a2
    CHECK_EQ(p37.get32(0x18), 0x44444444u); // a4 (a3 dropped)
    // a3 (0x33333333) must NOT appear at +0x1C for the 3-field quad.
    CHECK_EQ(p37.get32(0x1C), 0u);

    i32 s46 = QueueRequestQuad46(q, 0xA, 0xB, 0xC, 0xD);
    CommandPacket& p46 = q.ring_slot(static_cast<u32>(s46));
    CHECK_EQ(p46.opcode(), 46);
    CHECK_EQ(p46.get32(0x10), 0xAu);
    CHECK_EQ(p46.get32(0x14), 0xBu);
    CHECK_EQ(p46.get32(0x18), 0xDu); // a4
    CHECK_EQ(p46.get32(0x1C), 0xCu); // a3 on the wire

    i32 s55 = QueueRequestFlag55(q, 0x7777, 0x5, "hello");
    CommandPacket& p55 = q.ring_slot(static_cast<u32>(s55));
    CHECK_EQ(p55.opcode(), 55);
    CHECK_EQ(p55.get32(0x10), 0x7777u);
    CHECK(std::memcmp(p55.bytes + 0x14, "hello", 6) == 0);
    CHECK_EQ(p55.bytes[0x44], 5);

    i32 s30 = QueueRequestPerm30(q, 1, 2, 3, 4, /*apptValid=*/false);
    CHECK_EQ(s30, -1);                       // future-check failed -> -1

    i32 s30b = QueueRequestPerm30(q, 1, 2, 3, 4, /*apptValid=*/true);
    CommandPacket& p30 = q.ring_slot(static_cast<u32>(s30b));
    CHECK_EQ(p30.opcode(), 30);
    CHECK_EQ(p30.get32(0x10), 1u);
    CHECK_EQ(p30.get16(0x1C), 4);

    // Op 7 (fragment) size is the default 145.
    CommandPacket fr{}; fr.opcode() = 7;
    CHECK_EQ(ComputePacketSize(fr), 145);
}
