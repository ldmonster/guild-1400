#pragma once
#include "guild/common/types.h"
#include "sim/command.h"

#include <cstddef>
#include <vector>

// gilde.exe — guild::net  (MODULE: host->client SAVEGAME TRANSFER)
//
// When the host starts/loads a networked game it ships the whole savegame to every
// client over the lockstep command channel, as a header opcode-8 packet declaring
// the (128-padded) total length followed by a stream of opcode-9 packets each
// carrying a 128-byte chunk. The client reassembles the chunks into a heap buffer,
// wraps it as an in-memory VFS stream, and parses the save out of RAM.
//
// This module recovers that transfer protocol BYTE-FOR-BYTE from:
//   VIBE_Net_SendSaveGameToClients   @0x5abfe8  (read file, pad len, emit 8 then 9*)
//   VIBE_Command_EnqueueCmd08        @0x4943c0  (header: v2[0]=8, len at +16)
//   VIBE_Command_EnqueueCmd09Block   @0x4943e0  (chunk:  v3[0]=9, 128 bytes at +16)
//   VIBE_Command_HandleLoadBufAlloc  @0x4964a4  (rx op8: alloc len, cursor=0)
//   VIBE_Command_HandleLoadBufAppend @0x4964d8  (rx op9: memcpy 128, cursor+=128)
//   VIBE_Net_LoadReceivedSaveStream  @0x5ac140  (wait full, OpenMemoryStream, parse)
//
// The two opcodes write their payload at +16 (kFPayload) of the 153-byte command
// record, so the chunk fits well inside one packet. We REUSE the command codec
// (sim/command) for the record/encode; the file I/O and save-parse are the REUSED
// io/save + io/vfs layers (the consumer simply hands the reassembled bytes back).

namespace guild::net {

// ===========================================================================
// Recovered savegame-transfer wire layout
// ---------------------------------------------------------------------------
// HEADER packet (EnqueueCmd08 @0x4943c0):
//   +0x00  u8   opcode = 8
//   +0x01  u16  len    (ComputePacketSize: opcode 8 => 20 bytes)
//   +0x04  u32  cmdId  (ring slot, stamped by EnqueuePacket)
//   +0x08  u32  Count  (sequence)
//   +0x10  u32  totalLen  (the 128-padded byte length of the whole savegame)
//
// CHUNK packet (EnqueueCmd09Block @0x4943e0):
//   +0x00  u8   opcode = 9
//   +0x01  u16  len    (ComputePacketSize: opcode 9 => default 145 bytes)
//   +0x04  u32  cmdId
//   +0x08  u32  Count
//   +0x10  u8[128]  chunk data (kFragChunkBytes bytes of the savegame)
//
// LENGTH PADDING (VIBE_Net_SendSaveGameToClients @0x5ac03d):
//   total = ((fileLen >> 7) << 7) + 128;   // round DOWN to 128 then add one block
// i.e. padded = (fileLen / 128) * 128 + 128. Note this ALWAYS adds a trailing
// 128-byte block even when fileLen is an exact multiple of 128 (the last block's
// tail bytes are whatever the read buffer held — here zero-padded).
constexpr guild::u8  kOpSaveHeader = 8;     // VIBE_Command_EnqueueCmd08
constexpr guild::u8  kOpSaveChunk  = 9;     // VIBE_Command_EnqueueCmd09Block
constexpr guild::u32 kSaveChunkBytes = 0x80; // 128 — bytes per opcode-9 packet
constexpr guild::u32 kSaveTotalLenOffset = sim::kFPayload; // +0x10 in the header
constexpr guild::u32 kSaveChunkOffset    = sim::kFPayload; // +0x10 in each chunk

// gilde.exe 0x5ac03d — the 128-padding applied by SendSaveGameToClients.
inline guild::u32 PadSaveLength(guild::u32 fileLen) {
    return ((fileLen >> 7) << 7) + kSaveChunkBytes;   // (len/128)*128 + 128
}

// ===========================================================================
// Sender — VIBE_Net_SendSaveGameToClients @0x5abfe8
// ---------------------------------------------------------------------------
// Packetize `saveBytes` (the on-disk savegame content) into one opcode-8 header
// (totalLen = PadSaveLength(saveBytes.size())) followed by ceil(totalLen/128)
// opcode-9 chunk packets, enqueueing each into `q`. The original Sleeps every 32
// chunks and pumps messages; that pacing is a UI side-effect and omitted here. In
// standalone (single-player) mode the queue applies these locally, so the receiver
// reassembler below can consume them straight from the same queue. Returns the
// number of chunk packets emitted, or -1 on a (here impossible) read error.
//
// `out` (optional) collects the exact framed bytes of every emitted packet, in
// order, so a test can verify the on-wire byte stream (header then chunks).
int SendSaveGameToClients(sim::CommandQueue& q, const std::vector<guild::u8>& saveBytes,
                          std::vector<std::vector<guild::u8>>* out = nullptr);

// ===========================================================================
// Receiver reassembly — the cmd08/cmd09 apply handlers + LoadReceivedSaveStream
// ---------------------------------------------------------------------------
// Models dword_11AA48C (heap buffer) / dword_11AA490 (total len) / dword_11AA464
// (cursor). HandleLoadBufAlloc seeds it from an opcode-8 packet; HandleLoadBufAppend
// copies a 128-byte chunk from an opcode-9 packet. SaveStreamReassembler::complete()
// mirrors the LoadReceivedSaveStream readiness test
// `dword_11AA48C && dword_11AA464 >= dword_11AA490`.
struct SaveStreamReassembler {
    std::vector<guild::u8> buf;   // dword_11AA48C (allocated on opcode 8)
    guild::u32 total  = 0;        // dword_11AA490
    guild::u32 cursor = 0;        // dword_11AA464
    bool       allocated = false; // dword_11AA48C != 0

    // gilde.exe 0x4964a4 — opcode-8 apply: totalLen <- pkt[+16]; alloc; cursor=0.
    void HandleLoadBufAlloc(const sim::CommandPacket& pkt);
    // gilde.exe 0x4964d8 — opcode-9 apply: memcpy(buf+cursor, pkt[+16], 128); +=128.
    void HandleLoadBufAppend(const sim::CommandPacket& pkt);

    // gilde.exe 0x5ac162 — the LoadReceivedSaveStream wait predicate.
    bool complete() const { return allocated && cursor >= total; }
};

// gilde.exe 0x5ac140 — VIBE_Net_LoadReceivedSaveStream (transport+parse glue).
// Once the reassembler is complete, the original wraps dword_11AA48C as an
// in-memory VFS stream and parses the save (header + scalar block + tables). Here
// the transfer's job is to deliver the byte-exact reassembled buffer; the parse is
// the REUSED io/save path. Returns the reassembled savegame bytes (the first
// `total` bytes of buf), which equal the padded sender stream.
std::vector<guild::u8> LoadReceivedSaveStream(const SaveStreamReassembler& r);

// ===========================================================================
// One-shot driver (test/standalone): packetize, deliver locally, reassemble.
// ---------------------------------------------------------------------------
// Installs cmd08/cmd09 handlers on `q`, sends `saveBytes`, drives the queue's
// flush+exec so the packets apply locally, and returns the reassembled bytes. This
// is the single-machine (single-player / loopback host==client) transfer path.
std::vector<guild::u8> TransferSaveLocally(sim::CommandQueue& q,
                                           const std::vector<guild::u8>& saveBytes);

} // namespace guild::net
