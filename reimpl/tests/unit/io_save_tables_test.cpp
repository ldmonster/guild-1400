#include "test.h"

#include "io/save_tables.h"
#include "io/save_person.h"
#include "io/save_building.h"
#include "io/save.h"
#include "io/vfs.h"
#include "io/file.h"
#include "shim/IFileSystem.h"

#include <cstdint>
#include <cstring>
#include <map>
#include <string>
#include <vector>

using namespace guild::io;
using guild::u8;
using guild::u32;

// ---------------------------------------------------------------------------
// Writable in-memory IFileSystem (growable buffers persisting across open/close).
namespace {

class RwFile : public guild::shim::IFile {
public:
    RwFile(std::vector<u8>* store, bool writing) : store_(store), writing_(writing) {
        if (writing_) store_->clear();
    }
    std::size_t read(void* dst, std::size_t n) override {
        if (writing_) return 0;
        std::size_t avail = store_->size() - pos_;
        if (n > avail) n = avail;
        if (n) std::memcpy(dst, store_->data() + pos_, n);
        pos_ += n; return n;
    }
    std::size_t write(const void* src, std::size_t n) override {
        if (!writing_) return 0;
        const u8* p = static_cast<const u8*>(src);
        store_->insert(store_->end(), p, p + n);
        pos_ += n; return n;
    }
    std::int64_t seek(std::int64_t off, int whence) override {
        std::int64_t base = whence == 1 ? (std::int64_t)pos_
                          : whence == 2 ? (std::int64_t)store_->size() : 0;
        std::int64_t t = base + off; if (t < 0) return -1;
        pos_ = (std::size_t)t; return pos_;
    }
    std::int64_t tell() override { return (std::int64_t)pos_; }
    std::int64_t size() override { return (std::int64_t)store_->size(); }
private:
    std::vector<u8>* store_; bool writing_; std::size_t pos_ = 0;
};

class RwFs : public guild::shim::IFileSystem {
public:
    guild::shim::IFile* open(const char* path, const char* mode) override {
        bool writing = mode && (std::strchr(mode, 'w') || std::strchr(mode, 'W'));
        if (!writing && files_.find(path) == files_.end()) return nullptr;
        return new RwFile(&files_[path], writing);
    }
    void close(guild::shim::IFile* f) override { delete f; }
    bool exists(const char* path) override { return files_.count(path) != 0; }
    const std::vector<u8>* bytes(const char* path) {
        auto it = files_.find(path);
        return it == files_.end() ? nullptr : &it->second;
    }
private:
    std::map<std::string, std::vector<u8>> files_;
};

// Deterministic fill helper.
void Fill(u8* p, std::size_t n, u8 seed) {
    for (std::size_t i = 0; i < n; ++i) p[i] = (u8)(seed + i * 31 + (i >> 3) * 7);
}

} // namespace

// ===========================================================================
// Map-tile table
// ===========================================================================
TEST(io_save_tables, maptile_roundtrip) {
    RwFs fs; VfsInit(&fs, false);
    const int kN = 5;
    // The writer scans the FULL fixed-size array (8192 slots), so allocate it whole.
    std::vector<u8> src(kMapTileStride * kMapTileCapacity, 0),
                    dst(kMapTileStride * kMapTileCapacity, 0);
    for (int i = 0; i < kN; ++i) {
        u8* r = src.data() + i * kMapTileStride;
        Fill(r, kMapTileStride, (u8)(0x10 + i));
        r[0] = (u8)(1 + i); r[1] = 0; // nonzero marker word
    }
    VfsHandle* w = VfsOpenFile("mt.SAV", "wb"); CHECK(w);
    CHECK(SaveWriteMapTileTable(w, src.data()));
    VfsCloseStream(w);

    VfsHandle* r = VfsOpenFile("mt.SAV", "rb"); CHECK(r);
    CHECK(SaveLoadMapTileTable(r, dst.data()));
    VfsCloseStream(r);

    // Each record: only the serialized fields must match (offsets 0..1,2..5,6..9,
    // 10..13,14..17,18,19,28..58). Verify those byte ranges.
    for (int i = 0; i < kN; ++i) {
        u8* a = src.data() + i * kMapTileStride;
        u8* b = dst.data() + i * kMapTileStride;
        CHECK(std::memcmp(a, b, 20) == 0);          // +0..+19 (skip gap 20..27 -> ok all serialized except 20..27)
        CHECK(std::memcmp(a + 28, b + 28, 0x1F) == 0);
    }
    VfsShutdown();
}

