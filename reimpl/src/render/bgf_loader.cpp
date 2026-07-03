#include "render/bgf_loader.h"

#include <cstring>
#include <map>

// gilde.exe d3_io_fl.c — .BGF fast-chunk binary loader.
//
// The production reader streamed bytes through the VFS one field at a time via
// the VIBE_Bio_Read* helpers (each a thin VIBE_Vfs_ReadStream(dst,n,h,1) call).
// On 32-bit x86 those "swap" helpers do NOT actually byte-swap — the data is
// little-endian and copied verbatim. We reproduce that exactly with a little-
// endian cursor over a flat buffer.
namespace guild::render {

namespace {

// A bounds-checked little-endian read cursor (models VIBE_Vfs_ReadStream over a
// memory stream). Every reader returns false on a short read, matching the
// engine's truncation handling (a partial record aborts the load).
struct Cursor {
    const u8* p;
    const u8* end;

    bool ReadBytes(void* dst, size_t n) {
        if (static_cast<size_t>(end - p) < n) return false;
        std::memcpy(dst, p, n);
        p += n;
        return true;
    }
    // VIBE_Bio_ReadByte @0x5DC850.
    bool ReadByte(u8* v) { return ReadBytes(v, 1); }
    // VIBE_Bio_ReadDword / VIBE_Bio_ReadDwordSwapArgs @0x5DC894/0x5DC8B0 (LE, no swap).
    bool ReadU32(u32* v) {
        u8 b[4];
        if (!ReadBytes(b, 4)) return false;
        *v = (u32)b[0] | ((u32)b[1] << 8) | ((u32)b[2] << 16) | ((u32)b[3] << 24);
        return true;
    }
    bool ReadI32(i32* v) { return ReadU32(reinterpret_cast<u32*>(v)); }
    bool ReadF32(float* v) {
        u32 bits;
        if (!ReadU32(&bits)) return false;
        std::memcpy(v, &bits, 4);
        return true;
    }
    // VIBE_Bio_ReadVec3 @0x5DC938 — three consecutive LE floats.
    bool ReadVec3(float out[3]) {
        return ReadF32(&out[0]) && ReadF32(&out[1]) && ReadF32(&out[2]);
    }
    // VIBE_Bio_ReadString @0x5DC86C — read bytes until (and including) a NUL.
    bool ReadString(std::string& out) {
        out.clear();
        for (;;) {
            u8 c;
            if (!ReadByte(&c)) return false;
            if (!c) return true;
            out.push_back(static_cast<char>(c));
        }
    }
    bool AtOrPastEnd() const { return p >= end; }
    size_t Offset(const u8* base) const { return static_cast<size_t>(p - base); }
};

} // namespace

// gilde.exe 0x5F86FC — VIBE_Vfs_FindChunkStart.
//   Skips the leading 4-byte tag, then reads single-byte script tokens. A '-'
//   (0x2D) introduces a chunk: read u32 size + u32 magic; on magic match return
//   the position past the magic, else seek back (size-4) and continue. A '+'
//   (0x2B) or EOF (ReadToken returns 0x2B on EOF) ends the scan with failure. Any
//   other byte introduces a sized block (read u32 size, skip it).
bool BgfFindChunkStart(const u8* data, size_t size, size_t* chunkOffset) {
    if (!data || size < 4) return false;
    Cursor c{data, data + size};

    // Leading tag dword (the original reads 4 bytes and discards them).
    u32 tag;
    if (!c.ReadU32(&tag)) return false;

    for (;;) {
        // VIBE_Script_ReadToken @0x5E3BD0: read one byte; on EOF yield '+'; if the
        // byte is > 0x3A yield '\'' (a name token), else the byte itself.
        u8 raw;
        u8 token;
        if (!c.ReadByte(&raw))
            token = kBgfTokenEnd;          // EOF -> 0x2B
        else if (raw > 0x3A)
            token = kBgfTokenName;         // 0x27
        else
            token = raw;

        if (token == kBgfTokenChunk) {     // '-': chunk header follows
            u32 chunkSize, magic;
            if (!c.ReadU32(&chunkSize)) return false;
            if (!c.ReadU32(&magic)) return false;
            if (magic == kBgfFastChunkMagic) {
                *chunkOffset = c.Offset(data);  // positioned just past the magic
                return true;
            }
            // Not the fast-chunk: seek back (chunkSize - 4) and keep scanning.
            // (The original VIBE_Vfs_Seek(h, chunkSize-4, SEEK_CUR).)
            long back = static_cast<long>(chunkSize) - 4;
            const u8* np = c.p + back;
            if (np < data || np > c.end) return false;
            c.p = np;
            continue;
        }
        if (token == kBgfTokenEnd) {       // '+': end of stream / EOF
            return false;
        }
        // Every OTHER token (the name token '\'' for any byte > 0x3A, AND any other
        // small byte that isn't '-'/'+') is a sized block: read a u32 length and
        // seek forward past it.  (gilde.exe 0x5F86FC: `if (Token != '+') ReadDword;
        // Seek(+len)` — it skips for ANY non-'-'/'+' token, not just names.)
        u32 blockSize;
        if (!c.ReadU32(&blockSize)) return false;
        const u8* np = c.p + static_cast<long>(blockSize);
        if (np < data || np > c.end) return false;
        c.p = np;
    }
}

// gilde.exe 0x5F87B8 — VIBE_Model_LoadFastChunk (buffer form).
bool LoadFastChunk(const u8* data, size_t size, BgfModel& out) {
    size_t off;
    if (!BgfFindChunkStart(data, size, &off)) return false;

    Cursor c{data + off, data + size};

    // Header: materialCount (+0x1E0), vertexCount (+0x44), polyCount (+0x4C).
    if (!c.ReadU32(&out.materialCount)) return false;
    if (!c.ReadU32(&out.vertexCount)) return false;
    if (!c.ReadU32(&out.polyCount)) return false;

    // Hardening (W11): bound the declared counts by the bytes that remain before
    // allocating. The engine streamed from a finite file, so each declared record
    // is necessarily backed by bytes on disk; a count larger than the buffer can
    // satisfy is a truncated/malformed asset. Bounding here turns a would-be huge
    // std::vector::assign (bad_alloc / heap exhaustion) into the same clean parse
    // failure the per-record reads already produce — without changing any valid
    // load (a valid file always has the bytes). Each vertex record is >=24 bytes
    // (pos+normal); each poly >=12 (the 3 index dwords), so these are conservative
    // lower bounds that never reject a real asset.
    const size_t remain = static_cast<size_t>(c.end - c.p);
    if (out.vertexCount > remain / 24u) return false;
    if (out.polyCount   > remain / 12u) return false;

    // Vertex block: the engine allocates 24*(vertexCount+8) bytes and reads
    // (vertexCount+8) (pos,normal) pairs. We mirror the +8 slack so downstream
    // index math is identical.
    // (vertexCount is now bounded by remain/24 above, so vertexCount+8 cannot
    // overflow a u32.)
    const u32 vtxStored = out.vertexCount + 8;
    out.vertices.assign(vtxStored, BgfVertex{});
    if (static_cast<i32>(out.vertexCount) > 0) {
        for (u32 i = 0; i < vtxStored; ++i) {
            if (!c.ReadVec3(out.vertices[i].pos)) return false;
            if (!c.ReadVec3(out.vertices[i].normal)) return false;
        }
    }

    // Object flags dword (+0x1D4), read with the plain ReadDword.
    if (!c.ReadU32(&out.objectFlags)) return false;

    // Poly block: polyCount * 56 bytes. On-disk order (from LoadFastChunk):
    //   vtx[0], vtx[1], vtx[2] (3 u32), vec3 uv0, vec3 uv1, vec3 uv2, matIndex.
    out.polygons.assign(out.polyCount, BgfPolygon{});
    for (u32 i = 0; i < out.polyCount; ++i) {
        BgfPolygon& q = out.polygons[i];
        if (!c.ReadU32(&q.vtx[0])) return false;
        if (!c.ReadU32(&q.vtx[1])) return false;
        if (!c.ReadU32(&q.vtx[2])) return false;
        if (!c.ReadVec3(q.uv0)) return false;
        if (!c.ReadVec3(q.uv1)) return false;
        if (!c.ReadVec3(q.uv2)) return false;
        q.texId = -1;  // *(poly+36) = -1; resolved by the texture stage.
        if (static_cast<i32>(out.materialCount) <= 254) {
            u8 b;
            if (!c.ReadByte(&b)) return false;
            q.matIndex = (b == 0xFF) ? -1 : static_cast<i32>(b);
        } else {
            if (!c.ReadI32(&q.matIndex)) return false;
        }
    }

    // Material block: materialCount entries (3 strings + 6 bytes). Each entry is
    // >=9 bytes on disk (3 NUL terminators + 6 flag bytes), so bound the count by
    // the remaining bytes before allocating (W11 hardening — see note above).
    if (out.materialCount > static_cast<size_t>(c.end - c.p) / 9u) return false;
    out.materials.assign(out.materialCount, BgfMaterial{});
    for (u32 i = 0; i < out.materialCount; ++i) {
        BgfMaterial& mat = out.materials[i];
        if (!c.ReadString(mat.name0)) return false;
        if (!c.ReadString(mat.name1)) return false;
        if (!c.ReadString(mat.name2)) return false;
        if (!c.ReadByte(&mat.flag)) return false;
        if (!c.ReadByte(&mat.b1)) return false;
        if (!c.ReadByte(&mat.b2)) return false;
        if (!c.ReadByte(&mat.b3)) return false;
        if (!c.ReadByte(&mat.b4)) return false;
        if (!c.ReadByte(&mat.b5)) return false;
    }

    // Dummy block: dummyCount (read via the SwapArgs reader) then 88-byte entries.
    if (!c.ReadU32(&out.dummyCount)) return false;
    // Each dummy is >=25 bytes (name NUL + 2 vec3); bound the count by remaining
    // bytes before allocating (W11 hardening — see note above).
    if (out.dummyCount > static_cast<size_t>(c.end - c.p) / 25u) return false;
    out.dummies.assign(out.dummyCount, BgfDummy{});
    for (u32 i = 0; i < out.dummyCount; ++i) {
        BgfDummy& d = out.dummies[i];
        std::string name;
        if (!c.ReadString(name)) return false;
        std::memset(d.name, 0, sizeof(d.name));
        std::memcpy(d.name, name.data(),
                    name.size() < sizeof(d.name) - 1 ? name.size() : sizeof(d.name) - 1);
        if (!c.ReadVec3(d.pos)) return false;
        if (!c.ReadVec3(d.rot)) return false;
    }

    return true;
}

MeshGeometry* BgfGeometry::View() {
    geom.vertices    = vertices.data();
    geom.polygons    = polygons.data();
    geom.polyCount   = static_cast<i32>(polygons.size());
    geom.polyCap     = static_cast<i32>(polygons.size());
    geom.vertexCount = static_cast<i32>(vertices.size());
    return &geom;
}

bool BuildGeometry(const BgfModel& m, BgfGeometry& out) {
    // The engine's runtime vertex (80-byte record) carries ONE (u,v) pair, but the
    // on-disk poly stores per-CORNER uv rows. The original loader's vertex-dedup
    // step therefore splits a position-shared corner whose UV differs into its own
    // runtime vertex (and merges corners whose (index, u, v) coincide). Without
    // this, meshes that reuse a position with different UVs per face (every
    // fachwerk wall / roof) smear their textures (last-written UV wins).
    // One runtime vertex per unique (on-disk index, u-bits, v-bits) triple;
    // polygon ORDER stays 1:1 with the model (consumers map poly i -> model poly i).
    struct Key {
        u32 idx, ub, vb;
        bool operator<(const Key& o) const {
            if (idx != o.idx) return idx < o.idx;
            if (ub != o.ub) return ub < o.ub;
            return vb < o.vb;
        }
    };
    std::map<Key, u32> remap;
    std::vector<u32> corner((std::size_t)m.polygons.size() * 3, 0u);
    std::vector<Key> order;                    // creation order -> Key
    order.reserve(m.polygons.size() * 3);
    for (size_t i = 0; i < m.polygons.size(); ++i) {
        const BgfPolygon& q = m.polygons[i];
        for (int k = 0; k < 3; ++k) {
            if (q.vtx[k] >= m.vertices.size()) return false;
            u32 ub, vb;
            std::memcpy(&ub, &q.uv0[k], 4);
            std::memcpy(&vb, &q.uv1[k], 4);
            const Key key{q.vtx[k], ub, vb};
            auto it = remap.find(key);
            u32 ri;
            if (it == remap.end()) {
                ri = (u32)order.size();
                remap.emplace(key, ri);
                order.push_back(key);
            } else {
                ri = it->second;
            }
            corner[i * 3 + (std::size_t)k] = ri;
        }
    }

    out.vertices.assign(order.size(), Vertex{});
    for (size_t i = 0; i < order.size(); ++i) {
        const Key& key = order[i];
        Vertex& v = out.vertices[i];
        v.x = m.vertices[key.idx].pos[0];
        v.y = m.vertices[key.idx].pos[1];
        v.z = m.vertices[key.idx].pos[2];
        std::memcpy(&v.u, &key.ub, 4);
        std::memcpy(&v.v, &key.vb, 4);
    }

    out.polygons.assign(m.polygons.size(), Polygon{});
    for (size_t i = 0; i < m.polygons.size(); ++i) {
        const BgfPolygon& q = m.polygons[i];
        Polygon& p = out.polygons[i];
        p.v0 = &out.vertices[corner[i * 3 + 0]];
        p.v1 = &out.vertices[corner[i * 3 + 1]];
        p.v2 = &out.vertices[corner[i * 3 + 2]];
        p.uvX = q.uv0[0];
        p.uvY = q.uv0[1];
        p.uvZ = q.uv0[2];
        p.flags36 = 0;
        p.flags38 = 0;
        // TWO-SIDED material (+194 bit 1, the loader's "flag0 BYTE2 |= 2" alpha/
        // no-cull route): the crossed-plane foliage/fence polys draw from BOTH
        // sides — poly +36 bit 4 exempts them from the projection backface cull
        // (ProjectObjectVertices keeps + marks 0x40), and +38 bit 2 makes the
        // raster reverse the vertex load for back-wound tris (the 0x5f6f16
        // winding branch). Without these, half of every crossed-plane tree
        // crown culls away (sparse trees).
        if (q.matIndex >= 0 && (size_t)q.matIndex < m.materials.size() &&
            (m.materials[(size_t)q.matIndex].b2 & 2)) {
            p.flags36 |= 0x10;
            p.flags38 |= 0x04;
        }
        p.matIndex = q.matIndex;   // carry the material index for texture binding
    }
    return true;
}

} // namespace guild::render
