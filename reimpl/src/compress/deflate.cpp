// deflate.cpp — VIBE_Deflate_* compression driver and LZ77 matcher (zlib 1.1.4
// deflate.c). The Huffman tree builders and static tables live in trees.cpp; this
// file holds deflateInit2/deflate/deflateEnd, the per-strategy deflate_stored /
// deflate_fast / deflate_slow, fill_window, longest_match, read_buf, lm_init, and
// the tr_tally symbol-buffering helper. Each function carries its gilde.exe
// address. The struct field offsets in the original deflate_state are recorded in
// deflate_state.h.
#include "compress/deflate.h"
#include "compress/deflate_state.h"
#include <cstring>

namespace guild::compress {

// ---- hash + window helpers (deflate.c macros) ------------------------------
static inline void UpdateHash(DeflateState* s, unsigned c) {
    s->ins_h = ((s->ins_h << s->hash_shift) ^ c) & s->hash_mask;
}
// INSERT_STRING: hash window[strstart..+2], chain head, return old head.
static inline unsigned InsertString(DeflateState* s, unsigned str, unsigned& match_head) {
    UpdateHash(s, s->window[str + (kMinMatch - 1)]);
    match_head = s->prev[str & s->w_mask] = s->head[s->ins_h];
    s->head[s->ins_h] = (u16)str;
    return match_head;
}

// tr_tally — buffer one symbol; returns true when the block buffer is full.
// (zlib's _tr_tally macro, inlined into deflate_fast/deflate_slow.)
static inline bool TrTally(DeflateState* s, unsigned dist, unsigned lc) {
    s->d_buf[s->last_lit] = (u16)dist;
    s->l_buf[s->last_lit] = (u8)lc;
    s->last_lit++;
    if (dist == 0) {
        s->dyn_ltree[lc].fc++;
    } else {
        dist--;
        s->dyn_ltree[kLengthCode[lc] + kLiterals + 1].fc++;
        s->dyn_dtree[(dist < 256) ? kDistCode[dist] : kDistCode[256 + (dist >> 7)]].fc++;
    }
    return s->last_lit == (unsigned)(s->lit_bufsize - 1);
}

// flush a finished block to the output through the bit/byte writers in trees.cpp.
#define FLUSH_BLOCK_ONLY(s, last) do {                                          \
        TrFlushBlock((s), ((s)->block_start >= 0 ?                              \
            (int)(s)->block_start : -1),                                        \
            (unsigned)((s)->strstart - (s)->block_start), (last));             \
        (s)->block_start = (s)->strstart;                                       \
        FlushPending((s));                                                      \
    } while (0)

// forward decls
static void FlushPending(DeflateState* s);
static unsigned ReadBuf(DeflateState* s, u8* buf, unsigned size);

// =============================================================================
// read_buf — VIBE_Deflate_ReadBuffer @0x5edb40.
// Copy up to `size` bytes from next_in into buf, updating adler (zlib mode).
// =============================================================================
static unsigned ReadBuf(DeflateState* s, u8* buf, unsigned size) {
    unsigned len = (unsigned)s->avail_in;
    if (len > size) len = size;
    if (len == 0) return 0;
    s->avail_in -= len;
    if (!s->noheader) {
        s->adler = Adler32(s->adler, s->next_in, len);
    }
    std::memcpy(buf, s->next_in, len);
    s->next_in += len;
    s->total_in += len;
    return len;
}

// =============================================================================
// flush_pending — VIBE_Deflate_FlushPending @0x5ed578.
// Move queued pending_buf bytes into the output sink.
// =============================================================================
static void FlushPending(DeflateState* s) {
    unsigned len = (unsigned)s->pending;
    if (len == 0) return;
    s->out->insert(s->out->end(),
                   s->pending_buf.data() + s->pending_out,
                   s->pending_buf.data() + s->pending_out + len);
    s->total_out += len;
    s->pending_out += len;
    s->pending -= len;
    if (s->pending == 0) s->pending_out = 0;
}

