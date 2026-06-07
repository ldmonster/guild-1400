#include "test.h"

#include "io/save_world_load.h"
#include "io/save.h"
#include "io/vfs.h"
#include "sim/entity.h"
#include "shim/IFileSystem.h"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

// GUARDED end-to-end test: load the REAL shipped AUGSBURG.cty (a gzipped SAVE-GAME
// city seed, version 0x1003B, header.flag & 2 == partial) through the full world
// load driver, and verify the live entity arrays populate with sane counts and a few
// real person/building records. The test is skipped (passes trivially) when the
// original assets are not present.
//
// The .cty is gzip-framed; the VFS transparently gunzips it on open ("rb" + the
// gzip magic). The driver then runs the recovered table-load sequence over the
// inflated stream, populating sim::g_persons / sim::g_objects.

using namespace guild::io;
using guild::u8;
using guild::u32;

namespace {

// A passthrough host filesystem for the e2e test: serves real files from disk so
// the VFS's transparent gunzip path runs against the actual AUGSBURG.cty bytes.
class DiskFile : public guild::shim::IFile {
public:
    explicit DiskFile(std::vector<u8> data) : data_(std::move(data)) {}
    std::size_t read(void* dst, std::size_t n) override {
        std::size_t avail = data_.size() - pos_;
        if (n > avail) n = avail;
        if (n) std::memcpy(dst, data_.data() + pos_, n);
        pos_ += n;
        return n;
    }
    std::size_t write(const void*, std::size_t) override { return 0; }
    std::int64_t seek(std::int64_t off, int whence) override {
        std::int64_t base = whence == 1 ? (std::int64_t)pos_
                          : whence == 2 ? (std::int64_t)data_.size() : 0;
        std::int64_t t = base + off;
        if (t < 0) return -1;
        pos_ = (std::size_t)t;
        return pos_;
    }
    std::int64_t tell() override { return (std::int64_t)pos_; }
    std::int64_t size() override { return (std::int64_t)data_.size(); }
private:
    std::vector<u8> data_;
    std::size_t     pos_ = 0;
};

class DiskFs : public guild::shim::IFileSystem {
public:
    explicit DiskFs(std::string root) : root_(std::move(root)) {}
    guild::shim::IFile* open(const char* path, const char* mode) override {
        if (mode && (std::strchr(mode, 'w') || std::strchr(mode, 'W')))
            return nullptr;
        std::ifstream f(full(path), std::ios::binary);
        if (!f) return nullptr;
        std::vector<u8> data((std::istreambuf_iterator<char>(f)),
                             std::istreambuf_iterator<char>());
        return new DiskFile(std::move(data));
    }
    void close(guild::shim::IFile* f) override { delete f; }
    bool exists(const char* path) override {
        std::ifstream f(full(path), std::ios::binary);
        return (bool)f;
    }
private:
    std::string full(const char* path) const {
        std::string p = path;
        for (char& c : p) if (c == '\\') c = '/';
        return root_ + "/" + p;
    }
    std::string root_;
};

// Locate the europe_guild_1400_original asset root relative to the repo.
const char* FindAssetRoot() {
    static const char* candidates[] = {
        "europe_guild_1400_original/Resources/gamedata/Cities/AUGSBURG.cty",
        "reimpl/europe_guild_1400_original/Resources/gamedata/Cities/AUGSBURG.cty",
    };
    static std::string root;
    for (const char* c : candidates) {
        std::ifstream f(c, std::ios::binary);
        if (f) {
            std::string s = c;
            // strip the "/Resources/.../AUGSBURG.cty" suffix to get the asset root.
            auto pos = s.find("/Resources/");
            root = s.substr(0, pos);
            return root.c_str();
        }
    }
    return nullptr;
}

} // namespace

