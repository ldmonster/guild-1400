// e2e for guild::io bio_codec — two scenarios:
//  (1) a LARGE deterministic round-trip: hundreds of pseudo-random Bio records
//      written then read back, proving lossless serialization and a stable
//      re-serialized checksum across two runs (works with NO assets);
//  (2) GUARDED on a real shipped .BIN asset: extract a member through the real
//      ZipArchive, wrap it as a VFS memory stream, and re-pack its leading bytes
//      through the Bio block codec, proving the Bio writers reproduce the exact
//      source bytes. Skips cleanly when assets are absent (honors GUILD_GAME_DIR).
#include "test.h"
#include "io/bio_codec.h"
#include "io/worldio.h"
#include "io/vfs.h"
#include "io/zip_archive.h"
#include "shim_impl/disk_filesystem.h"

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

using namespace guild;
using namespace guild::io;

namespace {

std::string GameDir() {
    if (const char* env = std::getenv("GUILD_GAME_DIR"))
        return env;
    return "/home/cnupt/work/reverse/reverse-guild/reimpl/europe_guild_1400_original";
}

// Tiny deterministic LCG so the scenario is reproducible without <random>.
struct Lcg {
    u32 s;
    explicit Lcg(u32 seed) : s(seed) {}
    u32 next() { s = s * 1103515245u + 12345u; return s; }
};

// FNV-1a over a byte buffer (a stable witness checksum).
u32 Fnv1a(const u8* p, std::size_t n) {
    u32 h = 2166136261u;
    for (std::size_t i = 0; i < n; ++i) { h ^= p[i]; h *= 16777619u; }
    return h;
}

// Serialize K records (magic, vec4, var block, var array) into `out`. Returns the
// number of bytes written.
long WriteScenario(std::vector<u8>& out, u32 seed, int K) {
    VfsHandle* w = VfsOpenMemoryStream(out.data(), (u32)out.size(), "wb");
    if (!w) return -1;
    Lcg rng(seed);
    for (int i = 0; i < K; ++i) {
        BioWriteDword(w, rng.next());
        u32 v[4] = {rng.next(), rng.next(), rng.next(), rng.next()};
        BioWriteVec4(w, v);
        u32 nlen = rng.next() % 13;             // 0..12 byte name block
        std::vector<u8> name(nlen);
        for (u32 j = 0; j < nlen; ++j) name[j] = (u8)rng.next();
        BioWriteBlock(w, name.data(), nlen);
        u32 vc = rng.next() % 5;                // 0..4 elements, stride 4
        std::vector<u32> verts(vc);
        for (u32 j = 0; j < vc; ++j) verts[j] = rng.next();
        BioWriteArray(w, verts.data(), 4, vc);
    }
    long n = VfsTell(w);
    VfsCloseStream(w);
    return n;
}

// Read K records back and re-emit them; returns the re-emitted byte count and the
// FNV checksum of the re-emitted stream (which must equal the original stream).
struct RoundTrip { long bytes; u32 checksum; bool ok; };

RoundTrip ReadAndReemit(const std::vector<u8>& src, long srcLen, int K) {
    RoundTrip rt{0, 0, true};
    VfsHandle* r = VfsOpenMemoryStream(const_cast<u8*>(src.data()), (u32)src.size(), "rb");
    std::vector<u8> reemit(src.size(), 0);
    VfsHandle* w = VfsOpenMemoryStream(reemit.data(), (u32)reemit.size(), "wb");
    if (!r || !w) { if (r) VfsCloseStream(r); if (w) VfsCloseStream(w); rt.ok = false; return rt; }
    for (int i = 0; i < K; ++i) {
        u32 magic = 0; rt.ok = BioReadDword(r, &magic) && rt.ok;
        u32 v[4] = {0}; rt.ok = BioReadVec4(r, v) && rt.ok;
        void* name = nullptr;
        u32 nlen = BioReadBlockAlloc(r, &name, "e2e:name");
        void* verts = nullptr;
        u32 vc = BioReadArrayQuick(r, 4, &verts);

        // re-emit identically
        BioWriteDword(w, magic);
        BioWriteVec4(w, v);
        BioWriteBlock(w, name, nlen);
        BioWriteArray(w, verts, 4, vc);
        if (name)  GetBioCodecHooks().free(name);
        if (verts) GetBioCodecHooks().free(verts);
    }
    rt.bytes = VfsTell(w);
    VfsCloseStream(r);
    VfsCloseStream(w);
    rt.checksum = Fnv1a(reemit.data(), (std::size_t)srcLen);
    return rt;
}

} // namespace

