// trees.cpp — VIBE_Deflate_* Huffman tree builders (zlib 1.1.4 trees.c) plus the
// static length/distance tables recovered byte-for-byte from gilde.exe.
//
// The static_ltree/static_dtree/length_code/dist_code/base_* tables are emitted
// here exactly as the binary stores them (addresses noted per table). The dynamic
// tree builders (build_tree / scan_tree / send_tree / gen_bitlen / gen_codes /
// pqdownheap / compress_block) are 1:1 translations of the corresponding
// VIBE_Deflate_* functions.
#include "compress/deflate_state.h"

namespace guild::compress {

// =============================================================================
// Static tables (recovered from gilde.exe).
// =============================================================================

// extra_lbits @0x5FFC30
const int kExtraLbits[kLengthCodes] = {
    0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2,3,3,3,3,4,4,4,4,5,5,5,5,0
};
// extra_dbits @0x5FFCA4
const int kExtraDbits[kDCodes] = {
    0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7,8,8,9,9,10,10,11,11,12,12,13,13
};
// extra_blbits @0x5FFD1C
const int kExtraBlbits[kBlCodes] = {
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,2,3,7
};
// bl_order @0x5FFD68
const u8 kBlOrder[kBlCodes] = {
    16,17,18,0,8,7,9,6,10,5,11,4,12,3,13,2,14,1,15
};
// base_length @0x600574
const int kBaseLength[kLengthCodes] = {
    0,1,2,3,4,5,6,7,8,10,12,14,16,20,24,28,32,40,48,56,64,80,96,112,128,160,192,224,0
};
// base_dist @0x6005E8
const int kBaseDist[kDCodes] = {
    0,1,2,3,4,6,8,12,16,24,32,48,64,96,128,192,256,384,512,768,
    1024,1536,2048,3072,4096,6144,8192,12288,16384,24576
};

// _length_code @0x600474 — map (length-MIN_MATCH) -> length code.
const u8 kLengthCode[kMaxMatch - kMinMatch + 1] = {
     0, 1, 2, 3, 4, 5, 6, 7, 8, 8, 9, 9,10,10,11,11,12,12,12,12,
    13,13,13,13,14,14,14,14,15,15,15,15,16,16,16,16,16,16,16,16,
    17,17,17,17,17,17,17,17,18,18,18,18,18,18,18,18,19,19,19,19,
    19,19,19,19,20,20,20,20,20,20,20,20,20,20,20,20,20,20,20,20,
    21,21,21,21,21,21,21,21,21,21,21,21,21,21,21,21,22,22,22,22,
    22,22,22,22,22,22,22,22,22,22,22,22,23,23,23,23,23,23,23,23,
    23,23,23,23,23,23,23,23,24,24,24,24,24,24,24,24,24,24,24,24,
    24,24,24,24,24,24,24,24,24,24,24,24,24,24,24,24,24,24,24,24,
    25,25,25,25,25,25,25,25,25,25,25,25,25,25,25,25,25,25,25,25,
    25,25,25,25,25,25,25,25,25,25,25,25,26,26,26,26,26,26,26,26,
    26,26,26,26,26,26,26,26,26,26,26,26,26,26,26,26,26,26,26,26,
    26,26,26,26,27,27,27,27,27,27,27,27,27,27,27,27,27,27,27,27,
    27,27,27,27,27,27,27,27,27,27,27,27,27,27,27,28
};

// _dist_code @0x600274 (low 256) followed by @0x600374 (high 256, indexed dist>>7).
const u8 kDistCode[512] = {
    // low 256 (@0x600274)
     0, 1, 2, 3, 4, 4, 5, 5, 6, 6, 6, 6, 7, 7, 7, 7,
     8, 8, 8, 8, 8, 8, 8, 8, 9, 9, 9, 9, 9, 9, 9, 9,
    10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,
    11,11,11,11,11,11,11,11,11,11,11,11,11,11,11,11,
    12,12,12,12,12,12,12,12,12,12,12,12,12,12,12,12,
    12,12,12,12,12,12,12,12,12,12,12,12,12,12,12,12,
    13,13,13,13,13,13,13,13,13,13,13,13,13,13,13,13,
    13,13,13,13,13,13,13,13,13,13,13,13,13,13,13,13,
    14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,
    14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,
    14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,
    14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,
    15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,
    15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,
    15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,
    15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,
    // high 256 (@0x600374)
     0, 0,16,17,18,18,19,19,20,20,20,20,21,21,21,21,
    22,22,22,22,22,22,22,22,23,23,23,23,23,23,23,23,
    24,24,24,24,24,24,24,24,24,24,24,24,24,24,24,24,
    25,25,25,25,25,25,25,25,25,25,25,25,25,25,25,25,
    26,26,26,26,26,26,26,26,26,26,26,26,26,26,26,26,
    26,26,26,26,26,26,26,26,26,26,26,26,26,26,26,26,
    27,27,27,27,27,27,27,27,27,27,27,27,27,27,27,27,
    27,27,27,27,27,27,27,27,27,27,27,27,27,27,27,27,
    28,28,28,28,28,28,28,28,28,28,28,28,28,28,28,28,
    28,28,28,28,28,28,28,28,28,28,28,28,28,28,28,28,
    28,28,28,28,28,28,28,28,28,28,28,28,28,28,28,28,
    28,28,28,28,28,28,28,28,28,28,28,28,28,28,28,28,
    29,29,29,29,29,29,29,29,29,29,29,29,29,29,29,29,
    29,29,29,29,29,29,29,29,29,29,29,29,29,29,29,29,
    29,29,29,29,29,29,29,29,29,29,29,29,29,29,29,29,
    29,29,29,29,29,29,29,29,29,29,29,29,29,29,29,29
};

// bi_reverse — VIBE_Deflate_BiReverse @0x602790. Reverse the low `len` bits.
static unsigned BiReverse(unsigned code, int len) {
    unsigned res = 0;
    do {
        res |= code & 1;
        code >>= 1;
        res <<= 1;
    } while (--len > 0);
    return res >> 1;
}

// static_ltree @0x5FFD7C and static_dtree @0x6001FC. The binary stores these as
// precomputed data; they are the deterministic canonical static trees, so we
// build them once with tr_static_init's algorithm (verified to reproduce the
// recovered bytes: ltree[0]={0x0C,8}, ltree[256]={0,7}, dtree[i]={rev5(i),5}).
static CtData g_static_ltree[kLCodes + 2];
static CtData g_static_dtree[kDCodes];

static void StaticInit() {
    // length 8 for codes 0..143 and 280..287, 9 for 144..255, 7 for 256..279.
    int n;
    unsigned bl_count[kMaxBits + 1] = {0};
    n = 0;
    while (n <= 143) { g_static_ltree[n++].dl = 8; bl_count[8]++; }
    while (n <= 255) { g_static_ltree[n++].dl = 9; bl_count[9]++; }
    while (n <= 279) { g_static_ltree[n++].dl = 7; bl_count[7]++; }
    while (n <= 287) { g_static_ltree[n++].dl = 8; bl_count[8]++; }
    // assign canonical codes (gen_codes prologue) and bit-reverse them.
    unsigned next_code[kMaxBits + 1];
    unsigned code = 0;
    for (int bits = 1; bits <= kMaxBits; bits++) {
        code = (code + bl_count[bits - 1]) << 1;
        next_code[bits] = code;
    }
    for (n = 0; n <= kLCodes + 1; n++) {
        int len = g_static_ltree[n].dl;
        if (len == 0) continue;
        g_static_ltree[n].fc = (u16)BiReverse(next_code[len]++, len);
    }
    // distance tree: 30 codes of length 5, codes are bit-reversed 0..29.
    for (n = 0; n < kDCodes; n++) {
        g_static_dtree[n].dl = 5;
        g_static_dtree[n].fc = (u16)BiReverse((unsigned)n, 5);
    }
}

struct StaticInitOnce { StaticInitOnce() { StaticInit(); } };
static StaticInitOnce g_static_init_once;

// static_tree_desc table @0x64AD74 / 0x64AD88 / 0x64AD9C.
static const StaticTreeDesc kStaticLDesc  = { g_static_ltree, kExtraLbits, kLiterals + 1, kLCodes, kMaxBits };
static const StaticTreeDesc kStaticDDesc  = { g_static_dtree, kExtraDbits, 0, kDCodes, kMaxBits };
static const StaticTreeDesc kStaticBlDesc = { nullptr, kExtraBlbits, 0, kBlCodes, kMaxBlBits };

// configuration_table @0x5ECFD8 — {good_length, max_lazy, nice_length, max_chain, func}.
//   func: 0=deflate_stored, 1=deflate_fast, 2=deflate_slow.
const Config kConfigurationTable[10] = {
    /* 0 */ {0,    0,   0,    0,    0},
    /* 1 */ {4,    4,   8,    4,    1},
    /* 2 */ {4,    5,   16,   8,    1},
    /* 3 */ {4,    6,   32,   32,   1},
    /* 4 */ {4,    4,   16,   16,   2},
    /* 5 */ {8,    16,  32,   32,   2},
    /* 6 */ {8,    16,  128,  128,  2},
    /* 7 */ {8,    32,  128,  256,  2},
    /* 8 */ {32,   128, 258,  1024, 2},
    /* 9 */ {32,   258, 258,  4096, 2}
};

// =============================================================================
// Bit output (bi_buf/bi_valid live in the State; pending_buf is the byte sink).
// =============================================================================
static inline void PutByte(DeflateState* s, u8 c) {
    s->pending_buf[s->pending++] = c;
}
static inline void PutShortLSB(DeflateState* s, u16 w) {
    PutByte(s, (u8)(w & 0xff));
    PutByte(s, (u8)(w >> 8));
}

// send_bits (inline) — VIBE_Deflate_* uses the unrolled form throughout.
static void SendBits(DeflateState* s, int value, int length) {
    if (s->bi_valid > kBufSize - length) {
        s->bi_buf = (u16)(s->bi_buf | (value << s->bi_valid));
        PutShortLSB(s, s->bi_buf);
        s->bi_buf = (u16)((unsigned)value >> (kBufSize - s->bi_valid));
        s->bi_valid += length - kBufSize;
    } else {
        s->bi_buf = (u16)(s->bi_buf | (value << s->bi_valid));
        s->bi_valid += length;
    }
}
static inline void SendCode(DeflateState* s, int c, const CtData* tree) {
    SendBits(s, tree[c].fc, tree[c].dl);
}

// bi_flush — VIBE_Deflate_FlushBits @0x6027ac.
static void BiFlush(DeflateState* s) {
    if (s->bi_valid == 16) {
        PutShortLSB(s, s->bi_buf);
        s->bi_buf = 0;
        s->bi_valid = 0;
    } else if (s->bi_valid >= 8) {
        PutByte(s, (u8)(s->bi_buf & 0xff));
        s->bi_buf >>= 8;
        s->bi_valid -= 8;
    }
}

// bi_windup — VIBE_Deflate_BiWindup @0x602848.
void BiWindup(DeflateState* s) {
    if (s->bi_valid > 8) {
        PutShortLSB(s, s->bi_buf);
    } else if (s->bi_valid > 0) {
        PutByte(s, (u8)(s->bi_buf & 0xff));
    }
    s->bi_buf = 0;
    s->bi_valid = 0;
}

// =============================================================================
// init_block — VIBE_Deflate_TreeClear @0x6006cc.
// =============================================================================
void InitBlock(DeflateState* s) {
    for (int n = 0; n < kLCodes; n++) s->dyn_ltree[n].fc = 0;
    for (int n = 0; n < kDCodes; n++) s->dyn_dtree[n].fc = 0;
    for (int n = 0; n < kBlCodes; n++) s->bl_tree[n].fc = 0;
    s->dyn_ltree[kEndBlock].fc = 1;
    s->opt_len = s->static_len = 0;
    s->last_lit = 0;
}

// _tr_init — VIBE_Deflate_TreeInit @0x600664.
void TreeInit(DeflateState* s) {
    s->l_desc.dyn_tree = s->dyn_ltree;
    s->l_desc.stat = &kStaticLDesc;
    s->d_desc.dyn_tree = s->dyn_dtree;
    s->d_desc.stat = &kStaticDDesc;
    s->bl_desc.dyn_tree = s->bl_tree;
    s->bl_desc.stat = &kStaticBlDesc;
    s->bi_buf = 0;
    s->bi_valid = 0;
    s->last_eob_len = 8;
    InitBlock(s);
}

// =============================================================================
// pqdownheap — VIBE_Deflate_PqDownHeap @0x600748.
// smaller(tree,n,m): tree[n].fc < tree[m].fc || (== && depth[n] <= depth[m]).
// =============================================================================
static void PqDownHeap(DeflateState* s, CtData* tree, int k) {
    int v = s->heap[k];
    int j = k << 1;
    while (j <= s->heap_len) {
        if (j < s->heap_len &&
            (tree[s->heap[j + 1]].fc < tree[s->heap[j]].fc ||
             (tree[s->heap[j + 1]].fc == tree[s->heap[j]].fc &&
              s->depth[s->heap[j + 1]] <= s->depth[s->heap[j]]))) {
            j++;
        }
        if (tree[v].fc < tree[s->heap[j]].fc ||
            (tree[v].fc == tree[s->heap[j]].fc && s->depth[v] <= s->depth[s->heap[j]]))
            break;
        s->heap[k] = s->heap[j];
        k = j;
        j <<= 1;
    }
    s->heap[k] = v;
}

// gen_bitlen — VIBE_Deflate_GenBitlen @0x600864.
static void GenBitlen(DeflateState* s, TreeDesc* desc) {
    CtData* tree = desc->dyn_tree;
    int max_code = desc->max_code;
    const CtData* stree = desc->stat->static_tree;
    const int* extra = desc->stat->extra_bits;
    int base = desc->stat->extra_base;
    int max_length = desc->stat->max_length;
    int overflow = 0;

    for (int bits = 0; bits <= kMaxBits; bits++) s->bl_count[bits] = 0;

    tree[s->heap[s->heap_max]].dl = 0; // root of the heap

    int h;
    for (h = s->heap_max + 1; h < kHeapSize; h++) {
        int n = s->heap[h];
        int bits = tree[tree[n].dl].dl + 1;
        if (bits > max_length) { bits = max_length; overflow++; }
        tree[n].dl = (u16)bits;
        if (n > max_code) continue;
        s->bl_count[bits]++;
        int xbits = 0;
        if (n >= base) xbits = extra[n - base];
        u16 f = tree[n].fc;
        s->opt_len += (unsigned long)f * (bits + xbits);
        if (stree) s->static_len += (unsigned long)f * (stree[n].dl + xbits);
    }
    if (overflow == 0) return;

    do {
        int bits = max_length - 1;
        while (s->bl_count[bits] == 0) bits--;
        s->bl_count[bits]--;
        s->bl_count[bits + 1] += 2;
        s->bl_count[max_length]--;
        overflow -= 2;
    } while (overflow > 0);

    for (int bits = max_length; bits != 0; bits--) {
        int n = s->bl_count[bits];
        while (n != 0) {
            int m = s->heap[--h];
            if (m > max_code) continue;
            if ((unsigned)tree[m].dl != (unsigned)bits) {
                s->opt_len += ((long)bits - tree[m].dl) * tree[m].fc;
                tree[m].dl = (u16)bits;
            }
            n--;
        }
    }
}

// gen_codes — VIBE_Deflate_GenCodes @0x600ae0.
static void GenCodes(CtData* tree, int max_code, const u16* bl_count) {
    u16 next_code[kMaxBits + 1];
    unsigned code = 0;
    for (int bits = 1; bits <= kMaxBits; bits++) {
        code = (code + bl_count[bits - 1]) << 1;
        next_code[bits] = (u16)code;
    }
    for (int n = 0; n <= max_code; n++) {
        int len = tree[n].dl;
        if (len == 0) continue;
        tree[n].fc = (u16)BiReverse(next_code[len]++, len);
    }
}

// build_tree — VIBE_Deflate_BuildTree @0x600b5c.
static void BuildTree(DeflateState* s, TreeDesc* desc) {
    CtData* tree = desc->dyn_tree;
    const CtData* stree = desc->stat->static_tree;
    int elems = desc->stat->elems;
    int max_code = -1;
    int node;

    s->heap_len = 0;
    s->heap_max = kHeapSize;

    for (int n = 0; n < elems; n++) {
        if (tree[n].fc != 0) {
            s->heap[++(s->heap_len)] = max_code = n;
            s->depth[n] = 0;
        } else {
            tree[n].dl = 0;
        }
    }

    while (s->heap_len < 2) {
        node = s->heap[++(s->heap_len)] = (max_code < 2 ? ++max_code : 0);
        tree[node].fc = 1;
        s->depth[node] = 0;
        s->opt_len--;
        if (stree) s->static_len -= stree[node].dl;
    }
    desc->max_code = max_code;

    for (int n = s->heap_len / 2; n >= 1; n--) PqDownHeap(s, tree, n);

    node = elems;
    do {
        int n = s->heap[1];
        s->heap[1] = s->heap[s->heap_len--];
        PqDownHeap(s, tree, 1);
        int m = s->heap[1];

        s->heap[--(s->heap_max)] = n;
        s->heap[--(s->heap_max)] = m;

        tree[node].fc = (u16)(tree[n].fc + tree[m].fc);
        s->depth[node] = (u8)((s->depth[n] >= s->depth[m] ? s->depth[n] : s->depth[m]) + 1);
        tree[n].dl = tree[m].dl = (u16)node;

        s->heap[1] = node++;
        PqDownHeap(s, tree, 1);
    } while (s->heap_len >= 2);

    s->heap[--(s->heap_max)] = s->heap[1];

    GenBitlen(s, desc);
    GenCodes(tree, max_code, s->bl_count);
}

// scan_tree — VIBE_Deflate_ScanTree @0x600e10.
static void ScanTree(DeflateState* s, CtData* tree, int max_code) {
    int prevlen = -1;
    int nextlen = tree[0].dl;
    int count = 0;
    int max_count = 7;
    int min_count = 4;
    if (nextlen == 0) { max_count = 138; min_count = 3; }
    tree[max_code + 1].dl = (u16)0xffff;

    for (int n = 0; n <= max_code; n++) {
        int curlen = nextlen;
        nextlen = tree[n + 1].dl;
        if (++count < max_count && curlen == nextlen) {
            continue;
        } else if (count < min_count) {
            s->bl_tree[curlen].fc += count;
        } else if (curlen != 0) {
            if (curlen != prevlen) s->bl_tree[curlen].fc++;
            s->bl_tree[kRep3_6].fc++;
        } else if (count <= 10) {
            s->bl_tree[kRepZ3_10].fc++;
        } else {
            s->bl_tree[kRepZ11_138].fc++;
        }
        count = 0;
        prevlen = curlen;
        if (nextlen == 0) { max_count = 138; min_count = 3; }
        else if (curlen == nextlen) { max_count = 6; min_count = 3; }
        else { max_count = 7; min_count = 4; }
    }
}

// send_tree — VIBE_Deflate_SendTree @0x600f08.
static void SendTree(DeflateState* s, CtData* tree, int max_code) {
    int prevlen = -1;
    int nextlen = tree[0].dl;
    int count = 0;
    int max_count = 7;
    int min_count = 4;
    if (nextlen == 0) { max_count = 138; min_count = 3; }

    for (int n = 0; n <= max_code; n++) {
        int curlen = nextlen;
        nextlen = tree[n + 1].dl;
        if (++count < max_count && curlen == nextlen) {
            continue;
        } else if (count < min_count) {
            do { SendCode(s, curlen, s->bl_tree); } while (--count != 0);
        } else if (curlen != 0) {
            if (curlen != prevlen) { SendCode(s, curlen, s->bl_tree); count--; }
            SendCode(s, kRep3_6, s->bl_tree);
            SendBits(s, count - 3, 2);
        } else if (count <= 10) {
            SendCode(s, kRepZ3_10, s->bl_tree);
            SendBits(s, count - 3, 3);
        } else {
            SendCode(s, kRepZ11_138, s->bl_tree);
            SendBits(s, count - 11, 7);
        }
        count = 0;
        prevlen = curlen;
        if (nextlen == 0) { max_count = 138; min_count = 3; }
        else if (curlen == nextlen) { max_count = 6; min_count = 3; }
        else { max_count = 7; min_count = 4; }
    }
}

// build_bl_tree — VIBE_Deflate_BuildBlTree @0x60168c.
static int BuildBlTree(DeflateState* s) {
    ScanTree(s, s->dyn_ltree, s->l_desc.max_code);
    ScanTree(s, s->dyn_dtree, s->d_desc.max_code);
    BuildTree(s, &s->bl_desc);
    int max_blindex;
    for (max_blindex = kBlCodes - 1; max_blindex >= 3; max_blindex--) {
        if (s->bl_tree[kBlOrder[max_blindex]].dl != 0) break;
    }
    s->opt_len += 3 * (max_blindex + 1) + 5 + 5 + 4;
    return max_blindex;
}

// send_all_trees — VIBE_Deflate_SendAllTrees @0x6016fc.
static void SendAllTrees(DeflateState* s, int lcodes, int dcodes, int blcodes) {
    SendBits(s, lcodes - 257, 5);
    SendBits(s, dcodes - 1, 5);
    SendBits(s, blcodes - 4, 4);
    for (int rank = 0; rank < blcodes; rank++) {
        SendBits(s, s->bl_tree[kBlOrder[rank]].dl, 3);
    }
    SendTree(s, s->dyn_ltree, lcodes - 1);
    SendTree(s, s->dyn_dtree, dcodes - 1);
}

// compress_block — VIBE_Deflate_CompressBlock @0x6021a0.
static void CompressBlock(DeflateState* s, const CtData* ltree, const CtData* dtree) {
    unsigned lx = 0;
    if (s->last_lit != 0) {
        do {
            unsigned dist = s->d_buf[lx];
            int lc = s->l_buf[lx];
            lx++;
            if (dist == 0) {
                SendCode(s, lc, ltree); // literal
            } else {
                int code = kLengthCode[lc];
                SendCode(s, code + kLiterals + 1, ltree);
                int extra = kExtraLbits[code];
                if (extra != 0) {
                    lc -= kBaseLength[code];
                    SendBits(s, lc, extra);
                }
                dist--;
                code = (dist < 256) ? kDistCode[dist] : kDistCode[256 + (dist >> 7)];
                SendCode(s, code, dtree);
                extra = kExtraDbits[code];
                if (extra != 0) {
                    dist -= kBaseDist[code];
                    SendBits(s, dist, extra);
                }
            }
        } while (lx < s->last_lit);
    }
    SendCode(s, kEndBlock, ltree);
    s->last_eob_len = ltree[kEndBlock].dl;
}

// set_data_type — VIBE_Deflate_DetectDataType @0x602720.
static void SetDataType(DeflateState* s) {
    int n = 0;
    unsigned ascii_freq = 0;
    unsigned bin_freq = 0;
    while (n < 7) bin_freq += s->dyn_ltree[n++].fc;
    while (n < 128) ascii_freq += s->dyn_ltree[n++].fc;
    while (n < kLiterals) bin_freq += s->dyn_ltree[n++].fc;
    s->data_type = (bin_freq > (ascii_freq >> 2)) ? 0 /*Z_BINARY*/ : 1 /*Z_ASCII*/;
}

// copy_block — VIBE_Quant_EncodeRunLength @0x6028b8 (shared helper).
static void CopyBlock(DeflateState* s, const u8* buf, unsigned len, int header) {
    BiWindup(s);
    s->last_eob_len = 8;
    if (header) {
        PutShortLSB(s, (u16)len);
        PutShortLSB(s, (u16)~len);
    }
    while (len--) PutByte(s, *buf++);
}

// _tr_stored_block — VIBE_Deflate_StoredBlock @0x601a5c.
void TrStoredBlock(DeflateState* s, int buf, unsigned stored_len, int last) {
    SendBits(s, (kStoredBlock << 1) + last, 3);
    const u8* p = (buf < 0) ? nullptr : (s->window.data() + buf);
    CopyBlock(s, p, stored_len, 1);
}

// _tr_align — VIBE_Deflate_AlignBits @0x601b34.
// NOTE: the binary reads the PREVIOUS block's last_eob_len for the `< 9` check
// (load @0x601c24) and only stores 7 at the very end (@0x601d2b).
void TrAlign(DeflateState* s) {
    SendBits(s, kStaticTrees << 1, 3);
    SendCode(s, kEndBlock, g_static_ltree);
    BiFlush(s);
    if (1 + s->last_eob_len + 10 - s->bi_valid < 9) {
        SendBits(s, kStaticTrees << 1, 3);
        SendCode(s, kEndBlock, g_static_ltree);
        BiFlush(s);
    }
    s->last_eob_len = 7;
}

// _tr_flush_block — VIBE_Deflate_FlushBlock @0x601ea4.
void TrFlushBlock(DeflateState* s, int buf, unsigned stored_len, int last) {
    unsigned long opt_lenb, static_lenb;
    int max_blindex = 0;

    if (s->level > 0) {
        if (s->data_type == 2 /*Z_UNKNOWN*/) SetDataType(s);
        BuildTree(s, &s->l_desc);
        BuildTree(s, &s->d_desc);
        max_blindex = BuildBlTree(s);
        opt_lenb = (s->opt_len + 3 + 7) >> 3;
        static_lenb = (s->static_len + 3 + 7) >> 3;
        if (static_lenb <= opt_lenb) opt_lenb = static_lenb;
    } else {
        opt_lenb = static_lenb = stored_len + 5;
    }

    if (stored_len + 4 <= opt_lenb && buf != -1) {
        TrStoredBlock(s, buf, stored_len, last);
    } else if (static_lenb == opt_lenb) {
        SendBits(s, (kStaticTrees << 1) + last, 3);
        CompressBlock(s, g_static_ltree, g_static_dtree);
    } else {
        SendBits(s, (kDynTrees << 1) + last, 3);
        SendAllTrees(s, s->l_desc.max_code + 1, s->d_desc.max_code + 1, max_blindex + 1);
        CompressBlock(s, s->dyn_ltree, s->dyn_dtree);
    }
    InitBlock(s);
    if (last) BiWindup(s);
}

} // namespace guild::compress