// ===========================================================================
// Hotkey table
// ===========================================================================
TEST(io_save_tables, hotkey_roundtrip) {
    RwFs fs; VfsInit(&fs, false);
    HotkeyTable src{}, dst{};
    src.header = 0xCAFEBABE;
    for (int i = 0; i < kHotkeyCount; ++i) {
        src.colA[i] = 0x1000 + i; src.colB[i] = 0x2000 + i;
        src.colC[i] = 0x3000 + i; src.colD[i] = 0x4000 + i;
    }
    VfsHandle* w = VfsOpenFile("hk.SAV", "wb"); CHECK(w);
    CHECK(SaveWriteHotkeyTable(w, src)); VfsCloseStream(w);
    VfsHandle* r = VfsOpenFile("hk.SAV", "rb"); CHECK(r);
    CHECK(SaveLoadHotkeyTable(r, dst)); VfsCloseStream(r);

    CHECK_EQ(dst.header, src.header);
    for (int i = 0; i < kHotkeyCount; ++i) {
        CHECK_EQ(dst.colA[i], src.colA[i]); CHECK_EQ(dst.colB[i], src.colB[i]);
        CHECK_EQ(dst.colC[i], src.colC[i]); CHECK_EQ(dst.colD[i], src.colD[i]);
    }
    // on-stream layout: header then 4 dwords per i -> 4 + 24*16 = 388 bytes.
    const std::vector<u8>* raw = fs.bytes("hk.SAV");
    CHECK(raw); CHECK_EQ((u32)raw->size(), (u32)(4 + kHotkeyCount * 16));
    VfsShutdown();
}

// ===========================================================================
// Map tiles (version-gated square side)
// ===========================================================================
TEST(io_save_tables, maptiles_side_gate) {
    CHECK_EQ(MapTilesSide(0x10039), (u32)256);
    CHECK_EQ(MapTilesSide(0x1003A), (u32)512);
    CHECK_EQ(MapTilesSide(0x10041), (u32)512);
    CHECK_EQ(MapTilesSide(0x10042), (u32)768);
    CHECK_EQ(MapTilesSide(0x10045), (u32)768);
}

TEST(io_save_tables, maptiles_roundtrip_v512) {
    RwFs fs; VfsInit(&fs, false);
    std::vector<u8> hSrc(kMapRowStride * kMapRowStride), oSrc(kMapRowStride * kMapRowStride);
    std::vector<u8> hDst(kMapRowStride * kMapRowStride, 0), oDst(kMapRowStride * kMapRowStride, 0);
    for (size_t i = 0; i < hSrc.size(); ++i) { hSrc[i] = (u8)(i * 5 + 1); oSrc[i] = (u8)(i * 9 + 3); }

    // Write at full 768x768 (the writer is fixed-size).
    VfsHandle* w = VfsOpenFile("mp.SAV", "wb"); CHECK(w);
    CHECK(SaveWriteMapTiles(w, hSrc.data(), oSrc.data())); VfsCloseStream(w);

    // Read at v0x10042 (side 768) to consume the whole written grid (overlay >=0x10036).
    VfsHandle* r = VfsOpenFile("mp.SAV", "rb"); CHECK(r);
    CHECK(SaveLoadMapTiles(r, hDst.data(), oDst.data(), 0x10042)); VfsCloseStream(r);
    CHECK(hDst == hSrc);
    CHECK(oDst == oSrc);
    VfsShutdown();
}

// ===========================================================================
// History + carts
// ===========================================================================
TEST(io_save_tables, history_carts_roundtrip) {
    RwFs fs; VfsInit(&fs, false);
    HistoryHeader src{}, dst{};
    src.flag = 0x7A;
    src.base1 = 1000; src.cur1 = 1234;
    src.base2 = 5000; src.cur2 = 5678;
    for (int i = 0; i < kHistoryFlagsLen; ++i) src.flags[i] = (u8)(0x40 + i);
    // dst shares the same bases (load reconstructs cur = base + delta).
    dst.base1 = src.base1; dst.base2 = src.base2;

    std::vector<u8> cSrc(kCartCount * kCartStride, 0), cDst(kCartCount * kCartStride, 0);
    for (int k = 0; k < kCartCount; ++k) Fill(cSrc.data() + k * kCartStride, kCartStride, (u8)(k + 1));

    VfsHandle* w = VfsOpenFile("hc.SAV", "wb"); CHECK(w);
    CHECK(SaveWriteHistoryAndCarts(w, src, cSrc.data())); VfsCloseStream(w);
    VfsHandle* r = VfsOpenFile("hc.SAV", "rb"); CHECK(r);
    CHECK(SaveLoadHistoryAndCarts(r, dst, cDst.data())); VfsCloseStream(r);

    CHECK_EQ((u32)dst.flag, (u32)src.flag);
    CHECK_EQ(dst.cur1, src.cur1);
    CHECK_EQ(dst.cur2, src.cur2);
    for (int i = 0; i < kHistoryFlagsLen; ++i) CHECK_EQ((u32)dst.flags[i], (u32)src.flags[i]);
    // cart fields: +0 dword and 8 x {+4..(dword,byte) step 8}.
    for (int k = 0; k < kCartCount; ++k) {
        u8* a = cSrc.data() + k * kCartStride;
        u8* b = cDst.data() + k * kCartStride;
        CHECK(std::memcmp(a, b, 4) == 0);
        for (int i = 0; i < kCartEntryCount; ++i) {
            CHECK(std::memcmp(a + 4 + i * 8, b + 4 + i * 8, 5) == 0); // dword + byte
        }
    }
    VfsShutdown();
}