static inline void PutByte(DeflateState* s, u8 c) { s->pending_buf[s->pending++] = c; }
// putShortMSB — VIBE_Deflate_PutShort @0x5ed54c (big-endian, for zlib header/adler).
static inline void PutShortMSB(DeflateState* s, u16 w) {
    PutByte(s, (u8)(w >> 8));
    PutByte(s, (u8)(w & 0xff));
}

// =============================================================================
// lm_init — VIBE_Deflate_InitMatch @0x5edbc0.
// =============================================================================
static void LmInit(DeflateState* s) {
    s->window_size = 2 * s->w_size;
    std::memset(s->head.data(), 0, s->head.size() * sizeof(u16));

    const Config& cfg = kConfigurationTable[s->level];
    s->max_lazy_match = cfg.max_lazy;
    s->good_match = cfg.good_length;
    s->nice_match = cfg.nice_length;
    s->max_chain_length = cfg.max_chain;

    s->strstart = 0;
    s->block_start = 0;
    s->lookahead = 0;
    s->match_length = s->prev_length = kMinMatch - 1;
    s->match_available = 0;
    s->ins_h = 0;
}

// =============================================================================
// longest_match — VIBE_Deflate_LongestMatch @0x5edc94.
// Find the longest match for the string at strstart, starting at cur_match.
// =============================================================================
static unsigned LongestMatch(DeflateState* s, unsigned cur_match) {
    unsigned chain_length = s->max_chain_length;
    u8* scan = s->window.data() + s->strstart;
    unsigned best_len = s->prev_length;
    unsigned nice_match = (unsigned)s->nice_match;

    unsigned limit = s->strstart > (s->w_size - kMinLookahead)
                     ? s->strstart - (s->w_size - kMinLookahead) : 0;
    u16* prev = s->prev.data();
    unsigned wmask = s->w_mask;

    u8* strend = s->window.data() + s->strstart + kMaxMatch;
    u8 scan_end1 = scan[best_len - 1];
    u8 scan_end = scan[best_len];

    if (s->prev_length >= s->good_match) chain_length >>= 2;
    if (nice_match > s->lookahead) nice_match = s->lookahead;

    do {
        u8* match = s->window.data() + cur_match;
        if (match[best_len] != scan_end ||
            match[best_len - 1] != scan_end1 ||
            *match != *scan ||
            *++match != scan[1]) {
            cur_match = prev[cur_match & wmask];
            if (cur_match <= limit) break;
            continue;
        }
        scan += 2;
        match++;
        // match window[strstart+2..]; the original unrolls by 8.
        do {
        } while (*++scan == *++match && *++scan == *++match &&
                 *++scan == *++match && *++scan == *++match &&
                 *++scan == *++match && *++scan == *++match &&
                 *++scan == *++match && *++scan == *++match &&
                 scan < strend);

        unsigned len = kMaxMatch - (unsigned)(strend - scan);
        scan = strend - kMaxMatch;

        if (len > best_len) {
            s->match_start = cur_match;
            best_len = len;
            if (len >= nice_match) break;
            scan_end1 = scan[best_len - 1];
            scan_end = scan[best_len];
        }
        cur_match = prev[cur_match & wmask];
        if (cur_match <= limit) break;
    } while (--chain_length != 0);

    if (best_len <= s->lookahead) return best_len;
    return s->lookahead;
}

