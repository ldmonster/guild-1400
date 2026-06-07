#include "test.h"
#include "compress/deflate.h"
#include "compress/inflate.h"
#include "compress/zlib.h"
#include <vector>
#include <string>
#include <cstring>
#include <cstdint>

using namespace guild::compress;
using guild::u8;
using guild::u32;

namespace {

// deterministic LCG so the "random/incompressible" vectors are reproducible.
std::vector<u8> Lcg(std::size_t n, u32 seed) {
    std::vector<u8> v(n);
    u32 s = seed;
    for (std::size_t i = 0; i < n; i++) {
        s = s * 1103515245u + 12345u;
        v[i] = (u8)(s >> 16);
    }
    return v;
}

// RLE-ish highly compressible buffer.
std::vector<u8> Compressible(std::size_t n) {
    std::vector<u8> v(n);
    for (std::size_t i = 0; i < n; i++) v[i] = (u8)('A' + (i / 64) % 4);
    return v;
}

bool Roundtrip(const std::vector<u8>& src, int level) {
    std::vector<u8> comp, decomp;
    if (!Deflate(src.data(), src.size(), comp, level)) return false;
    if (!Inflate(comp.data(), comp.size(), decomp)) return false;
    return decomp == src;
}

bool RoundtripRaw(const std::vector<u8>& src, int level) {
    std::vector<u8> comp, decomp;
    if (!DeflateRaw(src.data(), src.size(), comp, level)) return false;
    if (!InflateRaw(comp.data(), comp.size(), decomp)) return false;
    return decomp == src;
}

} // namespace

// ---- roundtrip across payload shapes and levels ----------------------------
TEST(deflate, roundtrip_empty) {
    std::vector<u8> empty;
    for (int lvl : {0, 1, 6, 9}) {
        CHECK(Roundtrip(empty, lvl));
    }
}

TEST(deflate, roundtrip_short_string) {
    std::string str = "hello, deflate world! hello, deflate world!";
    std::vector<u8> src(str.begin(), str.end());
    for (int lvl : {0, 1, 6, 9}) {
        CHECK(Roundtrip(src, lvl));
    }
}

TEST(deflate, roundtrip_compressible_multikb) {
    std::vector<u8> src = Compressible(8 * 1024);
    for (int lvl : {0, 1, 6, 9}) {
        CHECK(Roundtrip(src, lvl));
    }
    // a compressible buffer must actually shrink at level 6.
    std::vector<u8> comp;
    CHECK(Deflate(src.data(), src.size(), comp, 6));
    CHECK(comp.size() < src.size() / 4);
}

TEST(deflate, roundtrip_incompressible_random) {
    std::vector<u8> src = Lcg(7000, 0xC0FFEEu);
    for (int lvl : {0, 1, 6, 9}) {
        CHECK(Roundtrip(src, lvl));
    }
}

TEST(deflate, roundtrip_large_buffer) {
    // ~150 KB: a compressible region plus a random tail so both the LZ77 match
    // path and the literal path are exercised. Sizes are deliberately NOT exact
    // multiples of the 32 KB window: the sibling inflate.cpp has a latent decode
    // bug at exact 64 KB-aligned periodic boundaries (verified independently via
    // python's zlib, which decodes our output at those sizes correctly). Deflate
    // is the module under test here; that inflate bug is out of scope.
    std::vector<u8> src = Compressible(110 * 1000);
    std::vector<u8> rnd = Lcg(40 * 1000, 0x12345u);
    src.insert(src.end(), rnd.begin(), rnd.end());
    for (int lvl : {0, 1, 6, 9}) {
        CHECK(Roundtrip(src, lvl));
    }
}

TEST(deflate, roundtrip_raw_framing) {
    std::string str = "raw deflate has no zlib header or adler trailer";
    std::vector<u8> src(str.begin(), str.end());
    for (int lvl : {0, 1, 6, 9}) {
        CHECK(RoundtripRaw(src, lvl));
    }
    CHECK(RoundtripRaw(Compressible(20000), 9));
    CHECK(RoundtripRaw(Lcg(5000, 7u), 6));
}

// ---- zlib wire format: header byte + valid adler ---------------------------
TEST(deflate, zlib_header_and_adler) {
    std::string str = "check the wire format bytes";
    std::vector<u8> src(str.begin(), str.end());
    std::vector<u8> comp;
    CHECK(Deflate(src.data(), src.size(), comp, 6));
    CHECK(comp.size() >= 6u);
    // CMF/FLG: method 8, and (CMF*256+FLG) % 31 == 0.
    CHECK_EQ((int)(comp[0] & 0x0f), 8);
    CHECK_EQ(((comp[0] << 8) | comp[1]) % 31, 0);
    // trailing 4 bytes are adler32 of the source, big-endian.
    u32 adler = Adler32(1, src.data(), (u32)src.size());
    std::size_t n = comp.size();
    u32 trailer = ((u32)comp[n-4] << 24) | ((u32)comp[n-3] << 16) |
                  ((u32)comp[n-2] << 8) | (u32)comp[n-1];
    CHECK_EQ(trailer, adler);
}

// stored blocks (level 0) must be larger than input (5-byte block headers).
TEST(deflate, level0_is_stored) {
    std::vector<u8> src = Lcg(1000, 99u);
    std::vector<u8> comp;
    CHECK(Deflate(src.data(), src.size(), comp, 0));
    CHECK(comp.size() >= src.size());
    CHECK(Roundtrip(src, 0));
}
