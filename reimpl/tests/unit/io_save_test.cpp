#include "test.h"

#include "io/save.h"
#include "io/worldio.h"
#include "io/vfs.h"
#include "io/file.h"
#include "shim/IFileSystem.h"

#include <cstdint>
#include <cstddef>
#include <cstring>
#include <map>
#include <memory>
#include <string>
#include <vector>

using namespace guild::io;
using guild::u8;
using guild::u32;

// ---------------------------------------------------------------------------
// Writable in-memory IFileSystem: open("wb") starts a fresh growable buffer;
// open("rb") returns a reader over the previously-written bytes. Files persist in
// the filesystem map across open/close so a save can be re-opened and loaded.
namespace {

class RwFile : public guild::shim::IFile {
public:
    explicit RwFile(std::vector<u8>* store, bool writing)
        : store_(store), writing_(writing) {
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
        auto& store = files_[path];           // creates on write or read-after-write
        if (!writing && files_.find(path) == files_.end())
            return nullptr;
        return new RwFile(&store, writing);
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

// Fill a header with deterministic, distinguishable field values.
SaveHeader MakeHeader() {
    SaveHeader h{};
    h.magic = kSaveVersionWriter;
    h.flagByte = 0x11;
    std::strncpy(h.name, "Augsburg", sizeof h.name - 1);
    for (int i = 0; i < 8; ++i) h.timestamp[i] = (u8)(0xA0 + i);
    h.season = 3;
    h.extraByte = 0x55;
    for (int i = 0; i < 14; ++i) h.gameTime[i] = (u8)(0x10 + i);
    std::memcpy(h.scenarioTag, "SCENARIO\0\0\0\0\0\0\0", 16);
    h.flagA = 1; h.flagB = 2; h.flagC = 3;
    h.wealth = 123456;
    h.flagD = 7;
    h.idA = 0x11112222;
    h.idB = 0x33334444;
    for (int i = 0; i < 8; ++i) h.thumbExtra[i] = 0xDEAD0000u + i;
    h.field132 = 9;
    std::strncpy(h.name96, "MySaveSlot", sizeof h.name96 - 1);
    return h;
}

SaveScalarBlock MakeScalar() {
    SaveScalarBlock b{};
    b.g649890 = 0x01020304;
    b.g632244 = 0x05060708;
    for (int i = 0; i < 14; ++i) b.gameTime[i] = (u8)(0x40 + i);
    b.season = 2;
    b.g6498E4 = 0xAABBCCDD;
    b.g64771C = 11; b.g647720 = 22; b.g647724 = 33;
    b.unk6477A8 = 0x99887766;
    for (int i = 0; i < 24; ++i) b.gB56450[i] = (u8)(0x80 + i);
    b.g649894 = 0x12345678;
    b.g632240 = 555000;
    return b;
}

} // namespace

// --- version table ---------------------------------------------------------
TEST(io_save, version_constants) {
    CHECK_EQ((u32)kSaveVersionCurrent, (u32)0x10045);
    CHECK_EQ((u32)kSaveVersionWriter,  (u32)0x10045);
    CHECK_EQ((u32)kSaveVersionLoadMin, (u32)0x10026);
    CHECK_EQ((u32)kSaveVersionLoadMax, (u32)0x10045);
    CHECK_EQ((u32)kSaveVersionMin,     (u32)0x10025);
    CHECK_EQ((u32)kThumbnailBytes,     (u32)0xE100);
}

// --- recovered header layout (offsets must match the +0xNN reads) ----------
TEST(io_save, header_field_offsets) {
    CHECK_EQ(offsetof(SaveHeader, magic),       (size_t)0x00);
    CHECK_EQ(offsetof(SaveHeader, flagByte),    (size_t)0x04);
    CHECK_EQ(offsetof(SaveHeader, name),        (size_t)0x05);
    CHECK_EQ(offsetof(SaveHeader, timestamp),   (size_t)0x28);
    CHECK_EQ(offsetof(SaveHeader, season),      (size_t)0x30);
    CHECK_EQ(offsetof(SaveHeader, extraByte),   (size_t)0x31);
    CHECK_EQ(offsetof(SaveHeader, gameTime),    (size_t)0x32);
    CHECK_EQ(offsetof(SaveHeader, scenarioTag), (size_t)0x40);
    CHECK_EQ(offsetof(SaveHeader, flagA),       (size_t)0x50);
    CHECK_EQ(offsetof(SaveHeader, wealth),      (size_t)0x54);
    CHECK_EQ(offsetof(SaveHeader, idA),         (size_t)0x5C);
    CHECK_EQ(offsetof(SaveHeader, idB),         (size_t)0x60);
    CHECK_EQ(offsetof(SaveHeader, thumbExtra),  (size_t)0x64);
    CHECK_EQ(offsetof(SaveHeader, field132),    (size_t)0x84);
    CHECK_EQ(offsetof(SaveHeader, name96),      (size_t)0x88);
}

// --- header read/write roundtrip + on-stream magic/version offset ----------
TEST(io_save, header_roundtrip_and_magic_offset) {
    RwFs fs;
    VfsInit(&fs, false);

    std::vector<u8> thumb(kThumbnailBytes);
    for (size_t i = 0; i < thumb.size(); ++i) thumb[i] = (u8)(i * 7 + 1);

    SaveHeader src = MakeHeader();
    VfsHandle* w = VfsOpenFile("hdr.SAV", "wb");
    CHECK(w != nullptr);
    CHECK(SaveWriteScenarioBlock(w, src, thumb.data()));
    VfsCloseStream(w);

    // magic sits at byte 0 of the stream; version is the writer's current version.
    const std::vector<u8>* raw = fs.bytes("hdr.SAV");
    CHECK(raw != nullptr);
    CHECK(raw->size() >= 4);
    u32 onDisk = (*raw)[0] | ((*raw)[1] << 8) | ((*raw)[2] << 16) | ((u32)(*raw)[3] << 24);
    CHECK_EQ(onDisk, (u32)kSaveVersionWriter);
    // flag byte immediately follows the 4-byte magic.
    CHECK_EQ((u32)(*raw)[4], (u32)src.flagByte);

    SaveHeader dst{};
    std::vector<u8> thumbBack(kThumbnailBytes, 0);
    VfsHandle* r = VfsOpenFile("hdr.SAV", "rb");
    CHECK(r != nullptr);
    CHECK(SaveLoadHeaderAndThumbnail(r, dst, thumbBack.data()));
    VfsCloseStream(r);

    CHECK_EQ(dst.magic, src.magic);
    CHECK_EQ(SaveVersionGet(), (u32)kSaveVersionWriter);
    CHECK_EQ(dst.flagByte, src.flagByte);
    CHECK(std::memcmp(dst.name, src.name, sizeof src.name) == 0);
    CHECK(std::memcmp(dst.timestamp, src.timestamp, 8) == 0);
    CHECK_EQ(dst.season, src.season);
    CHECK_EQ(dst.extraByte, src.extraByte);
    CHECK(std::memcmp(dst.gameTime, src.gameTime, 14) == 0);
    CHECK(std::memcmp(dst.scenarioTag, src.scenarioTag, 16) == 0);
    CHECK_EQ(dst.wealth, src.wealth);
    CHECK_EQ(dst.idA, src.idA);
    CHECK_EQ(dst.idB, src.idB);
    for (int i = 0; i < 8; ++i) CHECK_EQ(dst.thumbExtra[i], src.thumbExtra[i]);
    CHECK_EQ(dst.field132, src.field132);
    CHECK(std::memcmp(dst.name96, src.name96, sizeof src.name96) == 0);
    CHECK(thumbBack == thumb);

    VfsShutdown();
}

// --- version-gated field reading: v0x10025 vs v0x10045 parse different sets -
// A minimal-version stream omits the gametime/scenario/id/thumbnail block and the
// >=0x10038 / >=0x10039 trailers; a current-version stream includes them.
TEST(io_save, version_gated_field_reading) {
    RwFs fs;
    VfsInit(&fs, false);

    // -- build a v0x10025 stream by hand (only the always-present prefix). At this
    //    version: magic, flag, name[32], ts[8], season; NO extraByte (<0x10033),
    //    NO gametime/scenario block (<0x10028), field132 defaults to 2 (<0x10038),
    //    name96 = "Savegame" (<0x10039).
    {
        std::vector<u8> s;
        auto put32 = [&](u32 v){ s.push_back(v); s.push_back(v>>8); s.push_back(v>>16); s.push_back(v>>24); };
        put32(0x10025);                 // magic / version
        s.push_back(0x22);              // flag byte
        for (int i = 0; i < 32; ++i) s.push_back((u8)('a' + (i % 26))); // name
        for (int i = 0; i < 8; ++i)  s.push_back((u8)(i + 1));          // timestamp
        s.push_back(4);                 // season
        // nothing more for 0x10025
        VfsHandle* w = VfsOpenFile("v25.SAV", "wb");
        CHECK(w != nullptr);
        CHECK_EQ(VfsWriteStream(s.data(), 1, w, (u32)s.size()), (u32)s.size());
        VfsCloseStream(w);

        SaveHeader h{};
        VfsHandle* r = VfsOpenFile("v25.SAV", "rb");
        CHECK(r != nullptr);
        CHECK(SaveLoadHeaderAndThumbnail(r, h, nullptr));
        VfsCloseStream(r);

        CHECK_EQ(h.magic, (u32)0x10025);
        CHECK_EQ(SaveVersionGet(), (u32)0x10025);
        CHECK_EQ(h.season, (u8)4);
        // gates NOT taken: ids defaulted; field132 defaulted to 2; name96="Savegame".
        CHECK_EQ(h.field132, (u32)2);
        CHECK(std::strcmp(h.name96, "Savegame") == 0);
    }

    // -- v0x10045 stream via the writer (all gates taken). Re-read and confirm the
    //    extra fields ARE present (idA/idB non-default, field132 from file).
    {
        SaveHeader src = MakeHeader();
        std::vector<u8> thumb(kThumbnailBytes, 0x5A);
        VfsHandle* w = VfsOpenFile("v45.SAV", "wb");
        CHECK(w != nullptr);
        CHECK(SaveWriteScenarioBlock(w, src, thumb.data()));
        VfsCloseStream(w);

        SaveHeader h{};
        std::vector<u8> tb(kThumbnailBytes, 0);
        VfsHandle* r = VfsOpenFile("v45.SAV", "rb");
        CHECK(r != nullptr);
        CHECK(SaveLoadHeaderAndThumbnail(r, h, tb.data()));
        VfsCloseStream(r);

        CHECK_EQ(h.magic, (u32)0x10045);
        CHECK_EQ(h.idA, src.idA);
        CHECK_EQ(h.idB, src.idB);
        CHECK_EQ(h.extraByte, src.extraByte);
        CHECK_EQ(h.field132, src.field132);   // read from file, not defaulted
        CHECK(std::strcmp(h.name96, src.name96) == 0);
        CHECK(tb == thumb);
    }

    VfsShutdown();
}

// --- scalar block roundtrip with version gates -----------------------------
TEST(io_save, scalar_block_roundtrip) {
    RwFs fs;
    VfsInit(&fs, false);

    SaveScalarBlock src = MakeScalar();
    SaveVersionSet(kSaveVersionWriter);   // writer always at current version

    VfsHandle* w = VfsOpenFile("scal.SAV", "wb");
    CHECK(w != nullptr);
    CHECK(SaveWriteScalarBlock(w, src));
    VfsCloseStream(w);

    SaveScalarBlock dst{};
    SaveVersionSet(kSaveVersionWriter);   // load gates keyed off the version word
    VfsHandle* r = VfsOpenFile("scal.SAV", "rb");
    CHECK(r != nullptr);
    CHECK(SaveLoadScalarBlock(r, dst));
    VfsCloseStream(r);

    CHECK_EQ(dst.g649890, src.g649890);
    CHECK_EQ(dst.g632244, src.g632244);
    CHECK(std::memcmp(dst.gameTime, src.gameTime, 14) == 0);
    CHECK_EQ(dst.season, src.season);
    CHECK_EQ(dst.g6498E4, src.g6498E4);
    CHECK_EQ(dst.g64771C, src.g64771C);
    CHECK_EQ(dst.g647720, src.g647720);
    CHECK_EQ(dst.g647724, src.g647724);
    CHECK_EQ(dst.unk6477A8, src.unk6477A8);
    CHECK(std::memcmp(dst.gB56450, src.gB56450, 24) == 0);
    CHECK_EQ(dst.g649894, src.g649894);
    CHECK_EQ(dst.g632240, src.g632240);

    VfsShutdown();
}

// --- scalar block: <0x1003D defaults g632240 to 1000000 (gate not taken) ---
TEST(io_save, scalar_block_old_version_default) {
    RwFs fs;
    VfsInit(&fs, false);

    // Write a stream at an old version that lacks g649894 (<0x10030) and g632240
    // (<0x1003D) but has unk6477A8 (>=0x10022). We craft the bytes directly.
    std::vector<u8> s;
    auto put32 = [&](u32 v){ s.push_back(v); s.push_back(v>>8); s.push_back(v>>16); s.push_back(v>>24); };
    put32(0xAAAA0001);  // g649890
    put32(0xAAAA0002);  // g632244
    for (int i = 0; i < 14; ++i) s.push_back((u8)i);  // gameTime
    s.push_back(9);     // season
    put32(0xAAAA0003);  // g6498E4
    put32(0xAAAA0004);  // g64771C
    put32(0xAAAA0005);  // g647720
    put32(0xAAAA0006);  // g647724
    put32(0xAAAA0007);  // unk6477A8 (>=0x10022)
    for (int i = 0; i < 24; ++i) s.push_back((u8)(i + 100));  // B56450
    // no g649894 (<0x10030), no g632240 (<0x1003D)

    VfsHandle* w = VfsOpenFile("old.SAV", "wb");
    CHECK(w != nullptr);
    CHECK_EQ(VfsWriteStream(s.data(), 1, w, (u32)s.size()), (u32)s.size());
    VfsCloseStream(w);

    SaveScalarBlock dst{};
    SaveVersionSet(0x10022);   // gates: unk6477A8 yes; 649894 no; 632240 no
    VfsHandle* r = VfsOpenFile("old.SAV", "rb");
    CHECK(r != nullptr);
    CHECK(SaveLoadScalarBlock(r, dst));
    VfsCloseStream(r);

    CHECK_EQ(dst.g649890, (u32)0xAAAA0001);
    CHECK_EQ(dst.unk6477A8, (u32)0xAAAA0007);
    CHECK_EQ(dst.g632240, (u32)1000000);   // defaulted, not read

    VfsShutdown();
}

// --- pointer<->id relink: ids survive save->load and re-resolve -------------
// Build a relink table with one entry of each tagged kind, run the id->pointer
// resolver, and verify each slot resolves through the right type-specific map.
namespace {
struct ResolveCtx {
    std::map<u32, u32> person, building, object, cutscene;
};
u32 ResPerson(u32 id, void* c){ auto& m=((ResolveCtx*)c)->person;  auto i=m.find(id); return i==m.end()?0xFFFFFFFFu:i->second; }
u32 ResBuilding(u32 id, void* c){ auto& m=((ResolveCtx*)c)->building; auto i=m.find(id); return i==m.end()?0xFFFFFFFFu:i->second; }
u32 ResObject(u32 id, void* c){ auto& m=((ResolveCtx*)c)->object;   auto i=m.find(id); return i==m.end()?0xFFFFFFFFu:i->second; }
u32 ResCutscene(u32 id, void* c){ auto& m=((ResolveCtx*)c)->cutscene; auto i=m.find(id); return i==m.end()?0xFFFFFFFFu:i->second; }

void SetEntry(std::vector<u8>& t, int idx, u8 tag, u32 idOrPtr) {
    std::size_t off = (std::size_t)idx * kRelinkStride;
    t[off + kRelinkTagOffset] = tag;
    std::memcpy(t.data() + off + kRelinkPtrOffset, &idOrPtr, 4);
}
u32 GetEntry(const std::vector<u8>& t, int idx) {
    u32 v; std::memcpy(&v, t.data() + (std::size_t)idx * kRelinkStride + kRelinkPtrOffset, 4); return v;
}
} // namespace

TEST(io_save, relink_table_dispatch) {
    CHECK_EQ((u32)kRelinkStride, (u32)10);
    CHECK_EQ((u32)kRelinkBytes, (u32)327680);
    CHECK_EQ((int)kRelinkEntryCount, 32768);

    std::vector<u8> table(kRelinkBytes, 0);
    // ids stored in the table after save; resolvers map id -> live pointer-token.
    SetEntry(table, 0, kRelinkPerson,   100);
    SetEntry(table, 1, kRelinkBuilding, 200);
    SetEntry(table, 2, kRelinkObject,   300);
    SetEntry(table, 3, kRelinkCutscene, 400);
    SetEntry(table, 4, kRelinkPerson,   0);     // zero slot must be skipped
    SetEntry(table, 5, 77 /*unknown tag*/, 999); // untouched

    ResolveCtx ctx;
    ctx.person   = {{100, 0xAAAA0000u}};
    ctx.building = {{200, 0xBBBB0000u}};
    ctx.object   = {{300, 0xCCCC0000u}};
    ctx.cutscene = {{400, 0xDDDD0000u}};

    RelinkResolvers r{ ResPerson, ResBuilding, ResObject, ResCutscene, &ctx };
    RelinkTableIdsToPointers(table.data(), r);

    CHECK_EQ(GetEntry(table, 0), (u32)0xAAAA0000u);
    CHECK_EQ(GetEntry(table, 1), (u32)0xBBBB0000u);
    CHECK_EQ(GetEntry(table, 2), (u32)0xCCCC0000u);
    CHECK_EQ(GetEntry(table, 3), (u32)0xDDDD0000u);
    CHECK_EQ(GetEntry(table, 4), (u32)0);        // skipped (was 0)
    CHECK_EQ(GetEntry(table, 5), (u32)999);      // unknown tag untouched
}

// --- worldio scene-chunk tag gate ------------------------------------------
TEST(io_save, worldio_scene_tag_gate) {
    CHECK(WorldIoSceneTagValid(kSceneTagWriter));     // 980156603 passes
    CHECK(WorldIoSceneTagValid(kSceneTagMinVersion)); // exactly at min passes
    CHECK(!WorldIoSceneTagValid(kSceneTagMinVersion - 1)); // below min fails
    CHECK(!WorldIoSceneTagValid(0x3A6E00BBu));         // wrong high half fails
    CHECK(!WorldIoSceneTagValid(0));

    RwFs fs;
    VfsInit(&fs, false);
    VfsHandle* w = VfsOpenFile("scene.cty", "wb");
    CHECK(w != nullptr);
    CHECK(WorldIoWriteSceneHeader(w));
    VfsCloseStream(w);

    u32 tag = 0;
    VfsHandle* r = VfsOpenFile("scene.cty", "rb");
    CHECK(r != nullptr);
    CHECK(WorldIoReadSceneHeader(r, &tag));
    VfsCloseStream(r);
    CHECK_EQ(tag, (u32)kSceneTagWriter);

    VfsShutdown();
}

// --- Bio primitive wire format (raw little-endian) -------------------------
TEST(io_save, bio_primitives_wire_format) {
    RwFs fs;
    VfsInit(&fs, false);

    VfsHandle* w = VfsOpenFile("bio.dat", "wb");
    CHECK(w != nullptr);
    CHECK(BioWriteDword(w, 0x04030201));
    CHECK(BioWriteByte(w, 0xEF));
    CHECK(BioWriteDwordPair(w, 0x08070605, 0xDEADBEEF)); // second arg ignored
    VfsCloseStream(w);

    const std::vector<u8>* raw = fs.bytes("bio.dat");
    CHECK(raw != nullptr);
    CHECK_EQ((u32)raw->size(), (u32)9);   // 4 + 1 + 4 (NOT 4+1+8)
    CHECK_EQ((u32)(*raw)[0], (u32)0x01);
    CHECK_EQ((u32)(*raw)[3], (u32)0x04);
    CHECK_EQ((u32)(*raw)[4], (u32)0xEF);
    CHECK_EQ((u32)(*raw)[5], (u32)0x05);
    CHECK_EQ((u32)(*raw)[8], (u32)0x08);

    VfsHandle* r = VfsOpenFile("bio.dat", "rb");
    CHECK(r != nullptr);
    u32 d = 0; u8 b = 0;
    CHECK(BioReadDword(r, &d)); CHECK_EQ(d, (u32)0x04030201);
    CHECK(BioReadByte(r, &b));  CHECK_EQ((u32)b, (u32)0xEF);
    CHECK(BioReadDword(r, &d)); CHECK_EQ(d, (u32)0x08070605);
    VfsCloseStream(r);

    VfsShutdown();
}

// ===========================================================================
// Wave-11 hardening: malformed / truncated / oversized header+scalar streams.
// Every loader must fail safely (return false) on a short/degenerate stream and
// never read past the buffer. Valid-asset roundtrips above are unchanged.
// ===========================================================================
namespace {
// Write a raw byte buffer to `path` through the VFS (caller has VfsInit'd).
void WriteRawSave(const char* path, const std::vector<u8>& s) {
    VfsHandle* w = VfsOpenFile(path, "wb");
    CHECK(w != nullptr);
    if (w) {
        if (!s.empty())
            CHECK_EQ(VfsWriteStream(s.data(), 1, w, (u32)s.size()), (u32)s.size());
        VfsCloseStream(w);
    }
}
} // namespace

// 0-byte save: SaveLoadHeaderAndThumbnail can't even read the magic -> false.
TEST(io_save, malformed_zero_byte_header) {
    RwFs fs; VfsInit(&fs, false);
    WriteRawSave("z.SAV", {});
    SaveHeader h{};
    VfsHandle* r = VfsOpenFile("z.SAV", "rb");
    CHECK(r != nullptr);
    CHECK(!SaveLoadHeaderAndThumbnail(r, h, nullptr));
    VfsCloseStream(r);
    VfsShutdown();
}

// 1-byte and header-magic-only saves: the magic is partially / fully read but the
// subsequent fixed fields short-read -> false (no OOB).
TEST(io_save, malformed_tiny_header) {
    RwFs fs; VfsInit(&fs, false);
    // 1 byte: ReadExact(&magic,4) fails.
    WriteRawSave("one.SAV", std::vector<u8>{0x45});
    { SaveHeader h{}; VfsHandle* r = VfsOpenFile("one.SAV", "rb");
      CHECK(!SaveLoadHeaderAndThumbnail(r, h, nullptr)); VfsCloseStream(r); }
    // 4 bytes: magic reads, flag byte short-reads.
    WriteRawSave("four.SAV", std::vector<u8>{0x45,0x01,0x01,0x00});
    { SaveHeader h{}; VfsHandle* r = VfsOpenFile("four.SAV", "rb");
      CHECK(!SaveLoadHeaderAndThumbnail(r, h, nullptr)); VfsCloseStream(r); }
    VfsShutdown();
}

// Header truncated mid-thumbnail: a valid v0x10045 header prefix then the stream
// stops partway through the 0xE100 thumbnail. The thumbnail ReadExact fails.
TEST(io_save, malformed_truncated_thumbnail) {
    RwFs fs; VfsInit(&fs, false);
    // Write a real header+thumbnail, then physically truncate the on-disk bytes.
    SaveHeader src = MakeHeader();
    std::vector<u8> thumb(kThumbnailBytes, 0x5A);
    VfsHandle* w = VfsOpenFile("tt.SAV", "wb");
    CHECK(SaveWriteScenarioBlock(w, src, thumb.data()));
    VfsCloseStream(w);
    const std::vector<u8>* raw = fs.bytes("tt.SAV");
    CHECK(raw != nullptr);
    // Re-write only the first ~half (lands inside the thumbnail body).
    std::vector<u8> cut(raw->begin(), raw->begin() + raw->size() / 2);
    WriteRawSave("ttc.SAV", cut);
    SaveHeader h{};
    std::vector<u8> thumbBack(kThumbnailBytes, 0);
    VfsHandle* r = VfsOpenFile("ttc.SAV", "rb");
    CHECK(!SaveLoadHeaderAndThumbnail(r, h, thumbBack.data()));
    VfsCloseStream(r);
    VfsShutdown();
}

// Version below the floor (<0x10025): the LABEL_9 floor check rejects it. We build
// a stream whose always-present prefix parses but whose magic is too old.
TEST(io_save, malformed_version_below_floor) {
    RwFs fs; VfsInit(&fs, false);
    std::vector<u8> s;
    auto put32 = [&](u32 v){ s.push_back(v); s.push_back(v>>8); s.push_back(v>>16); s.push_back(v>>24); };
    put32(0x10000);                  // magic well below the 0x10025 floor
    s.push_back(0x00);               // flag byte
    for (int i = 0; i < 32; ++i) s.push_back(0); // name
    for (int i = 0; i < 8; ++i)  s.push_back(0); // timestamp
    s.push_back(0);                  // season
    WriteRawSave("old.SAV", s);
    SaveHeader h{};
    VfsHandle* r = VfsOpenFile("old.SAV", "rb");
    CHECK(!SaveLoadHeaderAndThumbnail(r, h, nullptr));  // floor check -> false
    VfsCloseStream(r);
    VfsShutdown();
}

// SaveLoadScalarBlock on a 0-byte / truncated stream -> false (no OOB).
TEST(io_save, malformed_truncated_scalar) {
    RwFs fs; VfsInit(&fs, false);
    SaveVersionSet(0x1003B);    // a defined, in-range version for the scalar gates
    WriteRawSave("sc.SAV", std::vector<u8>{0x01,0x02,0x03});  // a few bytes only
    SaveScalarBlock sb{};
    VfsHandle* r = VfsOpenFile("sc.SAV", "rb");
    CHECK(r != nullptr);
    CHECK(!SaveLoadScalarBlock(r, sb));
    VfsCloseStream(r);
    VfsShutdown();
}
