#include "test.h"

#include "io/save_world_load.h"
#include "io/save.h"
#include "io/save_person.h"
#include "io/vfs.h"
#include "sim/entity.h"
#include "shim/IFileSystem.h"

#include <cstdint>
#include <cstring>
#include <map>
#include <string>
#include <vector>

using namespace guild::io;
using guild::u8;
using guild::u16;
using guild::u32;
using guild::i32;

// ---------------------------------------------------------------------------
// Writable in-memory IFileSystem (same shape as io_save_test.cpp): "wb" starts a
// fresh growable buffer; "rb" reads back the previously-written bytes. This lets a
// synthetic world stream be written then loaded through the real loaders.
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
        pos_ += n;
        return n;
    }
    std::size_t write(const void* src, std::size_t n) override {
        if (!writing_) return 0;
        const u8* p = static_cast<const u8*>(src);
        store_->insert(store_->end(), p, p + n);
        pos_ += n;
        return n;
    }
    std::int64_t seek(std::int64_t off, int whence) override {
        std::int64_t base = whence == 1 ? (std::int64_t)pos_
                          : whence == 2 ? (std::int64_t)store_->size() : 0;
        std::int64_t t = base + off;
        if (t < 0) return -1;
        pos_ = (std::size_t)t;
        return pos_;
    }
    std::int64_t tell() override { return (std::int64_t)pos_; }
    std::int64_t size() override { return (std::int64_t)store_->size(); }
private:
    std::vector<u8>* store_;
    bool             writing_;
    std::size_t      pos_ = 0;
};

class RwFs : public guild::shim::IFileSystem {
public:
    guild::shim::IFile* open(const char* path, const char* mode) override {
        bool writing = mode && (std::strchr(mode, 'w') || std::strchr(mode, 'W'));
        auto& store = files_[path];
        if (!writing && files_.find(path) == files_.end())
            return nullptr;
        return new RwFile(&store, writing);
    }
    void close(guild::shim::IFile* f) override { delete f; }
    bool exists(const char* path) override { return files_.count(path) != 0; }
private:
    std::map<std::string, std::vector<u8>> files_;
};

// A tiny byte-stream builder mirroring the exact loader read order. The unit tests
// build a stream, hand it to the (real) loader, and assert byte-exact records.
struct Stream {
    std::vector<u8> b;
    void put(const void* p, u32 n) {
        const u8* q = static_cast<const u8*>(p);
        b.insert(b.end(), q, q + n);
    }
    void u8v(u8 v)   { put(&v, 1); }
    void u16v(u16 v) { put(&v, 2); }
    void u32v(u32 v) { put(&v, 4); }
    void i32v(i32 v) { put(&v, 4); }
    void zeros(u32 n) { b.insert(b.end(), n, 0); }
};

// Open a VFS read stream over a raw byte buffer (write it to "wb" then re-open "rb").
struct VfsBytes {
    RwFs fs;
    explicit VfsBytes(const std::vector<u8>& bytes) {
        VfsInit(&fs, false);
        VfsHandle* w = VfsOpenFile("synthetic.SAV", "wb");
        VfsWriteStream(bytes.data(), (u32)bytes.size(), w, 1);
        VfsCloseStream(w);
    }
    ~VfsBytes() { VfsShutdown(); }
    VfsHandle* reader() { return VfsOpenFile("synthetic.SAV", "rb"); }
};

} // namespace

