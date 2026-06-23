// Unit tests for guild::io bio_codec — the compound VIBE_Bio_* serializers and the
// three pure zlib/zip leaves. Golden byte layouts are derived directly from the
// IDA decompilation (see bio_codec.h addresses): every value is little-endian raw,
// vectors are N back-to-back dwords, a block is {u32 len, len bytes}, an array is
// {u32 count, u32 stride, count*stride bytes}.
#include "test.h"
#include "io/bio_codec.h"
#include "io/vfs.h"

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <vector>

using namespace guild;
using namespace guild::io;

namespace {

// Read four LE dwords out of a byte buffer at offset.
u32 LE32(const std::vector<u8>& b, std::size_t off) {
    return (u32)b[off] | ((u32)b[off + 1] << 8) | ((u32)b[off + 2] << 16) |
           ((u32)b[off + 3] << 24);
}

} // namespace

TEST(BioCodec, WriteVec3_GoldenBytes) {
    std::vector<u8> out(64, 0xAA);
    VfsHandle* w = VfsOpenMemoryStream(out.data(), (u32)out.size(), "wb");
    CHECK(w != nullptr);
    if (!w) return;
    const u32 v[3] = {0x11223344u, 0xDEADBEEFu, 0x00000007u};
    CHECK(BioWriteVec3(w, v));
    CHECK_EQ(VfsTell(w), (long)12);
    VfsCloseStream(w);
    CHECK_EQ(LE32(out, 0), 0x11223344u);
    CHECK_EQ(LE32(out, 4), 0xDEADBEEFu);
    CHECK_EQ(LE32(out, 8), 0x00000007u);
    CHECK_EQ(out[12], (u8)0xAA);   // nothing past 12 bytes touched
}

TEST(BioCodec, WriteVec4_GoldenBytes) {
    std::vector<u8> out(64, 0);
    VfsHandle* w = VfsOpenMemoryStream(out.data(), (u32)out.size(), "wb");
    CHECK(w != nullptr);
    if (!w) return;
    const u32 v[4] = {1u, 2u, 3u, 0xFFFFFFFFu};
    CHECK(BioWriteVec4(w, v));
    CHECK_EQ(VfsTell(w), (long)16);
    VfsCloseStream(w);
    CHECK_EQ(LE32(out, 0), 1u);
    CHECK_EQ(LE32(out, 4), 2u);
    CHECK_EQ(LE32(out, 8), 3u);
    CHECK_EQ(LE32(out, 12), 0xFFFFFFFFu);
}

TEST(BioCodec, ReadVec4_RoundTrip) {
    std::vector<u8> out(32, 0);
    const u32 v[4] = {0xCAFEBABEu, 0x0BADF00Du, 42u, 0u};
    VfsHandle* w = VfsOpenMemoryStream(out.data(), (u32)out.size(), "wb");
    if (w) { CHECK(BioWriteVec4(w, v)); VfsCloseStream(w); }
    VfsHandle* r = VfsOpenMemoryStream(out.data(), (u32)out.size(), "rb");
    CHECK(r != nullptr);
    if (!r) return;
    u32 got[4] = {0};
    CHECK(BioReadVec4(r, got));
    VfsCloseStream(r);
    CHECK_EQ(got[0], 0xCAFEBABEu);
    CHECK_EQ(got[1], 0x0BADF00Du);
    CHECK_EQ(got[2], 42u);
    CHECK_EQ(got[3], 0u);
}

TEST(BioCodec, WriteBlock_GoldenBytes) {
    const char payload[] = "GUILD!";   // 6 chars (no NUL written)
    std::vector<u8> out(64, 0);
    VfsHandle* w = VfsOpenMemoryStream(out.data(), (u32)out.size(), "wb");
    CHECK(w != nullptr);
    if (!w) return;
    CHECK(BioWriteBlock(w, payload, 6));
    CHECK_EQ(VfsTell(w), (long)(4 + 6));
    VfsCloseStream(w);
    CHECK_EQ(LE32(out, 0), 6u);          // length prefix
    CHECK_EQ(std::memcmp(out.data() + 4, payload, 6), 0);
}

TEST(BioCodec, WriteBlock_ZeroLength) {
    std::vector<u8> out(16, 0xEE);
    VfsHandle* w = VfsOpenMemoryStream(out.data(), (u32)out.size(), "wb");
    if (!w) { CHECK(false); return; }
    CHECK(BioWriteBlock(w, nullptr, 0));   // null data is OK when length==0
    CHECK_EQ(VfsTell(w), (long)4);
    VfsCloseStream(w);
    CHECK_EQ(LE32(out, 0), 0u);
}