// =============================================================================
// fill_window — VIBE_Deflate_FillWindow @0x5ede64.
// Slide the window when needed and read more input into it.
// =============================================================================
static void FillWindow(DeflateState* s) {
    unsigned wsize = s->w_size;
    do {
        unsigned more = s->window_size - s->lookahead - s->strstart;

        if (more == 0 && s->strstart == 0 && s->lookahead == 0) {
            more = wsize;
        } else if (more == (unsigned)-1) {
            more--;
        } else if (s->strstart >= wsize + (wsize - kMinLookahead)) {
            std::memcpy(s->window.data(), s->window.data() + wsize, wsize);
            s->match_start -= wsize;
            s->strstart -= wsize;
            s->block_start -= (long)wsize;

            unsigned n = s->hash_size;
            u16* p = s->head.data() + n;
            do {
                unsigned m = *--p;
                *p = (u16)(m >= wsize ? m - wsize : 0);
            } while (--n);

            n = wsize;
            p = s->prev.data() + n;
            do {
                unsigned m = *--p;
                *p = (u16)(m >= wsize ? m - wsize : 0);
            } while (--n);
            more += wsize;
        }
        if (s->avail_in == 0) return;

        unsigned n = ReadBuf(s, s->window.data() + s->strstart + s->lookahead, more);
        s->lookahead += n;

        if (s->lookahead >= kMinMatch) {
            s->ins_h = s->window[s->strstart];
            UpdateHash(s, s->window[s->strstart + 1]);
        }
    } while (s->lookahead < kMinLookahead && s->avail_in != 0);
}

// =============================================================================
// deflate_stored — VIBE_Deflate_Stored @0x5ee020.  (level 0)
// =============================================================================
enum BlockState { kNeedMore = 0, kBlockDone = 1, kFinishStarted = 2, kFinishDone = 3 };

static int DeflateStored(DeflateState* s, int flush) {
    unsigned long max_block_size = 0xffff;
    if (max_block_size > (unsigned long)s->pending_buf.size() - 5)
        max_block_size = (unsigned long)s->pending_buf.size() - 5;

    for (;;) {
        if (s->lookahead <= 1) {
            FillWindow(s);
            if (s->lookahead == 0 && flush == 0 /*Z_NO_FLUSH*/) return kNeedMore;
            if (s->lookahead == 0) break;
        }
        s->strstart += s->lookahead;
        s->lookahead = 0;

        unsigned long max_start = s->block_start + max_block_size;
        if (s->strstart == 0 || (unsigned long)s->strstart >= max_start) {
            s->lookahead = (unsigned)(s->strstart - max_start);
            s->strstart = (unsigned)max_start;
            FLUSH_BLOCK_ONLY(s, 0);
        }
        if (s->strstart - (unsigned)s->block_start >= s->w_size - kMinLookahead) {
            FLUSH_BLOCK_ONLY(s, 0);
        }
    }
    FLUSH_BLOCK_ONLY(s, flush == 4 /*Z_FINISH*/);
    return flush == 4 ? kFinishDone : kBlockDone;
}

// =============================================================================
// deflate_fast — VIBE_Deflate_Fast @0x5ee1b0.  (levels 1-3)
// =============================================================================
static int DeflateFast(DeflateState* s, int flush) {
    unsigned hash_head;
    for (;;) {
        if (s->lookahead < kMinLookahead) {
            FillWindow(s);
            if (s->lookahead < kMinLookahead && flush == 0) return kNeedMore;
            if (s->lookahead == 0) break;
        }
        hash_head = 0;
        if (s->lookahead >= kMinMatch) {
            InsertString(s, s->strstart, hash_head);
        }
        if (hash_head != 0 &&
            s->strstart - hash_head <= s->w_size - kMinLookahead &&
            s->strategy != 2 /*Z_HUFFMAN_ONLY*/) {
            s->match_length = LongestMatch(s, hash_head);
        }
        bool bflush;
        if (s->match_length >= kMinMatch) {
            bflush = TrTally(s, s->strstart - s->match_start, s->match_length - kMinMatch);
            s->lookahead -= s->match_length;

            if (s->match_length <= s->max_lazy_match && s->lookahead >= kMinMatch) {
                s->match_length--;
                do {
                    s->strstart++;
                    InsertString(s, s->strstart, hash_head);
                } while (--s->match_length != 0);
                s->strstart++;
            } else {
                s->strstart += s->match_length;
                s->match_length = 0;
                s->ins_h = s->window[s->strstart];
                UpdateHash(s, s->window[s->strstart + 1]);
            }
        } else {
            bflush = TrTally(s, 0, s->window[s->strstart]);
            s->lookahead--;
            s->strstart++;
        }
        if (bflush) FLUSH_BLOCK_ONLY(s, 0);
    }
    FLUSH_BLOCK_ONLY(s, flush == 4);
    return flush == 4 ? kFinishDone : kBlockDone;
}

