// Unit + malformed-input tests for net/net_savegame — the host->client savegame
// transfer reassembler (wave-11 hardening, ASAN-exercised).
//
//   VIBE_Net_SendSaveGameToClients   @0x5abfe8
//   VIBE_Command_HandleLoadBufAlloc  @0x4964a4  (opcode-8: declare total, alloc)
//   VIBE_Command_HandleLoadBufAppend @0x4964d8  (opcode-9: append 128-byte chunk)
//   VIBE_Net_LoadReceivedSaveStream  @0x5ac140  (deliver the reassembled bytes)
//
// The opcode-8/opcode-9 payloads are parsed straight from remote command packets,
// so the declared total length and the chunk appends are untrusted. A crafted
// opcode-8 declaring a tiny/huge total, an opcode-9 arriving before its opcode-8,
// or more chunks than the buffer holds must never read or write out of bounds.
#include "test.h"

#include "net/net_savegame.h"
#include "sim/command.h"

#include <cstring>
#include <vector>

using namespace guild;
using namespace guild::net;

namespace {

// Build an opcode-8 header packet declaring `total` bytes at +16.
sim::CommandPacket MakeOp8(u32 total) {
    sim::CommandPacket p{};
    p.bytes[sim::kFOpcode] = kOpSaveHeader;        // 8
    p.put32(kSaveTotalLenOffset, total);           // declared total at +16
    return p;
}

// Build an opcode-9 chunk packet whose 128-byte payload at +16 is `fill`.
sim::CommandPacket MakeOp9(u8 fill) {
    sim::CommandPacket p{};
    p.bytes[sim::kFOpcode] = kOpSaveChunk;          // 9
    for (u32 i = 0; i < kSaveChunkBytes; ++i)
        p.bytes[kSaveChunkOffset + i] = fill;
    return p;
}

}  // namespace

// --- Happy path: alloc then append fills the buffer, complete() flips ----------
TEST(NetSavegame, AllocThenAppendReassembles) {
    SaveStreamReassembler r;
    r.HandleLoadBufAlloc(MakeOp8(2 * kSaveChunkBytes));  // 256-byte stream
    CHECK(r.allocated);
    CHECK_EQ(r.total, 2u * kSaveChunkBytes);
    CHECK(!r.complete());

    r.HandleLoadBufAppend(MakeOp9(0x11));
    CHECK(!r.complete());                  // 128 of 256
    r.HandleLoadBufAppend(MakeOp9(0x22));
    CHECK(r.complete());                   // 256 of 256

    std::vector<u8> out = LoadReceivedSaveStream(r);
    CHECK_EQ(out.size(), 2u * kSaveChunkBytes);
    for (u32 i = 0; i < kSaveChunkBytes; ++i) CHECK_EQ(out[i], (u8)0x11);
    for (u32 i = kSaveChunkBytes; i < 2 * kSaveChunkBytes; ++i) CHECK_EQ(out[i], (u8)0x22);
}

// --- opcode-9 arriving BEFORE its opcode-8 (no buffer yet) must be a no-op ------
// HandleLoadBufAppend early-outs when !allocated; without that guard it would
// memcpy 128 bytes into an empty/unallocated buffer.
TEST(NetSavegame, AppendBeforeAllocIsNoOp) {
    SaveStreamReassembler r;
    CHECK(!r.allocated);
    r.HandleLoadBufAppend(MakeOp9(0xFF));  // must not touch memory
    CHECK(!r.allocated);
    CHECK_EQ(r.cursor, 0u);
    CHECK(r.buf.empty());
    CHECK(!r.complete());
    // LoadReceivedSaveStream on an un-allocated reassembler returns empty (guarded).
    CHECK(LoadReceivedSaveStream(r).empty());
}

// --- A 0-length declared total: buffer is empty, complete() is immediately true,
// and no append can write into it. -------------------------------------------
TEST(NetSavegame, ZeroLengthDeclaredTotal) {
    SaveStreamReassembler r;
    r.HandleLoadBufAlloc(MakeOp8(0));
    CHECK(r.allocated);
    CHECK_EQ(r.total, 0u);
    CHECK_EQ(r.buf.size(), 0u);
    CHECK(r.complete());                    // cursor(0) >= total(0)
    // An append against a 0-byte buffer must write nothing (every index is OOB).
    r.HandleLoadBufAppend(MakeOp9(0x7E));
    CHECK_EQ(r.buf.size(), 0u);
    CHECK(LoadReceivedSaveStream(r).empty());
}

