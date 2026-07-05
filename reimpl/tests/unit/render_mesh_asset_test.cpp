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
// NOTE: a former TwoSidedMaterialSetsPolyFlags test asserted BuildGeometry bakes
// material b2&2 onto poly flags36 bit4 / flags38 bit2 at load. That was reverted:
// (1) nothing in the render pipeline consumes those poly flags for two-sided /
// backface handling (the flags were inert), and (2) city_view3d documents that
// material +194 bit 1 (b2 & 2) is NOT the alpha/no-cull route. Correct two-sided
// foliage rendering is a deferred hardening task, reconstructed where the engine
// actually branches (projection cull / raster winding), not baked at load.

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

// ===========================================================================
// W11 hardening: malformed / truncated / oversized .BGF fast-chunk inputs.
// Every case must fail-safe (return false, no crash, no ASAN report) and never
// run off the buffer. Build with -fsanitize=address,undefined to exercise.
// ===========================================================================
namespace {

// Append a little-endian u32 to a byte vector.
void Pu32(std::vector<u8>& v, u32 x) {
    v.push_back((u8)(x & 0xFF)); v.push_back((u8)((x >> 8) & 0xFF));
    v.push_back((u8)((x >> 16) & 0xFF)); v.push_back((u8)((x >> 24) & 0xFF));
}
// Frame a raw fast-chunk: leading tag, '-' token, chunkSize, magic, then `body`.
std::vector<u8> FrameChunk(const std::vector<u8>& body) {
    std::vector<u8> out;
    Pu32(out, 0);                       // leading tag (skipped)
    out.push_back(kBgfTokenChunk);      // '-'
    Pu32(out, 4u + (u32)body.size());   // chunkSize (magic + body)
    Pu32(out, kBgfFastChunkMagic);
    out.insert(out.end(), body.begin(), body.end());
    return out;
}

} // namespace

// ---- 0-byte and sub-header buffers reject cleanly --------------------------
TEST(MeshAssetUnit, FastChunkEmptyAndTiny) {
    BgfModel m;
    CHECK(!LoadFastChunk(nullptr, 0, m));
    const u8 zero = 0;
    CHECK(!LoadFastChunk(&zero, 0, m));            // 0-byte
    CHECK(!LoadFastChunk(&zero, 1, m));            // 1-byte (< 4-byte tag)
    // Valid chunk header but no body bytes at all -> the count reads fail.
    std::vector<u8> hdrOnly = FrameChunk({});
    CHECK(!LoadFastChunk(hdrOnly.data(), hdrOnly.size(), m));
}

// ---- vertexCount declared larger than the file -> reject, no OOB -----------
TEST(MeshAssetUnit, FastChunkVertexCountTooLarge) {
    std::vector<u8> body;
    Pu32(body, 0);            // materialCount
    Pu32(body, 1000000u);     // vertexCount: far more than the buffer holds
    Pu32(body, 0);            // polyCount
    body.push_back(1); body.push_back(2);  // a few stray bytes
    std::vector<u8> buf = FrameChunk(body);
    BgfModel m;
    CHECK(!LoadFastChunk(buf.data(), buf.size(), m));  // bounded before alloc
}

// ---- polyCount declared larger than the file -> reject, no OOB -------------
TEST(MeshAssetUnit, FastChunkPolyCountTooLarge) {
    std::vector<u8> body;
    Pu32(body, 0);            // materialCount
    Pu32(body, 0);            // vertexCount (still allocates +8 slots)
    Pu32(body, 1000000u);     // polyCount: impossible for this buffer
    std::vector<u8> buf = FrameChunk(body);
    BgfModel m;
    CHECK(!LoadFastChunk(buf.data(), buf.size(), m));
}

