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
// End-to-end: mount a small asset set (a .BGF + its .BMP texture) in a mock VFS,
// load the model by name with materials resolved through the REAL texture decode,
// and verify the mesh geometry + the bound texture slot against a reference.
namespace {

class E2_MemFile : public guild::shim::IFile {
public:
    explicit E2_MemFile(const std::vector<u8>* d) : data_(d) {}
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
        pos_ = (std::size_t)t; return pos_;
    }
    std::int64_t tell() override { return (std::int64_t)pos_; }
    std::int64_t size() override { return (std::int64_t)data_->size(); }
private:
    const std::vector<u8>* data_;
    std::size_t pos_ = 0;
};

class E2_MockFs : public guild::shim::IFileSystem {
public:
    void add(const std::string& path, std::vector<u8> bytes) { files_[path] = std::move(bytes); }
    guild::shim::IFile* open(const char* path, const char*) override {
        auto it = files_.find(path);
        return it == files_.end() ? nullptr : new E2_MemFile(&it->second);
    }
    void close(guild::shim::IFile* f) override { delete f; }
    bool exists(const char* path) override { return files_.count(path) != 0; }
private:
    std::map<std::string, std::vector<u8>> files_;
};

// A quad (2 triangles) sharing one material "WALL".
std::vector<u8> MakeQuadBgf() {
    BgfModel m;
    m.materialCount = 1;
    m.vertexCount = 4;
    m.polyCount = 2;
    m.objectFlags = 0;
    m.dummyCount = 0;
    m.vertices.assign(m.vertexCount + 8, BgfVertex{});
    const float pos[4][3] = {{0,0,0},{1,0,0},{1,1,0},{0,1,0}};
    for (int i = 0; i < 4; ++i)
        for (int k = 0; k < 3; ++k) m.vertices[i].pos[k] = pos[i][k];

    auto mkPoly = [](u32 a, u32 b, u32 c) {
        BgfPolygon q{};
        q.vtx[0]=a; q.vtx[1]=b; q.vtx[2]=c; q.matIndex=0; q.texId=-1;
        return q;
    };
    m.polygons.push_back(mkPoly(0,1,2));
    m.polygons.push_back(mkPoly(0,2,3));

    BgfMaterial mat{};
    mat.name0 = "WALL";
    m.materials.push_back(mat);
    return Model_WriteFastChunk(m);
}

// 8x8 8-bit texture: pixel index = (x ^ y) & 0xFF.
std::vector<u8> MakeXorBmp(int side) {
    std::vector<u8> px((size_t)side * side);
    for (int y = 0; y < side; ++y)
        for (int x = 0; x < side; ++x)
            px[(size_t)y * side + x] = (u8)((x ^ y) & 0xFF);
    return BmpSaveIndexed(side, side, px.data(), nullptr);
}

} // namespace

// ---- Full flow: model-by-name with materials + bound texture ---------------
TEST(MeshAssetE2E, ModelByNameWithMaterialAndTexture) {
    const int texSide = 8;
    E2_MockFs fs;
    fs.add("*HOUSE.BGF", MakeQuadBgf());
    fs.add("*WALL.BMP",  MakeXorBmp(texSide));
    CHECK(guild::io::VfsInit(&fs, true));

    // Install the real VFS-backed texture cache so material resolution decodes the
    // bound .BMP rather than returning a placeholder slot.
    TextureAssetCache texCache(16);
    TextureLoaderSetCache(&texCache);
    g_texFixed = -1; g_texNextSlot = 100; g_texLoadRequests.clear();

    // Load the model by name (drives LoadBgfFile -> material dedup -> the real
    // TextureLoadByName per material).
    MeshAssetCache meshCache;
    Mesh* mesh = meshCache.LoadOrFind("*HOUSE.BGF", "HOUSE");
    CHECK(mesh != nullptr);

    // --- Mesh geometry vs reference ---
    CHECK_EQ((int)mesh->vertexCount, 4);
    CHECK_EQ((int)mesh->polyCount, 2);
    CHECK_EQ((int)mesh->materialCount, 1);

    // --- Material resolution: WALL requested exactly once ---
    CHECK_EQ((int)g_texLoadRequests.size(), 1);
    CHECK(g_texLoadRequests[0].name == std::string("WALL"));

    // --- Bound texture: both polys carry the SAME resolved texture slot, and the
    //     slot points at a decoded 8x8 record whose texels match the reference. ---
    int slot0 = mesh->polygons[0].texId;
    int slot1 = mesh->polygons[1].texId;
    CHECK(slot0 >= 0);
    CHECK_EQ(slot0, slot1);

    const Texture* tex = texCache.record(slot0);
    CHECK(tex != nullptr);
    CHECK_EQ((int)tex->mipWidth, texSide);
    CHECK_EQ((int)tex->texels.size(), texSide * texSide);
    CHECK_EQ((int)tex->texelMask, texSide * texSide - 1);

    // Reference: every texel index == (x ^ y).
    for (int y = 0; y < texSide; ++y)
        for (int x = 0; x < texSide; ++x)
            CHECK_EQ((int)TexelAt(*tex, x, y), (x ^ y) & 0xFF);

    // --- Cache coherence: a second by-name load returns the same mesh handle and
    //     does NOT re-resolve the texture. ---
    Mesh* again = meshCache.LoadOrFind("*HOUSE.BGF", "HOUSE");
    CHECK_EQ((void*)mesh, (void*)again);
    CHECK_EQ((int)g_texLoadRequests.size(), 1);

    TextureLoaderSetCache(nullptr);
    guild::io::VfsShutdown();
}
