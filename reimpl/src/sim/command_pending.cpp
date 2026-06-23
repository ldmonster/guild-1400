#include "sim/command_pending.h"

#include <cstring>

namespace guild::sim {

// ===========================================================================
// StagePendingBlock — gilde.exe 0x49436c
// ===========================================================================
// __usercall (ax = len, edx = src). The original:
//   if ( dword_11AA4A0 || (u16)len > 0x3FE ) return 0;
//   word_1077F60 = len;
//   qmemcpy(&unk_1077F62, src, len);
//   dword_11AA4A0 = (u16)len + 2;
//   return 1;
int StagePendingBlock(PendingState& state, u16 len, const void* src) {
    if (state.staged != 0 || len > kPendingMaxBlock) // already staged / too big
        return 0;
    state.block[0] = static_cast<u8>(len);           // word_1077F60 lo
    state.block[1] = static_cast<u8>(len >> 8);      // word_1077F60 hi
    if (len)
        std::memcpy(state.block + 2, src, len);      // unk_1077F62
    state.staged = static_cast<u32>(len) + 2;        // dword_11AA4A0
    return 1;
}

// ===========================================================================
// GeneratePendingPackets — gilde.exe 0x493584
// ===========================================================================
// Splits the staged block into 128-byte opcode-7 fragments and links them by
// Count through the +12 field. In the binary this writes the ring slots directly;
// here we reuse EnqueuePacket (which is ODR-owned by command.cpp) to stage each
// fragment, then patch the +12 chain afterward from the assigned Counts. The two
// are observably identical: each fragment ends with opcode 7, size, flag 0,
// cmdId = ring slot, Count = sequence, +12 = next fragment's Count (0 on last),
// and is linked onto the pending-send list. Returns the first fragment's Count.
u32 GeneratePendingPackets(CommandQueue& q, PendingState& state) {
    if (state.staged == 0 || q.disconnected())   // !dword_11AA4A0 || dword_764CF0
        return 0;

    const u32 firstCount = q.send_count() + 1;   // v11 = dword_11AA494 + 1
    u32 chunkOff = 0;                             // v12 (block byte cursor)
    i32 prevSlot = -1;                            // previous fragment's ring slot

    // Walk the block in 128-byte chunks (each chunk copies 128 bytes from the
    // block at offset 128*i; the first chunk starts at the length word).
    while (chunkOff < state.staged) {
        CommandPacket frag{};
        frag.opcode() = kOpFragment;             // *v2 = 7
        // qmemcpy(frag+16, block+chunkOff, 128). The producer always copies a full
        // 128-byte chunk; the block buffer is padded so reads past `staged` are 0.
        std::memcpy(frag.bytes + 16, state.block + chunkOff, kFragChunkBytes);

        i32 slot = q.EnqueuePacket(frag);        // assigns cmdId/Count/size, links
        if (slot < 0)
            break;                               // ring full / disconnected: stop

        // Chain: previous fragment's +12 = this fragment's Count.
        CommandPacket& cur = q.ring_slot(static_cast<u32>(slot));
        if (prevSlot >= 0)
            q.ring_slot(static_cast<u32>(prevSlot)).put32(12, cur.count());
        prevSlot = slot;

        chunkOff += kFragChunkBytes;             // v12 += 128
    }
    // Last fragment's +12 stays 0 (EnqueuePacket already zeroes +12), terminating
    // the chain.
    state.staged = 0;                            // dword_11AA4A0 = 0
    return firstCount;                           // v11
}

// EnqueuePacket tail emulation: enqueue the header, then link its +12 to the first
// generated fragment's Count. Faithful to EnqueuePacket @0x49388c lines 0x4939a1.
i32 EmitWithPendingBlock(CommandQueue& q, PendingState& state, CommandPacket& header) {
    i32 slot = q.EnqueuePacket(header);
    if (slot < 0)
        return slot;
    if (state.staged) {                          // if ( dword_11AA4A0 )
        u32 firstCount = GeneratePendingPackets(q, state);
        q.ring_slot(static_cast<u32>(slot)).put32(12, firstCount); // header->+12
    }
    return slot;
}

// ===========================================================================
// Fragment-chain search helper (the inner while-loop shared by both consumers).
// Find the fragment whose flag byte (+3) matches `flagByte` and whose Count (+8)
// matches `wantCount`, scanning the received list from `head` via `next`.
// ===========================================================================
namespace {
CommandPacket* FindFragment(CommandPacket* head, CommandPacket* (*next)(CommandPacket*),
                            u8 flagByte, u32 wantCount) {
    for (CommandPacket* p = head; p; p = next(p)) {
        if (p->bytes[3] == flagByte && p->count() == wantCount)
            return p;
    }
    return nullptr;
}
} // namespace

// ===========================================================================
// ReassembleReceived — gilde.exe 0x49377c
// ===========================================================================
// header.+12 holds the first fragment's Count (0 => no block, nothing to do).
// First fragment: reasm_len = frag+16 word; copy 124 bytes from frag+18 into
// reasm[0..123]; copy 2 bytes from frag+142 into reasm[124..125]. If reasm_len
// <= 126 we are done; else for each following fragment (chained via its +12)
// copy 128 bytes from frag+16 into reasm at +126, +254, ... until the cumulative
// length (starting at 126) reaches reasm_len.
//
// As each fragment is consumed the original clears it: opcode byte -> 0 (so the
// dispatch skip-loop won't re-process it) AND its +12 link -> 0. The header's +12
// is cleared the moment the first fragment is matched (before any copy).
//
// RETURN (binary): on full reassembly returns dword_11AA478 (the reasm_len); on
// header.+12==0 or a missing fragment returns 0. (The original returns the `eax`
// register, which is dword_11AA478 at the LABEL_19 completion point and 0/NULL on
// the not-found paths — NOT a fragment count.) The sole live caller
// (ExecReceivedCommands) discards this value.
int ReassembleReceived(PendingState& state, CommandPacket& header,
                       CommandPacket* head, CommandPacket* (*next)(CommandPacket*)) {
    u32 link = header.get32(12);                 // *(_DWORD *)(a1 + 12)
    if (link == 0)                               // no block referenced
        return 0;

    const u8 flagByte = header.bytes[3];         // *(a1 + 3)
    CommandPacket* frag = FindFragment(head, next, flagByte, link);
    if (!frag)
        return 0;                                // first fragment not arrived (result==0)
    header.put32(12, 0);                         // *(v1 + 12) = 0  (clear header link)

    int total = 126;                             // v3 = 126
    state.reasm_len = frag->get16(16);           // dword_11AA478 = *(u16*)(v1+16)
    std::memcpy(state.reasm + 0, frag->bytes + 18, kReassembleHeadCap); // 124 bytes
    std::memcpy(state.reasm + 124, frag->bytes + 142, 2);               // unk_1077BDC
    frag->bytes[0] = 0;                          // *(BYTE*)v1 = 0

    int reasmLen = static_cast<int>(state.reasm_len);
    if (reasmLen <= 126) {                        // single-fragment payload: LABEL_19
        frag->put32(12, 0);                       // *(v1 + 12) = 0
        return reasmLen;                          // result = dword_11AA478
    }

    // need more fragments
    u8* dst = state.reasm + 126;                 // &unk_1077BDE
    // HARDENING (wave-11, rule-8 safety): reasm_len comes straight off the wire
    // (frag+16, a u16 up to 65535). The original chunk loop has NO destination
    // bound — it copies 128 bytes per fragment until total >= reasm_len, so a
    // malformed fragment with a huge reasm_len overruns the fixed reasm buffer.
    // A valid block built by GeneratePendingPackets is capped at kPendingMaxBlock
    // (1022) so its reassembly never exceeds kReassembleBufBytes; this end-of-
    // buffer bound is byte-identical on every valid chain and only stops the copy
    // when a chunk would overrun.
    const u8* reasm_end = state.reasm + kReassembleBufBytes;
    while (true) {
        link = frag->get32(12);                  // this fragment's +12 -> next Count
        CommandPacket* nf = FindFragment(head, next, flagByte, link);
        if (!nf)
            return 0;                            // chain broken / fragment missing (result==0)
        frag->put32(12, 0);                      // *(v1 + 12) = 0  (clear consumed link)
        if (dst + kFragChunkBytes > reasm_end)
            return reasmLen;                     // safety stop (valid chains never hit)
        frag = nf;
        std::memcpy(dst, frag->bytes + 16, kFragChunkBytes); // 128 bytes
        total += 128;                            // v3 += 128
        dst += 128;
        frag->bytes[0] = 0;                      // *(BYTE*)v1 = 0
        if (total >= reasmLen) {                 // v3 >= dword_11AA478 -> LABEL_19
            frag->put32(12, 0);                  // *(v1 + 12) = 0
            return reasmLen;                     // result = dword_11AA478
        }
    }
}

// ===========================================================================
// CheckReassemblyComplete — gilde.exe 0x4936e4
// ===========================================================================
// Returns 1 (dispatchable) if: opcode is 7 (skip), header carries no block
// (+12 == 0), or every fragment of the chain is present on `head`. Returns 0 if
// a fragment is still missing.
int CheckReassemblyComplete(CommandPacket& header, CommandPacket* head,
                            CommandPacket* (*next)(CommandPacket*)) {
    if (header.bytes[0] == 7 || header.get32(12) == 0) // skip OR no block
        return 1;

    const u8 flagByte = header.bytes[3];
    u32 link = header.get32(12);
    CommandPacket* frag = FindFragment(head, next, flagByte, link);
    if (!frag)
        return 0;                                // first fragment missing

    int total = 126;                             // v3 = 126
    u16 reasmLen = frag->get16(16);              // v4 = *(u16*)(v1+16)
    if (reasmLen <= 126)                         // single-fragment payload: complete
        return 1;
    while (true) {
        link = frag->get32(12);
        CommandPacket* nf = FindFragment(head, next, flagByte, link);
        if (!nf)
            return 0;                            // a fragment is missing
        frag = nf;
        total += 128;                            // v3 += 128
        if (total >= reasmLen)                   // v3 >= v4
            return 1;
    }
}

} // namespace guild::sim
