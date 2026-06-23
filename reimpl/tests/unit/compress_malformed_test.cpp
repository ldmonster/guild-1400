// Wave-11 hardening: malformed / truncated / corrupt input tests for the
// reconstructed zlib codec (inflate state machine + adler/crc). These drive the
// real parse/decode entry points (Inflate / InflateRaw / Inflater::Process /
// Adler32 / CrcCompute) with adversarial bytes so ASAN+UBSAN exercises every
// bounds-check in the huffman tables, the window ring and the code reader.
//
// Contract under test: a malformed stream must FAIL SAFE — return a non-success
// zlib code, never read/write out of bounds, never leak. Valid streams keep
// their golden output (covered by compress_zlib_test / compress_inflate_window).
#include "test.h"
#include "compress/inflate.h"
#include "compress/zlib.h"
#include "compress/deflate.h"
#include "compress/crc.h"
#include "compress/gzip.h"
#include <vector>
#include <string>
#include <cstring>

using namespace guild::compress;
using guild::u8;
using guild::u32;

#include "compress_zlib_vectors.inc"

namespace {

// Build a real zlib stream from a payload so we can corrupt it deterministically.
std::vector<u8> MakeZlib(const std::string& s, int level = 6) {
    std::vector<u8> in(s.begin(), s.end());
    std::vector<u8> out;
    bool ok = Deflate(in.data(), in.size(), out, level);
    (void)ok;
    return out;
}
std::vector<u8> MakeRaw(const std::string& s, int level = 6) {
    std::vector<u8> in(s.begin(), s.end());
    std::vector<u8> out;
    bool ok = DeflateRaw(in.data(), in.size(), out, level);
    (void)ok;
    return out;
}

} // namespace

// ---------------------------------------------------------------------------
// Empty / 1-byte / header-only inputs must not over-read.
// ---------------------------------------------------------------------------
TEST(inflate_malformed, zero_byte_input) {
    std::vector<u8> out;
    // zlib wrapper: no method byte -> stays in METHOD, returns non-stream-end.
    CHECK(!Inflate(nullptr, 0, out));
    CHECK(out.empty());
    CHECK(!InflateRaw(nullptr, 0, out));
}

TEST(inflate_malformed, one_byte_zlib_header_only) {
    std::vector<u8> out;
    u8 b = 0x78; // valid CMF, but no FLG -> stops in FLAG state, not stream-end
    CHECK(!Inflate(&b, 1, out));
}

TEST(inflate_malformed, header_only_no_blocks) {
    // Valid 2-byte zlib header (0x78 0x9C) then EOF — must not run off the end.
    u8 hdr[2] = {0x78, 0x9C};
    std::vector<u8> out;
    CHECK(!Inflate(hdr, 2, out));
    CHECK(out.empty());
}

// ---------------------------------------------------------------------------
// Truncation at every byte boundary: never crash, never claim success early.
// ---------------------------------------------------------------------------
TEST(inflate_malformed, truncated_zlib_every_prefix) {
    std::vector<u8> full = MakeZlib(
        "the quick brown fox jumps over the lazy dog, repeatedly. "
        "the quick brown fox jumps over the lazy dog, repeatedly.");
    CHECK(full.size() > 4u);
    // Every strict prefix must NOT decode to stream-end (it's incomplete) and
    // must not read out of bounds (ASAN watches).
    for (std::size_t n = 0; n < full.size(); ++n) {
        std::vector<u8> out;
        bool ok = Inflate(full.data(), n, out);
        CHECK(!ok);
    }
    // The full stream still works.
    std::vector<u8> out;
    CHECK(Inflate(full.data(), full.size(), out));
}

TEST(inflate_malformed, truncated_raw_every_prefix) {
    std::vector<u8> full = MakeRaw(
        "raw deflate payload to be truncated at every boundary 12345 12345 12345");
    for (std::size_t n = 0; n < full.size(); ++n) {
        std::vector<u8> out;
        bool ok = InflateRaw(full.data(), n, out);
        CHECK(!ok);
    }
    std::vector<u8> out;
    CHECK(InflateRaw(full.data(), full.size(), out));
}

// Feed a valid stream one byte at a time but stop early: must remain kZOk and
// never over-read past the supplied chunk.
TEST(inflate_malformed, streamed_then_starved) {
    std::vector<u8> full = MakeZlib(std::string(4096, 'Q') + "tail");
    Inflater z(15);
    std::vector<u8> out;
    int r = kZOk;
    // feed all but the last 3 bytes
    for (std::size_t i = 0; i + 3 < full.size(); ++i) {
        r = z.Process(full.data() + i, 1, out, kZNoFlush);
        CHECK(r == kZOk || r == kZStreamEnd);
        if (r == kZStreamEnd) break;
    }
    // Starved before the adler trailer -> not stream-end yet.
    CHECK(r != kZDataError);
}

