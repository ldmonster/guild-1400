#include "render/mesh_asset.h"
#include "render/mesh_lod_name.h"
#include "io/vfs.h"
#include "shim/IFileSystem.h"
#include "tests/framework/test.h"

#include <cstring>
#include <map>
#include <string>
#include <vector>

using namespace guild;
using namespace guild::render;

// =============================================================================
// MeshLoadOrFind — golden tests for VIBE_Mesh_LoadOrFindByName @0x5d345c. Asserts
// the SEQUENCE of leaf names load-or-found for each LOD mode (base -> "_s" -> LOD
// frames), driven by BuildLodFileName, on top of the real MeshAssetCache backed by
// a mock VFS holding synthetic .BGFs.
// =============================================================================
namespace {

class LF_MemFile : public guild::shim::IFile {
public:
    explicit LF_MemFile(const std::vector<u8>* d) : data_(d) {}
    std::size_t read(void* dst, std::size_t n) override {
        std::size_t avail = data_->size() - pos_;
        if (n > avail) n = avail;
        if (n) std::memcpy(dst, data_->data() + pos_, n);
        pos_ += n;
        return n;
    }
    std::size_t write(const void*, std::size_t) override { return 0; }
    std::int64_t seek(std::int64_t off, int whence) override {
        std::int64_t base = whence == 1 ? (std::int64_t)pos_
                          : whence == 2 ? (std::int64_t)data_->size() : 0;
        std::int64_t t = base + off;
        if (t < 0 || (std::size_t)t > data_->size()) return -1;
        pos_ = (std::size_t)t;
        return pos_;
    }
    std::int64_t tell() override { return (std::int64_t)pos_; }
    std::int64_t size() override { return (std::int64_t)data_->size(); }
private:
    const std::vector<u8>* data_;
    std::size_t pos_ = 0;
};

class LF_MockFs : public guild::shim::IFileSystem {
public:
    void add(const std::string& path, std::vector<u8> bytes) { files_[path] = std::move(bytes); }
    guild::shim::IFile* open(const char* path, const char*) override {
        opened.push_back(path);
        auto it = files_.find(path);
        return it == files_.end() ? nullptr : new LF_MemFile(&it->second);
    }
    void close(guild::shim::IFile* f) override { delete f; }
    bool exists(const char* path) override { return files_.count(path) != 0; }
    std::vector<std::string> opened;
private:
    std::map<std::string, std::vector<u8>> files_;
};

std::vector<u8> MakeTriBgf() {
    BgfModel m;
    m.materialCount = 1; m.vertexCount = 3; m.polyCount = 1;
    m.objectFlags = 0; m.dummyCount = 0;
    m.vertices.assign(m.vertexCount + 8, BgfVertex{});
    m.vertices[1].pos[0] = 2; m.vertices[2].pos[1] = 2;
    BgfPolygon q{}; q.vtx[0]=0; q.vtx[1]=1; q.vtx[2]=2;
    q.uv0[1]=1; q.uv1[2]=1; q.matIndex = 0; q.texId = -1;
    m.polygons.push_back(q);
    BgfMaterial mat{}; mat.name0 = "BRICK"; m.materials.push_back(mat);
    return Model_WriteFastChunk(m);
}

constexpr u8 kEnabled = 0x80;

struct ModeGuard {
    u8 saved;
    ModeGuard(u8 v) : saved(LodModeByteMut()) { LodModeByteMut() = v; }
    ~ModeGuard() { LodModeByteMut() = saved; }
};

} // namespace

// Mode 0 (LOD enabled): orchestrator loads base then "_s"; no LOD frames.
// The cache key for the orchestrator is the dir/second name (a2).
TEST(MeshLoadOrFind, mode0_loads_base_then_s) {
    ModeGuard mg(kEnabled | 0);
    LF_MockFs fs;
    // BuildLodFileName base: out = name ("HOUSE"), key = dir ("HOUSE").
    // The cache opens `path` = base ("HOUSE"); "_s" path = "HOUSE_s".
    fs.add("HOUSE", MakeTriBgf());
    fs.add("HOUSE_s", MakeTriBgf());
    CHECK(guild::io::VfsInit(&fs, true));

    MeshAssetCache cache;
    Mesh* base = Mesh_LoadOrFindByName(cache, "HOUSE", "HOUSE");
    CHECK(base != nullptr);
    // base + "_s" registered.
    CHECK_EQ((int)cache.size(), 2);
    CHECK(cache.Find("HOUSE") != nullptr);
    CHECK(cache.Find("HOUSE_s") != nullptr);
}

// LOD disabled: only the base is loaded (no "_s").
TEST(MeshLoadOrFind, lod_disabled_loads_base_only) {
    ModeGuard mg(0x00);
    LF_MockFs fs;
    fs.add("HOUSE", MakeTriBgf());
    CHECK(guild::io::VfsInit(&fs, true));

    MeshAssetCache cache;
    Mesh* base = Mesh_LoadOrFindByName(cache, "HOUSE", "HOUSE");
    CHECK(base != nullptr);
    CHECK_EQ((int)cache.size(), 1);
    CHECK(cache.Find("HOUSE") != nullptr);
}

// Mode 1 (multi-LOD): base, "_s", LOD1 ("HOUSE_0"), LOD2 ("HOUSE_1").
TEST(MeshLoadOrFind, mode1_loads_lod_frames) {
    ModeGuard mg(kEnabled | 1);
    LF_MockFs fs;
    fs.add("HOUSE", MakeTriBgf());
    fs.add("HOUSE_s", MakeTriBgf());
    fs.add("HOUSE_0", MakeTriBgf());   // LOD1: v42 = 1-1
    fs.add("HOUSE_1", MakeTriBgf());   // LOD2: v42 = 2-1
    CHECK(guild::io::VfsInit(&fs, true));

    MeshAssetCache cache;
    Mesh* base = Mesh_LoadOrFindByName(cache, "HOUSE", "HOUSE");
    CHECK(base != nullptr);
    CHECK_EQ((int)cache.size(), 4);
    CHECK(cache.Find("HOUSE")   != nullptr);
    CHECK(cache.Find("HOUSE_s") != nullptr);
    CHECK(cache.Find("HOUSE_0") != nullptr);
    CHECK(cache.Find("HOUSE_1") != nullptr);
}

// Cache hit: a second load-or-find returns the SAME base handle, no re-load.
TEST(MeshLoadOrFind, cache_hit_returns_same_handle) {
    ModeGuard mg(kEnabled | 0);
    LF_MockFs fs;
    fs.add("HOUSE", MakeTriBgf());
    fs.add("HOUSE_s", MakeTriBgf());
    CHECK(guild::io::VfsInit(&fs, true));

    MeshAssetCache cache;
    Mesh* a = Mesh_LoadOrFindByName(cache, "HOUSE", "HOUSE");
    int opensAfterFirst = (int)fs.opened.size();
    Mesh* b = Mesh_LoadOrFindByName(cache, "HOUSE", "HOUSE");
    CHECK(a != nullptr);
    CHECK_EQ(a, b);
    // Second call hit the cache (Find(key)) and opened nothing new.
    CHECK_EQ((int)fs.opened.size(), opensAfterFirst);
}