// ===========================================================================
// Amt table (version-gated)
// ===========================================================================
TEST(io_save_tables, amt_roundtrip_current) {
    RwFs fs; VfsInit(&fs, false);
    const int kN = 3;
    std::vector<u8> src(kAmtStride * kAmtCapacity, 0), dst(kAmtStride * kAmtCapacity, 0);
    // initialize all slots to -1 (free), then make kN live.
    for (int i = 0; i < kAmtCapacity; ++i) {
        std::int32_t m1 = -1; std::memcpy(src.data() + i * kAmtStride, &m1, 4);
        std::memcpy(dst.data() + i * kAmtStride, &m1, 4);
    }
    for (int i = 0; i < kN; ++i) {
        u8* r = src.data() + i * kAmtStride;
        Fill(r, kAmtStride, (u8)(0x20 + i));
        std::int32_t id = 100 + i; std::memcpy(r, &id, 4); // +0 != -1
    }
    VfsHandle* w = VfsOpenFile("amt.SAV", "wb"); CHECK(w);
    CHECK(SaveWriteAmtTable(w, src.data())); VfsCloseStream(w);
    VfsHandle* r = VfsOpenFile("amt.SAV", "rb"); CHECK(r);
    CHECK(SaveLoadAmtTable(r, dst.data(), 0x10045, nullptr)); VfsCloseStream(r);

    for (int i = 0; i < kN; ++i) {
        u8* a = src.data() + i * kAmtStride;
        u8* b = dst.data() + i * kAmtStride;
        CHECK(std::memcmp(a, b, 4) == 0);        // +0
        CHECK_EQ((u32)a[8], (u32)b[8]);          // +8
        CHECK(std::memcmp(a + 20, b + 20, 4) == 0);
        CHECK_EQ((u32)a[38], (u32)b[38]);
        CHECK_EQ((u32)a[48], (u32)b[48]);
        CHECK(std::memcmp(a + 52, b + 52, 1024) == 0);
        CHECK_EQ((u32)a[116], (u32)b[116]);
        CHECK(std::memcmp(a + 124, b + 124, 0x98) == 0);
        CHECK(std::memcmp(a + 24, b + 24, 0xE) == 0);
        CHECK(std::memcmp(a + 12, b + 12, 4) == 0);
    }
    VfsShutdown();
}

TEST(io_save_tables, amt_old_version_defaults) {
    RwFs fs; VfsInit(&fs, false);
    // Write a current-version stream (one live record) but read with an old version
    // (<0x1003F skips the +24 gametime; <0x10040 defaults +12 to -1). To make the
    // streams line up we craft bytes by hand: count(1) + the per-record fields WITHOUT
    // the +24 (0xE) and +12 (4) trailers.
    std::vector<u8> s;
    auto put32 = [&](u32 v){ for(int i=0;i<4;++i) s.push_back((u8)(v>>(8*i))); };
    put32(1);                       // count
    put32(0x11223344);              // +0
    s.push_back(0x55);              // +8 (1)
    put32(0x66778899);              // +20 (4)
    s.push_back(0xAB);              // +38 (1)
    s.push_back(0xCD);              // +48 (1)
    for (int i = 0; i < 1024; ++i) s.push_back((u8)(i & 0xFF)); // +52 (0x40 x16)
    s.push_back(0xEF);              // +116 (1)
    for (int i = 0; i < 0x98; ++i) s.push_back((u8)(0x70 + (i & 0x1F))); // +124
    // NO +24, NO +12 in this old stream.
    VfsHandle* w = VfsOpenFile("amtold.SAV", "wb"); CHECK(w);
    CHECK_EQ(VfsWriteStream(s.data(), 1, w, (u32)s.size()), (u32)s.size());
    VfsCloseStream(w);

    std::vector<u8> dst(kAmtStride * kAmtCapacity, 0);
    u8 gtDefault[14]; for (int i = 0; i < 14; ++i) gtDefault[i] = (u8)(0xA0 + i);
    VfsHandle* r = VfsOpenFile("amtold.SAV", "rb"); CHECK(r);
    CHECK(SaveLoadAmtTable(r, dst.data(), 0x10030, gtDefault)); VfsCloseStream(r);

    u8* b = dst.data();
    CHECK_EQ((u32)b[0] | ((u32)b[1] << 8) | ((u32)b[2] << 16) | ((u32)b[3] << 24), (u32)0x11223344);
    CHECK(std::memcmp(b + 24, gtDefault, 14) == 0); // defaulted gametime
    std::int32_t v12; std::memcpy(&v12, b + 12, 4);
    CHECK_EQ(v12, (std::int32_t)-1);                // defaulted -1
    VfsShutdown();
}