// ---------------------------------------------------------------------------
// Bad block type (BTYPE == 3 is reserved/illegal).
// ---------------------------------------------------------------------------
TEST(inflate_malformed, illegal_block_type_3) {
    // Raw deflate: first byte low 3 bits = BFINAL(1) | BTYPE(11) = 0b111 = 0x07.
    u8 data[8] = {0x07, 0, 0, 0, 0, 0, 0, 0};
    std::vector<u8> out;
    CHECK(!InflateRaw(data, sizeof(data), out));
    // Via the streaming API, the error code is specifically kZDataError.
    Inflater z(-15);
    int r = z.Process(data, sizeof(data), out, kZFinish);
    CHECK(r == kZDataError);
    CHECK(std::string(z.msg() ? z.msg() : "") == "invalid block type");
}

// ---------------------------------------------------------------------------
// Stored block with a corrupt length (LEN != ~NLEN).
// ---------------------------------------------------------------------------
TEST(inflate_malformed, stored_block_bad_length_complement) {
    // Raw stored block: byte0 = BTYPE 00 (stored), BFINAL 1 -> 0x01.
    // After 3 type bits + align to byte boundary, next 4 bytes are LEN/NLEN LE.
    // Make LEN=5, NLEN=0x0000 (should be 0xFFFA) -> complement mismatch.
    u8 data[16] = {0x01, /*LEN*/0x05, 0x00, /*NLEN bad*/0x00, 0x00,
                   'h','e','l','l','o', 0,0,0,0,0,0};
    std::vector<u8> out;
    Inflater z(-15);
    int r = z.Process(data, sizeof(data), out, kZFinish);
    CHECK(r == kZDataError);
    CHECK(std::string(z.msg() ? z.msg() : "") == "invalid stored block lengths");
}

TEST(inflate_malformed, stored_block_good_length_then_truncated) {
    // Valid complement (LEN=10, NLEN=~10) but the 10 stored bytes are missing.
    const unsigned len = 10;
    u8 data[5];
    data[0] = 0x01;
    data[1] = (u8)(len & 0xff);
    data[2] = (u8)(len >> 8);
    data[3] = (u8)(~len & 0xff);
    data[4] = (u8)((~len >> 8) & 0xff);
    std::vector<u8> out;
    // Only the header is present; the body bytes are truncated -> not stream-end,
    // and no over-read of the 5-byte buffer.
    Inflater z(-15);
    int r = z.Process(data, sizeof(data), out, kZFinish);
    CHECK(r != kZStreamEnd);
    CHECK(r != kZDataError); // ran out of input, fail-soft (kZOk/kZBufError)
}

// A correctly-framed stored block must still decode (positive control).
TEST(inflate_malformed, stored_block_valid_roundtrip) {
    // level-0 deflate emits stored blocks; verify our decoder accepts them.
    std::vector<u8> full = MakeRaw("stored block content under test!", 0);
    std::vector<u8> out;
    CHECK(InflateRaw(full.data(), full.size(), out));
    CHECK_EQ(out.size(), (size_t)32);
}

// ---------------------------------------------------------------------------
// Dynamic-block header corruption: oversubscribed / out-of-range symbol counts.
// ---------------------------------------------------------------------------
TEST(inflate_malformed, dynamic_too_many_symbols) {
    // Raw deflate, BTYPE=10 (dynamic), BFINAL=1 -> low 3 bits = 0b101 = 0x05.
    // Then 14 bits HLIT/HDIST/HCLEN. Set HLIT and HDIST to their max so the
    // "too many length or distance symbols" guard ((t&0x1f)>29) fires.
    // Bit layout after the 3 type bits: 5 bits HLIT, 5 bits HDIST, 4 bits HCLEN.
    // We pack a 32-bit little-endian value with the type bits at the bottom.
    // type=0b101 ; HLIT=31 ; HDIST=31 -> table low/ mid nibble both 0x1f.
    u32 bits = 0;
    int pos = 0;
    auto put = [&](u32 v, int n) { bits |= (v & ((1u << n) - 1)) << pos; pos += n; };
    put(0b101, 3);   // BFINAL=1, BTYPE=10 (dynamic)
    put(31, 5);      // HLIT  -> 257+31 = 288 (max)  ; (t & 0x1f) == 31 > 29
    put(31, 5);      // HDIST
    put(0, 4);       // HCLEN
    // 3+5+5+4 = 17 bits fit in 3 bytes; pad to 8 so the NEEDBITS(14) read of the
    // table header never starves before the guard fires.
    u8 data[8] = {0,0,0,0,0,0,0,0};
    for (int i = 0; i < 4; ++i) data[i] = (u8)(bits >> (i * 8));
    std::vector<u8> out;
    Inflater z(-15);
    int r = z.Process(data, sizeof(data), out, kZFinish);
    CHECK(r == kZDataError);
    CHECK(std::string(z.msg() ? z.msg() : "") == "too many length or distance symbols");
}

