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
// 0. RECOVERED-CONSTANT GOLDEN PINS (wave-13 1:1 fidelity audit).
//
// The table loaders' record strides / capacities / counter biases are the core
// recovered 1:1 values of VIBE_Save_LoadGameFile @0x5a7604 and its callees. They
// are otherwise only used IMPLICITLY by the round-trip tests; this test pins the
// exact decompiled values so a drift in a constant fails loudly. Every value is
// sourced from the provenance comments in save_world_load.h / save_person.h —
// NOT invented.
// ===========================================================================
TEST(io_save_world, recovered_strides_and_constants_pinned) {
    using namespace guild::io;
    // VIBE_Save_LoadPersonIndexTable @0x5a7ffc — dword_13CE290, stride 67, 8192.
    CHECK_EQ(kSceneTileStride, 67);
    CHECK_EQ(kSceneTileCapacity, 8192);
    CHECK_EQ((u32)kSceneTileScanBytes, (u32)(67u * 8192u)); // 548864
    CHECK_EQ((u32)kSceneTileScanBytes, 548864u);

    // VIBE_Save_LoadGlobalCounters @0x5a86d0 — word_13C3110, stride 164, 16 recs.
    CHECK_EQ(kBuildCounterStride, 164);
    CHECK_EQ(kBuildCounterCount, 16);

    // VIBE_Save_LoadCityRecords @0x5a8d3c — word_12CE910, stride 536, 768 slots.
    CHECK_EQ(kCityRecStride, 536);
    CHECK_EQ(kCityRecCapacity, 768);
    // Counter biases re-applied on load (+84 dword += 1342, +396 dword += 1468).
    CHECK_EQ((i32)kCityCounterBiasA, 1342);
    CHECK_EQ((i32)kCityCounterBiasB, 1468);

    // VIBE_Save_LoadBuildingSlotTables @0x5aa058 — 5 slot tables (16-byte header +
    // 62 sub-records of 128-byte stride = 7952), 4 city-info recs of 756 bytes.
    CHECK_EQ(kCitySlotTableCount, 5);
    CHECK_EQ(kCitySlotSubCount, 62);
    CHECK_EQ(kCityInfoRecCount, 4);
    CHECK_EQ(16 + kCitySlotSubCount * 128, 7952);   // the in-memory table stride

    // VIBE_Save_LoadCharacterSlot @0x5a96c0 — 0x204 == 516-byte live-actor record.
    CHECK_EQ(kLiveActorRecSize, 516);
    CHECK_EQ(kLiveActorRecSize, 0x204);

    // VIBE_Save_LoadPersonTable @0x5a8190 — object/building array, 169-stride x256.
    CHECK_EQ(kObjStride, 169);
    CHECK_EQ((u32)kObjScanBytes, (u32)(169u * 256u)); // 43264

    // Version gate (VIBE_Save_LoadGameFile @0x5a775a): 0x10026 <= ver <= 0x10045.
    CHECK_EQ((u32)kSaveVersionLoadMin, 0x10026u);
    CHECK_EQ((u32)kSaveVersionLoadMax, 0x10045u);
}

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

// ===========================================================================
// Wave-11 hardening: malformed / truncated / oversized save streams. Each loader
// must fail safely (return false) on a short read and never read/write OOB. The
// in-bounds roundtrips above are unchanged.
//
// NOTE (BEHAVIORAL — needs MCP): LoadPersonIndexTable trusts the file `count` and
// LoadCityRecords trusts each record's leading `marker` as a direct slot index
// into fixed-capacity buffers (sceneTiles: 8192 entries; personBase: 768). A save
// with count > 8192 or marker >= 768 would scatter past the buffer — exactly as
// the original engine's unbounded write into dword_13CE290 / word_12CE910. Whether
// the binary clamps the index is a 1:1 question; these tests deliberately stay at
// or below capacity and exercise the truncation guards instead. Do NOT add a
// clamp without confirming the original had one.
// ===========================================================================