TEST(BioCodec, ReadBlockAlloc_RoundTrip) {
    const u8 payload[5] = {9, 8, 7, 6, 5};
    std::vector<u8> out(64, 0);
    VfsHandle* w = VfsOpenMemoryStream(out.data(), (u32)out.size(), "wb");
    if (w) { CHECK(BioWriteBlock(w, payload, 5)); VfsCloseStream(w); }

    VfsHandle* r = VfsOpenMemoryStream(out.data(), (u32)out.size(), "rb");
    CHECK(r != nullptr);
    if (!r) return;
    void* got = nullptr;
    u32 n = BioReadBlockAlloc(r, &got, "test:block");
    VfsCloseStream(r);
    CHECK_EQ(n, 5u);
    CHECK(got != nullptr);
    if (got) {
        CHECK_EQ(std::memcmp(got, payload, 5), 0);
        GetBioCodecHooks().free(got);
    }
}

TEST(BioCodec, ReadBlockQuick_RoundTrip) {
    const u8 payload[3] = {0xAB, 0xCD, 0xEF};
    std::vector<u8> out(32, 0);
    VfsHandle* w = VfsOpenMemoryStream(out.data(), (u32)out.size(), "wb");
    if (w) { CHECK(BioWriteBlock(w, payload, 3)); VfsCloseStream(w); }
    VfsHandle* r = VfsOpenMemoryStream(out.data(), (u32)out.size(), "rb");
    if (!r) { CHECK(false); return; }
    void* got = nullptr;
    u32 n = BioReadBlockQuick(r, &got);
    VfsCloseStream(r);
    CHECK_EQ(n, 3u);
    if (got) {
        CHECK_EQ(std::memcmp(got, payload, 3), 0);
        GetBioCodecHooks().free(got);
    }
}

TEST(BioCodec, WriteArray_GoldenBytes) {
    // 3 elements of stride 4 = 12 payload bytes.
    const u32 elems[3] = {0x01020304u, 0x05060708u, 0x090A0B0Cu};
    std::vector<u8> out(64, 0);
    VfsHandle* w = VfsOpenMemoryStream(out.data(), (u32)out.size(), "wb");
    CHECK(w != nullptr);
    if (!w) return;
    CHECK(BioWriteArray(w, elems, /*stride*/4, /*count*/3));
    CHECK_EQ(VfsTell(w), (long)(4 + 4 + 12));
    VfsCloseStream(w);
    CHECK_EQ(LE32(out, 0), 3u);   // count first
    CHECK_EQ(LE32(out, 4), 4u);   // stride second
    CHECK_EQ(LE32(out, 8), 0x01020304u);
    CHECK_EQ(LE32(out, 12), 0x05060708u);
    CHECK_EQ(LE32(out, 16), 0x090A0B0Cu);
}

TEST(BioCodec, ReadArrayDebug_MatchAndMismatch) {
    const u32 elems[2] = {0xAAAA5555u, 0x12345678u};
    std::vector<u8> out(64, 0);
    VfsHandle* w = VfsOpenMemoryStream(out.data(), (u32)out.size(), "wb");
    if (w) { CHECK(BioWriteArray(w, elems, 4, 2)); VfsCloseStream(w); }

    // Matching expected stride -> reads the array, returns count.
    VfsHandle* r = VfsOpenMemoryStream(out.data(), (u32)out.size(), "rb");
    if (!r) { CHECK(false); return; }
    void* got = nullptr;
    u32 n = BioReadArrayDebug(r, /*expectStride*/4, "test:arr", &got);
    VfsCloseStream(r);
    CHECK_EQ(n, 2u);
    CHECK(got != nullptr);
    if (got) { CHECK_EQ(std::memcmp(got, elems, 8), 0); GetBioCodecHooks().free(got); }

    // Mismatched stride -> skipped, null out, return 0.
    VfsHandle* r2 = VfsOpenMemoryStream(out.data(), (u32)out.size(), "rb");
    if (!r2) { CHECK(false); return; }
    void* got2 = (void*)0x1;
    u32 n2 = BioReadArrayDebug(r2, /*expectStride*/8, "test:arr", &got2);
    // After the skip the cursor should sit past count(4)+stride(4)+2*4(8) = 16.
    CHECK_EQ(VfsTell(r2), (long)16);
    VfsCloseStream(r2);
    CHECK_EQ(n2, 0u);
    CHECK(got2 == nullptr);
}

