#include "test.h"
#include "compress/inflate.h"
#include "compress/md5.h"
#include <vector>
#include <string>
#include <cstdio>

using namespace guild::compress;
using guild::u8;

#include "compress_inflate_window_vectors.inc"

namespace {

// Hex MD5 of a buffer using the project's reconstructed MD5 (oracle for content).
std::string Md5Hex(const std::vector<u8>& v) {
    Md5Context ctx;
    Md5Init(&ctx);
    Md5Update(&ctx, v.empty() ? (const u8*)"" : v.data(), (guild::u32)v.size());
    u8 d[16];
    Md5Final(d, &ctx);
    char buf[33];
    for (int i = 0; i < 16; i++)
        std::snprintf(buf + i * 2, 3, "%02x", d[i]);
    return std::string(buf, 32);
}

} // namespace

// Regression: 64KB-aligned periodic streams must inflate to the exact length and
// content. The vendored 1.1.4 inflate had a window-wrap/flush bug that inflated
// e.g. a 65536-byte stream to 98304 bytes. Each case checks BOTH length and a
// content MD5 against the value python's zlib produced for the same plaintext.
TEST(inflate_window, periodic_boundary_lengths) {
    for (const auto& c : kInflateWindowCases) {
        std::vector<u8> out;
        bool ok = Inflate(c.zlib, c.zlib_len, out);
        CHECK(ok);
        CHECK_EQ(out.size(), (size_t)c.plain_len);
        if (out.size() == (size_t)c.plain_len) {
            std::string got = Md5Hex(out);
            bool match = (got == std::string(c.md5));
            if (!match)
                std::printf("    inflate_window: len=%lu got_md5=%s want=%s\n",
                            c.plain_len, got.c_str(), c.md5);
            CHECK(match);
        }
    }
}