// ===========================================================================
// Game globals
// ===========================================================================
TEST(io_save_tables, gameglobals_roundtrip) {
    RwFs fs; VfsInit(&fs, false);
    GameGlobalsBlock src{}, dst{};
    u8* sp = reinterpret_cast<u8*>(&src);
    Fill(sp, sizeof(GameGlobalsBlock), 0x33);
    // re-stamp the named scalar fields so they survive (they are members):
    src.d910 = 0xA1; src.q5262_b = 0x1234; src.b5258 = 0x99;

    VfsHandle* w = VfsOpenFile("gg.SAV", "wb"); CHECK(w);
    CHECK(SaveWriteGameGlobals(w, src)); VfsCloseStream(w);
    VfsHandle* r = VfsOpenFile("gg.SAV", "rb"); CHECK(r);
    CHECK(SaveLoadGameGlobals(r, dst)); VfsCloseStream(r);

    CHECK_EQ(dst.d910, src.d910);
    CHECK_EQ((u32)dst.q5262_b, (u32)src.q5262_b);
    CHECK_EQ(dst.b5258, src.b5258);
    for (int i = 0; i < 16 * 5; ++i) CHECK_EQ(dst.matrix[i], src.matrix[i]);
    CHECK(std::memcmp(dst.block0, src.block0, 0x44) == 0);
    CHECK(std::memcmp(dst.block4, src.block4, 0x44) == 0);
    // grid records: only serialized bytes (+0..+1,+2..+3,+4,+8..+11) must match.
    for (int rec = 0; rec < 64; ++rec) {
        u8* a = src.grid + rec * 24;
        u8* b = dst.grid + rec * 24;
        CHECK(std::memcmp(a, b, 5) == 0);          // +0(2)+2(2)+4(1)
        CHECK(std::memcmp(a + 8, b + 8, 4) == 0);  // +8(4)
    }
    // on-stream size = scalar prefix (102) + matrix(16*5*4=320) + 5*0x44 + grid 64*9.
    const std::vector<u8>* raw = fs.bytes("gg.SAV");
    CHECK(raw);
    CHECK_EQ((u32)raw->size(), (u32)(102 + 320 + 5 * 0x44 + 64 * 9));
    VfsShutdown();
}

// ===========================================================================
// Building / counter table (version-gated)
// ===========================================================================
TEST(io_save_tables, building_table_roundtrip_current) {
    RwFs fs; VfsInit(&fs, false);
    std::vector<u8> src(kBuildStride * kBuildCapacity), dst(kBuildStride * kBuildCapacity, 0);
    Fill(src.data(), src.size(), 0x12);

    VfsHandle* w = VfsOpenFile("bt.SAV", "wb"); CHECK(w);
    CHECK(SaveWriteBuildingTable(w, src.data(), nullptr, 0x10045)); VfsCloseStream(w);
    VfsHandle* r = VfsOpenFile("bt.SAV", "rb"); CHECK(r);
    CHECK(SaveLoadBuildingTable(r, dst.data(), 0x10045)); VfsCloseStream(r);

    for (int i = 0; i < kBuildCapacity; ++i) {
        u8* a = src.data() + i * kBuildStride;
        u8* b = dst.data() + i * kBuildStride;
        // verify each serialized field range at current version.
        CHECK(std::memcmp(a, b, 2) == 0);            // +0
        CHECK(std::memcmp(a + 2, b + 2, 0x10) == 0); // +2
        for (int off : {20, 40, 28, 32, 44, 48, 52, 56, 64, 68, 72, 76, 80,
                        84, 88, 92, 60, 96, 100, 104})
            CHECK(std::memcmp(a + off, b + off, 4) == 0);
        CHECK(std::memcmp(a + 112, b + 112, 0xE) == 0);
        CHECK(std::memcmp(a + 128, b + 128, 4) == 0);
        CHECK(std::memcmp(a + 132, b + 132, 0x10) == 0);
        CHECK(std::memcmp(a + 148, b + 148, 0xC) == 0);
        CHECK(std::memcmp(a + 160, b + 160, 4) == 0);
        CHECK(std::memcmp(a + 108, b + 108, 4) == 0);
    }
    VfsShutdown();
}