// Corrupt the body of a real dynamic stream at each interior byte: the decoder
// must reject or starve, never run off the huffman tables or the window.
TEST(inflate_malformed, fuzz_corrupt_interior_bytes) {
    std::vector<u8> base = MakeZlib(
        std::string("Lorem ipsum dolor sit amet, consectetur adipiscing elit, "
                    "sed do eiusmod tempor incididunt ut labore et dolore magna. ")
        + std::string("padding-padding-padding-padding-padding-padding-padding-"));
    CHECK(base.size() > 6u);
    // Flip a high bit of each interior byte (skip the 2 header + 4 trailer bytes
    // so we hit the deflate body) and ensure no crash / no false success.
    for (std::size_t i = 2; i + 4 < base.size(); ++i) {
        std::vector<u8> bad = base;
        bad[i] ^= 0x40;
        std::vector<u8> out;
        bool ok = Inflate(bad.data(), bad.size(), out);
        // It may legitimately still decode (a flipped bit can be valid), but it
        // must never crash; ASAN is the real assertion here.
        (void)ok;
    }
    CHECK(true);
}

// XOR every byte across the whole stream with a sweep of values: pure fuzz to
// drive ASAN through the table/window code paths.
TEST(inflate_malformed, fuzz_xor_sweep) {
    std::vector<u8> base = MakeZlib("aaaaaaaaaabbbbbbbbbbccccccccccdddddddddd");
    for (u8 mask = 1; mask; mask = (u8)(mask << 1)) {
        for (std::size_t i = 0; i < base.size(); ++i) {
            std::vector<u8> bad = base;
            bad[i] ^= mask;
            std::vector<u8> out;
            (void)Inflate(bad.data(), bad.size(), out);
        }
    }
    CHECK(true);
}

// ---------------------------------------------------------------------------
// Window-size header edge: a zlib stream whose declared window (CMF high nibble)
// exceeds the decoder's wbits must be rejected ("invalid window size").
// ---------------------------------------------------------------------------
TEST(inflate_malformed, window_size_too_large) {
    // CMF = method(8) | (wbits-8 << 4). For wbits=15, high nibble = 7 -> 0x78.
    // Decoder built with windowBits=8 (wbits_=8): a 0x78 header asks for 15 ->
    // (method>>4)+8 = 15 > 8 -> "invalid window size".
    Inflater z(8);
    u8 hdr[4] = {0x78, 0x9C, 0x03, 0x00};
    std::vector<u8> out;
    int r = z.Process(hdr, sizeof(hdr), out, kZFinish);
    CHECK(r == kZDataError);
    CHECK(std::string(z.msg() ? z.msg() : "") == "invalid window size");
}

TEST(inflate_malformed, unknown_compression_method) {
    Inflater z(15);
    u8 hdr[2] = {0x77 /* method nibble 7, not 8 */, 0x00};
    std::vector<u8> out;
    int r = z.Process(hdr, sizeof(hdr), out, kZFinish);
    CHECK(r == kZDataError);
    CHECK(std::string(z.msg() ? z.msg() : "") == "unknown compression method");
}

TEST(inflate_malformed, bad_header_check_modulo) {
    Inflater z(15);
    // method ok (0x78) but FLG chosen so (CMF*256+FLG) % 31 != 0.
    u8 hdr[2] = {0x78, 0x9D /* one off from a valid 0x9C/0x01 etc */};
    // 0x789D % 31: ensure nonzero
    std::vector<u8> out;
    int r = z.Process(hdr, sizeof(hdr), out, kZFinish);
    if ((0x7800 + 0x9D) % 31 != 0) {
        CHECK(r == kZDataError);
        CHECK(std::string(z.msg() ? z.msg() : "") == "incorrect header check");
    }
}