// ---- vertexCount near UINT_MAX must not overflow the +8 slack -------------
TEST(MeshAssetUnit, FastChunkVertexCountOverflowGuard) {
    std::vector<u8> body;
    Pu32(body, 0);
    Pu32(body, 0xFFFFFFFEu);  // +8 would wrap to a tiny value without the guard
    Pu32(body, 0);
    std::vector<u8> buf = FrameChunk(body);
    BgfModel m;
    CHECK(!LoadFastChunk(buf.data(), buf.size(), m));  // rejected by the byte bound
}

// ---- truncated mid-vertex-block -> reject, no over-read --------------------
TEST(MeshAssetUnit, FastChunkTruncatedMidVertex) {
    std::vector<u8> body;
    Pu32(body, 0);            // materialCount
    Pu32(body, 4);            // vertexCount=4 -> reads (4+8)=12 records of 24 bytes
    Pu32(body, 0);            // polyCount
    // Provide only ~1.5 vertices worth of bytes (24*12 = 288 needed).
    for (int i = 0; i < 36; ++i) body.push_back((u8)i);
    std::vector<u8> buf = FrameChunk(body);
    BgfModel m;
    CHECK(!LoadFastChunk(buf.data(), buf.size(), m));
}

// ---- truncated material block (NUL-less name runs to EOF) -> reject --------
TEST(MeshAssetUnit, FastChunkUnterminatedMaterialName) {
    // Build a valid 1-vertex/0-poly/1-material chunk, then strip the trailing
    // material flag bytes + NULs so the name reader runs off the end.
    BgfModel src;
    src.materialCount = 1; src.vertexCount = 1; src.polyCount = 0; src.dummyCount = 0;
    src.vertices.assign(1 + 8, BgfVertex{});
    BgfMaterial mat{}; mat.name0 = "STONE";
    src.materials.push_back(mat);
    std::vector<u8> good = Model_WriteFastChunk(src);
    // Sanity: the well-formed buffer parses.
    BgfModel ok; CHECK(LoadFastChunk(good.data(), good.size(), ok));
    // Now truncate inside the material name (drop the NUL + flag bytes).
    std::vector<u8> bad(good.begin(), good.end() - 8);
    BgfModel m;
    CHECK(!LoadFastChunk(bad.data(), bad.size(), m));  // ReadString hits EOF
}

// ---- oversized poly index past vertexCount -> BuildGeometry rejects --------
TEST(MeshAssetUnit, BuildGeometryRejectsBadPolyIndex) {
    BgfModel m;
    m.vertexCount = 3;
    m.vertices.assign(3 + 8, BgfVertex{});
    BgfPolygon q{};
    q.vtx[0] = 0; q.vtx[1] = 1; q.vtx[2] = 99;  // index past the vertex array
    m.polygons.push_back(q);
    m.polyCount = 1;
    BgfGeometry g;
    CHECK(!BuildGeometry(m, g));  // out-of-range index -> clean false, no OOB
}

// ---- material index out of range survives the post-process (no heap OOB) ---
TEST(MeshAssetUnit, PostProcessOutOfRangeMaterialIndex) {
    ParsedModel pm;
    pm.skipVertexDedup = true;
    pm.vertices.assign(3, MeshVertex{});
    pm.vertices[1].pos[0] = 1; pm.vertices[2].pos[1] = 1;
    pm.polygons.assign(1, MeshPolygon{});
    pm.polygons[0].vtx[0]=0; pm.polygons[0].vtx[1]=1; pm.polygons[0].vtx[2]=2;
    pm.polygons[0].matIndex = 7;            // index way past the (1) material
    pm.materials.assign(1, MeshMaterial{});
    std::strcpy(pm.materials[0].name0, "ONE");
    pm.materials[0].uScale = 1; pm.materials[0].vScale = 1;

    Mesh out;
    g_texLoadRequests.clear(); g_texFixed = -1;
    // Must not corrupt the heap: the guards skip the bad index. (ASAN-checked.)
    bool ok = LoadBgfPostProcess(pm, "OOBMAT", 0, false, out);
    CHECK(ok);
    CHECK_EQ((int)out.polyCount, 1);
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
