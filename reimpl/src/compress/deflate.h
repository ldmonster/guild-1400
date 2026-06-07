#pragma once
#include "guild/common/types.h"
#include "compress/zlib.h"
#include <cstddef>
#include <vector>

// Vendored zlib 1.1.4 DEFLATE (compression) embedded in gilde.exe. The Inflate
// half lives in inflate.{h,cpp}; this is the matching encoder. It is the classic
// 1.1.x architecture: a sliding-window LZ77 matcher (deflate_stored / deflate_fast
// / deflate_slow chosen per level) feeding a literal/length-distance symbol buffer,
// which trees.c turns into dynamic or static Huffman blocks.
//
// Translated functions (gilde.exe addresses):
//   VIBE_Deflate_Init2          @0x5ed068  deflateInit2_
//   VIBE_Deflate_Reset          @0x5ed3b0  deflateReset
//   VIBE_Deflate_PutShort       @0x5ed54c  putShortMSB
//   VIBE_Deflate_FlushPending   @0x5ed578  flush_pending
//   VIBE_Deflate_Process        @0x5ed5f0  deflate
//   VIBE_Deflate_End            @0x5ed8b4  deflateEnd
//   VIBE_Deflate_ReadBuffer     @0x5edb40  read_buf
//   VIBE_Deflate_InitMatch      @0x5edbc0  lm_init
//   VIBE_Deflate_LongestMatch   @0x5edc94  longest_match
//   VIBE_Deflate_FillWindow     @0x5ede64  fill_window
//   VIBE_Deflate_Stored         @0x5ee020  deflate_stored
//   VIBE_Deflate_Fast           @0x5ee1b0  deflate_fast
//   VIBE_Deflate_Slow           @0x5ee530  deflate_slow
//   VIBE_Deflate_TreeInit       @0x600664  _tr_init
//   VIBE_Deflate_TreeClear      @0x6006cc  init_block
//   VIBE_Deflate_PqDownHeap     @0x600748  pqdownheap
//   VIBE_Deflate_GenBitlen      @0x600864  gen_bitlen
//   VIBE_Deflate_GenCodes       @0x600ae0  gen_codes
//   VIBE_Deflate_BuildTree      @0x600b5c  build_tree
//   VIBE_Deflate_ScanTree       @0x600e10  scan_tree
//   VIBE_Deflate_SendTree       @0x600f08  send_tree
//   VIBE_Deflate_BuildBlTree    @0x60168c  build_bl_tree
//   VIBE_Deflate_SendAllTrees   @0x6016fc  send_all_trees
//   VIBE_Deflate_StoredBlock    @0x601a5c  _tr_stored_block
//   VIBE_Deflate_AlignBits      @0x601b34  _tr_align
//   VIBE_Deflate_FlushBlock     @0x601ea4  _tr_flush_block
//   VIBE_Deflate_CompressBlock  @0x6021a0  compress_block
//   VIBE_Deflate_DetectDataType @0x602720  set_data_type
//   VIBE_Deflate_BiReverse      @0x602790  bi_reverse
//   VIBE_Deflate_FlushBits      @0x6027ac  bi_flush
//   VIBE_Deflate_BiWindup       @0x602848  bi_windup
//   VIBE_Quant_EncodeRunLength  @0x6028b8  copy_block (shared with another module)
//
// The original wires deflate to the game allocator and a z_stream; this keeps the
// algorithm bit-exact but exposes an in-memory driver (Deflate / DeflateRaw) plus
// a streaming Deflater. Output is decodable by any conforming inflate (including
// the sibling Inflater and system zlib), but is NOT required to be byte-identical
// to system zlib (memLevel/state differences are allowed).
namespace guild::compress {

struct DeflateState; // defined in deflate_state.h (module-private)

// Streaming deflater mirroring z_stream + deflate_state, flattened. windowBits
// 8..15 emit a zlib header+adler trailer; negative (-8..-15) emit raw DEFLATE.
class Deflater {
public:
    // level 0..9 (0 = stored, -1 = default 6). windowBits 8..15 (zlib) or negated
    // (raw). Returns false if parameters are invalid / allocation fails.
    explicit Deflater(int level = 6, int windowBits = 15);
    ~Deflater();
    Deflater(const Deflater&) = delete;
    Deflater& operator=(const Deflater&) = delete;

    // Feed all of `in`, append compressed bytes to `out`, finishing the stream.
    // Returns true on success (kZStreamEnd).
    bool Run(const u8* in, std::size_t in_len, std::vector<u8>& out);

    bool ok() const { return ok_; }

private:
    DeflateState* s_ = nullptr; // owned
    bool ok_ = false;
};

// One-shot helpers.
// Deflate: produce a zlib stream (header + deflate + adler32). Returns true ok.
bool Deflate(const u8* in, std::size_t in_len, std::vector<u8>& out, int level = 6);
// DeflateRaw: headerless DEFLATE (windowBits = -15).
bool DeflateRaw(const u8* in, std::size_t in_len, std::vector<u8>& out, int level = 6);

} // namespace guild::compress
