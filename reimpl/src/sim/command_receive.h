#pragma once
#include "guild/common/types.h"
#include "sim/command.h"
#include "sim/command_pending.h"

#include <vector>

// gilde.exe — Command RECEIVE / reassembly / group-dispatch path (guild::sim).
//
// This is the inbound half of the lockstep command system: pull framed packets
// off the transport into the received list, reassemble multi-fragment payloads,
// then walk the received list in sequence order dispatching each command (with
// group begin/exec/end framing). It complements the send-side ring/codec in
// command.{h,cpp} and the staging/reassembly buffers in command_pending.{h,cpp}.
//
// Recovered from:
//   VIBE_Command_ReceiveAndQueue        @0x493ebc
//   VIBE_Command_StoreReceivedPacket    @0x493f80  (received-list append, modeled
//                                                   here over an explicit list)
//   VIBE_Command_UnlinkReceivedPacket   @0x494028
//   VIBE_Command_ExecCommandGroup       @0x4942c0  (opcode 5/6/7 group framing)
//   VIBE_Command_ExecCommands           @0x494088  (dispatch loop + reassembly +
//                                                   sequence classification)
//
// The original holds one global received list head (dword_11AA498) and a free
// list (dword_11AA49C). We model the received list explicitly on a ReceiveDriver
// instance (one live instance reproduces the single global state). The packet
// records keep their byte-exact 153-byte layout, so the +12 fragment link, the
// +3 flag byte and the +8 Count drive reassembly exactly as on the wire.

namespace guild { namespace net { class NetTransport; } }

namespace guild::sim {

// Group framing opcodes (ExecCommandGroup).
constexpr u8 kOpBegin = 5;  // group-begin frame
constexpr u8 kOpEnd   = 6;  // group-end frame
constexpr u8 kOpSkip  = 7;  // skip marker (also the fragment opcode)

// Dispatch handler: receives the (reassembled) packet and its ACK entry (may be
// null when the packet is not a tracked command, cmdId == -1). Mirrors the
// funcs_4941F4[opcode] jump-table target signature once bound.
using ApplyHandler = void (*)(CommandPacket& pkt, AckEntry* ack);

// Group pre-pass handler: ExecCommandGroup first runs funcs_4942DD[opcode] over
// the framed packets to decide accept/reject (returns nonzero => reject). Default
// behavior accepts the group (returns 0). Tests can install a spy.
using GroupGateFn = int (*)(CommandPacket& pkt);

class ReceiveDriver {
public:
    ReceiveDriver();

    // Install the per-opcode dispatch handlers (the jump-table targets) and the
    // optional group-gate function.
    void set_handler(u8 opcode, ApplyHandler fn) { if (opcode < kNumOpcodes) handlers_[opcode] = fn; }
    void set_group_gate(GroupGateFn fn) { group_gate_ = fn; }

    // ACK/status table accessor (byte_B5FB60-equivalent), 10-byte entries.
    AckEntry& ack(u32 ringId) { return ack_[ringId & kSeqMask]; }
    const AckEntry& ack(u32 ringId) const { return ack_[ringId & kSeqMask]; }

    // Sequence counters (dword_11AA468 last-requested, dword_11AA470 last-sync).
    u32 last_req_count() const { return last_req_count_; }
    u32 last_sync_count() const { return last_sync_count_; }
    void set_last_sync_count(u32 c) { last_sync_count_ = c; }

    // The reassembly/pending buffers (consumed by the large-payload handlers).
    PendingState& pending() { return pending_; }

    // gilde.exe 0x493f80 — append a 153-byte packet onto the received list (its
    // length is recomputed via ComputePacketSize). Returns 0; modeled to never
    // exhaust (the original returns 1 + disconnects on free-pool exhaustion).
    int StoreReceivedPacket(const CommandPacket& src);

    // gilde.exe 0x493ebc — VIBE_Command_ReceiveAndQueue. Pull every fully framed
    // packet currently available from the transport into the received list. In
    // standalone mode (dword_764CE0 == -1) this is a no-op. `rxBuf` is the
    // transport's reassembly buffer (byte_11AA4A4); the driver re-arms it after
    // each completed packet, exactly as the original loops on dword_764CE4.
    // Returns 1 if the free pool was exhausted (unused here), else 0.
    int ReceiveAndQueue(guild::net::NetTransport& net, u8* rxBuf, u32 rxBufLen);

    // Direct feed for tests / standalone local-apply: store a packet as if it had
    // arrived from the network.
    void DeliverPacket(const CommandPacket& pkt) { StoreReceivedPacket(pkt); }

    // gilde.exe 0x494088 — VIBE_Command_ExecCommands. Walk the received list in
    // sequence order; handle opcode 5/6 group framing via ExecCommandGroup; for
    // each non-skip packet that passes CheckReassemblyComplete, unlink it,
    // reassemble its fragment chain into the pending buffer, classify the sequence
    // gap (advance / dup-sync / sync / lost), and dispatch funcs[opcode]. Returns 0.
    int ExecReceivedCommands();

    // gilde.exe 0x4942c0 — VIBE_Command_ExecCommandGroup. Run the group's gate
    // pre-pass; on accept stamp the begin+end frames opcode=1, on reject stamp the
    // whole [begin..end] span opcode=2 (so the main loop skips/handles them).
    // Returns 1 (always, matching the original). `begin` is the opcode-5 frame.
    int ExecCommandGroup(CommandPacket* begin);

    // --- inspection (tests) ---
    CommandPacket* received_head() const { return head_; }
    std::size_t received_count() const;

    // List traversal used by the reassembler (public so command_pending can walk).
    static CommandPacket* NextOf(CommandPacket* p);

private:
    // Intrusive received list nodes (byte-exact 153-byte record + explicit links,
    // since a 64-bit pointer does not fit in the original +145/+149 link bytes).
    struct Node { CommandPacket pkt; Node* prev = nullptr; Node* next = nullptr; };
    static Node* NodeOf(CommandPacket* p); // recover Node from embedded packet

    void Append(Node* n);
    void Unlink(Node* n);

    std::vector<Node*> nodes_;     // owns all allocated nodes (freed in dtor)
    Node* head_node_ = nullptr;    // dword_11AA498 — received list head
    CommandPacket* head_ = nullptr;// head_node_->pkt (cached for accessors)

    AckEntry ack_[kSendRingSlots]; // byte_B5FB60-equivalent
    u32 last_req_count_  = 0;      // dword_11AA468
    u32 last_sync_count_ = 0;      // dword_11AA470

    PendingState pending_;         // staging + reassembly buffers

    ApplyHandler handlers_[kNumOpcodes] = {};
    GroupGateFn  group_gate_ = nullptr;

public:
    ~ReceiveDriver();
};

} // namespace guild::sim