TEST(io_save_tables, building_record_old_version_gates) {
    RwFs fs; VfsInit(&fs, false);
    // At version 0x10013 (< all gates except baseline) the load omits +40, the
    // +84..+104 group, +112, +108. Write at the SAME version so the streams align.
    std::vector<u8> src(kBuildStride, 0), dst(kBuildStride, 0);
    Fill(src.data(), src.size(), 0x55);
    VfsHandle* w = VfsOpenFile("br.SAV", "wb"); CHECK(w);
    CHECK(SaveWriteBuildingRecord(w, src.data(), nullptr, 0x10013)); VfsCloseStream(w);
    VfsHandle* r = VfsOpenFile("br.SAV", "rb"); CHECK(r);
    CHECK(SaveLoadBuildingRecord(r, dst.data(), 0x10013)); VfsCloseStream(r);

    // baseline fields present:
    CHECK(std::memcmp(src.data(), dst.data(), 2) == 0);
    CHECK(std::memcmp(src.data() + 20, dst.data() + 20, 4) == 0);
    // +40 NOT read at this version -> dst keeps 0.
    std::int32_t v40; std::memcpy(&v40, dst.data() + 40, 4);
    CHECK_EQ(v40, (std::int32_t)0);
    // on-stream size: 2 + 0x10 + 4 (+20) + (28..80: 11 dwords =44) + 128(4)+132(0x10)+148(0xC)+160(4)
    const std::vector<u8>* raw = fs.bytes("br.SAV"); CHECK(raw);
    CHECK_EQ((u32)raw->size(), (u32)(2 + 0x10 + 4 + 11 * 4 + 4 + 0x10 + 0xC + 4));
    VfsShutdown();
}

// ===========================================================================
// Object sub-tables
// ===========================================================================
TEST(io_save_tables, object_light_roundtrip) {
    RwFs fs; VfsInit(&fs, false);
    const u32 kN = 4;
    std::vector<u8> src(kObjLightStride * kN), dst(kObjLightStride * kN, 0);
    Fill(src.data(), src.size(), 0x21);
    VfsHandle* w = VfsOpenFile("ol.SAV", "wb"); CHECK(w);
    CHECK(SaveWriteObjectLight(w, src.data(), kN)); VfsCloseStream(w);
    VfsHandle* r = VfsOpenFile("ol.SAV", "rb"); CHECK(r);
    CHECK(SaveLoadObjectLight(r, dst.data(), kN)); VfsCloseStream(r);
    for (u32 i = 0; i < kN; ++i) {
        u8* a = src.data() + i * kObjLightStride;
        u8* b = dst.data() + i * kObjLightStride;
        CHECK(std::memcmp(a, b, 4) == 0);
        CHECK_EQ((u32)a[4], (u32)b[4]);
        CHECK(std::memcmp(a + 8, b + 8, 12) == 0);
        CHECK(std::memcmp(a + 20, b + 20, 0xE) == 0);
        CHECK(std::memcmp(a + 34, b + 34, 0x80) == 0);
    }
    VfsShutdown();
}

TEST(io_save_tables, object_marks_roundtrip) {
    RwFs fs; VfsInit(&fs, false);
    const u32 kN = 3;
    std::vector<u8> src(kObjMarkStride * kN), dst(kObjMarkStride * kN, 0);
    Fill(src.data(), src.size(), 0x71);
    VfsHandle* w = VfsOpenFile("om.SAV", "wb"); CHECK(w);
    CHECK(SaveWriteObjectMarks(w, src.data(), kN)); VfsCloseStream(w);
    VfsHandle* r = VfsOpenFile("om.SAV", "rb"); CHECK(r);
    CHECK(SaveLoadObjectMarks(r, dst.data(), kN)); VfsCloseStream(r);
    for (u32 i = 0; i < kN; ++i) {
        u8* a = src.data() + i * kObjMarkStride;
        u8* b = dst.data() + i * kObjMarkStride;
        CHECK_EQ((u32)a[0], (u32)b[0]);
        CHECK(std::memcmp(a + 4, b + 4, 12) == 0);
        CHECK(std::memcmp(a + 16, b + 16, 0x40) == 0);
    }
    VfsShutdown();
}

