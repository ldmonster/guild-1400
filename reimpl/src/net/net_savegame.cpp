#include "net/net_savegame.h"
#include "sim/command.h"

#include <cstring>

// Translation notes
// -----------------
// The transfer is split across three binary sites: the producer
// (SendSaveGameToClients) which reads the file and emits opcode-8 + opcode-9*
// packets, and the two apply handlers (HandleLoadBufAlloc / HandleLoadBufAppend)
// that the receiver's ExecCommands dispatches as the packets arrive. We translate
// all three 1:1. The packetization byte layout — opcode at +0, payload at +16,
// 128-byte chunks, 128-padded total length — is preserved exactly; the queue's
// own EnqueuePacket stamps len/cmdId/Count so the framed bytes match the wire.

namespace guild::net {

// gilde.exe 0x5abfe8 — VIBE_Net_SendSaveGameToClients (read/pad/emit core).
int SendSaveGameToClients(sim::CommandQueue& q, const std::vector<guild::u8>& saveBytes,
                          std::vector<std::vector<guild::u8>>* out) {
    const guild::u32 fileLen = static_cast<guild::u32>(saveBytes.size());
    const guild::u32 total   = PadSaveLength(fileLen);   // 0x5ac03d padding

    // --- opcode-8 header: totalLen at +16 (EnqueueCmd08 @0x4943c0) ----------
    sim::CommandPacket hdr{};
    hdr.bytes[sim::kFOpcode] = kOpSaveHeader;            // v2[0] = 8
    hdr.put32(kSaveTotalLenOffset, total);               // *(v2+16) = a1 (padded len)
    q.EnqueuePacket(hdr);
    if (out) {
        // After EnqueuePacket the ring slot holds the framed packet (len/cmdId/Count
        // stamped). The framed length for opcode 8 is 20 bytes.
        const sim::CommandPacket& framed = q.ring_slot(static_cast<guild::u32>(q.send_count()) & sim::kSeqMask);
        out->emplace_back(framed.bytes, framed.bytes + framed.len());
    }

    // --- opcode-9 chunks: 128 bytes each at +16 (EnqueueCmd09Block @0x4943e0) -
    int chunks = 0;
    for (guild::u32 off = 0; off < total; off += kSaveChunkBytes) {
        sim::CommandPacket chunk{};
        chunk.bytes[sim::kFOpcode] = kOpSaveChunk;       // v3[0] = 9
        // qmemcpy(v4, a1, 128): copy 128 source bytes (zero-padded past fileLen).
        for (guild::u32 i = 0; i < kSaveChunkBytes; ++i) {
            guild::u32 src = off + i;
            chunk.bytes[kSaveChunkOffset + i] = (src < fileLen) ? saveBytes[src] : 0;
        }
        q.EnqueuePacket(chunk);
        if (out) {
            const sim::CommandPacket& framed = q.ring_slot(static_cast<guild::u32>(q.send_count()) & sim::kSeqMask);
            out->emplace_back(framed.bytes, framed.bytes + framed.len());
        }
        ++chunks;
    }
    return chunks;
}

// gilde.exe 0x4964a4 — VIBE_Command_HandleLoadBufAlloc (opcode-8 apply).
void SaveStreamReassembler::HandleLoadBufAlloc(const sim::CommandPacket& pkt) {
    total  = pkt.get32(kSaveTotalLenOffset);  // dword_11AA490 = *(a1+16)
    buf.assign(total, 0);                     // dword_11AA48C = alloc(total)
    cursor = 0;                               // dword_11AA464 = 0
    allocated = true;
}

// gilde.exe 0x4964d8 — VIBE_Command_HandleLoadBufAppend (opcode-9 apply).
void SaveStreamReassembler::HandleLoadBufAppend(const sim::CommandPacket& pkt) {
    if (!allocated)
        return;
    // qmemcpy(buf + cursor, a1 + 16, 0x80); cursor += 128.
    const guild::u32 n = kSaveChunkBytes;
    for (guild::u32 i = 0; i < n; ++i) {
        if (cursor + i < buf.size())
            buf[cursor + i] = pkt.bytes[kSaveChunkOffset + i];
    }
    cursor += kSaveChunkBytes;
}

// gilde.exe 0x5ac140 — VIBE_Net_LoadReceivedSaveStream (deliver the bytes).
std::vector<guild::u8> LoadReceivedSaveStream(const SaveStreamReassembler& r) {
    std::vector<guild::u8> result;
    if (!r.complete())
        return result;
    result.assign(r.buf.begin(), r.buf.begin() + r.total);
    return result;
}

// --- local one-shot driver --------------------------------------------------
// The apply handlers reference the active reassembler through this pointer (the
// original handlers write the file-global dword_11AA48C; here we bind a per-call
// instance for testability). Single-threaded, matching the original lockstep.
namespace {
SaveStreamReassembler* g_activeReasm = nullptr;

void Op8Handler(sim::CommandQueue&, sim::CommandPacket& pkt, sim::AckEntry*) {
    if (g_activeReasm) g_activeReasm->HandleLoadBufAlloc(pkt);
}
void Op9Handler(sim::CommandQueue&, sim::CommandPacket& pkt, sim::AckEntry*) {
    if (g_activeReasm) g_activeReasm->HandleLoadBufAppend(pkt);
}
} // namespace

std::vector<guild::u8> TransferSaveLocally(sim::CommandQueue& q,
                                           const std::vector<guild::u8>& saveBytes) {
    SaveStreamReassembler reasm;
    g_activeReasm = &reasm;
    q.set_handler(kOpSaveHeader, &Op8Handler);
    q.set_handler(kOpSaveChunk,  &Op9Handler);

    SendSaveGameToClients(q, saveBytes);
    // Drain: standalone => flush applies locally to the received list; exec
    // dispatches the cmd08/cmd09 handlers in sequence order, reassembling the buf.
    q.FlushSendQueue();
    q.ExecCommands();

    std::vector<guild::u8> bytes = LoadReceivedSaveStream(reasm);
    g_activeReasm = nullptr;
    return bytes;
}

} // namespace guild::net
