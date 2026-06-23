#include "sim/command.h"

#include <cstring>

// Socket I/O lives in guild::net (already implemented). The Command layer only
// needs "send this framed packet"; the host wires a real NetTransport, tests
// provide a mock. We forward-declare a thin hook so this translation unit has no
// link dependency on the transport.
namespace guild::sim {
namespace netglue {
// Mirrors VIBE_Net_SendPacket @0x43bc54 (flush the staged frame). A weak default
// is provided so a standalone (single-player) build links cleanly; the e2e test
// overrides it to route through a mock transport.
void SendPacket(const CommandPacket& pkt);
} // namespace netglue
} // namespace guild::sim

namespace guild::sim {

// In the original, intrusive prev/next links live in the trailing 8 bytes of
// each 153-byte record (+145/+149). On a 64-bit host a pointer does not fit in
// those bytes, so we keep each list's link pair in a side table indexed by the
// record's slot within its owning array (pool_ for the received/free lists,
// ring_ for the pending-send list). CommandQueue owns both arrays, so the side
// tables are plain per-instance members — no behavior change.
struct CommandQueue::Links { CommandPacket* prev = nullptr; CommandPacket* next = nullptr; };

CommandQueue::CommandQueue() {
    ring_      = new CommandPacket[kSendRingSlots];
    ack_       = new AckEntry[kSendRingSlots];
    pool_      = new CommandPacket[kPoolSlots];
    poolLinks_ = new Links[kPoolSlots];
    ringLinks_ = new Links[kSendRingSlots];
    Init();
}

CommandQueue::~CommandQueue() {
    delete[] ring_;
    delete[] ack_;
    delete[] pool_;
    delete[] poolLinks_;
    delete[] ringLinks_;
}

// --- link side-table accessors ----------------------------------------------
CommandQueue::Links* CommandQueue::PoolLink(CommandPacket* p) { return &poolLinks_[p - pool_]; }
CommandQueue::Links* CommandQueue::RingLink(CommandPacket* p) { return &ringLinks_[p - ring_]; }

CommandPacket* CommandQueue::ring_next(CommandPacket* p) { return RingLink(p)->next; }
CommandPacket* CommandQueue::ring_prev(CommandPacket* p) { return RingLink(p)->prev; }
void CommandQueue::ring_set_next(CommandPacket* p, CommandPacket* n)  { RingLink(p)->next = n; }
void CommandQueue::ring_set_prev(CommandPacket* p, CommandPacket* pr) { RingLink(p)->prev = pr; }

// --- Init -------------------------------------------------------------------
// gilde.exe 0x4931e0 — VIBE_Command_QueueInitAndSync (init half only; the net
// opcode-3 handshake/blocking-wait is host/net glue, deferred).
void CommandQueue::Init() {
    for (u32 i = 0; i < kPoolSlots; ++i) {
        std::memset(pool_[i].bytes, 0, kPacketStride);
        // prev link = previous record for i>0 else null (matches dword_10783F1);
        // next link = following record except for the last (dword_10783F5).
        poolLinks_[i].prev = (i == 0) ? nullptr : &pool_[i - 1];
        poolLinks_[i].next = (i >= kPoolSlots - 1) ? nullptr : &pool_[i + 1];
    }
    for (u32 i = 0; i < kSendRingSlots; ++i) {
        ringLinks_[i].prev = nullptr;
        ringLinks_[i].next = nullptr;
    }
    free_head_     = &pool_[0];   // dword_11AA49C
    received_head_ = nullptr;     // dword_11AA498
    pending_head_  = nullptr;     // dword_11AA46C
    send_count_    = 0;           // dword_11AA494

    // ACK table: every entry status=1 (free/init), ring=-1.
    for (u32 i = 0; i < kSendRingSlots; ++i) {
        ack_[i].status = 1;
        ack_[i].slot   = 0;
        ack_[i].ring   = -1;
        ack_[i].seq    = 0;
    }
    last_req_count_  = 0;
    last_sync_count_ = 0;
}

// --- free / received list helpers (pool-threaded) ---------------------------
CommandPacket* CommandQueue::FreePop() {
    CommandPacket* p = free_head_;
    if (!p)
        return nullptr;
    free_head_ = PoolLink(p)->next;   // dword_11AA49C = *(head+149)
    return p;
}

void CommandQueue::FreePush(CommandPacket* p) {
    // UnlinkReceivedPacket tail: push onto the free list head.
    Links* l = PoolLink(p);
    l->prev = nullptr;
    l->next = free_head_;
    free_head_ = p;
}

void CommandQueue::AppendReceived(CommandPacket* p) {
    Links* lp = PoolLink(p);
    if (received_head_) {
        CommandPacket* tail = received_head_;
        while (PoolLink(tail)->next)
            tail = PoolLink(tail)->next;
        lp->next = nullptr;
        lp->prev = tail;
        PoolLink(tail)->next = p;
    } else {
        lp->prev = nullptr;
        lp->next = nullptr;
        received_head_ = p;
    }
}

// --- ComputePacketSize ------------------------------------------------------
// gilde.exe 0x493034 — VIBE_Command_ComputePacketSize. Faithful translation of
// the switch(opcode). Variable opcodes (0x16/0x17/0x18) and the sync opcode
// (0x20) consult payload bytes, hence the packet argument.
u16 ComputePacketSize(const CommandPacket& pkt) {
    const u8* a1 = pkt.bytes;
    switch (a1[0]) {
        case 3: case 8: case 0x1C: case 0x28: case 0x31: case 0x3B:
        case 0x42: case 0x43: case 0x4A: case 0x4D: case 0x57: case 0x58:
            return 20;
        case 4:
            return 17;
        case 0xA:
            return 80;
        case 0xB:
            return 42;
        case 0xC:
            return 73;
        case 0xD: case 0x21: case 0x23: case 0x24: case 0x26: case 0x27:
        case 0x29: case 0x2A: case 0x33: case 0x39: case 0x3A: case 0x5A:
            return 24;
        case 0xF: case 0x2C:
            return 33;
        case 0x11: case 0x3D:
            return 39;
        case 0x12: case 0x1E:
            return 30;
        case 0x13: case 0x1A: case 0x25: case 0x2B: case 0x34: case 0x36:
        case 0x38: case 0x47: case 0x52: case 0x54: case 0x5B: case 0x5D:
            return 28;
        case 0x14:
            return 22;
        case 0x15:
            return 57;
        case 0x16: case 0x17: {
            // Variable: count byte at +20, then per-field (stride*count + 4).
            const u8* v3 = a1 + 21;
            int v4 = 21;
            int v5 = a1[20];
            int v6 = 0;
            if (v5) {
                do {
                    // 1:1 NOTE (wave-15, MCP-confirmed @0x4930b5): the original's
                    // 0x16/0x17 size walk is `v7 = v3[1]*v3[0]+4; v4+=v7; v3+=v7`
                    // looped exactly *(a1+20) times with NO bound (it can read past
                    // the 153-byte record on a malformed packet). The guard below
                    // is a never-hit safety net for our standalone layout; on every
                    // codec-built packet (payload <= kMaxPayload=119) it is
                    // byte-identical to the unbounded original.
                    if (v4 + 4 > static_cast<int>(kPacketStride))
                        break;
                    int v7 = v3[1] * v3[0] + 4;
                    ++v6;
                    v4 += v7;
                    v3 += v7;
                } while (v6 < v5);
            }
            return static_cast<u16>(v4);
        }
        case 0x18:
            return static_cast<u16>(5 * a1[20] + 21);
        case 0x19: case 0x53:
            return 36;
        case 0x1B:
            return 40;
        case 0x1D:
            return 95;
        case 0x20:
            return (a1[16] == 14) ? 17 : 141;
        case 0x22:
            return 61;
        case 0x2D: case 0x45:
            return 31;
        case 0x2E: case 0x3C:
            return 32;
        case 0x2F: case 0x30:
            return 60;
        case 0x32:
            return 48;
        case 0x35:
            return 93;
        case 0x37:
            return 69;
        case 0x3E: case 0x4F: case 0x5E:
            return 52;
        case 0x40:
            return 47;
        case 0x41:
            return 74;
        case 0x44:
            return 23;
        case 0x46:
            return 25;
        case 0x48:
            return 21;
        case 0x49:
            return 55;
        case 0x4B:
            return 144;
        case 0x4C:
            return 53;
        case 0x4E:
            return 64;
        case 0x50: case 0x51:
            return 68;
        case 0x55:
            return 97;
        case 0x56:
            return 56;
        case 0x5C:
            return 26;
        default:
            return 145;
    }
}

u16 ComputePacketSizeFixed(u8 opcode) {
    CommandPacket tmp{};
    tmp.bytes[0] = opcode;
    // Variable opcodes need payload; for fixed ones the zeroed packet is correct.
    return ComputePacketSize(tmp);
}

// --- EnqueuePacket ----------------------------------------------------------
// gilde.exe 0x49388c — VIBE_Command_EnqueuePacket.
i32 CommandQueue::EnqueuePacket(const CommandPacket& staged) {
    if (disconnected_)                       // dword_764CF0
        return -1;
    // v2 = (sendCount+1) & 0x7FFF — the ring slot this packet will occupy.
    u32 v2 = (send_count_ + 1) & kSeqMask;
    CommandPacket* slot = &ring_[v2];

    // Duplicate guard: if this exact ring slot is already on the pending list,
    // reject (the original walks dword_11AA46C comparing pointers).
    for (CommandPacket* p = pending_head_; p; p = ring_next(p)) {
        if (p == slot)
            return -1;
    }

    // Copy staged bytes into the ring slot, then stamp the header.
    std::memcpy(slot->bytes, staged.bytes, kPacketStride);
    ++send_count_;                            // dword_11AA494 (post-increment)

    u16 sz = ComputePacketSize(*slot);
    slot->set_flag(0);                        // +3 = 0
    slot->set_extra(0);                       // +12 = 0
    slot->set_len(sz);                        // +1 = size
    slot->set_cmd_id(v2);                     // +4 = ring slot index
    slot->set_count(send_count_);             // +8 = sequence Count

    if (slot->opcode() == kSyncType && slot->bytes[16] == kSyncMarker)
        last_sync_count_ = slot->count();     // dword_11AA470

    // Seed the ACK entry for this ring slot: status=0 (pending), ring index=v2.
    AckEntry& ae = ack_[v2];
    ae.status = 0;
    ae.ring   = static_cast<i32>(v2);

    // Append the ring slot onto the pending-send list (threaded via ring links).
    if (pending_head_) {
        CommandPacket* tail = pending_head_;
        while (ring_next(tail))
            tail = ring_next(tail);
        ring_set_prev(slot, tail);
        ring_set_next(tail, slot);
        ring_set_next(slot, nullptr);
    } else {
        ring_set_prev(slot, nullptr);
        pending_head_ = slot;
        ring_set_next(slot, nullptr);
    }
    return static_cast<i32>(v2);
}

// --- FlushSendQueue ---------------------------------------------------------
// gilde.exe 0x4934cc — VIBE_Command_FlushSendQueue.
int CommandQueue::FlushSendQueue() {
    CommandPacket* p = pending_head_;
    if (!p)
        return 0;

    if (standalone_) {                        // dword_764CE0 == -1
        while (p) {
            pending_head_ = p;
            int rc = StoreReceivedPacket(*p);  // apply locally
            if (rc) {
                pending_head_ = p;
                return 1;
            }
            CommandPacket* nxt = ring_next(p);
            pending_head_ = nxt;
            if (nxt)
                ring_set_prev(nxt, nullptr);
            p = nxt;
        }
    } else {
        while (pending_head_) {
            netglue::SendPacket(*pending_head_);
            CommandPacket* nxt = ring_next(pending_head_);
            pending_head_ = nxt;
            if (nxt)
                ring_set_prev(nxt, nullptr);
        }
    }
    pending_head_ = nullptr;
    return 0;
}

// --- StoreReceivedPacket ----------------------------------------------------
// gilde.exe 0x493f80 — VIBE_Command_StoreReceivedPacket.
int CommandQueue::StoreReceivedPacket(const CommandPacket& src) {
    CommandPacket* node = FreePop();
    if (!node)
        return 1;                              // free pool exhausted
    std::memcpy(node->bytes, src.bytes, kPacketStride);
    u16 sz = ComputePacketSize(*node);
    node->set_len(sz);
    AppendReceived(node);
    return 0;
}

// --- UnlinkReceivedPacket ---------------------------------------------------
// gilde.exe 0x494028 — VIBE_Command_UnlinkReceivedPacket.
void CommandQueue::UnlinkReceivedPacket(CommandPacket* pkt) {
    Links* l = PoolLink(pkt);
    if (l->prev)
        PoolLink(l->prev)->next = l->next;
    else
        received_head_ = l->next;
    if (l->next)
        PoolLink(l->next)->prev = l->prev;
    FreePush(pkt);
}

// --- ExecCommands -----------------------------------------------------------
// gilde.exe 0x494088 — VIBE_Command_ExecCommands. Walks the received list in
// link order (which is sequence order: StoreReceivedPacket appends in flush /
// arrival order), classifies sequence gaps against the last-requested /
// last-sync counters, dispatches each opcode, and recycles the node.
int CommandQueue::ExecCommands() {
    CommandPacket* node = received_head_;
    CommandPacket* groupStart = nullptr; // v3: pending group-begin
    while (node) {
        CommandPacket* next = PoolLink(node)->next; // v4

        // Group framing: opcode 5 begins, 6 ends, 7 skips.
        if (groupStart == nullptr && node->opcode() == kOpGroupBegin) {
            groupStart = node;
        } else if (groupStart && node->opcode() == kOpGroupEnd) {
            // ExecCommandGroup(groupStart) would run the framed block; deferred.
            node = groupStart;
            next = PoolLink(groupStart)->next;
            groupStart = nullptr;
        }

        if (node->opcode() != kOpGroupSkip) {
            // Unlink node from the received list, then dispatch and recycle.
            CommandPacket* prev = PoolLink(node)->prev;
            CommandPacket* nx   = PoolLink(node)->next;
            if (!prev)
                received_head_ = nx;
            else
                PoolLink(prev)->next = nx;
            if (nx)
                PoolLink(nx)->prev = prev;

            u8 op = node->opcode();
            if (op < kNumOpcodes) {
                AckEntry* ack = nullptr;
                u32 cmdId = node->cmd_id();
                if (cmdId != 0xFFFFFFFFu) {
                    AckEntry& ae = ack_[cmdId & kSeqMask];
                    ae.status = 2;             // applied
                    ae.slot   = 0;
                    ae.seq    = 0;
                    u32 cnt = node->count();
                    if (last_req_count_ + 1 == cnt) {
                        last_req_count_ = cnt;         // in-order advance
                    } else if (cnt == last_sync_count_) {
                        // duplicate-sync: diagnostic only in the original
                    } else if (node->opcode() == kSyncType && node->bytes[16] == kSyncMarker) {
                        // received sync: diagnostic only
                    } else {
                        last_req_count_ = cnt;         // lost a command: resync
                    }
                    ack = &ae;
                }
                if (handlers_[op])
                    handlers_[op](*this, *node, ack);
            }
            FreePush(node);   // node returns to the free pool
        }
        node = next;
    }
    return 0;
}

// --- ACK queries ------------------------------------------------------------
// gilde.exe 0x4939d4 / 0x4939fc — GetPacketStatusById / GetPacketSeqById.
int CommandQueue::GetPacketStatusById(u32 ringId) const {
    if (ringId < kSendRingSlots)
        return ack_[ringId].status;
    return -1;
}
i32 CommandQueue::GetPacketSeqById(u32 ringId) const {
    if (ringId < kSendRingSlots)
        return ack_[ringId].seq;
    return 0;
}

// --- default net glue (standalone build links cleanly) ----------------------
namespace netglue {
// Weak so the e2e test (which routes through a mock transport) can override.
__attribute__((weak)) void SendPacket(const CommandPacket&) {}
} // namespace netglue

} // namespace guild::sim
