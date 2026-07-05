#include "compress/inflate.h"
#include <cstring>
#include <functional>

namespace guild::compress {

namespace {

// --- static tables, recovered byte-for-byte from gilde.exe -------------------

// dword_5FEAC0 — bit-length code order (border[]).
const u32 kBorder[19] = {
    16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15
};

// dword_64AFD4 — inflate_mask[]: low-bit masks 0,1,3,7,... 0xffff (17 entries).
const u32 kMask[17] = {
    0x0000, 0x0001, 0x0003, 0x0007, 0x000f, 0x001f, 0x003f, 0x007f,
    0x00ff, 0x01ff, 0x03ff, 0x07ff, 0x0fff, 0x1fff, 0x3fff, 0x7fff, 0xffff
};

// dword_607F00 / dword_607F7C — length base + extra bits (cplens / cplext).
// The two 112 entries are the INVALID-code markers (huft_build copies them in;
// they encode exop=80+112 path which can never be selected for valid streams).
const u32 kCplens[31] = {
    3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31,
    35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258, 0, 0
};
const u32 kCplext[31] = {
    0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2,
    3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0, 112, 112
};

// dword_607FF8 / dword_608070 — distance base + extra bits (cpdist / cpdext).
const u32 kCpdist[30] = {
    1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193,
    257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577
};
const u32 kCpdext[30] = {
    0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6,
    7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13
};

constexpr int BMAX = 15;
constexpr int N_MAX = 288;
// huft work-pool cap from the binary (v41 > 0x5A0 -> kZMemError == MANY in zlib).
constexpr unsigned MANY = 0x5A0;

} // namespace

// =============================================================================
// huft_build — VIBE_Inflate_BuildHuffmanTree @0x6080e8
//
// Builds a canonical Huffman decode table from a vector of code lengths `b[n]`.
// `s` simple codes are literals (no base/extra); codes >= s index into the
// base table `d[]` (extra-bit base) and extra-count table `e[]`. The table is
// allocated out of a flat pool `hp[]` (the original ZALLOCs MANY huft entries).
//
// Returns kZOk / kZBufError(=-5, incomplete) / kZDataError(=-3, oversubscribed)
// / kZMemError(=-4, pool exhausted). On success *t points to the root table and
// *m holds the root bit-length. Faithful 1:1 with zlib 1.1.4 inftrees.c.
// =============================================================================
static int HuftBuild(const u32* b, unsigned n, unsigned s,
                     const u32* d, const u32* e,
                     InflateHuft** t, unsigned* m,
                     InflateHuft* hp, unsigned* hn, unsigned* v) {
    unsigned a;                  // counter for codes of length k
    unsigned c[BMAX + 1];        // bit length count table
    unsigned f;                  // i repeats in table every f entries
    int g;                       // maximum code length
    int h;                       // table level
    unsigned i;                  // counter, current code
    unsigned j;                  // counter
    int k;                       // number of bits in current code
    int l;                       // bits per table (returned in m)
    unsigned mask;
    unsigned* p;                 // pointer into c[], b[], or v[]
    InflateHuft* q;              // points to current table
    InflateHuft r;              // table entry for structure assignment
    InflateHuft* u[BMAX];        // table stack
    int w;                       // bits before this table == (l * h)
    unsigned x[BMAX + 1];        // bit offsets, then code stack
    unsigned* xp;                // pointer into x
    int y;                       // number of dummy codes added
    unsigned z;                  // number of entries in current table

    // Generate counts for each bit length.
    std::memset(c, 0, sizeof(c));
    p = const_cast<unsigned*>(b);
    i = n;
    do {
        c[*p]++;
        p++;
    } while (--i);
    if (c[0] == n) {             // null input -- all zero length codes
        *t = nullptr;
        *m = 0;
        return kZOk;
    }

    // Find minimum and maximum length, bound *m by those.
    l = *m;
    for (j = 1; j <= BMAX; j++)
        if (c[j])
            break;
    k = j;                       // minimum code length
    if ((unsigned)l < j)
        l = j;
    for (i = BMAX; i; i--)
        if (c[i])
            break;
    g = i;                       // maximum code length
    if ((unsigned)l > i)
        l = i;
    *m = l;

    // Adjust last length count to fill out codes, if needed.
    for (y = 1 << j; j < i; j++, y <<= 1)
        if ((y -= c[j]) < 0)
            return kZDataError;  // bad input: more codes than bits
    if ((y -= c[i]) < 0)
        return kZDataError;
    c[i] += y;

    // Generate starting offsets into the value table for each length.
    x[1] = j = 0;
    p = c + 1;
    xp = x + 2;
    while (--i) {                // note that i == g from above
        *xp++ = (j += *p++);
    }

    // Make a table of values in order of bit lengths.
    p = const_cast<unsigned*>(b);
    i = 0;
    do {
        if ((j = *p++) != 0)
            v[x[j]++] = i;
    } while (++i < n);
    n = x[g];                    // set n to length of v

    // Generate the Huffman codes and for each, make the table entries.
    x[0] = i = 0;                // first Huffman code is zero
    p = v;                       // grab values in bit order
    h = -1;                      // no tables yet -- level -1
    w = -l;                      // bits decoded == (l * h)
    u[0] = nullptr;              // just to keep compilers happy
    q = nullptr;                 // ditto
    z = 0;                       // ditto

    // go through the bit lengths (k already is bits in shortest code)
    for (; k <= g; k++) {
        a = c[k];
        while (a--) {
            // here i is the Huffman code of length k bits for value *p
            // make tables up to required level
            while (k > w + l) {
                h++;
                w += l;          // previous table always l bits

                // compute minimum size table less than or equal to l bits
                z = g - w;
                z = z > (unsigned)l ? (unsigned)l : z;        // upper limit on table size
                if ((f = 1 << (j = k - w)) > a + 1) {         // try a k-w bit table
                    // too few codes for k-w bit table
                    f -= a + 1;  // deduct codes from patterns left
                    xp = c + k;
                    if (j < z)
                        while (++j < z) {       // try smaller tables up to z bits
                            if ((f <<= 1) <= *++xp)
                                break;          // enough codes to use up j bits
                            f -= *xp;           // else deduct codes from patterns
                        }
                }
                z = 1 << j;      // table entries for j-bit table

                // allocate new table (note: hp/hn is the pool)
                if (*hn + z > MANY)               // (note: doesn't matter for fixed)
                    return kZMemError;             // not enough memory
                u[h] = q = hp + *hn;
                *hn += z;

                // connect to last table, if there is one
                if (h) {
                    x[h] = i;                // save pattern for backing up
                    r.bits = (u8)l;          // bits to dump before this table
                    r.exop = (u8)j;          // bits in this table
                    j = i >> (w - l);
                    r.base = (u32)(q - u[h - 1] - j);   // offset to this table
                    u[h - 1][j] = r;         // connect to last table
                } else {
                    *t = q;                  // first table is returned result
                }
            }

            // set up table entry in r
            r.bits = (u8)(k - w);
            if (p >= v + n) {
                r.exop = 128 + 64;           // out of values -- invalid code
            } else if (*p < s) {
                r.exop = (u8)(*p < 256 ? 0 : 32 + 64);   // 256 is end-of-block
                r.base = *p++;               // simple code is just the value
            } else {
                r.exop = (u8)(e[*p - s] + 16 + 64);      // non-simple -- look up in lists
                r.base = d[*p++ - s];
            }

            // fill code-like entries with r
            f = 1 << (k - w);
            for (j = i >> w; j < z; j += f)
                q[j] = r;

            // backwards increment the k-bit code i
            for (j = 1 << (k - 1); i & j; j >>= 1)
                i ^= j;
            i ^= j;

            // backup over finished tables
            mask = (1 << w) - 1;             // needed on HP, cc/svr4 systems
            while ((i & mask) != x[h]) {
                h--;                         // don't need to update q
                w -= l;
                mask = (1 << w) - 1;
            }
        }
    }

    // Return kZBufError if we were given an incomplete table.
    return (y != 0 && g != 1) ? kZBufError : kZOk;
}

// inflate_trees_bits — VIBE_Inflate_BuildBitsTree @0x608760
static int InflateTreesBits(const u32* c, unsigned* bb, InflateHuft** tb,
                            InflateHuft* hp, const char** msg) {
    unsigned hn = 0;
    unsigned v[19];
    int r = HuftBuild(c, 19, 19, nullptr, nullptr, tb, bb, hp, &hn, v);
    if (r == kZDataError) {
        *msg = "oversubscribed dynamic bit lengths tree";
    } else if (r == kZBufError || *bb == 0) {
        *msg = "incomplete dynamic bit lengths tree";
        r = kZDataError;
    }
    return r;
}

// inflate_trees_dynamic — VIBE_Inflate_BuildDynamicTrees @0x6087fc
static int InflateTreesDynamic(unsigned nl, unsigned nd, const u32* c,
                               unsigned* bl, unsigned* bd,
                               InflateHuft** tl, InflateHuft** td,
                               InflateHuft* hp, const char** msg) {
    unsigned hn = 0;
    unsigned v[288];

    int r = HuftBuild(c, nl, 257, kCplens, kCplext, tl, bl, hp, &hn, v);
    if (r != kZOk || *bl == 0) {
        if (r == kZDataError) {
            *msg = "oversubscribed literal/length tree";
        } else if (r != kZMemError) {
            *msg = "incomplete literal/length tree";
            r = kZDataError;
        }
        return r;
    }

    r = HuftBuild(c + nl, nd, 0, kCpdist, kCpdext, td, bd, hp, &hn, v);
    if (r == kZDataError) {
        *msg = "oversubscribed distance tree";
        return kZDataError;
    } else if (r == kZBufError) {
        *msg = "incomplete distance tree";
        return kZDataError;
    } else if (r != kZOk && r != kZMemError) {
        *msg = "empty distance tree with lengths";
        return kZDataError;
    } else if (r != kZOk) {
        return r;
    }
    // success: zlib also accepts nd==0 (no distances) -- mirrors (*bd || nl > 257)
    if (*bd == 0 && nl > 257) {
        *msg = "empty distance tree with lengths";
        return kZDataError;
    }
    return kZOk;
}

// =============================================================================
// Fixed Huffman trees — VIBE_Inflate_GetFixedTrees @0x60899c
//
// The original returns precomputed globals (bl=9 @0x64B018, bd=5 @0x64B01C, the
// length table @0x64B020, the distance table @0x64C020). We build them once at
// first use with huft_build over the standard fixed code lengths, which produces
// byte-identical tables. fixed_bl == 9, fixed_bd == 5 (verified against the IDB).
// =============================================================================
struct FixedTrees {
    unsigned bl = 9, bd = 5;
    InflateHuft* tl = nullptr;
    InflateHuft* td = nullptr;
    std::vector<InflateHuft> pool;
    bool built = false;

