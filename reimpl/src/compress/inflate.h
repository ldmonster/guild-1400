#pragma once
#include "guild/common/types.h"
#include "compress/zlib.h"
#include <cstddef>
#include <vector>

// Vendored zlib 1.1.x inflate (decompression) embedded in gilde.exe. This is the
// classic 1.1.x architecture: an inflate_blocks state machine driving inflate_codes,
// with Huffman tables built by huft_build and a fast inner loop (inflate_fast).
//
// Translated functions (gilde.exe addresses):
//   VIBE_Inflate_Reset            @0x5ec824   inflateReset
//   VIBE_Inflate_End              @0x5ec87c   inflateEnd
//   VIBE_Inflate_Init2            @0x5ec8c4   inflateInit2_
//   VIBE_Inflate_Process          @0x5eca44   inflate (zlib-header wrapper FSM)
//   VIBE_Inflate_BlocksReset      @0x5feb0c   inflate_blocks_reset
//   VIBE_Inflate_BlocksNew        @0x5feb7c   inflate_blocks_new
//   VIBE_Inflate_BlocksProcess    @0x5fec3c   inflate_blocks
//   VIBE_Inflate_BlocksFree       @0x5ffa80   inflate_blocks_free
//   VIBE_Inflate_FlushWindow      @0x607d30   inflate_flush
//   VIBE_Inflate_CreateBlocksState@0x6074a0   inflate_codes_new
//   VIBE_Inflate_DecodeCodes      @0x60751c   inflate_codes
//   VIBE_Inflate_BuildHuffmanTree @0x6080e8   huft_build
//   VIBE_Inflate_BuildBitsTree    @0x608760   inflate_trees_bits
//   VIBE_Inflate_BuildDynamicTrees@0x6087fc   inflate_trees_dynamic
//   VIBE_Inflate_GetFixedTrees    @0x60899c   inflate_trees_fixed
//   VIBE_Decompress_InflateFast   @0x60a1f0   inflate_fast
//   VIBE_Zlib_Adler32             @0x5ffaf0   (see zlib.h)
//
// The original wires inflate to the game allocator and operates on a z_stream
// (next_in/avail_in/total_in/next_out/avail_out/total_out/msg/state/...). The
// reconstruction keeps the algorithm bit-exact but exposes an in-memory driver
// (Inflate / InflateRaw) plus the low-level Inflater for streaming.
//
// NOTE on framing: the 1.1.x core handles RAW deflate (windowBits < 0) and ZLIB
// (windowBits in 8..15). GZIP framing is NOT handled here in the original — it
// lived in the separate gzio.c FILE wrapper. See gzip.h for an in-memory gzip
// reader built on InflateRaw.
namespace guild::compress {

// A Huffman decode table entry (struct inflate_huft / "huft").
//   +0x00 e : op bits below 16 -> #extra bits; 16 = literal; >16 = next-table op
//   +0x01 b : number of bits this code consumes
//   +0x04 base/next : literal value, length/distance base, or table offset
struct InflateHuft {
    u8  exop;   // +0x00
    u8  bits;   // +0x01
    u16 pad;    // +0x02 (alignment; the original stores e/b in a 4-byte word)
    u32 base;   // +0x04 (n union: value/base, or t pointer-as-offset)
};

// inflate return states are the zlib codes in zlib.h (kZOk, kZStreamEnd, ...).

// Streaming inflater. Mirrors the z_stream + internal_state + inflate_blocks_state
// triple but flattened. windowBits: 8..15 = zlib header; negative = raw deflate.
class Inflater {
public:
    explicit Inflater(int windowBits = 15);
    // inflate_blocks_free @0x5ffa80 / inflateEnd: release the owned Blocks state
    // (and its codes). Out-of-line because Blocks is an incomplete type here.
    ~Inflater();

    // Reset to start of a fresh stream (keeps the allocated window).
    void Reset();

    // Feed compressed input; appends decoded bytes to `out`. Returns a zlib code:
    //   kZStreamEnd  - stream finished (zlib: checksum verified)
    //   kZOk         - more input expected
    //   kZDataError / kZStreamError / kZMemError - error (msg() set)
    int Process(const u8* in, std::size_t in_len, std::vector<u8>& out, int flush = kZFinish);

    const char* msg() const { return msg_; }
    bool raw() const { return raw_; }

private:
    // --- z_stream-ish bookkeeping --------------------------------------------
    const u8* next_in_  = nullptr;
    std::size_t avail_in_ = 0;
    std::vector<u8>* out_ = nullptr;
    const char* msg_ = nullptr;
    bool raw_ = false;
    int  wbits_ = 15;

    // wrapper FSM (inflate state mode, see VIBE_Inflate_Process)
    int  mode_   = 0;     // 0=METHOD ... matches the byte at *state
    u32  method_ = 0;
    u32  checkComputed_ = 0; // running adler of output
    u32  checkExpected_ = 0; // value read from trailer
    bool needsAdler_ = true; // false for raw

public:
    // inflate_blocks state (public only so the file-local decoder helpers in
    // inflate.cpp can name it; not part of the stable API).
    struct Blocks;

private:
    Blocks* b_ = nullptr;
    std::vector<u8> window_;

    void NewBlocks();
    void BlocksReset();
    int  RunBlocks(int r); // returns blocks-level code, draining window to out_
};

// One-shot helpers (allocate, run to completion, free).
// Inflate: expects a zlib stream (0x78 ...). Returns true on success.
bool Inflate(const u8* in, std::size_t in_len, std::vector<u8>& out);
// InflateRaw: headerless DEFLATE (zlib windowBits = -15).
bool InflateRaw(const u8* in, std::size_t in_len, std::vector<u8>& out);

} // namespace guild::compress