// LoadPersonIndexTable: count of 0 reads nothing and succeeds (empty table).
TEST(io_save_world, person_index_empty_count) {
    Stream s; s.u32v(0);
    VfsBytes vb(s.b);
    VfsHandle* h = vb.reader();
    static u8 tiles[guild::io::kSceneTileScanBytes];
    std::memset(tiles, 0, sizeof tiles);
    u32 count = 0xDEAD;
    bool ok = LoadPersonIndexTable(h, tiles, &count);
    VfsCloseStream(h);
    CHECK(ok);
    CHECK_EQ(count, 0u);
}

// LoadPersonIndexTable: count says 4 but the stream is truncated mid-record ->
// the per-field RD fails and the loader returns false (no OOB read).
TEST(io_save_world, person_index_truncated_midrecord) {
    Stream s;
    s.u32v(4);                 // claims 4 records...
    // ...but supply only ~1.5 records of bytes (record is 51 wire bytes).
    for (int k = 0; k < 51 + 20; ++k) s.u8v((u8)k);
    VfsBytes vb(s.b);
    VfsHandle* h = vb.reader();
    static u8 tiles[guild::io::kSceneTileScanBytes];
    std::memset(tiles, 0, sizeof tiles);
    u32 count = 0;
    bool ok = LoadPersonIndexTable(h, tiles, &count);
    VfsCloseStream(h);
    CHECK(!ok);                // short read -> safe failure
}

// LoadPersonIndexTable: 0-byte stream (cannot even read the count) -> false.
TEST(io_save_world, person_index_zero_byte) {
    Stream s;                  // empty
    VfsBytes vb(s.b);
    VfsHandle* h = vb.reader();
    static u8 tiles[guild::io::kSceneTileScanBytes];
    u32 count = 0;
    bool ok = LoadPersonIndexTable(h, tiles, &count);
    VfsCloseStream(h);
    CHECK(!ok);
}

// LoadPersonIndexTable: capacity-boundary. Writing the very last in-range slot
// (index kSceneTileCapacity-1) must stay inside sceneTiles[] (ASAN watches the
// upper bound). We only build the header + that many records' worth of zero
// bytes; success means the highest write landed at the last valid stride.
TEST(io_save_world, person_index_capacity_boundary) {
    const int cap = guild::io::kSceneTileCapacity;     // 8192
    Stream s;
    s.u32v(cap);                                        // exactly capacity records
    s.zeros((u32)cap * 51u);                            // 51 wire bytes each
    VfsBytes vb(s.b);
    VfsHandle* h = vb.reader();
    static u8 tiles[guild::io::kSceneTileScanBytes];
    std::memset(tiles, 0, sizeof tiles);
    u32 count = 0;
    bool ok = LoadPersonIndexTable(h, tiles, &count);  // last slot == cap-1
    VfsCloseStream(h);
    CHECK(ok);
    CHECK_EQ(count, (u32)cap);
}

// LoadGlobalCounters: truncated stream (fewer than 16 full records) -> false.
TEST(io_save_world, global_counters_truncated) {
    Stream s;
    for (int k = 0; k < 100; ++k) s.u8v((u8)k);   // far short of 16*152
    VfsBytes vb(s.b);
    VfsHandle* h = vb.reader();
    static u8 counters[guild::io::kBuildCounterStride * 16];
    std::memset(counters, 0, sizeof counters);
    bool ok = LoadGlobalCounters(h, counters, 0x1003B);
    VfsCloseStream(h);
    CHECK(!ok);
}

