#include "test.h"
#include "compress/md5.h"
#include "compress/crc.h"

#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

using namespace guild;

namespace {

std::vector<u8> MakeBuffer(std::size_t n) {
    std::vector<u8> buf(n);
    for (std::size_t i = 0; i < n; ++i)
        buf[i] = static_cast<u8>((i * 131 + 7) & 0xFF);
    return buf;
}

std::string ToHex(const u8* d, std::size_t n) {
    static const char* hexd = "0123456789abcdef";
    std::string out;
    for (std::size_t i = 0; i < n; ++i) {
        out += hexd[d[i] >> 4];
        out += hexd[d[i] & 0xF];
    }
    return out;
}

std::string Md5OneShot(const std::vector<u8>& buf) {
    compress::Md5Context ctx;
    compress::Md5Init(&ctx);
    compress::Md5Update(&ctx, buf.data(), static_cast<u32>(buf.size()));
    u8 digest[16];
    compress::Md5Final(digest, &ctx);
    return ToHex(digest, 16);
}

std::string Md5Chunked(const std::vector<u8>& buf, std::size_t chunk) {
    compress::Md5Context ctx;
    compress::Md5Init(&ctx);
    for (std::size_t off = 0; off < buf.size(); off += chunk) {
        std::size_t n = std::min(chunk, buf.size() - off);
        compress::Md5Update(&ctx, buf.data() + off, static_cast<u32>(n));
    }
    u8 digest[16];
    compress::Md5Final(digest, &ctx);
    return ToHex(digest, 16);
}

} // namespace

// MD5: incremental (init/update-in-chunks/final) must equal the one-shot digest,
// across several awkward chunk sizes that cross 64-byte block boundaries.
TEST(HashE2E, Md5IncrementalMatchesOneShot) {
    auto buf = MakeBuffer(5000);
    std::string one = Md5OneShot(buf);
    CHECK(one == "a2fe5d55a63e285e96171a15c0cd1630"); // independent python golden
    for (std::size_t chunk : {1u, 3u, 7u, 31u, 63u, 64u, 65u, 100u, 1000u, 4096u}) {
        CHECK(Md5Chunked(buf, chunk) == one);
    }
}

// CRC-32: chaining via the running value (chunked) must equal the one-shot CRC.
// CrcCompute applies ~ on entry and exit, so feeding back the previous return
// value cancels the complements between chunks.
TEST(HashE2E, Crc32IncrementalMatchesOneShot) {
    auto buf = MakeBuffer(5000);
    u32 one = compress::CrcCompute(0, buf.data(), static_cast<u32>(buf.size()));
    CHECK_EQ(one, 0x09D9A1FBu); // independent python golden (binascii.crc32)

    u32 running = 0;
    std::size_t chunk = 333;
    for (std::size_t off = 0; off < buf.size(); off += chunk) {
        std::size_t n = std::min(chunk, buf.size() - off);
        running = compress::CrcCompute(running, buf.data() + off,
                                       static_cast<u32>(n));
    }
    CHECK_EQ(running, one);
}

// CRC-16: chunked update threads the running CRC directly (no complement),
// so it must equal the one-shot value with no special handling.
TEST(HashE2E, Crc16IncrementalMatchesOneShot) {
    auto buf = MakeBuffer(5000);
    u16 one = compress::Crc16Update(0, buf.data(), static_cast<int>(buf.size()));

    u16 running = 0;
    std::size_t chunk = 257;
    for (std::size_t off = 0; off < buf.size(); off += chunk) {
        std::size_t n = std::min(chunk, buf.size() - off);
        running = compress::Crc16Update(running, buf.data() + off,
                                        static_cast<int>(n));
    }
    CHECK_EQ(running, one);
}

// Stability: hashing the same buffer twice yields identical results.
TEST(HashE2E, StableAcrossRuns) {
    auto buf = MakeBuffer(4321);
    CHECK(Md5OneShot(buf) == Md5OneShot(buf));
    u32 c1 = compress::CrcCompute(0, buf.data(), static_cast<u32>(buf.size()));
    u32 c2 = compress::CrcCompute(0, buf.data(), static_cast<u32>(buf.size()));
    CHECK_EQ(c1, c2);
    u16 d1 = compress::Crc16Update(0, buf.data(), static_cast<int>(buf.size()));
    u16 d2 = compress::Crc16Update(0, buf.data(), static_cast<int>(buf.size()));
    CHECK_EQ(d1, d2);
}
