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
    // ReassembleReceived returns dword_11AA478 (the reassembled length) on success
    // (gilde.exe 0x49377c LABEL_19), NOT a fragment count.
    int ret = ReassembleReceived(reasm, hp, &recv[0], &WalkNext);
    CHECK_EQ(ret, 400);
    CHECK_EQ(reasm.reasm_len, 400u);
    // reasm[0..399] must equal the original data.
    bool ok = true;
    for (int i = 0; i < 400; ++i)
        if (reasm.reasm[i] != data[i]) { ok = false; break; }
    CHECK(ok);
}

// ---------------------------------------------------------------------------
// HARDENING (wave-11): malformed reassembly — a first fragment that declares a
// huge reasm_len (+16, up to 65535) followed by a long fragment chain would drive
// the 128-byte-per-chunk copy far past the fixed 1536-byte reasm buffer (heap/
// buffer overflow). The end-of-buffer bound must stop the copy. A valid block is
// capped at kPendingMaxBlock so it never reaches this bound (round-trip unchanged).
// ---------------------------------------------------------------------------
TEST(SimCmdRecv, Malformed_Reassemble_HugeReasmLen) {
    // Hand-build a header + a long chain of fragment packets with a bogus huge
    // declared length. Fragments are matched by (flag byte +3, Count +8).
    const u8 kFlag = 0x42;
    const u32 kFirst = 100;
    const int kNumFrags = 40;  // 40*128 == 5120 bytes >> 1536-byte reasm buffer

    std::vector<CommandPacket> frags;
    frags.reserve(kNumFrags + 1);
    // Slot 0 is the header (owns the block; +12 -> first fragment Count).
    CommandPacket header{};
    header.opcode() = 0x20;
    header.bytes[3] = kFlag;
    header.put32(12, kFirst);
    frags.push_back(header);

    for (int k = 0; k < kNumFrags; ++k) {
        CommandPacket f{};
        f.opcode() = 7;                 // fragment marker
        f.bytes[3] = kFlag;
        f.set_count(kFirst + static_cast<u32>(k));
        // +12 -> next fragment's Count (last one chains to a missing Count).
        f.put32(12, kFirst + static_cast<u32>(k) + 1);
        if (k == 0)
            f.put16(16, 0xFFFF);        // bogus huge reasm_len on the FIRST fragment
        frags.push_back(f);
    }

    g_walk_list = &frags;
    PendingState reasm;
    int ret = ReassembleReceived(reasm, frags[0], &frags[0], &WalkNext);
    // Must not overrun the reasm buffer (ASAN gate). The safety bound stops the
    // copy well before all 40 fragments are consumed, then returns reasm_len
    // (== dword_11AA478, here the bogus 0xFFFF). Faithful completion paths return
    // reasm_len too; the property under test is "no overrun".
    CHECK_EQ(ret, 0xFFFF);
    CHECK_EQ(reasm.reasm_len, 0xFFFFu);
}

// ---------------------------------------------------------------------------
// HARDENING (wave-11): StagePendingBlock rejects an over-cap length faithfully
// (the original's own len > 0x3FE guard). A huge len never copies past block[].
// ---------------------------------------------------------------------------
TEST(SimCmdRecv, Malformed_StagePendingBlock_OverCapLength) {
    PendingState st;
    u8 data[8] = {1,2,3,4,5,6,7,8};
    // len just over the cap (1023 > 1022) -> rejected, nothing staged.
    CHECK_EQ(StagePendingBlock(st, 1023, data), 0);
    CHECK_EQ(st.staged, 0u);
    // The maximum legal length is accepted (boundary).
    u8 big[1022];
    for (int i = 0; i < 1022; ++i) big[i] = static_cast<u8>(i);
    CHECK_EQ(StagePendingBlock(st, 1022, big), 1);
    CHECK_EQ(st.staged, 1024u);
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
