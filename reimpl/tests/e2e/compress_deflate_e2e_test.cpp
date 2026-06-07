#include "test.h"
#include "compress/deflate.h"
#include "compress/inflate.h"
#include "compress/zlib.h"
#include "compress/md5.h"
#include <vector>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>

using namespace guild::compress;
using guild::u8;
using guild::u32;

namespace {

std::vector<u8> Md5Hex(const std::vector<u8>& data) {
    Md5Context ctx;
    Md5Init(&ctx);
    Md5Update(&ctx, data.data(), (u32)data.size());
    u8 digest[16];
    Md5Final(digest, &ctx);
    return std::vector<u8>(digest, digest + 16);
}

// Build a ~200KB semi-structured buffer: repeated text mixed with a varying tail
// so both the LZ77 match path and the literal path are exercised heavily.
std::vector<u8> Make200K() {
    std::vector<u8> v;
    const char* line = "The quick brown fox jumps over the lazy dog. 0123456789. ";
    u32 s = 0xABCDEF01u;
    while (v.size() < 200u * 1024u) {
        for (const char* p = line; *p; ++p) v.push_back((u8)*p);
        // occasional pseudo-random byte to avoid a purely periodic stream.
        s = s * 1103515245u + 12345u;
        if ((s >> 28) == 0) v.push_back((u8)(s >> 16));
    }
    return v;
}

} // namespace

// ---- whole-flow roundtrip at level 6, md5-verified -------------------------
TEST(deflate_e2e, roundtrip_200k_level6_md5) {
    std::vector<u8> src = Make200K();
    CHECK(src.size() >= 200u * 1024u);

    std::vector<u8> comp;
    CHECK(Deflate(src.data(), src.size(), comp, 6));
    CHECK(comp.size() < src.size()); // must actually compress

    std::vector<u8> decomp;
    CHECK(Inflate(comp.data(), comp.size(), decomp));
    CHECK_EQ(decomp.size(), src.size());

    std::vector<u8> a = Md5Hex(src);
    std::vector<u8> b = Md5Hex(decomp);
    CHECK(a == b);
}

// ---- our deflate output is decodable by python's zlib.decompress -----------
// Proves wire-format correctness against a fully independent implementation.
TEST(deflate_e2e, python_zlib_decodes_our_output) {
    std::vector<u8> src = Make200K();
    std::vector<u8> comp;
    CHECK(Deflate(src.data(), src.size(), comp, 6));

    // write compressed bytes to a temp file.
    char comp_path[] = "/tmp/guild_deflate_e2e_compXXXXXX";
    char plain_path[] = "/tmp/guild_deflate_e2e_plainXXXXXX";
    int cfd = mkstemp(comp_path);
    int pfd = mkstemp(plain_path);
    CHECK(cfd >= 0 && pfd >= 0);
    if (cfd < 0 || pfd < 0) return;
    FILE* cf = fdopen(cfd, "wb");
    FILE* pf = fdopen(pfd, "wb");
    std::fwrite(comp.data(), 1, comp.size(), cf);
    std::fwrite(src.data(), 1, src.size(), pf);
    std::fclose(cf);
    std::fclose(pf);

    // python decompresses our stream and compares to the original plaintext.
    char cmd[1024];
    std::snprintf(cmd, sizeof(cmd),
        "python3 -c \""
        "import zlib,sys;"
        "d=zlib.decompress(open('%s','rb').read());"
        "p=open('%s','rb').read();"
        "sys.exit(0 if d==p else 1)\"",
        comp_path, plain_path);
    int rc = std::system(cmd);
    CHECK_EQ(rc, 0);

    std::remove(comp_path);
    std::remove(plain_path);
}

// ---- raw stream also decodes via python (wbits=-15) ------------------------
TEST(deflate_e2e, python_zlib_decodes_raw) {
    std::vector<u8> src = Make200K();
    std::vector<u8> comp;
    CHECK(DeflateRaw(src.data(), src.size(), comp, 9));

    char comp_path[] = "/tmp/guild_deflate_e2e_rawXXXXXX";
    char plain_path[] = "/tmp/guild_deflate_e2e_rplXXXXXX";
    int cfd = mkstemp(comp_path);
    int pfd = mkstemp(plain_path);
    CHECK(cfd >= 0 && pfd >= 0);
    if (cfd < 0 || pfd < 0) return;
    FILE* cf = fdopen(cfd, "wb");
    FILE* pf = fdopen(pfd, "wb");
    std::fwrite(comp.data(), 1, comp.size(), cf);
    std::fwrite(src.data(), 1, src.size(), pf);
    std::fclose(cf);
    std::fclose(pf);

    char cmd[1024];
    std::snprintf(cmd, sizeof(cmd),
        "python3 -c \""
        "import zlib,sys;"
        "d=zlib.decompress(open('%s','rb').read(),-15);"
        "p=open('%s','rb').read();"
        "sys.exit(0 if d==p else 1)\"",
        comp_path, plain_path);
    int rc = std::system(cmd);
    CHECK_EQ(rc, 0);

    std::remove(comp_path);
    std::remove(plain_path);
}