// LoadCityRecords: count=0 -> reads only the preamble, succeeds, no record writes.
TEST(io_save_world, city_records_empty_count) {
    const u32 ver = 0x1003B;
    Stream s;
    s.u16v(0x1234);   // word_63CC5C
    s.i32v(0);        // count = 0
    s.u32v(0); s.u32v(0);              // idA / idB
    for (int i = 0; i < 8; ++i) s.u32v(0);  // handlers (>=0x10017)
    VfsBytes vb(s.b);
    VfsHandle* h = vb.reader();
    static u8 persons[536 * guild::io::kCityRecCapacity];
    std::memset(persons, 0, sizeof persons);
    u16 marker = 0; u32 count = 0xDEAD, a = 0, b = 0, hh[8] = {};
    bool ok = LoadCityRecords(h, persons, ver, &marker, &count, &a, &b, hh);
    VfsCloseStream(h);
    CHECK(ok);
    CHECK_EQ(count, 0u);
    CHECK_EQ((u32)marker, 0x1234u);
}

// LoadCityRecords: count=1 but the stream is truncated mid-record -> false.
TEST(io_save_world, city_records_truncated_midrecord) {
    const u32 ver = 0x1003B;
    Stream s;
    s.u16v(0); s.i32v(1);             // count = 1
    s.u32v(0); s.u32v(0);
    for (int i = 0; i < 8; ++i) s.u32v(0);
    s.u16v(0);                         // record marker = slot 0 ...
    for (int k = 0; k < 12; ++k) s.u8v(0);  // ...then truncate (record is 536-ish)
    VfsBytes vb(s.b);
    VfsHandle* h = vb.reader();
    static u8 persons[536 * guild::io::kCityRecCapacity];
    std::memset(persons, 0, sizeof persons);
    u16 marker = 0; u32 count = 0, a = 0, b = 0, hh[8] = {};
    bool ok = LoadCityRecords(h, persons, ver, &marker, &count, &a, &b, hh);
    VfsCloseStream(h);
    CHECK(!ok);
}

// LoadBuildingSlotTables: truncated stream -> false (a sub-record RD short-reads).
TEST(io_save_world, building_slot_tables_truncated) {
    Stream s;
    for (int k = 0; k < 200; ++k) s.u8v((u8)k);   // far short of the full table
    VfsBytes vb(s.b);
    VfsHandle* h = vb.reader();
    static u8 slotScratch[7952 * 5];
    static u8 infoScratch[756 * 4];
    std::memset(slotScratch, 0, sizeof slotScratch);
    std::memset(infoScratch, 0, sizeof infoScratch);
    bool ok = LoadBuildingSlotTables(h, slotScratch, infoScratch, 0x1003B);
    VfsCloseStream(h);
    CHECK(!ok);
}

// LoadCharacterSlot: header-only / truncated -> false; null args rejected.
TEST(io_save_world, character_slot_truncated_and_null) {
    Stream s; s.i32v(7);   // index only, no body
    VfsBytes vb(s.b);
    VfsHandle* h = vb.reader();
    static u8 rec[516];
    std::memset(rec, 0, sizeof rec);
    i32 idx = 0; u32 pid = 0;
    // null stream / null record rejected up front (before touching the stream).
    CHECK(!LoadCharacterSlot(nullptr, rec, 0x1003B, &idx, &pid));
    CHECK(!LoadCharacterSlot(h, nullptr, 0x1003B, &idx, &pid));
    bool ok = LoadCharacterSlot(h, rec, 0x1003B, &idx, &pid);
    VfsCloseStream(h);
    CHECK(!ok);
}