// ===========================================================================
// 1. LoadPersonIndexTable — map-tile / scene-node 67-stride records.
// ===========================================================================
TEST(io_save_world, person_index_table_roundtrip) {
    // Build: count(=3), then 3 records of 8 fields (51 wire bytes each).
    Stream s;
    const int N = 3;
    s.u32v(N);
    // Per-record distinguishable field values (in WIRE order: +0,2,6,10,14,18,19,28).
    std::vector<std::vector<u8>> recs;
    for (int i = 0; i < N; ++i) {
        std::vector<u8> w;
        u16 m = (u16)(0x1000 + i);          // +0 (2)
        u32 a = 0x11110000u + i;             // +2 (4)
        u32 b2 = 0x22220000u + i;            // +6 (4)
        u32 c = 0x33330000u + i;             // +10 (4)
        u32 d = 0x44440000u + i;             // +14 (4)
        u8 e = (u8)(0x50 + i);               // +18 (1)
        u8 f = (u8)(0x60 + i);               // +19 (1)
        auto app = [&](const void* p, int n){ const u8* q=(const u8*)p; w.insert(w.end(),q,q+n); };
        app(&m,2); app(&a,4); app(&b2,4); app(&c,4); app(&d,4); app(&e,1); app(&f,1);
        for (int k = 0; k < 0x1F; ++k) w.push_back((u8)(0x80 + i + k)); // +28 (31)
        recs.push_back(w);
        s.put(w.data(), (u32)w.size());
    }

    VfsBytes vb(s.b);
    VfsHandle* h = vb.reader();
    CHECK(h != nullptr);

    static u8 tiles[guild::io::kSceneTileScanBytes];
    std::memset(tiles, 0xFF, sizeof tiles);
    u32 count = 0;
    bool ok = LoadPersonIndexTable(h, tiles, &count);
    VfsCloseStream(h);
    CHECK(ok);
    CHECK_EQ(count, (u32)N);

    // Each record must land at tiles + i*67 with the exact field bytes, with the
    // unread gap (+20..+27) untouched (loader never writes there).
    for (int i = 0; i < N; ++i) {
        u8* r = tiles + (std::size_t)i * guild::io::kSceneTileStride;
        const std::vector<u8>& w = recs[i];
        // Reconstruct the in-memory record from the wire fields at their offsets.
        u8 expect[guild::io::kSceneTileStride];
        std::memset(expect, 0xFF, sizeof expect);
        std::memcpy(expect + 0,  &w[0],  2);
        std::memcpy(expect + 2,  &w[2],  4);
        std::memcpy(expect + 6,  &w[6],  4);
        std::memcpy(expect + 10, &w[10], 4);
        std::memcpy(expect + 14, &w[14], 4);
        std::memcpy(expect + 18, &w[18], 1);
        std::memcpy(expect + 19, &w[19], 1);
        std::memcpy(expect + 28, &w[20], 0x1F);
        CHECK(std::memcmp(r + 0, expect + 0, 20) == 0);     // fields up to +19
        CHECK(std::memcmp(r + 28, expect + 28, 0x1F) == 0); // +28 block
    }
}

// ===========================================================================
// 2. LoadGlobalCounters — 16 building/counter records (164-stride), version-gated.
// ===========================================================================
TEST(io_save_world, global_counters_roundtrip) {
    const u32 ver = 0x1003B;
    // Build the exact READ order so each consumed dword is distinct; then verify the
    // total bytes consumed == 16 * 152 (the per-record wire size at 0x1003B).
    Stream s;
    u32 tok = 0;
    auto w = [&](int n){ for (int k=0;k<n;++k) s.u8v((u8)(tok++ & 0xFF)); };
    for (int slot = 0; slot < 16; ++slot) {
        w(2); w(0x10); w(4);          // +0 +2 +0x14
        w(4);                          // >=1002C +0x28
        w(4); w(4); w(4); w(4); w(4); w(4); // v2 v10 v25 v27 v29 v28
        w(4); w(4); w(4); w(4); w(4); // v3 v11 v13 v15 v17
        for (int i=0;i<7;++i) w(4);   // >=10014
        w(0xE);                        // >=10015
        w(4); w(0x10); w(0xC); w(4);  // v22 v24 v26 v21
        w(4);                          // >=10018
    }
    const std::size_t expectBytes = 16 * 152;
    CHECK_EQ(s.b.size(), expectBytes);

    VfsBytes vb(s.b);
    VfsHandle* h = vb.reader();
    static u8 counters[guild::io::kBuildCounterStride * 16];
    std::memset(counters, 0, sizeof counters);
    bool ok = LoadGlobalCounters(h, counters, ver);
    long pos = VfsTell(h);
    VfsCloseStream(h);
    CHECK(ok);
    CHECK_EQ((u32)pos, (u32)expectBytes);   // consumed exactly the whole stream

    // The leading word of each record lands at counters[164*slot + 0].
    for (int slot = 0; slot < 16; ++slot) {
        u16 lead;
        std::memcpy(&lead, counters + 164 * slot, 2);
        // First byte of slot 0's record is token 0; subsequent slots continue the
        // token counter, so the lead word's low byte is deterministic.
        CHECK(lead != 0 || slot == 0);
    }
}