TEST(BioCodec, ReadArrayQuick_RoundTrip) {
    const u8 bytes[6] = {1, 2, 3, 4, 5, 6};
    std::vector<u8> out(64, 0);
    VfsHandle* w = VfsOpenMemoryStream(out.data(), (u32)out.size(), "wb");
    if (w) { CHECK(BioWriteArray(w, bytes, /*stride*/2, /*count*/3)); VfsCloseStream(w); }
    VfsHandle* r = VfsOpenMemoryStream(out.data(), (u32)out.size(), "rb");
    if (!r) { CHECK(false); return; }
    void* got = nullptr;
    u32 n = BioReadArrayQuick(r, /*expectStride*/2, &got);
    VfsCloseStream(r);
    CHECK_EQ(n, 3u);
    if (got) { CHECK_EQ(std::memcmp(got, bytes, 6), 0); GetBioCodecHooks().free(got); }
}

// --- pure leaves -----------------------------------------------------------

TEST(BioCodec, ZipTellCurrentFile_Pure) {
    // Model the unz_s struct: a byte block whose +124 slot points at a file-info
    // block whose +24 dword is the byte offset.
    std::vector<u8> fileInfo(64, 0);
    int off = 0x4321;
    std::memcpy(fileInfo.data() + 24, &off, sizeof(off));   // +24 unaligned dword
    std::vector<u8> unz(160, 0);
    void* fi = fileInfo.data();
    std::memcpy(unz.data() + 124, &fi, sizeof(fi));         // +124 unaligned ptr
    CHECK_EQ(ZipTellCurrentFile(unz.data()), 0x4321);

    // null base -> param error
    CHECK_EQ(ZipTellCurrentFile(nullptr), kUnzParamError);
    // null file-info slot -> param error
    std::vector<u8> unz2(160, 0);
    CHECK_EQ(ZipTellCurrentFile(unz2.data()), kUnzParamError);
}

TEST(BioCodec, InflateSyncPoint_Pure) {
    u8 s1 = 1, s0 = 0, s7 = 7;
    CHECK(InflateSyncPoint(&s1));
    CHECK(!InflateSyncPoint(&s0));
    CHECK(!InflateSyncPoint(&s7));
    CHECK(!InflateSyncPoint(nullptr));
}

TEST(BioCodec, InflateSetWindowDictionary_Pure) {
    // slot[10] = window write pointer; copy dict there; slots 12,13 = write+len.
    u8 window[64] = {0};
    void* slots[16] = {0};
    slots[10] = window;
    const u8 dict[4] = {0xDE, 0xAD, 0xBE, 0xEF};
    u32 r = InflateSetWindowDictionary(slots, dict, 4);
    CHECK_EQ(r, 4u);
    CHECK_EQ(std::memcmp(window, dict, 4), 0);
    CHECK(slots[12] == (void*)(window + 4));
    CHECK(slots[13] == (void*)(window + 4));
    // null window object -> returns length, no crash
    CHECK_EQ(InflateSetWindowDictionary(nullptr, dict, 4), 4u);
}

// Custom allocator hooks are observed.
TEST(BioCodec, AllocHooks_Observed) {
    static u32 lastSize; static const char* lastTag; static int allocs;
    lastSize = 0; lastTag = nullptr; allocs = 0;
    BioCodecHooks h;
    h.alloc = [](u32 sz, const char* tag) -> void* { lastSize = sz; lastTag = tag; allocs++; return std::malloc(sz ? sz : 1); };
    h.free  = [](void* p) { std::free(p); };
    SetBioCodecHooks(h);

    const u8 payload[7] = {1,2,3,4,5,6,7};
    std::vector<u8> out(32, 0);
    VfsHandle* w = VfsOpenMemoryStream(out.data(), (u32)out.size(), "wb");
    if (w) { BioWriteBlock(w, payload, 7); VfsCloseStream(w); }
    VfsHandle* r = VfsOpenMemoryStream(out.data(), (u32)out.size(), "rb");
    void* got = nullptr;
    if (r) { BioReadBlockAlloc(r, &got, "tag:custom"); VfsCloseStream(r); }
    CHECK_EQ(lastSize, 7u);
    CHECK_EQ(allocs, 1);
    CHECK(lastTag != nullptr && std::strcmp(lastTag, "tag:custom") == 0);
    if (got) h.free(got);

    SetBioCodecHooks(BioCodecHooks{});   // restore inert defaults
}