// --- MORE chunks than the declared buffer holds: the per-byte bound stops the
// copy at buf.size(); the surplus chunk writes nothing past the end. -----------
TEST(NetSavegame, OverAppendBeyondBufferStaysInBounds) {
    SaveStreamReassembler r;
    r.HandleLoadBufAlloc(MakeOp8(kSaveChunkBytes));   // room for exactly one chunk
    r.HandleLoadBufAppend(MakeOp9(0xAA));             // fills the buffer
    CHECK_EQ(r.cursor, kSaveChunkBytes);
    // A second chunk arrives though the buffer is full: must not overrun the 128B.
    r.HandleLoadBufAppend(MakeOp9(0xBB));
    CHECK_EQ(r.buf.size(), (std::size_t)kSaveChunkBytes);  // buffer unchanged size
    for (u32 i = 0; i < kSaveChunkBytes; ++i) CHECK_EQ(r.buf[i], (u8)0xAA); // not 0xBB
    CHECK(r.complete());
}

// --- A non-chunk-aligned declared total: the last partial chunk's copy clamps to
// buf.size(), and the delivered stream is exactly `total` bytes. ---------------
TEST(NetSavegame, NonAlignedTotalClampsLastChunk) {
    SaveStreamReassembler r;
    const u32 total = kSaveChunkBytes + 40;   // 168: one full chunk + a 40-byte tail
    r.HandleLoadBufAlloc(MakeOp8(total));
    r.HandleLoadBufAppend(MakeOp9(0x01));     // bytes 0..127
    CHECK(!r.complete());                     // cursor 128 < 168
    r.HandleLoadBufAppend(MakeOp9(0x02));     // would write 128..255, clamped to 168
    CHECK(r.complete());                      // cursor 256 >= 168
    CHECK_EQ(r.buf.size(), (std::size_t)total);
    // tail beyond 168 was never written (clamped); first 40 of the 2nd chunk landed.
    for (u32 i = kSaveChunkBytes; i < total; ++i) CHECK_EQ(r.buf[i], (u8)0x02);
    std::vector<u8> out = LoadReceivedSaveStream(r);
    CHECK_EQ(out.size(), (std::size_t)total);
}

// --- A huge declared total that exceeds the cursor must keep complete()==false so
// LoadReceivedSaveStream never copies a length larger than the buffer. ---------
// (The alloc still sizes buf to `total`; we keep `total` modest here so the test
// doesn't try a multi-GB allocation — the point is the cursor/total/buf invariant.)
TEST(NetSavegame, IncompleteStreamYieldsEmpty) {
    SaveStreamReassembler r;
    r.HandleLoadBufAlloc(MakeOp8(4 * kSaveChunkBytes));   // need 4 chunks
    r.HandleLoadBufAppend(MakeOp9(0x55));                 // only 1 delivered
    CHECK(!r.complete());
    // The transfer is incomplete: deliver nothing (the wait predicate is unmet).
    CHECK(LoadReceivedSaveStream(r).empty());
}

// --- LoadReceivedSaveStream defensive clamp: a total larger than buf must never
// read past the end of buf even if complete() were somehow satisfied. ----------
TEST(NetSavegame, LoadClampsTotalToBufferSize) {
    SaveStreamReassembler r;
    r.allocated = true;
    r.buf.assign(64, 0x09);     // only 64 bytes actually allocated
    r.total = 1u << 20;         // but `total` claims a megabyte (desync / hostile)
    r.cursor = r.total;         // force complete() == true
    CHECK(r.complete());
    std::vector<u8> out = LoadReceivedSaveStream(r);
    CHECK_EQ(out.size(), 64u);  // clamped to the real buffer, no OOB read
    for (u8 b : out) CHECK_EQ(b, (u8)0x09);
}

// --- Sender padding: PadSaveLength always rounds down to 128 then adds a block --
TEST(NetSavegame, PadSaveLengthGolden) {
    CHECK_EQ(PadSaveLength(0u),   (u32)kSaveChunkBytes);          // 128
    CHECK_EQ(PadSaveLength(1u),   (u32)kSaveChunkBytes);          // 128
    CHECK_EQ(PadSaveLength(127u), (u32)kSaveChunkBytes);          // 128
    CHECK_EQ(PadSaveLength(128u), (u32)(2 * kSaveChunkBytes));    // 256 (exact mult => +block)
    CHECK_EQ(PadSaveLength(200u), (u32)(2 * kSaveChunkBytes));    // 256
}

// --- Sender of an EMPTY savegame still emits the trailing padding block ---------
TEST(NetSavegame, SendEmptySaveEmitsOneChunk) {
    sim::CommandQueue q;
    q.Init();
    q.set_standalone(true);
    q.set_disconnected(false);
    std::vector<u8> empty;
    int chunks = SendSaveGameToClients(q, empty);
    CHECK_EQ(chunks, 1);    // total == 128 => exactly one opcode-9 chunk
}