// =============================================================================
// deflate_slow — VIBE_Deflate_Slow @0x5ee530.  (levels 4-9)
// =============================================================================
static int DeflateSlow(DeflateState* s, int flush) {
    unsigned hash_head;
    for (;;) {
        if (s->lookahead < kMinLookahead) {
            FillWindow(s);
            if (s->lookahead < kMinLookahead && flush == 0) return kNeedMore;
            if (s->lookahead == 0) break;
        }
        hash_head = 0;
        if (s->lookahead >= kMinMatch) {
            InsertString(s, s->strstart, hash_head);
        }
        s->prev_length = s->match_length;
        s->prev_match = s->match_start;
        s->match_length = kMinMatch - 1;

        if (hash_head != 0 && s->prev_length < s->max_lazy_match &&
            s->strstart - hash_head <= s->w_size - kMinLookahead) {
            if (s->strategy != 2) {
                s->match_length = LongestMatch(s, hash_head);
            }
            if (s->match_length <= 5 &&
                (s->strategy == 1 /*Z_FILTERED*/ ||
                 (s->match_length == kMinMatch && s->strstart - s->match_start > 4096))) {
                s->match_length = kMinMatch - 1;
            }
        }

        if (s->prev_length >= kMinMatch && s->match_length <= s->prev_length) {
            unsigned max_insert = s->strstart + s->lookahead - kMinMatch;
            bool bflush = TrTally(s, s->strstart - 1 - s->prev_match, s->prev_length - kMinMatch);
            s->lookahead -= s->prev_length - 1;
            s->prev_length -= 2;
            do {
                if (++s->strstart <= max_insert) {
                    InsertString(s, s->strstart, hash_head);
                }
            } while (--s->prev_length != 0);
            s->match_available = 0;
            s->match_length = kMinMatch - 1;
            s->strstart++;
            if (bflush) FLUSH_BLOCK_ONLY(s, 0);
        } else if (s->match_available) {
            bool bflush = TrTally(s, 0, s->window[s->strstart - 1]);
            if (bflush) {
                TrFlushBlock(s, s->block_start >= 0 ? (int)s->block_start : -1,
                             (unsigned)(s->strstart - s->block_start), 0);
                s->block_start = s->strstart;
                FlushPending(s);
            }
            s->strstart++;
            s->lookahead--;
        } else {
            s->match_available = 1;
            s->strstart++;
            s->lookahead--;
        }
    }
    if (s->match_available) {
        TrTally(s, 0, s->window[s->strstart - 1]);
        s->match_available = 0;
    }
    FLUSH_BLOCK_ONLY(s, flush == 4);
    return flush == 4 ? kFinishDone : kBlockDone;
}