TEST(io_save_tables, object_main_no_blob_roundtrip) {
    RwFs fs; VfsInit(&fs, false);
    const u32 kN = 2;
    std::vector<u8> src(kObjMainStride * kN, 0), dst(kObjMainStride * kN, 0);
    for (u32 i = 0; i < kN; ++i) {
        u8* r = src.data() + i * kObjMainStride;
        Fill(r, kObjMainStride, (u8)(0x10 + i));
        std::memset(r + 124, 0, 8); // null blob ptr+len -> writes a 0 length dword
    }
    VfsHandle* w = VfsOpenFile("omn.SAV", "wb"); CHECK(w);
    CHECK(SaveWriteObjectMain(w, src.data(), kN)); VfsCloseStream(w);
    VfsHandle* r = VfsOpenFile("omn.SAV", "rb"); CHECK(r);
    CHECK(SaveLoadObjectMain(r, dst.data(), kN, 0x10045, kObjBlobChunk)); VfsCloseStream(r);
    for (u32 i = 0; i < kN; ++i) {
        u8* a = src.data() + i * kObjMainStride;
        u8* b = dst.data() + i * kObjMainStride;
        CHECK_EQ((u32)a[0], (u32)b[0]);
        CHECK(std::memcmp(a + 4, b + 4, 4) == 0);
        CHECK(std::memcmp(a + 20, b + 20, 0x30) == 0);
        for (int k = 0; k < 8; ++k) CHECK(std::memcmp(a + 140 + k * 4, b + 140 + k * 4, 4) == 0);
        CHECK(std::memcmp(a + 172, b + 172, 0xA0) == 0);
        std::uint32_t len; std::memcpy(&len, b + 128, 4); CHECK_EQ(len, (u32)0);
    }
    VfsShutdown();
}

// ===========================================================================
// Action queues — pointer<->index conversion
// ===========================================================================
TEST(io_save_tables, action_pool_link_roundtrip) {
    CHECK_EQ(ActionPoolCount(0x1003B), (u32)0x2000);
    CHECK_EQ(ActionPoolCount(0x1003C), (u32)0x8000);

    // pointer<->index conversion (the original's +145/+149 transform).
    std::vector<u8> pool(kActionStride * 6, 0);
    CHECK_EQ(ActionPtrToIndex(pool.data(), pool.data() + 2 * kActionStride), (std::int32_t)2);
    CHECK_EQ(ActionPtrToIndex(pool.data(), nullptr), (std::int32_t)-1);
    CHECK(ActionIndexToPtr(pool.data(), 3) == pool.data() + 3 * kActionStride);
    CHECK(ActionIndexToPtr(pool.data(), -1) == nullptr);

    RwFs fs; VfsInit(&fs, false);
    const u32 kN = 6;
    std::vector<u8> src(kActionStride * kN, 0), dst(kActionStride * kN, 0);
    for (u32 i = 0; i < kN; ++i) Fill(src.data() + i * kActionStride, kActionStride, (u8)(0x40 + i));
    // index-encoded links: record 0.next -> 2, record 0.prev -> -1; record 5.next -> 1.
    auto setIdx = [&](int rec, int off, std::int32_t v) {
        std::memcpy(src.data() + rec * kActionStride + off, &v, 4);
    };
    setIdx(0, kActionLinkNext, 2);
    setIdx(0, kActionLinkPrev, -1);
    setIdx(5, kActionLinkNext, 1);

    VfsHandle* w = VfsOpenFile("aq.SAV", "wb"); CHECK(w);
    CHECK(SaveWriteActionPool(w, src.data(), kN)); VfsCloseStream(w);

    // On disk the +145 index of record 0 == 2, prev == -1.
    const std::vector<u8>* raw = fs.bytes("aq.SAV"); CHECK(raw);
    std::int32_t onNext, onPrev;
    std::memcpy(&onNext, raw->data() + 0 * kActionStride + kActionLinkNext, 4);
    std::memcpy(&onPrev, raw->data() + 0 * kActionStride + kActionLinkPrev, 4);
    CHECK_EQ(onNext, (std::int32_t)2);
    CHECK_EQ(onPrev, (std::int32_t)-1);

    VfsHandle* r = VfsOpenFile("aq.SAV", "rb"); CHECK(r);
    CHECK(SaveLoadActionPool(r, dst.data(), kN)); VfsCloseStream(r);
    // whole 153-byte records preserved byte-for-byte (links included).
    CHECK(std::memcmp(src.data(), dst.data(), kActionStride * kN) == 0);
    VfsShutdown();
}

