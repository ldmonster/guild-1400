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
// In-memory writable filesystem (one growable buffer per path).
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

void Fill(u8* p, std::size_t n, u8 seed) {
    for (std::size_t i = 0; i < n; ++i) p[i] = (u8)(seed + i * 37 + (i >> 4) * 11);
}

// A populated "world": small arrays for each table, sized to a handful of records.
struct World {
    static constexpr u32 kPersons = 4;   // 169-byte object records
    static constexpr u32 kScene   = 3;   // 536-byte person/scene records
    static constexpr u32 kBuild   = kBuildCapacity; // 16
    static constexpr u32 kObjMain = 3;
    static constexpr u32 kObjLight = 2;
    static constexpr u32 kObjMark = 2;
    static constexpr u32 kAmtLive = 2;
    static constexpr u32 kAction = 8;

    std::vector<u8> persons{std::vector<u8>(kObjStride * kPersons, 0)};
    std::vector<u8> scene{std::vector<u8>(kPersStride * kScene, 0)};
    std::vector<u8> build{std::vector<u8>(kBuildStride * kBuild, 0)};
    std::vector<u8> objMain{std::vector<u8>(kObjMainStride * kObjMain, 0)};
    std::vector<u8> objLight{std::vector<u8>(kObjLightStride * kObjLight, 0)};
    std::vector<u8> objMark{std::vector<u8>(kObjMarkStride * kObjMark, 0)};
    std::vector<u8> amt{std::vector<u8>(kAmtStride * kAmtCapacity, 0)};
    std::vector<u8> action{std::vector<u8>(kActionStride * kAction, 0)};
    HotkeyTable hotkeys{};
    GameGlobalsBlock globals{};
    HistoryHeader history{};
    std::vector<u8> carts{std::vector<u8>(kCartCount * kCartStride, 0)};
    GameStateHeaderPreamble preamble{};
    PersonSceneLinks sceneLinks[kScene]{};

    void Populate() {
        for (u32 i = 0; i < kPersons; ++i) {
            u8* r = persons.data() + i * kObjStride;
            Fill(r, kObjStride, (u8)(0x21 + i));
            r[0] = (u8)(2 + i); // alive, not kind-30
        }
        for (u32 i = 0; i < kScene; ++i) {
            u8* r = scene.data() + i * kPersStride;
            Fill(r, kPersStride, (u8)(0x55 + i));
            std::int32_t cA = 9000 + i, cB = 12000 + i;
            std::memcpy(r + 84, &cA, 4); std::memcpy(r + 396, &cB, 4);
            sceneLinks[i] = PersonSceneLinks{ 100u + i, 200u + i, 300u + i, 400u + i };
        }
        Fill(build.data(), build.size(), 0x12);
        for (u32 i = 0; i < kObjMain; ++i) {
            u8* r = objMain.data() + i * kObjMainStride;
            Fill(r, kObjMainStride, (u8)(0x10 + i));
            std::memset(r + 124, 0, 8); // no blob
        }
        Fill(objLight.data(), objLight.size(), 0x61);
        Fill(objMark.data(), objMark.size(), 0x71);
        for (u32 i = 0; i < kAmtCapacity; ++i) {
            std::int32_t m1 = -1; std::memcpy(amt.data() + i * kAmtStride, &m1, 4);
        }
        for (u32 i = 0; i < kAmtLive; ++i) {
            u8* r = amt.data() + i * kAmtStride;
            Fill(r, kAmtStride, (u8)(0x20 + i));
            std::int32_t id = 500 + i; std::memcpy(r, &id, 4);
        }
        for (u32 i = 0; i < kAction; ++i)
            Fill(action.data() + i * kActionStride, kActionStride, (u8)(0x40 + i));
        // index-encoded intra-pool links: record 0.next -> 3, record 0.prev -> -1.
        std::int32_t idx3 = 3, m1 = -1;
        std::memcpy(action.data() + 0 * kActionStride + kActionLinkNext, &idx3, 4);
        std::memcpy(action.data() + 0 * kActionStride + kActionLinkPrev, &m1, 4);

        hotkeys.header = 0xABCD1234;
        for (int i = 0; i < kHotkeyCount; ++i) {
            hotkeys.colA[i] = 0x1000 + i; hotkeys.colB[i] = 0x2000 + i;
            hotkeys.colC[i] = 0x3000 + i; hotkeys.colD[i] = 0x4000 + i;
        }
        Fill(reinterpret_cast<u8*>(&globals), sizeof(GameGlobalsBlock), 0x88);
        globals.d910 = 0x1111; globals.q5262_b = 0x2222; globals.b5258 = 0x33;

        history.flag = 0x6C;
        history.base1 = 1000; history.cur1 = 1500;
        history.base2 = 7000; history.cur2 = 7321;
        for (int i = 0; i < kHistoryFlagsLen; ++i) history.flags[i] = (u8)(0x50 + i);
        Fill(carts.data(), carts.size(), 0x05);

        preamble.marker63CC5C = 0x1357;
        preamble.id6498E8 = 0xAA; preamble.id6498EC = 0xBB;
        for (int i = 0; i < 8; ++i) preamble.handlerIds[i] = 0xC0 + i;
    }
};

} // namespace