// =============================================================================
// deflate driver — VIBE_Deflate_Process @0x5ed5f0 (single-shot, Z_FINISH).
// =============================================================================
static int RunDeflate(DeflateState* s, int flush) {
    // header (status == INIT_STATE)
    if (s->status == kInitState) {
        unsigned header = (8 + ((s->w_bits - 8) << 4)) << 8; // Z_DEFLATED + cinfo
        // gilde.exe 0x5ed5f0: level_flags = (level - 1) >> 1, clamped to 3
        // (level 0 wraps to a huge value and clamps to 3 — zlib 1.1.4 formula).
        unsigned level_flags = ((unsigned)s->level - 1) >> 1;
        if (level_flags > 3) level_flags = 3;
        header |= (level_flags << 6);
        if (s->strstart != 0) header |= 0x20; // PRESET_DICT (unused here)
        header += 31 - (header % 31);
        s->status = kBusyState;
        PutShortMSB(s, (u16)header);
        s->adler = 1;
    }

    if (s->pending != 0) {
        FlushPending(s);
    }

    int bstate = kNeedMore;
    if (s->strstart != 0 || s->lookahead != 0 || flush != 0 || s->status == kFinishState) {
        int func = kConfigurationTable[s->level].func;
        if (func == 0) bstate = DeflateStored(s, flush);
        else if (func == 1) bstate = DeflateFast(s, flush);
        else bstate = DeflateSlow(s, flush);

        if (bstate == kFinishStarted || bstate == kFinishDone) {
            s->status = kFinishState;
        }
    }

    if (flush != 4 /*Z_FINISH*/) return kZOk;
    if (s->noheader) return kZStreamEnd;

    // append adler32 trailer (zlib framing).
    PutShortMSB(s, (u16)(s->adler >> 16));
    PutShortMSB(s, (u16)(s->adler & 0xffff));
    FlushPending(s);
    s->noheader = -1;
    return kZStreamEnd;
}

// =============================================================================
// deflateInit2 — VIBE_Deflate_Init2 @0x5ed068.
// =============================================================================
static bool DeflateInit2(DeflateState* s, int level, int windowBits, int memLevel) {
    if (level == -1 /*Z_DEFAULT_COMPRESSION*/) level = 6;

    int noheader = 0;
    if (windowBits < 0) { noheader = 1; windowBits = -windowBits; }

    if (memLevel < 1 || memLevel > 9 ||
        windowBits < 8 || windowBits > 15 ||
        level < 0 || level > 9) {
        return false;
    }

    s->noheader = noheader;
    s->w_bits = windowBits;
    s->w_size = 1u << s->w_bits;
    s->w_mask = s->w_size - 1;

    s->hash_bits = memLevel + 7;
    s->hash_size = 1u << s->hash_bits;
    s->hash_mask = s->hash_size - 1;
    s->hash_shift = (s->hash_bits + kMinMatch - 1) / kMinMatch;

    s->window.assign(s->w_size * 2, 0);
    s->prev.assign(s->w_size, 0);
    s->head.assign(s->hash_size, 0);

    s->lit_bufsize = 1 << (memLevel + 6);
    s->pending_buf.assign((std::size_t)s->lit_bufsize * 4, 0);
    s->l_buf.assign(s->lit_bufsize, 0);
    s->d_buf.assign(s->lit_bufsize, 0);

    s->level = level;
    s->strategy = 0; // Z_DEFAULT_STRATEGY
    s->method = 8;

    // deflateReset
    s->pending = 0;
    s->pending_out = 0;
    s->total_in = s->total_out = 0;
    s->status = s->noheader ? kBusyState : kInitState;
    s->adler = 1;
    s->last_flush = 0;
    TreeInit(s);
    LmInit(s);
    return true;
}

// =============================================================================
// Public driver.
// =============================================================================
Deflater::Deflater(int level, int windowBits) {
    s_ = new DeflateState();
    ok_ = DeflateInit2(s_, level, windowBits, 8 /*default memLevel*/);
}

Deflater::~Deflater() {
    delete s_;
}

bool Deflater::Run(const u8* in, std::size_t in_len, std::vector<u8>& out) {
    if (!ok_) return false;
    s_->next_in = in;
    s_->avail_in = in_len;
    s_->out = &out;
    int r = RunDeflate(s_, 4 /*Z_FINISH*/);
    return r == kZStreamEnd;
}

bool Deflate(const u8* in, std::size_t in_len, std::vector<u8>& out, int level) {
    Deflater z(level, 15);
    return z.Run(in, in_len, out);
}

bool DeflateRaw(const u8* in, std::size_t in_len, std::vector<u8>& out, int level) {
    Deflater z(level, -15);
    return z.Run(in, in_len, out);
}

} // namespace guild::compress
