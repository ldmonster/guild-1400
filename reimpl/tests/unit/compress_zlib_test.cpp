#include "test.h"
#include "compress/inflate.h"
#include "compress/zlib.h"
#include "compress/gzip.h"
#include <vector>
#include <cstring>

using namespace guild::compress;
using guild::u8;

#include "compress_zlib_vectors.inc"

namespace {

std::vector<u8> Vec(const unsigned char* p, unsigned long n) {
    return std::vector<u8>(p, p + n);
}

bool Eq(const std::vector<u8>& a, const unsigned char* p, unsigned long n) {
    if (a.size() != n) return false;
    return n == 0 || std::memcmp(a.data(), p, n) == 0;
}

} // namespace

// --- Adler-32 (VIBE_Zlib_Adler32 @0x5ffaf0) ---------------------------------
TEST(zlib, adler32_known) {
    CHECK_EQ(Adler32(1, (const u8*)"123456789", 9), (guild::u32)k_adler_123456789);
    CHECK_EQ(Adler32(1, (const u8*)"Hello, world!", 13), (guild::u32)k_adler_hello);
    // empty input keeps the seed
    CHECK_EQ(Adler32(1, (const u8*)"", 0), 1u);
    // null data resets to 1 (the original returns 1)
    CHECK_EQ(Adler32(12345, nullptr, 7), 1u);
    // chaining across a split equals one-shot
    guild::u32 a = Adler32(1, (const u8*)"12345", 5);
    a = Adler32(a, (const u8*)"6789", 4);
    CHECK_EQ(a, (guild::u32)k_adler_123456789);
    // a buffer larger than NMAX (5552) exercises the block modulo
    std::vector<u8> big(20000);
    for (size_t i = 0; i < big.size(); ++i) big[i] = (u8)(i * 7 + 3);
    guild::u32 ref = Adler32(1, big.data(), (guild::u32)big.size());
    // split in three pieces -> same
    guild::u32 chk = Adler32(1, big.data(), 6000);
    chk = Adler32(chk, big.data() + 6000, 6000);
    chk = Adler32(chk, big.data() + 12000, (guild::u32)big.size() - 12000);
    CHECK_EQ(ref, chk);
}

// --- Inflate zlib framing ----------------------------------------------------
TEST(zlib, inflate_empty) {
    std::vector<u8> out;
    CHECK(Inflate(k_empty_zlib, k_empty_zlib_len, out));
    CHECK(Eq(out, k_empty_plain, k_empty_plain_len));
}

TEST(zlib, inflate_short_string) {
    std::vector<u8> out;
    CHECK(Inflate(k_short_zlib, k_short_zlib_len, out));
    CHECK(Eq(out, k_short_plain, k_short_plain_len));
}

TEST(zlib, inflate_rle_multikb) {
    std::vector<u8> out;
    CHECK(Inflate(k_rle_zlib, k_rle_zlib_len, out));
    CHECK_EQ(out.size(), (size_t)k_rle_plain_len);
    CHECK(Eq(out, k_rle_plain, k_rle_plain_len));
}

TEST(zlib, inflate_random_incompressible) {
    std::vector<u8> out;
    CHECK(Inflate(k_rnd_zlib, k_rnd_zlib_len, out));
    CHECK_EQ(out.size(), (size_t)k_rnd_plain_len);
    CHECK(Eq(out, k_rnd_plain, k_rnd_plain_len));
}

// --- Raw deflate framing (windowBits = -15) ----------------------------------
TEST(zlib, inflate_raw_deflate) {
    std::vector<u8> out;
    CHECK(InflateRaw(k_short_raw, k_short_raw_len, out));
    CHECK(Eq(out, k_short_plain, k_short_plain_len));
}

// --- gzip framing (windowBits = 31) ------------------------------------------
TEST(zlib, gunzip_short_string) {
    std::vector<u8> out;
    CHECK(Gunzip(k_short_gzip, k_short_gzip_len, out));
    CHECK(Eq(out, k_short_plain, k_short_plain_len));
}

// --- streaming in small chunks reaches stream-end ----------------------------
TEST(zlib, inflate_streaming_chunks) {
    Inflater z(15);
    std::vector<u8> out;
    int r = guild::compress::kZOk;
    for (unsigned long i = 0; i < k_rle_zlib_len; ++i) {
        r = z.Process(k_rle_zlib + i, 1, out, guild::compress::kZNoFlush);
        if (r == guild::compress::kZStreamEnd) break;
        CHECK(r == guild::compress::kZOk);
    }
    CHECK(r == guild::compress::kZStreamEnd);
    CHECK(Eq(out, k_rle_plain, k_rle_plain_len));
}

// --- corrupt stream is rejected ----------------------------------------------
TEST(zlib, inflate_rejects_bad_check) {
    std::vector<u8> bad = Vec(k_short_zlib, k_short_zlib_len);
    bad.back() ^= 0xFF; // corrupt the adler trailer
    std::vector<u8> out;
    CHECK(!Inflate(bad.data(), bad.size(), out));
}

TEST(zlib, inflate_rejects_bad_header) {
    std::vector<u8> bad = Vec(k_short_zlib, k_short_zlib_len);
    bad[0] = 0x77; // not a deflate method / bad header check
    std::vector<u8> out;
    CHECK(!Inflate(bad.data(), bad.size(), out));
}
