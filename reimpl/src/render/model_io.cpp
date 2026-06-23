#include "render/model_io.h"

#include <cstring>  // memcpy

namespace guild::render {

MeshGeometry* Model::View() {
    geom.vertices = vertices.empty() ? nullptr : vertices.data();
    geom.polygons = polygons.empty() ? nullptr : polygons.data();
    geom.polyCount = (i32)polygons.size();
    geom.polyCap = (i32)polygons.size();
    geom.vertexCount = (i32)vertices.size();
    return &geom;
}

namespace {
// Little-endian readers (the original reads multi-byte fields straight out of the
// file byte buffer; on the 32-bit x86 target that is a raw little-endian load).
struct Reader {
    const u8* p;
    const u8* end;
    bool ok = true;
    u32 u32v() {
        if (p + 4 > end) { ok = false; return 0; }
        u32 v; std::memcpy(&v, p, 4); p += 4; return v;
    }
    float f32() {
        if (p + 4 > end) { ok = false; return 0.0f; }
        float v; std::memcpy(&v, p, 4); p += 4; return v;
    }
    u8 byte() {
        if (p + 1 > end) { ok = false; return 0; }
        return *p++;
    }
};
constexpr u32 kMagic = 0x4C444D47;  // 'GMDL' little-endian
} // namespace

bool LoadSyntheticModel(const u8* data, size_t size, Model& out) {
    Reader r{data, data + size};
    if (r.u32v() != kMagic)
        return false;
    u32 vcount = r.u32v();
    u32 pcount = r.u32v();
    if (!r.ok)
        return false;

    // Hardening (W11): bound the declared counts by the bytes that remain before
    // allocating. A vertex record is 21 bytes (5 floats + 1 light byte) and a
    // polygon record is 26 bytes (3 indices + 3 floats + 2 flag bytes); a count
    // larger than the buffer can back is malformed and would otherwise drive a
    // huge std::vector::assign (bad_alloc). A well-formed file always fits.
    const size_t remain = static_cast<size_t>(r.end - r.p);
    if (vcount > remain / 21u)
        return false;
    if (pcount > remain / 26u)
        return false;

    out.vertices.assign(vcount, Vertex{});
    for (u32 i = 0; i < vcount; ++i) {
        Vertex& vx = out.vertices[i];
        vx.x = r.f32();
        vx.y = r.f32();
        vx.z = r.f32();
        vx.u = r.f32();
        vx.v = r.f32();
        vx.lightIdx = r.byte();
    }
    if (!r.ok)
        return false;

    out.polygons.assign(pcount, Polygon{});
    for (u32 i = 0; i < pcount; ++i) {
        Polygon& poly = out.polygons[i];
        u32 i0 = r.u32v();
        u32 i1 = r.u32v();
        u32 i2 = r.u32v();
        poly.uvX = r.f32();
        poly.uvY = r.f32();
        poly.uvZ = r.f32();
        poly.flags36 = r.byte();
        poly.flags38 = r.byte();
        if (!r.ok || i0 >= vcount || i1 >= vcount || i2 >= vcount)
            return false;
        poly.v0 = &out.vertices[i0];
        poly.v1 = &out.vertices[i1];
        poly.v2 = &out.vertices[i2];
    }
    if (!r.ok)
        return false;

    out.View();
    return true;
}

} // namespace guild::render