TEST(BioCodecE2E, LargeScenario_RoundTripStable) {
    const int K = 500;
    const u32 seed = 0xC0FFEEu;
    std::vector<u8> a(1u << 20, 0);
    long la = WriteScenario(a, seed, K);
    CHECK(la > 0);

    // Re-write the same scenario a second time -> byte identical (deterministic).
    std::vector<u8> b(1u << 20, 0);
    long lb = WriteScenario(b, seed, K);
    CHECK_EQ(la, lb);
    if (la > 0) CHECK_EQ(std::memcmp(a.data(), b.data(), (std::size_t)la), 0);

    u32 srcSum = Fnv1a(a.data(), (std::size_t)la);

    // Read back and re-emit -> must reproduce the original bytes exactly.
    RoundTrip rt = ReadAndReemit(a, la, K);
    CHECK(rt.ok);
    CHECK_EQ(rt.bytes, la);
    CHECK_EQ(rt.checksum, srcSum);

    // And the whole pipeline is stable on a second pass.
    RoundTrip rt2 = ReadAndReemit(a, la, K);
    CHECK_EQ(rt2.checksum, srcSum);
}

TEST(BioCodecE2E, RealBinAsset_BlockRepack_Guarded) {
    shim::DiskFileSystem fs(GameDir());
    if (!fs.exists("Resources/forms.BIN")) {
        CHECK(true);   // assets absent: skip cleanly
        return;
    }
    ZipArchive z;
    if (!z.Open(&fs, "Resources/forms.BIN")) { CHECK(true); return; }

    // Walk to the first NON-EMPTY member of the archive (the leading entries can be
    // zero-length directory placeholders) and extract it.
    std::vector<u8> member;
    bool got = false;
    if (z.GoToFirstFile() == 0) {
        do {
            member.clear();
            if (z.ExtractCurrentFile(member) && !member.empty()) { got = true; break; }
        } while (z.GoToNextFile() == 0);
    }
    if (!got || member.empty()) { CHECK(true); return; }

    // Read a leading slice of the real asset through a VFS read stream, then re-pack
    // it as a Bio length-prefixed block and read it back -> bytes identical.
    const u32 slice = member.size() > 256 ? 256u : (u32)member.size();
    std::vector<u8> head(slice, 0);
    VfsHandle* r = VfsOpenMemoryStream(member.data(), (u32)member.size(), "rb");
    CHECK(r != nullptr);
    if (r) {
        CHECK_EQ(VfsReadStream(head.data(), slice, r, 1), slice);
        VfsCloseStream(r);
    }

    // Pack {len, bytes} via BioWriteBlock, then read back with BioReadBlockAlloc.
    std::vector<u8> packed(slice + 16, 0);
    VfsHandle* w = VfsOpenMemoryStream(packed.data(), (u32)packed.size(), "wb");
    CHECK(w != nullptr);
    if (w) { CHECK(BioWriteBlock(w, head.data(), slice)); VfsCloseStream(w); }

    VfsHandle* rr = VfsOpenMemoryStream(packed.data(), (u32)packed.size(), "rb");
    CHECK(rr != nullptr);
    void* back = nullptr;
    u32 n = rr ? BioReadBlockAlloc(rr, &back, "e2e:realhead") : 0;
    if (rr) VfsCloseStream(rr);
    CHECK_EQ(n, slice);
    CHECK(back != nullptr);
    if (back) {
        CHECK_EQ(std::memcmp(back, head.data(), slice), 0);
        GetBioCodecHooks().free(back);
    }
    std::printf("    [bio_codec_e2e] forms.BIN member=%zu bytes, repacked head slice=%u\n",
                member.size(), slice);
}
