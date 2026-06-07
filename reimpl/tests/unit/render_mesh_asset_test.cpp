#include "render/mesh_asset.h"
#include "render/texture_asset.h"
#include "render/texture_loader.h"
#include "render/bmp.h"
#include "io/vfs.h"
#include "shim/IFileSystem.h"
#include "tests/framework/test.h"

#include <cmath>
#include <cstring>
#include <map>
#include <string>
#include <vector>

using namespace guild;
using namespace guild::render;

// ---------------------------------------------------------------------------
// Mock IFileSystem backed by in-memory byte buffers (same shape as io_vfs_test).
namespace {

class MA_MemFile : public guild::shim::IFile {
public:
    explicit MA_MemFile(const std::vector<u8>* d) : data_(d) {}
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

class MA_MockFs : public guild::shim::IFileSystem {
public:
    void add(const std::string& path, std::vector<u8> bytes) { files_[path] = std::move(bytes); }
    guild::shim::IFile* open(const char* path, const char*) override {
        auto it = files_.find(path);
        return it == files_.end() ? nullptr : new MA_MemFile(&it->second);
    }
    void close(guild::shim::IFile* f) override { delete f; }
    bool exists(const char* path) override { return files_.count(path) != 0; }
private:
    std::map<std::string, std::vector<u8>> files_;
};

// Build a synthetic .BGF: one triangle, one material "BRICK".
std::vector<u8> MakeTriangleBgf() {
    BgfModel m;
    m.materialCount = 1;
    m.vertexCount = 3;
    m.polyCount = 1;
    m.objectFlags = 0;
    m.dummyCount = 0;

    m.vertices.assign(m.vertexCount + 8, BgfVertex{});
    m.vertices[0].pos[0] = 0; m.vertices[0].pos[1] = 0; m.vertices[0].pos[2] = 0;
    m.vertices[1].pos[0] = 2; m.vertices[1].pos[1] = 0; m.vertices[1].pos[2] = 0;
    m.vertices[2].pos[0] = 0; m.vertices[2].pos[1] = 2; m.vertices[2].pos[2] = 0;

    BgfPolygon q{};
    q.vtx[0] = 0; q.vtx[1] = 1; q.vtx[2] = 2;
    q.uv0[0] = 0; q.uv0[1] = 1; q.uv0[2] = 0;
    q.uv1[0] = 0; q.uv1[1] = 0; q.uv1[2] = 1;
    q.uv2[0] = 0; q.uv2[1] = 0; q.uv2[2] = 0;
    q.matIndex = 0;
    q.texId = -1;
    m.polygons.push_back(q);

    BgfMaterial mat{};
    mat.name0 = "BRICK";
    m.materials.push_back(mat);

    return Model_WriteFastChunk(m);
}

// Build a synthetic 4x4 8-bit BMP whose pixel (x,y) index = x + 4*y.
std::vector<u8> MakeRampBmp(int side) {
    std::vector<u8> px((size_t)side * side);
    for (int y = 0; y < side; ++y)
        for (int x = 0; x < side; ++x)
            px[(size_t)y * side + x] = (u8)(x + side * y);
    u8 pal[256 * 3];
    for (int i = 0; i < 256; ++i) { pal[3*i]=(u8)i; pal[3*i+1]=(u8)(255-i); pal[3*i+2]=(u8)(i*2); }
    return BmpSaveIndexed(side, side, px.data(), pal);
}

} // namespace

// ---- Model_WriteFastChunk round-trips through the existing reader ----------
TEST(MeshAssetUnit, FastChunkWriteReadRoundTrip) {
    std::vector<u8> bytes = MakeTriangleBgf();
    BgfModel m;
    CHECK(LoadFastChunk(bytes.data(), bytes.size(), m));
    CHECK_EQ((int)m.materialCount, 1);
    CHECK_EQ((int)m.vertexCount, 3);
    CHECK_EQ((int)m.polyCount, 1);
    CHECK_EQ((int)m.vertices.size(), 3 + 8);   // engine +8 slack preserved
    CHECK(std::fabs(m.vertices[1].pos[0] - 2.0f) < 1e-6f);
    CHECK_EQ((int)m.polygons[0].vtx[1], 1);
    CHECK_EQ((int)m.polygons[0].matIndex, 0);
    CHECK(m.materials[0].name0 == "BRICK");
}

// ---- Mesh_LoadByName: synthetic .BGF from a mock VFS -> expected mesh -------
TEST(MeshAssetUnit, MeshLoadByNameFromVfs) {
    MA_MockFs fs;
    fs.add("*TRI.BGF", MakeTriangleBgf());
    CHECK(guild::io::VfsInit(&fs, true));
    TextureLoaderSetCache(nullptr);  // geometry-only: legacy slot ids
    g_texFixed = -1; g_texNextSlot = 100; g_texLoadRequests.clear();

    Mesh mesh;
    CHECK(Mesh_LoadByName("*TRI.BGF", "TRI", mesh));
    CHECK_EQ((int)mesh.vertexCount, 3);
    CHECK_EQ((int)mesh.polyCount, 1);
    CHECK_EQ((int)mesh.materialCount, 1);
    // The material's diffuse name was resolved exactly once.
    CHECK_EQ((int)g_texLoadRequests.size(), 1);
    CHECK(g_texLoadRequests[0].name == std::string("BRICK"));

    guild::io::VfsShutdown();
}

// ---- Mesh_LoadByName fails cleanly on a missing path -----------------------
TEST(MeshAssetUnit, MeshLoadByNameMissing) {
    MA_MockFs fs;
    CHECK(guild::io::VfsInit(&fs, true));
    Mesh mesh;
    CHECK(!Mesh_LoadByName("*NOPE.BGF", "NOPE", mesh));
    guild::io::VfsShutdown();
}

// ---- Texture_LoadByName: decode a synthetic BMP into texels ----------------
TEST(MeshAssetUnit, TextureLoadByNameDecodesTexels) {
    const int side = 4;
    MA_MockFs fs;
    fs.add("*RAMP.BMP", MakeRampBmp(side));
    CHECK(guild::io::VfsInit(&fs, true));

    TextureAssetCache cache(8);
    int slot = cache.LoadByName("*RAMP.BMP", "RAMP");
    CHECK(slot >= 0);

    const Texture* rec = cache.record(slot);
    CHECK(rec != nullptr);
    CHECK_EQ((int)rec->mipWidth, side);
    CHECK_EQ((int)rec->baseWidth, side);
    CHECK_EQ((int)rec->texels.size(), side * side);
    // Power-of-two square: texelMask == w*w-1, widthShift == log2(w).
    CHECK_EQ((int)rec->texelMask, side * side - 1);
    CHECK_EQ((int)rec->widthShift, 2);
    // Texel (x,y) index = x + 4*y (TexelAt addresses (V<<shift)+U).
    for (int y = 0; y < side; ++y)
        for (int x = 0; x < side; ++x)
            CHECK_EQ((int)TexelAt(*rec, x, y), x + side * y);

    guild::io::VfsShutdown();
}

// ---- Texture cache returns the SAME slot on re-load ------------------------
TEST(MeshAssetUnit, TextureCacheReuse) {
    MA_MockFs fs;
    fs.add("*RAMP.BMP", MakeRampBmp(4));
    CHECK(guild::io::VfsInit(&fs, true));

    TextureAssetCache cache(8);
    int a = cache.LoadByName("*RAMP.BMP", "RAMP");
    int b = cache.LoadByName("*RAMP.BMP", "RAMP");
    CHECK(a >= 0);
    CHECK_EQ(a, b);  // same record, re-load is a cache hit (refcount bumped)
    CHECK_EQ((int)cache.set().records[(size_t)a].refCount, 2);

    guild::io::VfsShutdown();
}

// ---- MeshAssetCache returns the SAME handle on re-load ---------------------
TEST(MeshAssetUnit, MeshCacheReuse) {
    MA_MockFs fs;
    fs.add("*TRI.BGF", MakeTriangleBgf());
    CHECK(guild::io::VfsInit(&fs, true));
    TextureLoaderSetCache(nullptr);
    g_texFixed = -1; g_texNextSlot = 100; g_texLoadRequests.clear();

    MeshAssetCache cache;
    Mesh* a = cache.LoadOrFind("*TRI.BGF", "TRI");
    Mesh* b = cache.LoadOrFind("*tri.BGF", "tri");  // case-insensitive key
    CHECK(a != nullptr);
    CHECK_EQ((void*)a, (void*)b);          // same handle, not re-parsed
    CHECK_EQ((int)cache.size(), 1);
    // The second (cache-hit) load did NOT re-resolve the texture.
    CHECK_EQ((int)g_texLoadRequests.size(), 1);

    guild::io::VfsShutdown();
}