    void Build() {
        if (built)
            return;
        pool.assign(MANY, InflateHuft{});
        unsigned hn = 0;
        unsigned v[288];
        u32 c[288];
        unsigned k;
        for (k = 0; k < 144; k++) c[k] = 8;
        for (; k < 256; k++) c[k] = 9;
        for (; k < 280; k++) c[k] = 7;
        for (; k < 288; k++) c[k] = 8;
        bl = 9;
        HuftBuild(c, 288, 257, kCplens, kCplext, &tl, &bl, pool.data(), &hn, v);
        for (k = 0; k < 30; k++) c[k] = 5;
        bd = 5;
        HuftBuild(c, 30, 0, kCpdist, kCpdext, &td, &bd, pool.data(), &hn, v);
        built = true;
    }
};
static FixedTrees g_fixed;

// =============================================================================
// inflate_blocks + inflate_codes state, flattened into Inflater::Blocks.
// =============================================================================

// inflate_blocks modes (matches the byte at *state in the binary).
enum BlocksMode {
    kbTYPE = 0,   // get type bits (3, including end bit)
    kbLENS,       // get lengths for stored
    kbSTORED,     // processing stored block
    kbTABLE,      // get table lengths
    kbBTREE,      // get bit lengths tree for a dynamic block
    kbDTREE,      // get length, distance trees for a dynamic block
    kbCODES,      // processing fixed or dynamic block
    kbDRY,        // output remaining window bytes
    kbDONE,       // finished last block, done
    kbBLKBAD,     // ot a data error -- stuck here
};

// inflate_codes modes.
enum CodesMode {
    kcSTART = 0,  // x: set up for kcLEN
    kcLEN,        // i: get length/literal/eob next
    kcLENEXT,     // i: getting length extra (have base)
    kcDIST,       // i: get distance next
    kcDISTEXT,    // i: getting distance extra
    kcCOPY,       // o: copying bytes in window, waiting for space
    kcLIT,        // o: got literal, waiting for output space
    kcWASH,       // o: got eob, possibly still output waiting
    kcEND,        // x: got eob and all data flushed
    kcBADCODE,    // x: got error
};

struct Codes {
    int  mode = kcSTART;
    unsigned len = 0;          // length/literal/code length value
    // sub-union (lit / copy.dist / code trees)
    unsigned lit = 0;
    unsigned copyGet = 0;      // bits to get for extra
    unsigned copyDist = 0;     // distance back to copy from
    InflateHuft* tree = nullptr;
    unsigned need = 0;         // bits needed
    unsigned char lbits = 0;   // ltree bits decoded per branch
    unsigned char dbits = 0;   // dtree bits decoder per branch
    InflateHuft* ltree = nullptr;
    InflateHuft* dtree = nullptr;
};

struct Inflater::Blocks {
    int  mode = kbTYPE;
    // mode dependent information
    unsigned left = 0;         // if STORED, bytes left to copy
    unsigned table = 0;        // table lengths (14 bits) (if TABLE)
    unsigned index = 0;        // index into blens (if BTREE) or last (if DTREE)
    std::vector<u32> blens;    // bit lengths of codes
    unsigned bb = 0;           // bit length tree depth
    InflateHuft* tb = nullptr; // bit length decoding tree