// ===========================================================================
// Full save: write EVERY table (in the canonical writer order) to one stream;
// reload into a fresh world; verify byte-exact reconstruction of every serialized
// field.  Version pinned to the writer's current version (0x10045).
// ===========================================================================
TEST(io_save_tables_e2e, full_world_save_reload_byte_exact) {
    RwFs fs; VfsInit(&fs, false);
    const u32 ver = 0x10045;

    World src; src.Populate();

    // ---- write phase (mirrors the WriteGameFile table order for these tables) ----
    VfsHandle* w = VfsOpenFile("world.SAV", "wb"); CHECK(w);
    // map-tile table (uses persons buffer base merely as a tile array here is wrong;
    // instead exercise the dedicated tables we own):
    CHECK(SaveWriteGameStateHeaderPreamble(w, src.preamble, World::kScene));
    for (u32 i = 0; i < World::kScene; ++i)
        CHECK(SaveWritePersonSceneRecord(w, src.scene.data() + i * kPersStride, src.sceneLinks[i]));
    CHECK(SaveWritePersonRecords(w, src.persons.data(), World::kPersons));
    CHECK(SaveWriteBuildingTable(w, src.build.data(), nullptr, ver));
    CHECK(SaveWriteObjectMain(w, src.objMain.data(), World::kObjMain));
    CHECK(SaveWriteObjectLight(w, src.objLight.data(), World::kObjLight));
    CHECK(SaveWriteObjectMarks(w, src.objMark.data(), World::kObjMark));
    CHECK(SaveWriteAmtTable(w, src.amt.data()));
    CHECK(SaveWriteActionPool(w, src.action.data(), World::kAction));
    CHECK(SaveWriteHotkeyTable(w, src.hotkeys));
    CHECK(SaveWriteGameGlobals(w, src.globals));
    CHECK(SaveWriteHistoryAndCarts(w, src.history, src.carts.data()));
    VfsCloseStream(w);

    // ---- reload phase into a fresh world ----
    World dst; // zero-initialized
    for (u32 i = 0; i < kAmtCapacity; ++i) {
        std::int32_t m1 = -1; std::memcpy(dst.amt.data() + i * kAmtStride, &m1, 4);
    }
    dst.history.base1 = src.history.base1; // load reconstructs cur = base + delta
    dst.history.base2 = src.history.base2;
    PersonSceneLinks gotLinks[World::kScene]{};
    u32 liveScene = 0;

    VfsHandle* r = VfsOpenFile("world.SAV", "rb"); CHECK(r);
    CHECK(SaveLoadGameStateHeaderPreamble(r, dst.preamble, &liveScene));
    CHECK_EQ(liveScene, World::kScene);
    for (u32 i = 0; i < World::kScene; ++i)
        CHECK(SaveLoadPersonSceneRecord(r, dst.scene.data() + i * kPersStride, &gotLinks[i]));
    CHECK(SaveLoadPersonRecords(r, dst.persons.data(), World::kPersons, ver));
    CHECK(SaveLoadBuildingTable(r, dst.build.data(), ver));
    CHECK(SaveLoadObjectMain(r, dst.objMain.data(), World::kObjMain, ver, kObjBlobChunk));
    CHECK(SaveLoadObjectLight(r, dst.objLight.data(), World::kObjLight));
    CHECK(SaveLoadObjectMarks(r, dst.objMark.data(), World::kObjMark));
    CHECK(SaveLoadAmtTable(r, dst.amt.data(), ver, nullptr));
    CHECK(SaveLoadActionPool(r, dst.action.data(), World::kAction));
    CHECK(SaveLoadHotkeyTable(r, dst.hotkeys));
    CHECK(SaveLoadGameGlobals(r, dst.globals));
    CHECK(SaveLoadHistoryAndCarts(r, dst.history, dst.carts.data()));
    VfsCloseStream(r);

    // ---- verification: byte-exact serialized fields ----
    // preamble
    CHECK_EQ((u32)dst.preamble.marker63CC5C, (u32)src.preamble.marker63CC5C);
    CHECK_EQ(dst.preamble.id6498E8, src.preamble.id6498E8);
    for (int i = 0; i < 8; ++i) CHECK_EQ(dst.preamble.handlerIds[i], src.preamble.handlerIds[i]);

    // scene records: links + counters + representative ranges
    for (u32 i = 0; i < World::kScene; ++i) {
        u8* a = src.scene.data() + i * kPersStride;
        u8* b = dst.scene.data() + i * kPersStride;
        CHECK_EQ(gotLinks[i].idAt91, src.sceneLinks[i].idAt91);
        CHECK_EQ(gotLinks[i].idAt97, src.sceneLinks[i].idAt97);
        std::int32_t gA, gB; std::memcpy(&gA, b + 84, 4); std::memcpy(&gB, b + 396, 4);
        std::int32_t sA, sB; std::memcpy(&sA, a + 84, 4); std::memcpy(&sB, a + 396, 4);
        CHECK_EQ(gA, sA); CHECK_EQ(gB, sB);
        CHECK(std::memcmp(a + 136, b + 136, 168) == 0);
        CHECK(std::memcmp(a + 496, b + 496, 24) == 0);
        CHECK(std::memcmp(a + 520, b + 520, 4) == 0);
    }

    // person/object records
    for (u32 i = 0; i < World::kPersons; ++i) {
        u8* a = src.persons.data() + i * kObjStride;
        u8* b = dst.persons.data() + i * kObjStride;
        CHECK_EQ((u32)a[0], (u32)b[0]);
        CHECK(std::memcmp(a + 1, b + 1, 4) == 0);
        CHECK(std::memcmp(a + 5, b + 5, 0x20) == 0);
        CHECK(std::memcmp(a + 101, b + 101, 0x30) == 0);
        CHECK(std::memcmp(a + 153, b + 153, 0x10) == 0);
    }

    // building table
    for (u32 i = 0; i < World::kBuild; ++i) {
        u8* a = src.build.data() + i * kBuildStride;
        u8* b = dst.build.data() + i * kBuildStride;
        CHECK(std::memcmp(a, b, 2) == 0);
        CHECK(std::memcmp(a + 2, b + 2, 0x10) == 0);
        CHECK(std::memcmp(a + 84, b + 84, 4) == 0);
        CHECK(std::memcmp(a + 160, b + 160, 4) == 0);
    }

    // object sub-tables
    for (u32 i = 0; i < World::kObjMain; ++i) {
        u8* a = src.objMain.data() + i * kObjMainStride;
        u8* b = dst.objMain.data() + i * kObjMainStride;
        CHECK_EQ((u32)a[0], (u32)b[0]);
        CHECK(std::memcmp(a + 20, b + 20, 0x30) == 0);
        CHECK(std::memcmp(a + 172, b + 172, 0xA0) == 0);
    }
    for (u32 i = 0; i < World::kObjLight; ++i)
        CHECK(std::memcmp(src.objLight.data() + i * kObjLightStride + 34,
                          dst.objLight.data() + i * kObjLightStride + 34, 0x80) == 0);
    for (u32 i = 0; i < World::kObjMark; ++i)
        CHECK(std::memcmp(src.objMark.data() + i * kObjMarkStride + 16,
                          dst.objMark.data() + i * kObjMarkStride + 16, 0x40) == 0);

    // amt
    for (u32 i = 0; i < World::kAmtLive; ++i) {
        u8* a = src.amt.data() + i * kAmtStride;
        u8* b = dst.amt.data() + i * kAmtStride;
        CHECK(std::memcmp(a, b, 4) == 0);
        CHECK(std::memcmp(a + 52, b + 52, 1024) == 0);
        CHECK(std::memcmp(a + 124, b + 124, 0x98) == 0);
        CHECK(std::memcmp(a + 24, b + 24, 0xE) == 0);
    }

    // action pool: links (indices) and whole 153-byte records preserved.
    std::int32_t gotIdx = 0;
    std::memcpy(&gotIdx, dst.action.data() + 0 * kActionStride + kActionLinkNext, 4);
    CHECK_EQ(gotIdx, (std::int32_t)3);
    for (u32 i = 0; i < World::kAction; ++i)
        CHECK(std::memcmp(src.action.data() + i * kActionStride,
                          dst.action.data() + i * kActionStride, kActionStride) == 0);

    // hotkeys
    CHECK_EQ(dst.hotkeys.header, src.hotkeys.header);
    for (int i = 0; i < kHotkeyCount; ++i) CHECK_EQ(dst.hotkeys.colD[i], src.hotkeys.colD[i]);

    // globals
    CHECK_EQ(dst.globals.d910, src.globals.d910);
    for (int i = 0; i < 16 * 5; ++i) CHECK_EQ(dst.globals.matrix[i], src.globals.matrix[i]);
    CHECK(std::memcmp(dst.globals.block2, src.globals.block2, 0x44) == 0);

    // history + carts
    CHECK_EQ((u32)dst.history.flag, (u32)src.history.flag);
    CHECK_EQ(dst.history.cur1, src.history.cur1);
    CHECK_EQ(dst.history.cur2, src.history.cur2);
    for (u32 k = 0; k < kCartCount; ++k) {
        u8* a = src.carts.data() + k * kCartStride;
        u8* b = dst.carts.data() + k * kCartStride;
        CHECK(std::memcmp(a, b, 4) == 0);
        for (int e = 0; e < kCartEntryCount; ++e)
            CHECK(std::memcmp(a + 4 + e * 8, b + 4 + e * 8, 5) == 0);
    }

    VfsShutdown();
}