// ---------------------------------------------------------------------------
// Reuse / reset safety: a failed decode followed by Reset + valid decode works
// (catches leaks of the per-block Codes / huft state on the error path).
// ---------------------------------------------------------------------------
TEST(inflate_malformed, reset_after_error_then_valid) {
    Inflater z(15);
    u8 bad[8] = {0x78, 0x9C, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    std::vector<u8> out;
    z.Process(bad, sizeof(bad), out, kZFinish);
    z.Reset();
    out.clear();
    int r = z.Process(k_short_zlib, k_short_zlib_len, out, kZFinish);
    CHECK(r == kZStreamEnd);
}

// Decode the SAME stream repeatedly through one Inflater (Reset between) — a
// leak in the block/codes pool would show as growth under ASAN's leak check.
TEST(inflate_malformed, repeated_decode_no_leak) {
    Inflater z(15);
    for (int i = 0; i < 64; ++i) {
        std::vector<u8> out;
        z.Reset();
        int r = z.Process(k_rle_zlib, k_rle_zlib_len, out, kZFinish);
        CHECK(r == kZStreamEnd);
    }
}

// ---------------------------------------------------------------------------
// gzip framing: truncated / corrupt headers must fail-safe (VIBE_Gzip_CheckHeader).
// ---------------------------------------------------------------------------
TEST(inflate_malformed, gzip_empty_and_short) {
    std::vector<u8> out;
    CHECK(!Gunzip(nullptr, 0, out));
    u8 one = 0x1f;
    CHECK(!Gunzip(&one, 1, out));            // magic0 only
    u8 magic[2] = {0x1f, 0x8b};
    CHECK(!Gunzip(magic, 2, out));           // magic, no method/flags
}

TEST(inflate_malformed, gzip_bad_magic_and_method) {
    std::vector<u8> out;
    u8 badmagic[10] = {0x1f, 0x00, 8, 0, 0,0,0,0, 0,0};
    CHECK(!Gunzip(badmagic, sizeof(badmagic), out));
    u8 badmethod[10] = {0x1f, 0x8b, 9 /*not deflate*/, 0, 0,0,0,0, 0,0};
    CHECK(!Gunzip(badmethod, sizeof(badmethod), out));
    u8 reserved[12] = {0x1f, 0x8b, 8, 0xE0 /*reserved bits*/, 0,0,0,0, 0,0, 0,0};
    CHECK(!Gunzip(reserved, sizeof(reserved), out));
}

TEST(inflate_malformed, gzip_truncated_in_optional_fields) {
    // FNAME flag set (0x08) but the name string is never NUL-terminated and the
    // buffer ends mid-name. Must not over-read.
    u8 hdr[] = {0x1f, 0x8b, 8, 0x08, 0,0,0,0, 0,0, 'n','a','m','e'};
    std::vector<u8> out;
    CHECK(!Gunzip(hdr, sizeof(hdr), out));
    // FEXTRA with a declared length far past the buffer end.
    u8 hx[] = {0x1f, 0x8b, 8, 0x04, 0,0,0,0, 0,0, 0xFF, 0xFF /*len=65535*/, 1,2,3};
    CHECK(!Gunzip(hx, sizeof(hx), out));
}

TEST(inflate_malformed, gzip_header_ok_but_no_trailer) {
    // Valid header, but fewer than 8 bytes remain for CRC32+ISIZE trailer.
    u8 hdr[] = {0x1f, 0x8b, 8, 0, 0,0,0,0, 0,0, 0x01, 0x02, 0x03};
    std::vector<u8> out;
    CHECK(!Gunzip(hdr, sizeof(hdr), out));   // payload < 8
}

TEST(inflate_malformed, gzip_valid_roundtrip_control) {
    std::vector<u8> out;
    CHECK(Gunzip(k_short_gzip, k_short_gzip_len, out));
    CHECK_EQ(out.size(), (size_t)k_short_plain_len);
}

// ---------------------------------------------------------------------------
// Adler-32 / CRC on empty and degenerate inputs (no over-read on len 0).
// ---------------------------------------------------------------------------
TEST(inflate_malformed, adler_empty_and_null) {
    // empty buffer keeps the seed
    CHECK_EQ(Adler32(1, (const u8*)"", 0), 1u);
    CHECK_EQ(Adler32(0, (const u8*)"", 0), 0u);
    // a non-null pointer with len 0 must not read the byte
    u8 dummy = 0xAB;
    CHECK_EQ(Adler32(1, &dummy, 0), 1u);
    // null pointer resets to 1 regardless of len (faithful to the binary)
    CHECK_EQ(Adler32(9999, nullptr, 12345), 1u);
}

TEST(inflate_malformed, crc_empty_and_null) {
    // CRC of empty input == initial value passed through (~~crc).
    CHECK_EQ(CrcCompute(0, (const u8*)"", 0), 0u);
    CHECK_EQ(CrcCompute(0xFFFFFFFFu, (const u8*)"", 0), 0xFFFFFFFFu);
    // non-null pointer, len 0: no read of the byte.
    u8 dummy = 0xCD;
    CHECK_EQ(CrcCompute(0, &dummy, 0), 0u);
    // null data -> returns 0 (the binary's early-out).
    CHECK_EQ(CrcCompute(0, nullptr, 100), 0u);
    // CRC-16 over empty input keeps the seed.
    CHECK_EQ(Crc16Update(0, (const u8*)"", 0), (guild::u16)0);
}
