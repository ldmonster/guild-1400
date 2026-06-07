#include "test.h"
#include "compress/md5.h"
#include "compress/crc.h"

#include <cstring>
#include <string>

using namespace guild;

namespace {

std::string Md5Hex(const std::string& s) {
    compress::Md5Context ctx;
    compress::Md5Init(&ctx);
    compress::Md5Update(&ctx, reinterpret_cast<const u8*>(s.data()),
                        static_cast<u32>(s.size()));
    u8 digest[16];
    compress::Md5Final(digest, &ctx);
    static const char* hexd = "0123456789abcdef";
    std::string out;
    for (u8 b : digest) {
        out += hexd[b >> 4];
        out += hexd[b & 0xF];
    }
    return out;
}

u32 Crc32(const std::string& s) {
    return compress::CrcCompute(0, reinterpret_cast<const u8*>(s.data()),
                                static_cast<u32>(s.size()));
}

u16 Crc16(const std::string& s) {
    return compress::Crc16Update(0, reinterpret_cast<const u8*>(s.data()),
                                 static_cast<int>(s.size()));
}

} // namespace

// --- MD5 golden vectors (RFC 1321 Appendix A.5) ---
TEST(Md5, RfcVectorEmpty) {
    CHECK(Md5Hex("") == "d41d8cd98f00b204e9800998ecf8427e");
}
TEST(Md5, RfcVectorAbc) {
    CHECK(Md5Hex("abc") == "900150983cd24fb0d6963f7d28e17f72");
}
TEST(Md5, RfcVectorMessageDigest) {
    CHECK(Md5Hex("message digest") == "f96b697d7cb7938d525a2f31aaf161d0");
}
TEST(Md5, RfcVectorAlphabet) {
    // md5("abcdefghijklmnopqrstuvwxyz")
    CHECK(Md5Hex("abcdefghijklmnopqrstuvwxyz") ==
          "c3fcd3d76192e4007dfb496cca67e13b");
}
TEST(Md5, LongBlockBoundary) {
    // 80 bytes forces a buffered carry across the 64-byte block boundary.
    std::string s(80, 'a');
    // md5 of 80 'a' chars, computed via python hashlib.
    CHECK(Md5Hex(s) == "b15af9cdabbaea0516866a33d8fd0f98");
}

// --- CRC-32: standard reflected poly 0xEDB88320 ---
TEST(Crc32, CheckValue) {
    CHECK_EQ(Crc32("123456789"), 0xCBF43926u);
}
TEST(Crc32, Empty) {
    CHECK_EQ(Crc32(""), 0u);
}
TEST(Crc32, Abc) {
    CHECK_EQ(Crc32("abc"), 0x352441C2u);
}
TEST(Crc32, NullDataReturnsZero) {
    CHECK_EQ(compress::CrcCompute(0, nullptr, 100), 0u);
}
TEST(Crc32, TableEntryOne) {
    CHECK_EQ(compress::CrcGetTable()[1], 0x77073096u);
}

// --- CRC-16: CRC-16/ARC (reflected 0x8005, init 0) ---
TEST(Crc16, CheckValue) {
    CHECK_EQ(Crc16("123456789"), static_cast<u16>(0xBB3D));
}
TEST(Crc16, Empty) {
    CHECK_EQ(Crc16(""), static_cast<u16>(0));
}
TEST(Crc16, SingleChar) {
    // crc16-arc("A") = 0x30C0 (computed independently in python)
    CHECK_EQ(Crc16("A"), static_cast<u16>(0x30C0));
}
