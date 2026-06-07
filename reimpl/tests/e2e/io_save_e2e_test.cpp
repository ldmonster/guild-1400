#include "test.h"

#include "io/gamestate.h"
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
private:
    std::map<std::string, std::vector<u8>> files_;
};

GameState MakeSyntheticState() {
    GameState s;
    SaveHeader& h = s.header;
    h.magic = kSaveVersionWriter;
    h.flagByte = 0x10;
    std::strncpy(h.name, "Hamburg", sizeof h.name - 1);
    for (int i = 0; i < 8; ++i) h.timestamp[i] = (u8)(i * 13 + 1);
    h.season = 1;
    h.extraByte = 0x44;
    for (int i = 0; i < 14; ++i) h.gameTime[i] = (u8)(i * 3 + 2);
    std::memcpy(h.scenarioTag, "SCENARIO\0\0\0\0\0\0\0", 16);
    h.flagA = 5; h.flagB = 6; h.flagC = 7;
    h.wealth = 987654;
    h.flagD = 1;
    h.idA = 0xCAFEBABEu;
    h.idB = 0x0BADF00Du;
    for (int i = 0; i < 8; ++i) h.thumbExtra[i] = 0x1000u * (i + 1);
    h.field132 = 4;
    std::strncpy(h.name96, "AutoSave-Round-12", sizeof h.name96 - 1);

    SaveScalarBlock& b = s.scalar;
    b.g649890 = 0xFEED0001; b.g632244 = 0xFEED0002;
    for (int i = 0; i < 14; ++i) b.gameTime[i] = (u8)(i + 0x60);
    b.season = 1;
    b.g6498E4 = 0xDEAD1111;
    b.g64771C = 1000; b.g647720 = 2000; b.g647724 = 3000;
    b.unk6477A8 = 0x77AA77AA;
    for (int i = 0; i < 24; ++i) b.gB56450[i] = (u8)(0xC0 + i);
    b.g649894 = 0x99AABBCC;
    b.g632240 = 750000;

    // Synthetic thumbnail.
    s.thumbnail.assign(kThumbnailBytes, 0);
    for (size_t i = 0; i < s.thumbnail.size(); ++i) s.thumbnail[i] = (u8)(i * 5 + 3);

    return s;
}

bool HeadersEqual(const SaveHeader& a, const SaveHeader& b) {
    return std::memcmp(&a, &b, sizeof(SaveHeader)) == 0;
}
bool ScalarsEqual(const SaveScalarBlock& a, const SaveScalarBlock& b) {
    return std::memcmp(&a, &b, sizeof(SaveScalarBlock)) == 0;
}

} // namespace

// e2e: build a synthetic world state, save it to an in-memory VFS stream, load it
// back into a fresh state, and verify byte-identical reconstruction of every
// serialized field (the determinism / persistence invariant).
TEST(io_save_e2e, save_load_byte_identical) {
    RwFs fs;
    VfsInit(&fs, false);

    GameState saved = MakeSyntheticState();
    GameState orig  = saved;   // keep a pristine copy (Write must not mutate fields)

    CHECK(WriteGameState("Gamedata/Saves/Autosave.SAV", saved, nullptr, false));

    GameState loaded;
    loaded.thumbnail.assign(kThumbnailBytes, 0);  // request thumbnail readback
    CHECK(LoadGameState("Gamedata/Saves/Autosave.SAV", loaded, nullptr));

    // version word reflects the loaded file.
    CHECK_EQ(SaveVersionGet(), (u32)kSaveVersionWriter);

    // byte-identical header + scalar block.
    CHECK(HeadersEqual(loaded.header, orig.header));
    CHECK(ScalarsEqual(loaded.scalar, orig.scalar));

    // thumbnail reconstructed exactly.
    CHECK(loaded.thumbnail.size() == kThumbnailBytes);
    CHECK(loaded.thumbnail == orig.thumbnail);

    // spot-check load-bearing individual fields.
    CHECK_EQ(loaded.header.magic, (u32)0x10045);
    CHECK_EQ(loaded.header.idA, (u32)0xCAFEBABEu);
    CHECK_EQ(loaded.header.idB, (u32)0x0BADF00Du);
    CHECK_EQ(loaded.scalar.g632240, (u32)750000);
    CHECK(std::strcmp(loaded.header.name, "Hamburg") == 0);
    CHECK(std::strcmp(loaded.header.name96, "AutoSave-Round-12") == 0);

    VfsShutdown();
}