// ===========================================================================
// Person/scene record (GameStateHeader body) + preamble
// ===========================================================================
TEST(io_save_tables, person_scene_record_roundtrip) {
    RwFs fs; VfsInit(&fs, false);
    std::vector<u8> src(kPersStride, 0), dst(kPersStride, 0);
    Fill(src.data(), src.size(), 0x09);
    // counter fields at +84 / +396 are bias-encoded; pick values that survive.
    std::int32_t cA = 9000, cB = 12000;
    std::memcpy(src.data() + 84, &cA, 4);
    std::memcpy(src.data() + 396, &cB, 4);

    PersonSceneLinks links{ 111, 222, 333, 444 }, got{};
    VfsHandle* w = VfsOpenFile("ps.SAV", "wb"); CHECK(w);
    CHECK(SaveWritePersonSceneRecord(w, src.data(), links)); VfsCloseStream(w);
    VfsHandle* r = VfsOpenFile("ps.SAV", "rb"); CHECK(r);
    CHECK(SaveLoadPersonSceneRecord(r, dst.data(), &got)); VfsCloseStream(r);

    // links round-trip.
    CHECK_EQ(got.idAt91, links.idAt91); CHECK_EQ(got.idAt92, links.idAt92);
    CHECK_EQ(got.idAt95, links.idAt95); CHECK_EQ(got.idAt97, links.idAt97);
    // counters round-trip through the bias.
    std::int32_t gA, gB; std::memcpy(&gA, dst.data() + 84, 4); std::memcpy(&gB, dst.data() + 396, 4);
    CHECK_EQ(gA, cA); CHECK_EQ(gB, cB);
    // a few representative non-link byte ranges.
    CHECK(std::memcmp(src.data() + 48, dst.data() + 48, 16) == 0);
    CHECK(std::memcmp(src.data() + 136, dst.data() + 136, 168) == 0);
    CHECK(std::memcmp(src.data() + 496, dst.data() + 496, 24) == 0);
    CHECK(std::memcmp(src.data() + 520, dst.data() + 520, 4) == 0);
    VfsShutdown();
}

TEST(io_save_tables, gamestate_preamble_roundtrip) {
    RwFs fs; VfsInit(&fs, false);
    GameStateHeaderPreamble src{}, dst{};
    src.marker63CC5C = 0x1357;
    src.id6498E8 = 0xAAAA; src.id6498EC = 0xBBBB;
    for (int i = 0; i < 8; ++i) src.handlerIds[i] = 0xC000 + i;
    VfsHandle* w = VfsOpenFile("gp.SAV", "wb"); CHECK(w);
    CHECK(SaveWriteGameStateHeaderPreamble(w, src, 42)); VfsCloseStream(w);
    u32 liveOut = 0;
    VfsHandle* r = VfsOpenFile("gp.SAV", "rb"); CHECK(r);
    CHECK(SaveLoadGameStateHeaderPreamble(r, dst, &liveOut)); VfsCloseStream(r);
    CHECK_EQ((u32)dst.marker63CC5C, (u32)src.marker63CC5C);
    CHECK_EQ(liveOut, (u32)42);
    CHECK_EQ(dst.id6498E8, src.id6498E8); CHECK_EQ(dst.id6498EC, src.id6498EC);
    for (int i = 0; i < 8; ++i) CHECK_EQ(dst.handlerIds[i], src.handlerIds[i]);
    VfsShutdown();
}

// ===========================================================================
// Person/object table (169-byte records) — non-plant path + version gates
// ===========================================================================
TEST(io_save_tables, person_object_records_roundtrip_current) {
    RwFs fs; VfsInit(&fs, false);
    const u32 kN = 3;
    std::vector<u8> src(kObjStride * kN, 0), dst(kObjStride * kN, 0);
    for (u32 i = 0; i < kN; ++i) {
        u8* r = src.data() + i * kObjStride;
        Fill(r, kObjStride, (u8)(0x30 + i));
        r[0] = (u8)(2 + i);  // alive, NOT kind-30 (no plantmap)
    }
    VfsHandle* w = VfsOpenFile("po.SAV", "wb"); CHECK(w);
    CHECK(SaveWritePersonRecords(w, src.data(), kN)); VfsCloseStream(w);
    VfsHandle* r = VfsOpenFile("po.SAV", "rb"); CHECK(r);
    CHECK(SaveLoadPersonRecords(r, dst.data(), kN, 0x10045)); VfsCloseStream(r);

    for (u32 i = 0; i < kN; ++i) {
        u8* a = src.data() + i * kObjStride;
        u8* b = dst.data() + i * kObjStride;
        CHECK_EQ((u32)a[0], (u32)b[0]);
        CHECK(std::memcmp(a + 1, b + 1, 4) == 0);
        CHECK(std::memcmp(a + 5, b + 5, 0x20) == 0);
        CHECK(std::memcmp(a + 37, b + 37, 6) == 0);   // +37,+39,+41 words
        CHECK(std::memcmp(a + 43, b + 43, 4) == 0);
        CHECK(std::memcmp(a + 53, b + 53, 24) == 0);  // +53..+76 dwords (53,57,61,65,69,73)
        CHECK(std::memcmp(a + 90, b + 90, 2) == 0);
        CHECK_EQ((u32)a[92], (u32)b[92]);
        CHECK(std::memcmp(a + 101, b + 101, 0x30) == 0);
        // post-load forced fields:
        std::int32_t v48; std::memcpy(&v48, b + 48, 4); CHECK_EQ(v48, (std::int32_t)5000);
        std::int32_t v149; std::memcpy(&v149, b + 149, 4); CHECK_EQ(v149, (std::int32_t)-1);
        // +153 lightmap read at >=0x10043:
        CHECK(std::memcmp(a + 153, b + 153, 0x10) == 0);
    }
    VfsShutdown();
}

