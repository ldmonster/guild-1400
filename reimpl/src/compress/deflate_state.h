#pragma once
// Module-private layout shared between deflate.cpp (LZ77 + driver) and trees.cpp
// (Huffman tree builders). This mirrors zlib 1.1.4's deflate_state / ct_data; the
// original deflate_state struct in gilde.exe is accessed through the byte offsets
// seen in the pseudocode (e.g. window=+0x30, strstart=+0x64, bi_buf=+0x16B0,
// pending=+0x14). Here it is expressed as a readable struct with the same fields.
#include "guild/common/types.h"
#include <vector>

namespace guild::compress {

// --- deflate constants (zlib 1.1.4 deflate.h / trees.h) ----------------------
constexpr int kLengthCodes = 29;            // # length codes (not counting EOB)
constexpr int kLiterals    = 256;
constexpr int kLCodes      = kLiterals + 1 + kLengthCodes; // 286
constexpr int kDCodes      = 30;
constexpr int kBlCodes     = 19;
constexpr int kHeapSize    = 2 * kLCodes + 1; // 573
constexpr int kMaxBits     = 15;
constexpr int kMaxBlBits   = 7;
constexpr int kEndBlock    = 256;
constexpr int kRep3_6      = 16;
constexpr int kRepZ3_10    = 17;
constexpr int kRepZ11_138  = 18;

constexpr int kMinMatch = 3;
constexpr int kMaxMatch = 258;
constexpr int kMinLookahead = kMaxMatch + kMinMatch + 1; // 262
constexpr int kStoredBlock = 0;
constexpr int kStaticTrees = 1;
constexpr int kDynTrees    = 2;
constexpr int kBufSize     = 16; // bit buffer width

// deflate_state.status values
constexpr int kInitState = 42;
constexpr int kBusyState = 113;
constexpr int kFinishState = 666;

// One tree node: { freq | code }  and  { dad | len }. zlib unions these; we keep
// them as separate u16s. Offsets in the original are +0 (fc) and +2 (dl).
struct CtData {
    u16 fc; // +0x00  frequency, or final Huffman code
    u16 dl; // +0x02  parent in tree (dad), or final code length (len)
};

// static_tree_desc — mirrors gilde.exe static_l_desc/static_d_desc/bl_desc.
struct StaticTreeDesc {
    const CtData* static_tree; // +0x00
    const int*    extra_bits;  // +0x04
    int           extra_base;  // +0x08
    int           elems;       // +0x0C
    int           max_length;  // +0x10
};

// tree_desc — the per-tree descriptor stored in deflate_state.
struct TreeDesc {
    CtData* dyn_tree;             // +0x00
    int     max_code;            // +0x04
    const StaticTreeDesc* stat;  // +0x08
};

// config_s — configuration_table entry (good/lazy/nice/chain per level).
struct Config {
    u16 good_length;
    u16 max_lazy;
    u16 nice_length;
    u16 max_chain;
    int func; // 0=stored, 1=fast, 2=slow
};

// configuration_table @0x5ECFD8 — recovered byte-for-byte from gilde.exe.
// (The static Huffman trees @0x5FFD7C/@0x6001FC and the length/distance lookup
// tables are defined in trees.cpp; the symbol-buffering helper in deflate.cpp
// needs only kLengthCode/kDistCode, declared below.)
extern const Config kConfigurationTable[10];
extern const u8  kDistCode[512];                            // @0x600274 + @0x600374
extern const u8  kLengthCode[kMaxMatch - kMinMatch + 1];    // @0x600474
extern const int kBaseLength[kLengthCodes];                 // @0x600574
extern const int kBaseDist[kDCodes];                        // @0x6005E8
extern const int kExtraLbits[kLengthCodes];                 // @0x5FFC30
extern const int kExtraDbits[kDCodes];                      // @0x5FFCA4
extern const int kExtraBlbits[kBlCodes];                    // @0x5FFD1C
extern const u8  kBlOrder[kBlCodes];                        // @0x5FFD68

// The deflate_state. Field names follow zlib; comments note original byte offsets.
struct DeflateState {
    // --- z_stream-ish I/O (the original keeps a real z_stream; flattened here) -
    const u8* next_in = nullptr;   // strm->next_in
    std::size_t avail_in = 0;      // strm->avail_in
    std::size_t total_in = 0;      // strm->total_in
    std::vector<u8>* out = nullptr;// strm->next_out sink
    std::size_t total_out = 0;     // strm->total_out
    u32 adler = 1;                 // strm->adler