// ===========================================================================
// 3. LoadCityRecords — the 536-byte person/scene records (populates g_persons).
//    Build one record at version 0x1003B and assert byte-exact reconstruction +
//    counter-bias re-application + scatter into the marker slot.
// ===========================================================================
namespace {
// Emit a city record's WIRE bytes at version 0x1003B (the shipped-city version), and
// also compute the EXPECTED in-memory 536-byte record. `marker` is the slot index.
void EmitCityRecord1003B(Stream& s, u16 marker, u8 expect[536]) {
    std::memset(expect, 0, 536);
    const u32 ver = 0x1003B;
    u32 tok = 0x1000;
    auto wr = [&](int off, int n) {
        for (int k = 0; k < n; ++k) {
            u8 v = (u8)((tok + off + k) & 0xFF);
            s.u8v(v);
            expect[off + k] = v;
        }
    };
    s.u16v(marker); std::memcpy(expect + 0, &marker, 2);
    // version < 0x1003E -> +520 = -1 (no read)
    { i32 m1 = -1; std::memcpy(expect + 520, &m1, 4); }
    wr(2,1); wr(4,4); wr(8,1); wr(9,1); wr(10,2); wr(12,1); wr(13,1);
    wr(16,4); wr(20,4); wr(24,4); wr(28,4); wr(32,4); wr(36,4);
    wr(40,2); wr(44,4);                 // >=0x1003B
    wr(48,0x10);
    wr(64,0x10);                        // >=0x10031
    wr(80,2);
    // +84 dword with +1342 bias re-applied on load.
    { i32 raw = 0x0700; s.i32v(raw); i32 biased = raw + (i32)guild::io::kCityCounterBiasA;
      std::memcpy(expect + 84, &biased, 4); }
    wr(88,1); wr(92,0x20);
    wr(124,4);                          // >=0x10024
    wr(128,5); wr(136,0xA8);
    wr(356,1); wr(357,1); wr(358,1); wr(359,1); wr(360,1); wr(361,1);
    wr(364,4); wr(368,4);
    wr(372,4);                          // >=0x10020 dword
    wr(380,4); wr(384,1);
    // +396 dword with +1468 bias.
    { i32 raw = 0x0900; s.i32v(raw); i32 biased = raw + (i32)guild::io::kCityCounterBiasB;
      std::memcpy(expect + 396, &biased, 4); }
    // +400: read then (version<0x1003E) overwritten with 4.
    { i32 raw = 0x12345; s.i32v(raw); i32 four = 4; std::memcpy(expect + 400, &four, 4); }
    wr(404,4); wr(408,4); wr(412,4); wr(416,4); wr(420,4); wr(424,4); wr(428,4);
    wr(432,1); wr(433,1); wr(436,0x10); wr(456,4); wr(460,4); wr(464,0x10);
    wr(480,4); wr(484,4); wr(488,4);
    wr(492,4);                          // >=0x1002A
    wr(453,1);                          // >=0x10021
    // +388 link (v10).
    { i32 link = 0x55667788; s.i32v(link); std::memcpy(expect + 388, &link, 4); }
    wr(496,0x18);                       // >=0x10021
    wr(524,4); wr(528,1); wr(529,1); wr(530,1); wr(531,1); wr(532,1); // >=0x10036
    (void)ver;
}
} // namespace

TEST(io_save_world, city_record_roundtrip_1003B) {
    Stream s;
    s.u16v(0xABCD);   // word_63CC5C
    const i32 count = 2;
    s.i32v(count);    // dword_647724
    s.u32v(0x1111u);  // idA (dword_6498E8)
    s.u32v(0x2222u);  // idB (dword_6498EC[0])
    for (int i = 0; i < 8; ++i) s.u32v(0x3000u + i); // handler ids (>=0x10017)

    u8 expect[2][536];
    const u16 markers[2] = {5, 17};
    EmitCityRecord1003B(s, markers[0], expect[0]);
    EmitCityRecord1003B(s, markers[1], expect[1]);

    guild::sim::ResetEntityArrays();
    VfsBytes vb(s.b);
    VfsHandle* h = vb.reader();
    u16 mk = 0; u32 cnt = 0, idA = 0, idB = 0, handlers[8] = {};
    u8* personBase = reinterpret_cast<u8*>(&guild::sim::g_persons[0]);
    bool ok = LoadCityRecords(h, personBase, 0x1003B, &mk, &cnt, &idA, &idB, handlers);
    VfsCloseStream(h);

    CHECK(ok);
    CHECK_EQ((u32)mk, (u32)0xABCD);
    CHECK_EQ(cnt, (u32)count);
    CHECK_EQ(idA, (u32)0x1111);
    CHECK_EQ(idB, (u32)0x2222);
    for (int i = 0; i < 8; ++i) CHECK_EQ(handlers[i], (u32)(0x3000u + i));

    for (int rec = 0; rec < 2; ++rec) {
        u8* r = personBase + (std::size_t)markers[rec] * 536;
        CHECK(std::memcmp(r, expect[rec], 536) == 0);
    }
}