// ===========================================================================
// 7. RelinkPersonRecordColumns @0x5abb84 (the person-record column slice
//    0x5abbe2..0x5abc4f) — the four-column transform, golden-pinned with
//    constructed link ids (-1 / hit / miss / 0). Recovered from the live MCP
//    decompile (wave-15):
//      +364 (v7[91]) / +368 (v7[92]): id == -1 -> 0 (@0x5abc00/0x5abc15);
//        else GameObject_ResolveEntityById(&col, 0, id, 0) @0x5abe22/0x5abe38,
//        which scans the 169-stride OBJECT/BUILDING array dword_13CE298 (id @+1,
//        == VIBE_Building_FindById's scan): hit keeps the id, miss -> 0. There
//        is NO `id == 0` special case in the binary.
//      +380 (v7[95]): id == -1 -> 0; else He_FindFirstHandlerByFilter(1,1,id)
//        @0x5abe4f, which on the partial .cty path (no He records) returns 0.
//      +388 (v7[97]): 0 unconditionally (@0x5abc3d).
//    Gate (@0x5abb8e): dword_6498E4 (player id) must resolve via
//    Person_FindRecordById, else nothing is relinked (returns false).
// ===========================================================================
TEST(io_save_world, relink_columns_resolve_clear_he_zero) {
    guild::sim::ResetEntityArrays();

    // Live object/building array: one record with id 0x4242 (BuildingFindById
    // hit). The binary resolves any id != -1 against this 169-stride array.
    guild::sim::g_objects[0].alive = 1;
    guild::sim::g_objects[0].id    = 0x4242;
    guild::sim::g_personArrayLoaded = true;

    // Player record (the relink gate): marker live, id 0x77 in the parallel
    // id column so PersonFindRecordById(0x77) resolves.
    guild::sim::g_persons[0].marker = 1;
    guild::sim::g_persons[0].id     = 0x77;
    guild::sim::g_personIds[0]      = 0x77;

    // Two more live person records carrying the link columns under test.
    auto setCols = [](int slot, i32 c364, i32 c368, i32 c380, i32 c388) {
        guild::sim::g_persons[slot].marker = 1;
        guild::sim::g_persons[slot].id     = 0x100 + slot;
        guild::sim::g_personIds[slot]      = 0x100 + slot;
        u8* r = reinterpret_cast<u8*>(&guild::sim::g_persons[slot]);
        std::memcpy(r + 364, &c364, 4);
        std::memcpy(r + 368, &c368, 4);
        std::memcpy(r + 380, &c380, 4);
        std::memcpy(r + 388, &c388, 4);
    };
    // slot 1: +364 = -1 (->0), +368 = hit 0x4242 (kept), +380 = 9 (He miss ->0),
    //         +388 = 5 (-> always 0).
    setCols(1, -1, 0x4242, 9, 5);
    // slot 2: +364 = miss 0xDEAD (->0), +368 = hit 0x4242 (kept), +380 = -1 (->0),
    //         +388 = -1 (-> always 0).
    setCols(2, 0xDEAD, 0x4242, -1, -1);

    // A free slot (marker == -1) must be skipped entirely.
    guild::sim::g_persons[3].marker = -1;
    i32 untouched = 0x1234;
    std::memcpy(reinterpret_cast<u8*>(&guild::sim::g_persons[3]) + 364, &untouched, 4);

    const bool relinked = RelinkPersonRecordColumns(0x77);
    CHECK(relinked);

    auto col = [](int slot, int off) {
        i32 v; std::memcpy(&v,
            reinterpret_cast<u8*>(&guild::sim::g_persons[slot]) + off, 4);
        return v;
    };
    // slot 1
    CHECK_EQ(col(1, 364), 0);          // -1 -> 0
    CHECK_EQ(col(1, 368), 0x4242);     // hit kept
    CHECK_EQ(col(1, 380), 0);          // He miss -> 0
    CHECK_EQ(col(1, 388), 0);          // always 0
    // slot 2
    CHECK_EQ(col(2, 364), 0);          // miss -> 0
    CHECK_EQ(col(2, 368), 0x4242);     // hit kept
    CHECK_EQ(col(2, 380), 0);          // -1 -> 0
    CHECK_EQ(col(2, 388), 0);          // always 0
    // free slot untouched
    CHECK_EQ(col(3, 364), 0x1234);

    // Gate: an unresolvable player id relinks nothing and returns false.
    guild::sim::ResetEntityArrays();
    CHECK(!RelinkPersonRecordColumns(0x999));
}
