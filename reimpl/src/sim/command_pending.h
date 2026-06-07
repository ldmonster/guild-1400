#pragma once
#include "guild/common/types.h"
#include "sim/command.h"

// gilde.exe — Command multi-fragment STAGING + REASSEMBLY (namespace guild::sim).
//
// Large command payloads (cutscene info / message-box text / HE blobs / speech
// buffers) do not fit in a single 153-byte packet. The original splits them into
// a separate "pending block" staging buffer, then emits the block as a chain of
// fixed opcode-7 fragment packets; on the receive side the header packet that
// referenced the block walks the fragment chain and reassembles it into a global
// I/O buffer that the Ex* handler then reads.
//
// This module recovers that machinery byte-for-byte from:
//   VIBE_Command_StagePendingBlock        @0x49436c
//   VIBE_Command_GeneratePendingPackets   @0x493584   (called from EnqueuePacket)
//   VIBE_Command_ReassembleReceived       @0x49377c
//   VIBE_Command_CheckReassemblyComplete  @0x4936e4
//
// The original keeps three global regions; we model them on one PendingState
// instance (one live instance per session reproduces the single global state):
//
//   --- staging block (StagePendingBlock / GeneratePendingPackets producer) ---
//   word_1077F60   (0x1077F60)  block length header (u16), max 0x3FE (1022)
//   unk_1077F62    (0x1077F62)  block data, immediately follows the length word
//   dword_11AA4A0  (0x11AA4A0)  staged byte count == len + 2  (0 => nothing staged)
//
//   --- reassembly buffer (ReassembleReceived consumer) ---
//   dword_11AA478  (0x11AA478)  total reassembled length (= first fragment's +16 word)
//   unk_1077B60    (0x1077B60)  reassembled payload; first 124 bytes from frag#0+18
//   unk_1077BDC    (0x1077BDC)  +124: 2 bytes from frag#0+142
//   unk_1077BDE    (0x1077BDE)  +126: subsequent 128-byte fragment chunks
//
// Fragment packet wire layout (the opcode-7 chunks GeneratePendingPackets emits):
//   +0   opcode 7
//   +1   computed size (ComputePacketSize -> 145 for op 7, the default)
//   +3   flag byte (0)
//   +4   cmdId / ring slot
//   +8   Count (sequence)
//   +12  link to NEXT fragment's Count (0 on the last fragment)
//   +16  128 bytes of block data for this chunk (chunk N covers block[128*N ..])
//
// The header packet that owns the block stores the FIRST fragment's Count in its
// own +12 field (EnqueuePacket does `header->+12 = GeneratePendingPackets()`),
// so the receiver matches fragment#0 by (flag==header.flag && Count==header.+12),
// then follows each fragment's +12 to the next.

namespace guild::sim {

// Geometry constants recovered from the four functions above.
constexpr u32 kPendingMaxBlock   = 0x3FE; // 1022 — StagePendingBlock length cap
constexpr u32 kFragChunkBytes    = 0x80;  // 128 — bytes copied per opcode-7 chunk
constexpr u32 kReassembleHeadCap = 0x7C;  // 124 — bytes taken from the FIRST fragment
constexpr u8  kOpFragment        = 7;     // opcode of a generated fragment packet
// Reassembly buffer capacity: first 124 + 2 (the +142 carry) + N*128. The largest
// observed consumer reads 0x35C (860) bytes; we size generously.
constexpr u32 kReassembleBufBytes = 0x600; // 1536 (126 + 8*128 worst case + slack)

// Pending-block staging + multi-fragment reassembly state (the original globals).
struct PendingState {
    // --- staging block (producer side) ---
    u8  block[2 + kPendingMaxBlock]; // word_1077F60 (len) || unk_1077F62 (data)
    u32 staged = 0;                  // dword_11AA4A0 — staged byte count (len+2)

    // --- reassembly buffer (consumer side) ---
    u8  reasm[kReassembleBufBytes];  // unk_1077B60 ...
    u32 reasm_len = 0;               // dword_11AA478 — total reassembled length

    PendingState() { Reset(); }
    void Reset() {
        for (u8& b : block) b = 0;
        staged = 0;
        for (u8& b : reasm) b = 0;
        reasm_len = 0;
    }

    // word_1077F60 accessor (block length header, little-endian u16).
    u16 block_len() const { return static_cast<u16>(block[0] | (block[1] << 8)); }
};

// gilde.exe 0x49436c — VIBE_Command_StagePendingBlock(len, src).
// If a block is already staged (state.staged != 0) or len > 0x3FE, returns 0
// (rejected). Otherwise writes the length word at block[0..1], copies `len` bytes
// of `src` after it, sets state.staged = len + 2, returns 1.
int StagePendingBlock(PendingState& state, u16 len, const void* src);

// gilde.exe 0x493584 — VIBE_Command_GeneratePendingPackets(q, state).
// Splits the staged block (state.block, state.staged bytes) into ceil(staged/128)
// opcode-7 fragment packets, enqueues each into the ring of `q`, chains them by
// Count through their +12 field, and clears the staging block. Returns the Count
// (== q.send_count()+1 at entry) of the FIRST generated fragment — this is what
// EnqueuePacket stores in the owning header packet's +12 link. Returns 0 if no
// block is staged or the queue is disconnected.
//
// NOTE: in the binary this is called from inside EnqueuePacket (which we must not
// redefine). The host integration is: enqueue the header packet, then if a block
// is staged call GeneratePendingPackets and write its return into the header's
// +12 field. EmitWithPendingBlock() below performs exactly that sequence.
u32 GeneratePendingPackets(CommandQueue& q, PendingState& state);

// Enqueue a header packet that owns a staged block: mirrors the EnqueuePacket tail
//   header->+12 = GeneratePendingPackets()
// without redefining EnqueuePacket. Returns the header packet's ring slot, or the
// raw EnqueuePacket result if no block is staged.
i32 EmitWithPendingBlock(CommandQueue& q, PendingState& state, CommandPacket& header);

// gilde.exe 0x49377c — VIBE_Command_ReassembleReceived(state, header, head).
// `header` is the packet that owns the fragment chain (its +12 holds the first
// fragment's Count); `head` is the received-list head to search for fragments.
// Walks the chain (match by flag byte +3 and Count +8), copies each fragment's
// payload into state.reasm, sets state.reasm_len from the first fragment's +16
// word, and marks consumed fragments' opcode byte to 0. Returns the number of
// fragments consumed (0 if the header carried no block, i.e. header.+12 == 0).
int ReassembleReceived(PendingState& state, CommandPacket& header,
                       CommandPacket* head, CommandPacket* (*next)(CommandPacket*));

// gilde.exe 0x4936e4 — VIBE_Command_CheckReassemblyComplete(header, head, next).
// Returns 1 if the packet is dispatchable now: it is a skip (opcode 7), it owns
// no block (header.+12 == 0), or every fragment of its chain has already arrived
// on `head`. Returns 0 if the chain is incomplete (a fragment is still missing).
int CheckReassemblyComplete(CommandPacket& header, CommandPacket* head,
                            CommandPacket* (*next)(CommandPacket*));

} // namespace guild::sim
