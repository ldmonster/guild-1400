#include "render/mesh_asset.h"
#include "render/mesh_lod_name.h"

#include <cctype>
#include <cstring>

// =============================================================================
// guild::render — VFS-backed by-name asset loaders + the model/anim cache.
// See mesh_asset.h for the original-function map. The parse cores (LoadBgfFile,
// LoadFastChunk, LoadBinaryAnimation) are REUSED unchanged; this file only adds
// the VFS open/slurp front-ends, the fast-chunk writer, and the cache.
// =============================================================================
namespace guild::render {

using guild::io::VfsOpenFile;
using guild::io::VfsReadStream;
using guild::io::VfsSeek;
using guild::io::VfsTell;
using guild::io::VfsCloseStream;
using guild::io::VfsHandle;

// ---------------------------------------------------------------------------
// VfsSlurp — open + seek-to-end + tell + rewind + read-all + close. Mirrors the
// open/read/close idiom shared by the loaders (e.g. VIBE_Model_FastChunkIo reads
// the whole stream into an AllocDebug buffer the same way: Tell -> Seek(0) ->
// ReadStream(buf, size, h, 1)).
// ---------------------------------------------------------------------------
bool VfsSlurp(const char* path, std::vector<u8>& out) {
    out.clear();
    if (!path)
        return false;
    VfsHandle* h = VfsOpenFile(path, "rb");
    if (!h)
        return false;

    VfsSeek(h, 0, 2 /*SEEK_END*/);
    long len = VfsTell(h);
    VfsSeek(h, 0, 0 /*SEEK_SET*/);
    if (len < 0) {
        VfsCloseStream(h);
        return false;
    }

    out.assign(static_cast<std::size_t>(len), 0);
    bool ok = true;
    if (len > 0) {
        u32 got = VfsReadStream(out.data(), 1, h, static_cast<u32>(len));
        // 0xFFFFFFFF == read error; a short read means the stream lied about its
        // length — trim to what we actually got (the loaders bounds-check anyway).
        if (got == 0xFFFFFFFFu) {
            ok = false;
        } else if (got != static_cast<u32>(len)) {
            out.resize(got);
        }
    }
    VfsCloseStream(h);
    return ok;
}

// ---------------------------------------------------------------------------
// gilde.exe 0x5D2348 — Mesh_LoadByName.
// ---------------------------------------------------------------------------
bool Mesh_LoadByName(const char* path, const std::string& name, Mesh& out) {
    std::vector<u8> buf;
    if (!VfsSlurp(path, buf))
        return false;
    const std::string& meshName = name.empty() ? std::string(path ? path : "") : name;
    return LoadBgfFile(buf.data(), buf.size(), meshName, out);
}

// ---------------------------------------------------------------------------
// gilde.exe 0x5F87B8 — Model_LoadByName (fast-chunk reader, VFS form).
// ---------------------------------------------------------------------------
bool Model_LoadByName(const char* path, BgfModel& out) {
    std::vector<u8> buf;
    if (!VfsSlurp(path, buf))
        return false;
    return LoadFastChunk(buf.data(), buf.size(), out);
}

// ---------------------------------------------------------------------------
// gilde.exe 0x5E450C — Anim_LoadByName (binary animation, VFS form).
// ---------------------------------------------------------------------------
bool Anim_LoadByName(const char* path, const std::string& name, u8 loadFlag,
                     Animation& out) {
    std::vector<u8> buf;
    if (!VfsSlurp(path, buf))
        return false;
    return LoadBinaryAnimation(buf.data(), buf.size(), name.c_str(), loadFlag, out);
}

// ---------------------------------------------------------------------------
// Little-endian write helpers, mirroring the VIBE_Bio_Write* primitives (each a
// raw LE store on the 32-bit x86 target — no byte-swap despite the names).
// ---------------------------------------------------------------------------
namespace {

void WrU32(std::vector<u8>& v, u32 x) {
    v.push_back((u8)(x & 0xFF));
    v.push_back((u8)((x >> 8) & 0xFF));
    v.push_back((u8)((x >> 16) & 0xFF));
    v.push_back((u8)((x >> 24) & 0xFF));
}
void WrI32(std::vector<u8>& v, i32 x) { WrU32(v, (u32)x); }
void WrF32(std::vector<u8>& v, float f) {
    u32 bits;
    std::memcpy(&bits, &f, 4);
    WrU32(v, bits);
}
void WrVec3(std::vector<u8>& v, const float f[3]) {
    WrF32(v, f[0]);
    WrF32(v, f[1]);
    WrF32(v, f[2]);
}
// VIBE_Bio_WriteString @0x5dc8ec — write the bytes + a NUL terminator.
void WrString(std::vector<u8>& v, const std::string& s) {
    for (char c : s) v.push_back((u8)c);
    v.push_back(0);
}
void WrStringN(std::vector<u8>& v, const char* s, std::size_t cap) {
    std::size_t i = 0;
    for (; i < cap && s[i]; ++i) v.push_back((u8)s[i]);
    v.push_back(0);
}

} // namespace

// ---------------------------------------------------------------------------
// gilde.exe 0x5F9558 — Model_WriteFastChunk (the serializer half of FastChunkIo).
// Emits the bare fast-chunk record the reader (BgfFindChunkStart / LoadFastChunk)
// expects: a '-' chunk tag, the chunk size, the magic, then the header/vertex/
// poly/material/dummy blocks in the exact VIBE_Bio_Write* order.
// ---------------------------------------------------------------------------
std::vector<u8> Model_WriteFastChunk(const BgfModel& m) {
    // First serialize the body (everything after the magic), then prepend the
    // '-' tag + chunkSize + magic. chunkSize counts the magic + body (the reader
    // skips chunkSize-4 to step over a non-matching chunk; here it is the only
    // chunk so the value is informational, but we make it exact).
    std::vector<u8> body;

    // Header: materialCount, vertexCount, polyCount.
    WrU32(body, m.materialCount);
    WrU32(body, m.vertexCount);
    WrU32(body, m.polyCount);

    // Vertex block: (vertexCount + 8) records of { vec3 pos, vec3 normal }.
    const u32 vtxStored = m.vertexCount + 8;
    for (u32 i = 0; i < vtxStored; ++i) {
        BgfVertex zero{};
        const BgfVertex& vtx = (i < m.vertices.size()) ? m.vertices[i] : zero;
        WrVec3(body, vtx.pos);
        WrVec3(body, vtx.normal);
    }

    // Object flags dword.
    WrU32(body, m.objectFlags);

    // Poly block: vtx[3], uv0, uv1, uv2, matIndex (byte or u32 per materialCount).
    for (u32 i = 0; i < m.polyCount && i < m.polygons.size(); ++i) {
        const BgfPolygon& q = m.polygons[i];
        WrU32(body, q.vtx[0]);
        WrU32(body, q.vtx[1]);
        WrU32(body, q.vtx[2]);
        WrVec3(body, q.uv0);
        WrVec3(body, q.uv1);
        WrVec3(body, q.uv2);
        if (static_cast<i32>(m.materialCount) <= 254) {
            u8 b = (q.matIndex < 0) ? 0xFF : (u8)(q.matIndex & 0xFF);
            body.push_back(b);
        } else {
            WrI32(body, q.matIndex);
        }
    }

    // Material block: 3 strings + 6 flag bytes.
    for (u32 i = 0; i < m.materialCount && i < m.materials.size(); ++i) {
        const BgfMaterial& mat = m.materials[i];
        WrString(body, mat.name0);
        WrString(body, mat.name1);
        WrString(body, mat.name2);
        body.push_back(mat.flag);
        body.push_back(mat.b1);
        body.push_back(mat.b2);
        body.push_back(mat.b3);
        body.push_back(mat.b4);
        body.push_back(mat.b5);
    }

    // Dummy block: dummyCount, then { string name, vec3 pos, vec3 rot }.
    WrU32(body, m.dummyCount);
    for (u32 i = 0; i < m.dummyCount && i < m.dummies.size(); ++i) {
        const BgfDummy& d = m.dummies[i];
        WrStringN(body, d.name, sizeof(d.name));
        WrVec3(body, d.pos);
        WrVec3(body, d.rot);
    }

    // Frame the chunk: a leading tag dword (skipped by BgfFindChunkStart), then
    // the '-' chunk token, u32 chunkSize, u32 magic, body.
    std::vector<u8> out;
    WrU32(out, 0);                                  // leading 4-byte tag (discarded)
    out.push_back(kBgfTokenChunk);                  // '-'
    const u32 chunkSize = 4u + static_cast<u32>(body.size());  // magic + body
    WrU32(out, chunkSize);
    WrU32(out, kBgfFastChunkMagic);
    out.insert(out.end(), body.begin(), body.end());
    return out;
}

// ---------------------------------------------------------------------------
// MeshAssetCache — models the stock-object registry + VIBE_Mesh_FindStockObject.
// ---------------------------------------------------------------------------
std::string MeshAssetCache::Key(const std::string& name) {
    // VIBE_Util_StrCmpNoCaseN over 63 chars: case-insensitive, truncated.
    std::string k = name.substr(0, 63);
    for (char& c : k) c = (char)std::toupper((unsigned char)c);
    return k;
}

Mesh* MeshAssetCache::Find(const std::string& name) {
    auto it = map_.find(Key(name));
    return it == map_.end() ? nullptr : it->second.get();
}

Mesh* MeshAssetCache::LoadOrFind(const char* path, const std::string& name) {
    if (Mesh* hit = Find(name))
        return hit;  // cache hit -> same handle, no re-parse

    auto mesh = std::make_unique<Mesh>();
    if (!Mesh_LoadByName(path, name, *mesh))
        return nullptr;

    const std::string k = Key(name);
    Mesh* raw = mesh.get();
    map_[k] = std::move(mesh);
    order_.push_back(k);
    return raw;
}

// ---------------------------------------------------------------------------
// gilde.exe 0x5D345C — VIBE_Mesh_LoadOrFindByName.
// Orchestrates BuildLodFileName + the cache. `base` (v8) is the file leaf name the
// loader opens; `key` (v7) is the registry/cache key. The original's LoadAndRegister
// is the cache's LoadOrFind (load-if-absent + register); we reuse it for every
// variant rather than redefining the base loader.
// ---------------------------------------------------------------------------
Mesh* Mesh_LoadOrFindByName(MeshAssetCache& cache, const char* name, const char* dir) {
    char base[256];  // v8 — file leaf name
    char key[256];   // v7 — registry key

    // 1. Base-LOD names.
    BuildLodFileName(name, dir, base, 0, key);

    // 2. Cache lookup: FindStockObject(key) else FindStockObject(base).
    Mesh* obj = cache.Find(key);
    if (!obj)
        obj = cache.Find(base);
    if (obj)
        return obj;

    // 3. Miss -> load+register the base (LoadAndRegister(base, key)).
    obj = cache.LoadOrFind(base, key);

    // 4. The "_s" variant (only emitted when LOD is enabled).
    if (BuildLodFileName(name, dir, base, -1, key))
        cache.LoadOrFind(base, key);

    // 5. Multi-LOD mode: load LOD frames 1..2.
    if ((LodModeByte() & 0x7F) == 1) {
        for (int i = 1; i < 3; ++i) {
            // Skip indices BuildLodFileName declines (returns 0); stop at i >= 3.
            while (!BuildLodFileName(name, dir, base, i, key)) {
                if (++i >= 3)
                    return obj;
            }
            cache.LoadOrFind(base, key);
        }
    }

    return obj;
}

} // namespace guild::render
