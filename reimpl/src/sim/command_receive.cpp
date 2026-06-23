#include "sim/command_receive.h"
#include "net/transport.h"

#include <cstring>

namespace guild::sim {

// Node embeds the 153-byte packet as its FIRST member, so a CommandPacket* taken
// from a node aliases the node base and we can recover the node by cast.
ReceiveDriver::Node* ReceiveDriver::NodeOf(CommandPacket* p) {
    return reinterpret_cast<Node*>(p);
}
CommandPacket* ReceiveDriver::NextOf(CommandPacket* p) {
    Node* n = NodeOf(p);
    return n->next ? &n->next->pkt : nullptr;
}

ReceiveDriver::ReceiveDriver() {
    for (auto& e : ack_) { e.status = 1; e.slot = 0; e.ring = -1; e.seq = 0; }
}

ReceiveDriver::~ReceiveDriver() {
    for (Node* n : nodes_)
        delete n;
}

std::size_t ReceiveDriver::received_count() const {
    std::size_t c = 0;
    for (Node* n = head_node_; n; n = n->next) ++c;
    return c;
}

// --- received list helpers (mirror StoreReceivedPacket / UnlinkReceivedPacket) -
void ReceiveDriver::Append(Node* n) {
    n->next = nullptr;
    if (!head_node_) {
        n->prev = nullptr;
        head_node_ = n;                          // dword_11AA498 = node
    } else {
        Node* tail = head_node_;
        while (tail->next) tail = tail->next;     // walk to list end (original loop)
        n->prev = tail;
        tail->next = n;
    }
    head_ = head_node_ ? &head_node_->pkt : nullptr;
}

void ReceiveDriver::Unlink(Node* n) {
    if (n->prev)
        n->prev->next = n->next;
    else
        head_node_ = n->next;                     // dword_11AA498 = node->+149
    if (n->next)
        n->next->prev = n->prev;
    n->prev = nullptr;
    n->next = nullptr;
    head_ = head_node_ ? &head_node_->pkt : nullptr;
}

// gilde.exe 0x493f80 — VIBE_Command_StoreReceivedPacket. Take a free node, copy
// the 153 bytes in, recompute the length word, append to the received list.
int ReceiveDriver::StoreReceivedPacket(const CommandPacket& src) {
    Node* n = new Node();
    nodes_.push_back(n);
    std::memcpy(n->pkt.bytes, src.bytes, kPacketStride);
    n->pkt.set_len(ComputePacketSize(n->pkt));   // *(WORD*)(v4+1) = size
    Append(n);
    return 0;
}

// gilde.exe 0x493ebc — VIBE_Command_ReceiveAndQueue.
//   if ( dword_764CE0 == -1 ) return 0;               // standalone: nothing
//   if ( dword_764CE4 ) { ReceivePacket(); if (dword_764CE4) return 0; }
//   do { ++recvCounter; if (StoreReceivedPacket(rxBuf)) return 1;
//        dword_764CE4 = rxBuf; ReceivePacket(); } while (!dword_764CE4);
//
// dword_764CE4 is the transport's active reassembly buffer pointer: non-null
// means "still assembling, no whole packet yet"; cleared to 0 by ReceivePacket
// when a full packet has landed in rxBuf. We mirror that with the transport's
// CompletedThisCall()/SetRecvBuffer() contract: arm the buffer, pump, and when a
// packet completes (buffer auto-cleared) store it and re-arm.
int ReceiveDriver::ReceiveAndQueue(guild::net::NetTransport& net, u8* rxBuf, u32 rxBufLen) {
    (void)rxBufLen;
    // Standalone (host/single-player): the original early-outs (dword_764CE0==-1).
    if (!net.connected())
        return 0;

    // Pump once; if no whole packet completed, nothing to queue yet.
    net.SetRecvBuffer(rxBuf);
    net.ReceivePacket();
    if (!net.CompletedThisCall())
        return 0;

    // A packet completed: store it, then keep pumping while packets keep completing.
    do {
        CommandPacket pkt{};
        std::memcpy(pkt.bytes, rxBuf, kPacketStride);
        if (StoreReceivedPacket(pkt))
            return 1;
        net.SetRecvBuffer(rxBuf);                // dword_764CE4 = &byte_11AA4A4
        net.ReceivePacket();
    } while (net.CompletedThisCall());
    return 0;
}

// gilde.exe 0x4942c0 — VIBE_Command_ExecCommandGroup. The group runs from the
// opcode-5 begin frame (a1) to the next opcode-6 end frame. First a gate pre-pass
// runs funcs_4942DD[opcode] over each framed packet (starting at a1->+149); if any
// returns nonzero the group is rejected. On accept: stamp begin and end opcode=1.
// On reject: stamp every frame in [begin..end] opcode=2. Returns 1.
int ExecCommandGroup_impl(CommandPacket* begin, GroupGateFn gate,
                          CommandPacket* (*next)(CommandPacket*)) {
    // Gate pre-pass over the frames after `begin`, up to (not incl.) the end frame.
    int accept = 1;                              // v3 = 1
    CommandPacket* p = next(begin);              // v2 = *(a1+149)
    while (p && p->bytes[0] != kOpEnd) {
        if (gate && gate(*p))                    // funcs_4942DD[op]() != 0 => reject
            accept = 0;
        if (!accept)
            break;
        p = next(p);
    }

    if (accept) {
        begin->bytes[0] = 1;                     // *(BYTE*)a1 = 1
        CommandPacket* i = begin;
        while (i->bytes[0] != kOpEnd)            // find the end frame
            i = next(i);
        i->bytes[0] = 1;                         // *(BYTE*)i = 1
    } else {
        CommandPacket* j = begin;
        while (j->bytes[0] != kOpEnd) {          // stamp the whole span = 2
            j->bytes[0] = 2;
            j = next(j);
        }
        j->bytes[0] = 2;                         // and the end frame
    }
    return 1;
}

int ReceiveDriver::ExecCommandGroup(CommandPacket* begin) {
    return ExecCommandGroup_impl(begin, group_gate_, &ReceiveDriver::NextOf);
}

// gilde.exe 0x494088 — VIBE_Command_ExecCommands (the dispatch loop, networked
// branch with reassembly). The disconnected-cleanup branch (dword_764CF0) that
// acks+frees the whole list is host teardown glue and is omitted (documented).
int ReceiveDriver::ExecReceivedCommands() {
    CommandPacket* node = head_;                 // v2 = dword_11AA498
    CommandPacket* groupStart = nullptr;         // v3 (ebx) = 0
    while (node) {
        CommandPacket* nextNode = NextOf(node);  // v4 (esi) = *(v2+149)
        // edx: 1 = this node may be dispatched individually; 0 = a group is open
        // (frames inside a group are NOT dispatched standalone). Re-armed each
        // iteration (mov edx, 1).
        int dispatchGate = 1;                    // edx = 1

        // --- group framing (opcode 5 begin / 6 end) ---
        // Disasm 0x4940f6..0x494176 (edx = dispatchGate):
        //   edx starts at 1 every iteration (mov edx, 1 @0x4940eb).
        if (!groupStart && node->bytes[0] == kOpBegin) {
            groupStart = node;                   // v3 = v2 (ebx = ecx) — open group
            // falls to loc_494101: ebx != 0 => edx = 0 below.
        } else if (groupStart && node->bytes[0] == kOpEnd) {
            // loc_494156: group open AND end-frame.
            int r = ExecCommandGroup(groupStart);// eax = run the framed block
            // @0x494166: if (eax != 0) skip `xor edx,edx`; else edx = 0.
            // ExecCommandGroup ALWAYS returns 1, so edx KEEPS its value (1) here —
            // the reprocessed begin frame (now opcode 1) IS dispatched.
            if (r == 0)
                dispatchGate = 0;
            node = groupStart;                   // v2 = v3 (ecx = ebx)
            nextNode = NextOf(groupStart);       // v4 = *(v3+149)
            groupStart = nullptr;                // v3 = 0 (ebx cleared) — group closed
            // ebx == 0 now, so the trailing guard leaves edx unchanged (== 1).
        }
        // loc_494101: edx = (ebx != 0) ? 0 : edx. A frame is dispatched only when no
        // group is open — covers both the just-opened begin frame and inner frames.
        // (The group-close branch leaves groupStart == 0 so edx stays 1 there.)
        if (groupStart)
            dispatchGate = 0;

        // --- dispatch (opcode 7 frames are skipped; gated by dispatchGate) ---
        if (dispatchGate && node->bytes[0] != kOpSkip &&
            CheckReassemblyComplete(*node, head_, &ReceiveDriver::NextOf)) {
            // Unlink this node and move it to the (implicit) free list.
            Node* n = NodeOf(node);
            // If we unlinked the head, advance nextNode bookkeeping is already from
            // the pre-unlink NextOf snapshot above.
            Unlink(n);

            // Reassemble any fragment chain into the pending buffer. After this the
            // large-payload handlers read pending_.reasm; simple packets are no-ops.
            ReassembleReceived(pending_, *node, head_, &ReceiveDriver::NextOf);

            if (node->bytes[0] < kNumOpcodes) {  // *v11 < 96
                AckEntry* ackp = nullptr;        // v12
                u32 cmdId = node->cmd_id();      // *((_DWORD*)v11+1)
                if (cmdId == 0xFFFFFFFFu) {
                    ackp = nullptr;              // not a tracked command
                } else {
                    AckEntry& ae = ack_[cmdId & kSeqMask];
                    ae.status = 2;               // *v12 = 2
                    ae.slot   = 0;               // v12[1] = 0
                    ae.seq    = 0;               // *(v12+6) = 0
                    u32 count = node->count();   // v14 = *((_DWORD*)v11+2)
                    if (last_req_count_ + 1 == count) {
                        last_req_count_ = count;          // in order: advance
                    } else if (count == last_sync_count_) {
                        // "Received a Command with Count == cm_LastSyncCount": diag
                    } else if (node->bytes[0] == 32 && node->bytes[16] == 14) {
                        // "Received Sync": diagnostic only
                    } else {
                        // "Lost a Command": resync last-requested to this Count
                        last_req_count_ = count;
                    }
                    ackp = &ae;
                }
                if (handlers_[node->bytes[0]])
                    handlers_[node->bytes[0]](*node, ackp); // funcs_4941F4[op](v11,v12)
            }
        }
        node = nextNode;                         // v2 = v4
    }
    return 0;
}

} // namespace guild::sim
