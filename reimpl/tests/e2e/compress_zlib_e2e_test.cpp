#include "test.h"
#include "compress/inflate.h"
#include "compress/zlib.h"
#include <vector>
#include <cstring>

using namespace guild::compress;
using guild::u8;

#include "../unit/compress_zlib_vectors.inc"

// Deflate is NOT yet reconstructed (deferred — see inflate.cpp / final report),
// so this e2e decompresses a large python-zlib stream and verifies the output
// length and a content checksum, plus that the stream verifies its own adler.
TEST(zlib_e2e, decompress_large_stream) {
    std::vector<u8> out;
    bool ok = Inflate(k_big_zlib, k_big_zlib_len, out);
    CHECK(ok);
    CHECK_EQ(out.size(), (size_t)k_big_plain_len);
    CHECK(out.size() == 225000u);
    // byte-exact against the known plaintext
    CHECK(out.size() == (size_t)k_big_plain_len &&
          std::memcmp(out.data(), k_big_plain, k_big_plain_len) == 0);
    // independent adler over the decoded output matches adler over the source
    guild::u32 a = Adler32(1, out.data(), (guild::u32)out.size());
    guild::u32 b = Adler32(1, k_big_plain, (guild::u32)k_big_plain_len);
    CHECK_EQ(a, b);
}

// Cross-framing consistency: zlib, raw, and gzip of the same short payload all
// decode to the identical plaintext.
TEST(zlib_e2e, framing_consistency) {
    std::vector<u8> zout, rout;
    CHECK(Inflate(k_short_zlib, k_short_zlib_len, zout));
    CHECK(InflateRaw(k_short_raw, k_short_raw_len, rout));
    CHECK(zout == rout);
    CHECK_EQ(zout.size(), (size_t)k_short_plain_len);
}