TEST(io_save_tables, person_object_records_old_version_lightmap_default) {
    RwFs fs; VfsInit(&fs, false);
    // Write at 0x10041 (>=0x10028 so +73 present; <0x10032 so a discard dword is
    // present; <0x10043 so the +153 lightmap is DEFAULTED, not read).
    const u32 kN = 1; const u32 ver = 0x10041;
    std::vector<u8> src(kObjStride, 0), dst(kObjStride, 0);
    Fill(src.data(), src.size(), 0x44);
    src[0] = 5;
    // The writer always writes +73 and +153 (it is byte-emitting). For the old-version
    // read to line up we must craft the stream: count + writer fields, but at ver the
    // loader expects a discard dword AFTER +69 (since <0x10032) and NO +153. Hand-build.
    std::vector<u8> s;
    auto put32 = [&](u32 v){ for (int i=0;i<4;++i) s.push_back((u8)(v>>(8*i))); };
    put32(1);                              // count
    s.push_back(5);                        // +0 (kind, not 30)
    for (int i=0;i<4;++i) s.push_back((u8)(i+1));    // +1
    for (int i=0;i<0x20;++i) s.push_back((u8)(0x60+i)); // +5
    for (int i=0;i<6;++i) s.push_back((u8)(0x10+i));  // +37,+39,+41 (2 each)
    for (int i=0;i<4;++i) s.push_back((u8)(0x20+i));  // +43
    s.push_back(0x77);                     // +47
    s.push_back(0x88);                     // +52
    for (int i=0;i<4;++i) s.push_back((u8)(0xA0+i));  // +53
    for (int i=0;i<4;++i) s.push_back((u8)(0xB0+i));  // +57
    for (int i=0;i<4;++i) s.push_back((u8)(0xC0+i));  // +61
    s.push_back(2); s.push_back(0); s.push_back(0); s.push_back(0); // +65 (<=4, no clamp)
    for (int i=0;i<4;++i) s.push_back((u8)(0xD0+i));  // +69
    for (int i=0;i<4;++i) s.push_back((u8)(0xE0+i));  // +73 (>=0x10028)
    for (int i=0;i<4;++i) s.push_back((u8)(0xF0+i));  // discard dword (<0x10032)
    for (int i=0;i<2;++i) s.push_back((u8)(0x11+i));  // +90 (2)
    s.push_back(0x99);                     // +92 (1)
    for (int i=0;i<0x30;++i) s.push_back((u8)(0x40+i)); // +101 (kind != 30)
    // NO +153 lightmap (defaulted at this version).
    VfsHandle* w = VfsOpenFile("poold.SAV", "wb"); CHECK(w);
    CHECK_EQ(VfsWriteStream(s.data(), 1, w, (u32)s.size()), (u32)s.size());
    VfsCloseStream(w);

    VfsHandle* r = VfsOpenFile("poold.SAV", "rb"); CHECK(r);
    CHECK(SaveLoadPersonRecords(r, dst.data(), kN, ver)); VfsCloseStream(r);
    u8* b = dst.data();
    CHECK_EQ((u32)b[0], (u32)5);
    // lightmap defaulted: byte_153 |= 1, +165 = -1.
    CHECK((b[153] & 1) != 0);
    std::int32_t v165; std::memcpy(&v165, b + 165, 4); CHECK_EQ(v165, (std::int32_t)-1);
    std::int32_t v48; std::memcpy(&v48, b + 48, 4); CHECK_EQ(v48, (std::int32_t)5000);
    (void)src;
    VfsShutdown();
}