// ===========================================================================
// 4. LoadBuildingSlotTables — 5 city-slot tables + 4 city-info records. Verify the
//    exact wire size consumed (the table is fixed-shape at version 0x1003B).
// ===========================================================================
TEST(io_save_world, building_slot_tables_consumes_exact) {
    const u32 ver = 0x1003B;
    // Per city-slot table: 16 header + 62 * (2+4*8+2+4) = 16 + 62*40 = 2496 wire bytes.
    const std::size_t slotTableWire = 16 + 62 * (std::size_t)(2 + 4 * 8 + 2 + 4);
    // Per city-info record at 0x1003B: compute by summing the read sizes.
    std::size_t infoWire = 0x20 + 8 + 1 + 4 + 4 + 2 + 8 + 1 + 1
                         + 10 * 8 + 10 * 8 + 1 + 1 + 4 * 4 + 4 * 4 + 4
                         + 11 + 1 + 7 + 4 + 8 * 0x12 + 4 + 4 + 0xD0 + 8 /*>=0x10037*/;
    const std::size_t total = 5 * slotTableWire + 4 * infoWire;

    Stream s;
    for (std::size_t i = 0; i < total; ++i) s.u8v((u8)(i & 0xFF));

    VfsBytes vb(s.b);
    VfsHandle* h = vb.reader();
    static u8 slotScratch[7952 * 5];
    static u8 infoScratch[756 * 4];
    bool ok = LoadBuildingSlotTables(h, slotScratch, infoScratch, ver);
    long pos = VfsTell(h);
    VfsCloseStream(h);
    CHECK(ok);
    CHECK_EQ((u32)pos, (u32)total);   // consumed exactly the whole table
}

// ===========================================================================
// 5. LoadCharacterSlot — one 516-byte live actor (version 0x1003B), with the
//    hidden-flag and the +140 word mask applied.
// ===========================================================================
TEST(io_save_world, character_slot_roundtrip_1003B) {
    Stream s;
    s.i32v(42);          // index
    u8 expect[516];
    std::memset(expect, 0, sizeof expect);
    u32 tok = 0x2000;
    auto wr = [&](int off, int n) {
        for (int k = 0; k < n; ++k) { u8 v = (u8)((tok + off + k) & 0xFF); s.u8v(v); expect[off + k] = v; }
    };
    wr(5,0x20); wr(44,4); wr(48,4);
    // +140 word: we write a known value, then expect the loader to OR 0x20 (hidden)
    // and finally mask with 0xDFFB.
    { u16 w140 = 0x0001; s.put(&w140, 2); std::memcpy(expect + 140, &w140, 2); }
    wr(368,0x30); wr(304,0x40); wr(56,0x10); wr(72,0xC);
    s.u8v(1);            // hidden flag == 1 -> +140 |= 0x20
    s.u32v(0x99AA);      // person id -> +300
    wr(424,0x40);        // >=0x10013

    // Compute expected +140: (orig | 0x20) & 0xDFFB.
    { u16 v = 0x0001; v |= 0x20; v &= 0xDFFB; std::memcpy(expect + 140, &v, 2); }
    { u32 pid = 0x99AA; std::memcpy(expect + 300, &pid, 4); }

    VfsBytes vb(s.b);
    VfsHandle* h = vb.reader();
    static u8 rec[516];
    std::memset(rec, 0, sizeof rec);
    i32 idx = 0; u32 pid = 0;
    bool ok = LoadCharacterSlot(h, rec, 0x1003B, &idx, &pid);
    VfsCloseStream(h);
    CHECK(ok);
    CHECK_EQ(idx, (i32)42);
    CHECK_EQ(pid, (u32)0x99AA);
    // Verify the serialized fields + the masked +140 word + the +300 person id.
    CHECK(std::memcmp(rec + 5,   expect + 5,   0x20) == 0);
    CHECK(std::memcmp(rec + 44,  expect + 44,  4) == 0);
    CHECK(std::memcmp(rec + 304, expect + 304, 0x40) == 0);
    CHECK(std::memcmp(rec + 300, expect + 300, 4) == 0);
    u16 w140; std::memcpy(&w140, rec + 140, 2);
    u16 e140; std::memcpy(&e140, expect + 140, 2);
    CHECK_EQ((u32)w140, (u32)e140);
}