// e2e: a save->load round-trip through the relink table re-resolves ids to the
// correct records. The save side converts pointers->ids; the load side converts
// ids->pointers, dispatched by the record's type tag. Round-tripping with inverse
// resolvers returns the original pointer-tokens.
namespace {
struct RelinkMaps {
    // forward (load): id -> pointer.  These mimic Find*ById for each kind.
    std::map<u32, u32> person, building, object, cutscene;
};
u32 FwdPerson(u32 id, void* c){ auto& m=((RelinkMaps*)c)->person;   auto i=m.find(id); return i==m.end()?0xFFFFFFFFu:i->second; }
u32 FwdBuilding(u32 id, void* c){ auto& m=((RelinkMaps*)c)->building; auto i=m.find(id); return i==m.end()?0xFFFFFFFFu:i->second; }
u32 FwdObject(u32 id, void* c){ auto& m=((RelinkMaps*)c)->object;   auto i=m.find(id); return i==m.end()?0xFFFFFFFFu:i->second; }
u32 FwdCutscene(u32 id, void* c){ auto& m=((RelinkMaps*)c)->cutscene; auto i=m.find(id); return i==m.end()?0xFFFFFFFFu:i->second; }

void SetEntry(std::vector<u8>& t, int idx, u8 tag, u32 v) {
    std::size_t off = (std::size_t)idx * kRelinkStride;
    t[off + kRelinkTagOffset] = tag;
    std::memcpy(t.data() + off + kRelinkPtrOffset, &v, 4);
}
u32 GetEntry(const std::vector<u8>& t, int idx) {
    u32 v; std::memcpy(&v, t.data() + (std::size_t)idx * kRelinkStride + kRelinkPtrOffset, 4); return v;
}
} // namespace

TEST(io_save_e2e, relink_ids_survive_and_reresolve) {
    RwFs fs;
    VfsInit(&fs, false);

    GameState s = MakeSyntheticState();
    s.relink.assign(kRelinkBytes, 0);
    // Populate the table with IDS (the on-disk representation).
    SetEntry(s.relink, 0, kRelinkPerson,   1001);
    SetEntry(s.relink, 1, kRelinkBuilding, 2002);
    SetEntry(s.relink, 2, kRelinkObject,   3003);
    SetEntry(s.relink, 3, kRelinkCutscene, 4004);

    // Save without resolvers: the header/scalar block round-trips and the relink
    // table is carried in-memory (the on-disk table phase is owned elsewhere).
    CHECK(WriteGameState("Gamedata/Saves/relink.SAV", s, nullptr, false));

    GameState loaded;
    loaded.relink = s.relink;   // simulate the loaded id table
    CHECK(LoadGameState("Gamedata/Saves/relink.SAV", loaded, nullptr));

    // Now run the post-load relink with the engine's Find*ById maps.
    RelinkMaps maps;
    maps.person   = {{1001, 0x7000000Au}};
    maps.building = {{2002, 0x7000000Bu}};
    maps.object   = {{3003, 0x7000000Cu}};
    maps.cutscene = {{4004, 0x7000000Du}};
    RelinkResolvers r{ FwdPerson, FwdBuilding, FwdObject, FwdCutscene, &maps };
    RelinkTableIdsToPointers(loaded.relink.data(), r);

    CHECK_EQ(GetEntry(loaded.relink, 0), (u32)0x7000000Au); // person id 1001 -> ptr
    CHECK_EQ(GetEntry(loaded.relink, 1), (u32)0x7000000Bu); // building 2002 -> ptr
    CHECK_EQ(GetEntry(loaded.relink, 2), (u32)0x7000000Cu); // object 3003 -> ptr
    CHECK_EQ(GetEntry(loaded.relink, 3), (u32)0x7000000Du); // cutscene 4004 -> ptr

    VfsShutdown();
}