    Codes* codes = nullptr;    // if CODES, current inflate_codes state

    int  last = 0;             // true if this block is the last block
    // mode independent information
    unsigned bitk = 0;         // bits in bit buffer
    u32 bitb = 0;              // bit buffer
    std::vector<InflateHuft> hufts; // huft pool (MANY entries)
    u8*  window = nullptr;     // sliding window
    u8*  end = nullptr;        // one byte after window
    u8*  read = nullptr;       // window read pointer
    u8*  write = nullptr;      // window write pointer
    u32  check = 0;            // check on output
    bool needCheck = true;     // false for raw

    ~Blocks() { delete codes; }
};

// =============================================================================
// inflate_flush — VIBE_Inflate_FlushWindow @0x607d30
//
// Copies as much as possible from the sliding window to the output. In the
// original this calls the user output callback / drains avail_out; here we
// append to out_ and update the running adler check (checkfn).
// =============================================================================
int Inflater::RunBlocks(int r) {
    Blocks* s = b_;
    // 1:1 port of VIBE_Inflate_FlushWindow @0x607d30. The original drains the
    // sliding window into the user's avail_out in up to two segments; here
    // avail_out is unbounded (we append everything to out_), so the clamps that
    // bound each segment by avail_out never bind. The two structural pieces that
    // MUST be preserved are (1) the second segment is taken whenever the first
    // segment ends exactly at s->end (`v17 == end`), not merely when write<read,
    // and (2) inside that branch s->write is reset to s->window when write==end
    // (`if ( v7 == *(a1+52) ) *(a1+52) = v18;`). Omitting (2) leaves write stuck
    // at end on a 64KB-aligned wrap, so the next flush re-emits the whole window
    // (the 32768-byte over-output bug).
    auto flush = [&](int rr) -> int {
        // first segment: [read, end) if write<read else [read, write).  (v16/v4/v15)
        u8* p = s->read;                                  // v12/v16 (read)
        u8* q = (s->read > s->write) ? s->end : s->write; // v4
        u32 n = (u32)(q - p);                             // v15 (avail_out unbounded)
        if (n) {
            if (s->needCheck)
                s->check = Adler32(s->check, p, n);
            out_->insert(out_->end(), p, p + n);
            checkComputed_ = s->check;
        }
        p += n;                                           // v17 = read + n
        if (p == s->end) {                                // v17 == end (a1+44)
            // 0x607d30: if ( end == write ) write = window;  then copy [window, write)
            if (s->end == s->write)
                s->write = s->window;
            p = s->window;                                // v18
            n = (u32)(s->write - p);                      // v14
            if (n) {
                if (s->needCheck)
                    s->check = Adler32(s->check, p, n);
                out_->insert(out_->end(), p, p + n);
                checkComputed_ = s->check;
            }
            p += n;                                       // v17 = window + n
        }
        s->read = p;                                      // a1+48 = v17
        return rr;
    };
    flush(r);
    return r;
}

// Lambda-free helper used by inflate_codes/inflate_blocks to drain the window
// when it fills; identical semantics to inflate_flush above.
static u32 WindowFree(Inflater::Blocks* s, u8*& q) {
    // bytes available to write in the window before wrapping/read pointer
    if (q < s->read)
        return (u32)(s->read - q - 1);
    return (u32)(s->end - q);
}

// =============================================================================
// inflate_fast — VIBE_Decompress_InflateFast @0x60a1f0
//
// Fast inner loop: decodes length/literal then distance using the current
// trees, copying matches directly in the window. Operates while there are at
// least 258 output bytes and 10 input bytes available.
// =============================================================================
static int InflateFast(unsigned bl, unsigned bd, InflateHuft* tl, InflateHuft* td,
                       Inflater::Blocks* s, const u8*& in, std::size_t& avail) {
    InflateHuft* t;
    unsigned e;        // extra bits or operation
    u32 b = s->bitb;   // bit buffer
    unsigned k = s->bitk; // bits in bit buffer
    const u8* p = in;  // input data pointer
    std::size_t n = avail; // bytes available there
    const std::size_t n0 = avail; // avail_in at entry (for UNGRAB)
    u8* q = s->write;  // output window write pointer
    unsigned m;        // bytes to end of window or read pointer
    unsigned ml = kMask[bl];
    unsigned md = kMask[bd];
    unsigned c, d;

    // UNGRAB (zlib inffast.c) — gilde.exe 0x60a1f0 performs this on every exit
    // path: return over-grabbed whole bytes from the bit buffer to the input
    // stream (c = min(avail_in - n, k >> 3); n += c; p -= c; k -= c << 3).
    // The bit buffer `b` is deliberately left untouched: refills re-OR the
    // same bytes over their own copies.
    auto ungrab = [&]() {
        std::size_t cc = n0 - n;                 // bytes consumed this call
        if ((std::size_t)(k >> 3) < cc) cc = k >> 3;
        n += cc;
        p -= cc;
        k -= (unsigned)cc << 3;
    };

    m = WindowFree(s, q);

    do {
        while (k < 20) {
            b |= (u32)(*p++) << k;
            k += 8;
            n--;
        }
        t = tl + (b & ml);
        e = t->exop;
        if (e == 0) {
            b >>= t->bits;
            k -= t->bits;
            *q++ = (u8)t->base;
            m--;
            continue;
        }
        for (;;) {
            b >>= t->bits;
            k -= t->bits;
            if (e & 16) {                       // then it's a literal/length
                e &= 15;
                c = t->base + (b & kMask[e]);
                b >>= e;
                k -= e;
                // decode distance base of block to copy
                while (k < 15) {
                    b |= (u32)(*p++) << k;
                    k += 8;
                    n--;
                }
                t = td + (b & md);
                e = t->exop;
                for (;;) {
                    b >>= t->bits;
                    k -= t->bits;
                    if (e & 16) {               // get extra bits to add to distance base
                        while (k < (e & 15)) {
                            b |= (u32)(*p++) << k;
                            k += 8;
                            n--;
                        }
                        d = t->base + (b & kMask[e & 15]);
                        b >>= (e & 15);
                        k -= (e & 15);
                        // do the copy
                        m -= c;
                        {
                            u8* r;
                            if ((unsigned)(q - s->window) >= d) {
                                r = q - d;
                                *q++ = *r++;       // minimum count is three,
                                *q++ = *r++;       // so unroll loop a little
                                c -= 2;
                            } else {               // else offset after window
                                unsigned dd = d - (unsigned)(q - s->window);
                                r = s->end - dd;
                                if (c > dd) {      // if source crosses,
                                    c -= dd;       // wrapped copy
                                    do {
                                        *q++ = *r++;
                                    } while (--dd);
                                    r = s->window;
                                }
                            }
                            do {                   // copy all or what's left
                                *q++ = *r++;
                            } while (--c);
                        }
                        break;
                    } else if ((e & 64) == 0) {  // next table
                        t += t->base + (b & kMask[e]);
                        e = t->exop;
                        continue;
                    } else {
                        // gilde.exe 0x60a1f0: "invalid distance code" exit
                        ungrab();
                        s->bitb = b;
                        s->bitk = k;
                        avail = n;
                        in = p;
                        s->write = q;
                        return kZDataError;
                    }
                }
                break;
            } else if ((e & 64) == 0) {           // next table
                t += t->base + (b & kMask[e]);
                e = t->exop;
                if (e == 0) {
                    b >>= t->bits;
                    k -= t->bits;
                    *q++ = (u8)t->base;
                    m--;
                    break;
                }
                continue;
            } else if (e & 32) {                  // end of block
                // gilde.exe 0x60a1f0: EOB exit also UNGRABs
                ungrab();
                s->bitb = b;
                s->bitk = k;
                avail = n;
                in = p;
                s->write = q;
                return kZStreamEnd;
            } else {
                // gilde.exe 0x60a1f0: "invalid literal/length code" exit
                ungrab();
                s->bitb = b;
                s->bitk = k;
                avail = n;
                in = p;
                s->write = q;
                return kZDataError;
            }
        }
    } while (m >= 258 && n >= 10);

    // not enough input or output -- restore pointers (UNGRAB per 0x60a1f0) and return
    ungrab();
    s->bitb = b;
    s->bitk = k;
    avail = n;
    in = p;
    s->write = q;
    return kZOk;
}

// =============================================================================
// inflate_codes — VIBE_Inflate_DecodeCodes @0x60751c
//
// The non-fast literal/length/distance decoder. Drives Codes state machine,
// copying matches into the window. Returns a zlib code; kZStreamEnd on EOB.
// =============================================================================
static int InflateCodes(Inflater::Blocks* s, Codes* c,
                        const u8*& in, std::size_t& avail,
                        std::function<int(int)> flush) {
    unsigned j;            // temporary storage
    InflateHuft* t;        // temporary pointer
    unsigned e;            // extra bits or operation
    u32 b = s->bitb;       // bit buffer
    unsigned k = s->bitk;  // bits in bit buffer
    const u8* p = in;      // input data pointer
    std::size_t n = avail; // bytes available there
    u8* q = s->write;      // output window write pointer
    unsigned m = WindowFree(s, q);
    int r = kZOk;

    #define NEEDBYTE  do { if (n == 0) goto leave; } while (0)
    #define NEXTBYTE  (n--, *p++)

    for (;;) {
        switch (c->mode) {
        case kcSTART:
            if (m >= 258 && n >= 10) {
                s->bitb = b; s->bitk = k; avail = n; in = p; s->write = q;
                r = InflateFast(c->lbits, c->dbits, c->ltree, c->dtree, s, in, avail);
                p = in; n = avail; b = s->bitb; k = s->bitk; q = s->write;
                m = WindowFree(s, q);
                if (r != kZOk) {
                    c->mode = (r == kZStreamEnd ? kcWASH : kcBADCODE);
                    break;
                }
            }
            c->need = c->lbits;
            c->tree = c->ltree;
            c->mode = kcLEN;
            [[fallthrough]];
        case kcLEN:
            j = c->need;
            while (k < j) {
                NEEDBYTE;
                r = kZOk;
                b |= (u32)NEXTBYTE << k;
                k += 8;
            }
            t = c->tree + (b & kMask[j]);
            b >>= t->bits;
            k -= t->bits;
            e = t->exop;
            if (e == 0) {                 // literal
                c->lit = t->base;
                c->mode = kcLIT;
                break;
            }
            if (e & 16) {                 // length
                c->copyGet = e & 15;
                c->len = t->base;
                c->mode = kcLENEXT;
                break;
            }
            if ((e & 64) == 0) {          // next table
                c->need = e;
                c->tree = t + t->base;
                break;
            }
            if (e & 32) {                 // end of block
                c->mode = kcWASH;
                break;
            }
            c->mode = kcBADCODE;
            s->bitb = b; s->bitk = k; avail = n; in = p; s->write = q;
            // msg set by caller-level wrapper ("invalid literal/length code")
            return flush(kZDataError);
        case kcLENEXT:
            j = c->copyGet;
            while (k < j) {
                NEEDBYTE;
                r = kZOk;
                b |= (u32)NEXTBYTE << k;
                k += 8;
            }
            c->len += (b & kMask[j]);
            b >>= j;
            k -= j;
            c->need = c->dbits;
            c->tree = c->dtree;
            c->mode = kcDIST;
            [[fallthrough]];
        case kcDIST:
            j = c->need;
            while (k < j) {
                NEEDBYTE;
                r = kZOk;
                b |= (u32)NEXTBYTE << k;
                k += 8;
            }
            t = c->tree + (b & kMask[j]);
            b >>= t->bits;
            k -= t->bits;
            e = t->exop;
            if (e & 16) {                 // distance
                c->copyGet = e & 15;
                c->copyDist = t->base;
                c->mode = kcDISTEXT;
                break;
            }
            if ((e & 64) == 0) {          // next table
                c->need = e;
                c->tree = t + t->base;
                break;
            }
            c->mode = kcBADCODE;
            s->bitb = b; s->bitk = k; avail = n; in = p; s->write = q;
            return flush(kZDataError);
        case kcDISTEXT:
            j = c->copyGet;
            while (k < j) {
                NEEDBYTE;
                r = kZOk;
                b |= (u32)NEXTBYTE << k;
                k += 8;
            }
            c->copyDist += (b & kMask[j]);
            b >>= j;
            k -= j;
            c->mode = kcCOPY;
            [[fallthrough]];
        case kcCOPY: {
            unsigned dist = c->copyDist;
            u8* f;
            // back up over window if needed
            {
                unsigned off = (unsigned)(q - s->window);
                if (off >= dist)
                    f = q - dist;
                else
                    f = s->end - (dist - off);
            }
            while (c->len) {
                if (m == 0) {
                    if (q == s->end && s->read != s->window) {
                        q = s->window;
                        m = WindowFree(s, q);
                    }
                    if (m == 0) {
                        s->write = q;
                        r = flush(r);
                        q = s->write; m = WindowFree(s, q);
                        if (q == s->end) {
                            q = s->window; m = WindowFree(s, q);
                        }
                        if (m == 0)
                            goto leave;
                    }
                }
                r = kZOk;
                *q++ = *f++;
                m--;
                if (f == s->end)
                    f = s->window;
                c->len--;
            }
            c->mode = kcSTART;
            break;
        }
        case kcLIT:
            if (m == 0) {
                if (q == s->end && s->read != s->window) {
                    q = s->window;
                    m = WindowFree(s, q);
                }
                if (m == 0) {
                    s->write = q;
                    r = flush(r);
                    q = s->write; m = WindowFree(s, q);
                    if (q == s->end) {
                        q = s->window; m = WindowFree(s, q);
                    }
                    if (m == 0)
                        goto leave;
                }
            }
            r = kZOk;
            *q++ = (u8)c->lit;
            m--;
            c->mode = kcSTART;
            break;
        case kcWASH:
            if (k > 7) {                  // return unused byte, if any
                k -= 8;
                n++;
                p--;
            }
            s->write = q;
            r = flush(r);
            q = s->write; m = WindowFree(s, q);
            if (s->read != s->write)
                goto leave;
            c->mode = kcEND;
            [[fallthrough]];
        case kcEND:
            r = kZStreamEnd;
            goto leave;
        case kcBADCODE:
            r = kZDataError;
            goto leave;
        default:
            r = kZStreamError;
            goto leave;
        }
    }
leave:
    s->bitb = b; s->bitk = k; avail = n; in = p; s->write = q;
    #undef NEEDBYTE
    #undef NEXTBYTE
    return flush(r);
}

// =============================================================================
// inflate_blocks — VIBE_Inflate_BlocksProcess @0x5fec3c
// =============================================================================
static int InflateBlocks(Inflater::Blocks* s, const u8*& in, std::size_t& avail,
                         int r, const char** msg,
                         std::function<int(int)> flush) {
    u32 b = s->bitb;
    unsigned k = s->bitk;
    const u8* p = in;
    std::size_t n = avail;
    u8* q = s->write;
    unsigned m = WindowFree(s, q);

    #define NEEDBITS(j) do { while (k < (j)) { if (n == 0) { goto exit; } r = kZOk; b |= (u32)(*p++) << k; k += 8; n--; } } while (0)
    #define DUMPBITS(j) do { b >>= (j); k -= (j); } while (0)

    for (;;) {
        switch (s->mode) {
        case kbTYPE: {
            NEEDBITS(3);
            unsigned t = (unsigned)b & 7;
            s->last = t & 1;
            switch (t >> 1) {
            case 0:                       // stored
                DUMPBITS(3);
                t = k & 7;                // go to byte boundary
                DUMPBITS(t);
                s->mode = kbLENS;
                break;
            case 1: {                     // fixed
                g_fixed.Build();
                Codes* c = new Codes();
                c->lbits = (u8)g_fixed.bl;
                c->dbits = (u8)g_fixed.bd;
                c->ltree = g_fixed.tl;
                c->dtree = g_fixed.td;
                c->mode = kcSTART;
                delete s->codes;
                s->codes = c;
                DUMPBITS(3);
                s->mode = kbCODES;
                break;
            }
            case 2:                       // dynamic
                DUMPBITS(3);
                s->mode = kbTABLE;
                break;
            case 3:                       // illegal
                DUMPBITS(3);
                s->mode = kbBLKBAD;
                *msg = "invalid block type";
                r = kZDataError;
                s->bitb = b; s->bitk = k; avail = n; in = p; s->write = q;
                return flush(r);
            }
            break;
        }
        case kbLENS: {
            NEEDBITS(32);
            if ((((~b) >> 16) & 0xffff) != (b & 0xffff)) {
                s->mode = kbBLKBAD;
                *msg = "invalid stored block lengths";
                r = kZDataError;
                s->bitb = b; s->bitk = k; avail = n; in = p; s->write = q;
                return flush(r);
            }
            s->left = (unsigned)b & 0xffff;
            b = k = 0;
            s->mode = s->left ? kbSTORED : (s->last ? kbDRY : kbTYPE);
            break;
        }
        case kbSTORED:
            if (n == 0)
                goto exit;
            if (m == 0) {
                if (q == s->end && s->read != s->window) {
                    q = s->window; m = WindowFree(s, q);
                }
                if (m == 0) {
                    s->write = q; r = flush(r);
                    q = s->write; m = WindowFree(s, q);
                    if (q == s->end) {
                        q = s->window; m = WindowFree(s, q);
                    }
                    if (m == 0)
                        goto exit;
                }
            }
            r = kZOk;
            {
                unsigned t = s->left;
                if (t > n) t = (unsigned)n;
                if (t > m) t = m;
                std::memcpy(q, p, t);
                p += t; n -= t;
                q += t; m -= t;
                s->left -= t;
                if (s->left == 0)
                    s->mode = s->last ? kbDRY : kbTYPE;
            }
            break;
        case kbTABLE: {
            NEEDBITS(14);
            unsigned t = s->table = (unsigned)b & 0x3fff;
            if ((t & 0x1f) > 29 || ((t >> 5) & 0x1f) > 29) {
                s->mode = kbBLKBAD;
                *msg = "too many length or distance symbols";
                r = kZDataError;
                s->bitb = b; s->bitk = k; avail = n; in = p; s->write = q;
                return flush(r);
            }
            t = 258 + (t & 0x1f) + ((t >> 5) & 0x1f);
            s->blens.assign(t, 0);
            DUMPBITS(14);
            s->index = 0;
            s->mode = kbBTREE;
            [[fallthrough]];
        }
        case kbBTREE:
            while (s->index < 4 + (s->table >> 10)) {
                NEEDBITS(3);
                s->blens[kBorder[s->index++]] = (unsigned)b & 7;
                DUMPBITS(3);
            }
            while (s->index < 19)
                s->blens[kBorder[s->index++]] = 0;
            s->bb = 7;
            {
                int t = InflateTreesBits(s->blens.data(), &s->bb, &s->tb, s->hufts.data(), msg);
                if (t != kZOk) {
                    r = t;
                    if (r == kZDataError)
                        s->mode = kbBLKBAD;
                    s->bitb = b; s->bitk = k; avail = n; in = p; s->write = q;
                    return flush(r);
                }
            }
            s->index = 0;
            s->mode = kbDTREE;
            [[fallthrough]];
        case kbDTREE:
            for (;;) {
                unsigned t = s->table;
                if (!(s->index < 258 + (t & 0x1f) + ((t >> 5) & 0x1f)))
                    break;
                InflateHuft* h;
                unsigned i, j, cc;
                t = s->bb;
                NEEDBITS(t);
                h = s->tb + (b & kMask[t]);
                t = h->bits;
                cc = h->base;
                if (cc < 16) {
                    DUMPBITS(t);
                    s->blens[s->index++] = cc;
                } else {                  // c == 16..18: repeat last or zero
                    i = cc == 18 ? 7 : cc - 14;
                    j = cc == 18 ? 11 : 3;
                    NEEDBITS(t + i);
                    DUMPBITS(t);
                    j += (b & kMask[i]);
                    DUMPBITS(i);
                    i = s->index;
                    t = s->table;
                    if (i + j > 258 + (t & 0x1f) + ((t >> 5) & 0x1f) ||
                        (cc == 16 && i < 1)) {
                        s->blens.clear();
                        s->mode = kbBLKBAD;
                        *msg = "invalid bit length repeat";
                        r = kZDataError;
                        s->bitb = b; s->bitk = k; avail = n; in = p; s->write = q;
                        return flush(r);
                    }
                    cc = cc == 16 ? s->blens[i - 1] : 0;
                    do {
                        s->blens[i++] = cc;
                    } while (--j);
                    s->index = i;
                }
            }
            s->tb = nullptr;
            {
                unsigned bl = 9, bd = 6;
                InflateHuft* tl = nullptr;
                InflateHuft* td = nullptr;
                unsigned nl = 257 + (s->table & 0x1f);
                unsigned nd = 1 + ((s->table >> 5) & 0x1f);
                int t = InflateTreesDynamic(nl, nd, s->blens.data(), &bl, &bd,
                                            &tl, &td, s->hufts.data(), msg);
                if (t != kZOk) {
                    if (t == kZDataError)
                        s->mode = kbBLKBAD;
                    r = t;
                    s->blens.clear();
                    s->bitb = b; s->bitk = k; avail = n; in = p; s->write = q;
                    return flush(r);
                }
                Codes* c = new Codes();
                c->lbits = (u8)bl;
                c->dbits = (u8)bd;
                c->ltree = tl;
                c->dtree = td;
                c->mode = kcSTART;
                delete s->codes;
                s->codes = c;
            }
            s->blens.clear();
            s->mode = kbCODES;
            [[fallthrough]];
        case kbCODES: {
            s->bitb = b; s->bitk = k; avail = n; in = p; s->write = q;
            r = InflateCodes(s, s->codes, in, avail, flush);
            if (r != kZStreamEnd)
                return r; // flush already applied inside InflateCodes
            r = kZOk;
            // reset codes (it allocated trees in the shared huft pool, freed implicitly)
            delete s->codes;
            s->codes = nullptr;
            p = in; n = avail; b = s->bitb; k = s->bitk; q = s->write;
            m = WindowFree(s, q);
            if (!s->last) {
                s->mode = kbTYPE;
                break;
            }
            s->mode = kbDRY;
            [[fallthrough]];
        }
        case kbDRY:
            s->write = q;
            r = flush(r);
            q = s->write; m = WindowFree(s, q);
            if (s->read != s->write)
                goto exit;
            s->mode = kbDONE;
            [[fallthrough]];
        case kbDONE:
            r = kZStreamEnd;
            goto exit;
        case kbBLKBAD:
            r = kZDataError;
            goto exit;
        default:
            r = kZStreamError;
            goto exit;
        }
    }
exit:
    s->bitb = b; s->bitk = k; avail = n; in = p; s->write = q;
    #undef NEEDBITS
    #undef DUMPBITS
    return flush(r);
}

// =============================================================================
// Inflater driver
// =============================================================================
Inflater::Inflater(int windowBits) {
    wbits_ = windowBits;
    raw_ = windowBits < 0;
    if (raw_)
        wbits_ = -windowBits;
    if (wbits_ < 8) wbits_ = 8;
    if (wbits_ > 15) wbits_ = 15;
    needsAdler_ = !raw_;
    window_.assign((std::size_t)1 << wbits_, 0);
    NewBlocks();
    Reset();
}

// inflate_blocks_free @0x5ffa80 / inflateEnd — release the owned Blocks (whose
// own ~Blocks frees its codes). Defined here where Blocks is a complete type.
Inflater::~Inflater() {
    delete b_;
}

void Inflater::NewBlocks() {
    delete b_;
    b_ = new Blocks();
    b_->hufts.assign(MANY, InflateHuft{});
    b_->window = window_.data();
    b_->end = window_.data() + window_.size();
    b_->needCheck = needsAdler_;
}

void Inflater::BlocksReset() {
    Blocks* s = b_;
    delete s->codes;
    s->codes = nullptr;
    s->mode = kbTYPE;
    s->bitk = 0;
    s->bitb = 0;
    s->read = s->write = s->window;
    s->check = needsAdler_ ? Adler32(0, nullptr, 0) : 0; // adler seed = 1; raw=0
    s->last = 0;
}

void Inflater::Reset() {
    mode_ = raw_ ? 7 /*BLOCKS*/ : 0 /*METHOD*/;
    method_ = 0;
    checkComputed_ = needsAdler_ ? 1u : 0u;
    checkExpected_ = 0;
    msg_ = nullptr;
    BlocksReset();
}

// VIBE_Inflate_Process @0x5eca44 — the zlib-header wrapper FSM, plus a raw mode
// that goes straight to the block decoder. Returns a zlib code.
int Inflater::Process(const u8* in, std::size_t in_len, std::vector<u8>& out, int flush) {
    next_in_ = in;
    avail_in_ = in_len;
    out_ = &out;

    auto blockFlush = [&](int rr) -> int { return RunBlocks(rr); };

    int r = kZBufError;
    for (;;) {
        switch (mode_) {
        case 0: // METHOD
            if (avail_in_ == 0) return r;
            r = kZOk;
            method_ = *next_in_++; avail_in_--;
            if ((method_ & 0xf) != 8) {
                mode_ = 13; // BAD
                msg_ = "unknown compression method";
                break;
            }
            if ((method_ >> 4) + 8 > (unsigned)wbits_) {
                mode_ = 13;
                msg_ = "invalid window size";
                break;
            }
            mode_ = 1; // FLAG
            [[fallthrough]];
        case 1: { // FLAG
            if (avail_in_ == 0)
                return r;
            r = kZOk;
            unsigned f = *next_in_++; avail_in_--;
            if (((method_ << 8) + f) % 31) {
                mode_ = 13; msg_ = "incorrect header check"; break;
            }
            if (f & 0x20) { mode_ = 2; break; } // FDICT
            mode_ = 7; // BLOCKS
            break;
        }
        case 2: case 3: case 4: case 5: // DICT4..DICT0
            mode_ = 13; msg_ = "need dictionary"; return kZNeedDict;
        case 7: { // BLOCKS
            const u8* p = next_in_;
            std::size_t avail = avail_in_;
            int rr = InflateBlocks(b_, p, avail, kZOk, &msg_, blockFlush);
            next_in_ = p; avail_in_ = avail;
            if (rr == kZDataError) {
                mode_ = 13;
                break;
            }
            r = rr; // kZOk = need more input/output; kZStreamEnd = block stream done
            if (r != kZStreamEnd)
                return r;
            r = kZOk;
            BlocksReset();
            if (raw_) {
                mode_ = 12; // DONE
                break;
            }
            mode_ = 8; // CHECK4
            [[fallthrough]];
        }
        case 8: case 9: case 10: case 11: { // CHECK4..CHECK1
            if (avail_in_ == 0)
                return r;
            r = kZOk;
            unsigned shift = (11 - mode_) * 8; // 8->24,9->16,10->8,11->0
            if (mode_ == 8) checkExpected_ = 0;
            checkExpected_ += (u32)(*next_in_++) << shift;
            avail_in_--;
            if (mode_ == 11) {
                if (checkComputed_ != checkExpected_) {
                    mode_ = 13; msg_ = "incorrect data check"; break;
                }
                mode_ = 12; // DONE
                return kZStreamEnd;
            }
            mode_++;
            break;
        }
        case 12: // DONE
            return kZStreamEnd;
        case 13: // BAD
            return kZDataError;
        default:
            return kZStreamError;
        }
    }
    (void)flush;
}

// One-shot helpers.
bool Inflate(const u8* in, std::size_t in_len, std::vector<u8>& out) {
    Inflater z(15);
    int r = z.Process(in, in_len, out, kZFinish);
    return r == kZStreamEnd;
}

bool InflateRaw(const u8* in, std::size_t in_len, std::vector<u8>& out) {
    Inflater z(-15);
    int r = z.Process(in, in_len, out, kZFinish);
    return r == kZStreamEnd;
}

} // namespace guild::compress