TEST(io_save_world_real, load_augsburg_full_world) {
    const char* root = FindAssetRoot();
    if (!root) {
        // Assets not present in this checkout — skip (trivial pass).
        CHECK(true);
        return;
    }
    std::printf("[augsburg] asset root: %s\n", root);

    DiskFs fs(root);
    VfsInit(&fs, /*caseInsensitive=*/false);
    CHECK(fs.exists("Resources/gamedata/Cities/AUGSBURG.cty"));

    guild::sim::ResetEntityArrays();

    WorldState world{};
    bool ok = LoadWorld("Resources/gamedata/Cities/AUGSBURG.cty", world);
    std::printf("[augsburg] LoadWorld -> %s\n", ok ? "true" : "false");

    // Report the recovered world-state counts.
    std::printf("[augsburg] sceneTileCount = %u\n", world.sceneTileCount);
    std::printf("[augsburg] objectCount    = %u\n", world.objectCount);
    std::printf("[augsburg] cityMarker     = 0x%04X\n", (unsigned)world.cityMarker);
    std::printf("[augsburg] cityRecCount   = %u\n", world.cityRecCount);
    std::printf("[augsburg] playerIdA/B    = %u / %u\n", world.playerIdA, world.playerIdB);
    std::printf("[augsburg] buildingSlots  = %s\n",
                world.buildingSlotsLoaded ? "loaded" : "no");

    // Count the live person records the loader scattered into g_persons.
    int livePersons = 0, firstId = -1;
    for (int i = 0; i < guild::sim::kPersonCapacity; ++i) {
        if (guild::sim::g_persons[i].marker != -1) {
            if (firstId < 0) firstId = guild::sim::g_persons[i].id;
            ++livePersons;
        }
    }
    std::printf("[augsburg] live g_persons = %d (first id=%d)\n", livePersons, firstId);

    // Count live object/building records and sample a few.
    int liveObjects = 0, firstObjId = -1, firstObjAlive = -1;
    for (int i = 0; i < guild::sim::kObjectCapacity; ++i) {
        if (guild::sim::g_objects[i].alive) {
            if (firstObjId < 0) {
                firstObjId = guild::sim::g_objects[i].id;
                firstObjAlive = guild::sim::g_objects[i].alive;
            }
            ++liveObjects;
        }
    }
    std::printf("[augsburg] live g_objects = %d (first: alive=%d id=%d)\n",
                liveObjects, firstObjAlive, firstObjId);

    // ---- assertions: the real city must load to a sane world ------------------
    CHECK(ok);
    if (ok) {
        // The shipped Augsburg seed is version 0x1003B, partial (header.flag & 2).
        // Recovered from the real file (stable across the shipped asset):
        //   551 map-tile / scene-node records, 55 live object/building records,
        //   16 building-type counters, 1 person/scene record, the 5 city-slot
        //   tables + 4 city-info records. The driver populates the live arrays.
        CHECK(SaveVersionGet() == (u32)0x1003B);
        CHECK(SaveVersionGet() >= kSaveVersionLoadMin);
        CHECK(SaveVersionGet() <= kSaveVersionLoadMax);

        // Scene / map-tile table populated.
        CHECK(world.sceneTileCount == 551u);
        CHECK(world.sceneTileCount <= (u32)guild::io::kSceneTileCapacity);

        // Object / building array populated (55 live records).
        CHECK(world.objectCount == 55u);
        CHECK((u32)liveObjects == world.objectCount);
        CHECK(firstObjAlive > 0);    // a live alive-byte
        CHECK(firstObjId >= 0);      // a real object id

        // Person / scene record table populated (the city seed carries 1).
        CHECK(world.cityRecCount == 1u);
        CHECK(world.cityRecCount <= (u32)guild::io::kCityRecCapacity);
        CHECK(livePersons == (int)world.cityRecCount);
        CHECK(firstId >= 0);         // a real, scattered person id

        // Building-slot tables (5 city-slot tables + 4 city-info records) read.
        CHECK(world.buildingSlotsLoaded);
    }

    VfsShutdown();
}