    int status = 0;        // +0x04
    std::vector<u8> pending_buf; // +0x08 (lit_bufsize*... bytes)
    int lit_bufsize = 0;
    std::size_t pending_out = 0; // +0x10 index into pending_buf
    std::size_t pending = 0;     // +0x14 # bytes queued
    int noheader = 0;            // +0x18 (raw if set)
    u8  method = 8;              // +0x1D
    int last_flush = 0;          // +0x20

    // --- LZ77 window -------------------------------------------------------
    unsigned w_size = 0;   // +0x24
    unsigned w_bits = 0;   // +0x28
    unsigned w_mask = 0;   // +0x2C
    std::vector<u8>  window; // +0x30 (2*w_size)
    unsigned window_size = 0; // +0x34
    std::vector<u16> prev;   // +0x38 (w_size)
    std::vector<u16> head;   // +0x3C (hash_size)
    unsigned ins_h = 0;      // +0x40
    unsigned hash_size = 0;  // +0x44
    unsigned hash_bits = 0;  // +0x48
    unsigned hash_mask = 0;  // +0x4C
    unsigned hash_shift = 0; // +0x50
    long block_start = 0;    // +0x54
    unsigned match_length = 0; // +0x58
    unsigned prev_match = 0;   // +0x5C
    int match_available = 0;   // +0x60
    unsigned strstart = 0;     // +0x64
    unsigned match_start = 0;  // +0x68
    unsigned lookahead = 0;    // +0x6C
    unsigned prev_length = 0;  // +0x70
    unsigned max_chain_length = 0; // +0x74
    unsigned max_lazy_match = 0;   // +0x78 (== max_insert_length)
    int level = 0;        // +0x7C
    int strategy = 0;     // +0x80
    unsigned good_match = 0; // +0x84
    int nice_match = 0;      // +0x88

    // --- Huffman trees -----------------------------------------------------
    CtData dyn_ltree[kHeapSize];        // +0x8C  (HEAP_SIZE entries)
    CtData dyn_dtree[2*kDCodes + 1];    // +0x980
    CtData bl_tree[2*kBlCodes + 1];     // +0xA74
    TreeDesc l_desc;                    // +0xB10
    TreeDesc d_desc;                    // +0xB1C
    TreeDesc bl_desc;                   // +0xB28
    u16 bl_count[kMaxBits + 1];         // +0xB34
    int heap[kHeapSize];                // +0xB54 (heap[2*L_CODES+1])
    int heap_len = 0;                   // +0x1448
    int heap_max = 0;                   // +0x144C
    u8  depth[kHeapSize];               // +0x1450
    // Symbol buffers. The original overlaps these inside pending_buf (d_buf as
    // u16 at +0x16AC, l_buf as u8 at +0x16A0) to save memory; the algorithm is
    // identical with separate arrays, so we keep them split for clarity.
    std::vector<u8>  l_buf;            // +0x16A0  one byte per symbol (literal/len-3)
    std::vector<u16> d_buf;            // +0x16AC  match distance (0 = literal)
    unsigned last_lit = 0;             // +0x16A8  # symbols buffered this block
    unsigned long opt_len = 0;         // +0x16B0  bit length of current block (dyn)
    unsigned long static_len = 0;      // +0x16B4  bit length if static trees used
    int last_eob_len = 8;              // length in bits of the EOB code last sent
    u16 bi_buf = 0;                    // +0x16B0  bit accumulator
    int bi_valid = 0;                  // +0x16B4  # valid bits in bi_buf
    int data_type = 2;                 // +0x1C    Z_UNKNOWN until set_data_type
};

// trees.cpp entry points (called from deflate.cpp).
void TreeInit(DeflateState* s);          // _tr_init
void InitBlock(DeflateState* s);         // init_block
void TrFlushBlock(DeflateState* s, int buf, unsigned stored_len, int last);
void TrAlign(DeflateState* s);           // _tr_align
void TrStoredBlock(DeflateState* s, int buf, unsigned stored_len, int last);
void BiWindup(DeflateState* s);          // bi_windup

} // namespace guild::compress
