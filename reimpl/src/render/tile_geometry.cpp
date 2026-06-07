#include "render/tile_geometry.h"

namespace guild::render {

// flt_628B4C = 2.0 — the terrain type-byte -> light multiplier (0x628b4c: 00 00 00 40).
static constexpr float kTypeLightMul = 2.0f;
// dword_1406A84 stride/base for the sort-key shift is folded into texSortId by the
// caller (>>7 of the texture record stride); see header.

// Clamp exactly as the engine: cast to int, cap high at 255, store low byte.
//   if ((int)x > 255) byte = 0xFF; else byte = (u8)(int)x;
u8 ClampLightByte(float x) {
    int t = (int)x;            // truncate toward zero
    if (t > 255) return 0xFF;  // the `LOBYTE(v)=-1` 0xFF store
    return (u8)t;              // low byte (engine stored the low 8 bits)
}

// gilde.exe 0x5bf22c — VIBE_Floor_RenderTerrain per-vertex build.
u32 BuildTileVertex(Vertex& v, const TileLightParams& p, const float acc[3],
                    u8 heightSample, u8 typeByte) {
    // world = heightAxis * h + acc   (v287/v304/v321 + v327/v328/v329)
    float h = (float)heightSample;
    v.x = p.heightAxis[0] * h + acc[0];
    v.y = p.heightAxis[1] * h + acc[1];
    v.z = p.heightAxis[2] * h + acc[2];

    // l = (typeByte & 0x7F) * 2.0   (v384 = (type & 0x7F) * flt_628B4C)
    float l = (float)(typeByte & 0x7F) * kTypeLightMul;

    u8 R, G, B;
    if ((i8)typeByte >= 0) {
        // high bit clear: ambient-only branch
        R = ClampLightByte(p.lightScale[0] * l + p.ambient[0]);  // +66
        G = ClampLightByte(p.lightScale[1] * l + p.ambient[1]);  // +65
        B = ClampLightByte(p.lightScale[2] * l + p.ambient[2]);  // +64
    } else {
        // high bit set: shadow-biased branch (adds flt_13FD530/534/538)
        R = ClampLightByte(p.lightScale[0] * l + p.shadowBias[0] + p.ambient[0]);
        G = ClampLightByte(p.lightScale[1] * l + p.shadowBias[1] + p.ambient[1]);
        B = ClampLightByte(p.lightScale[2] * l + p.shadowBias[2] + p.ambient[2]);
    }

    // Engine byte layout: +66 = R, +65 = G(_pad41), +64 = B(color0); then the
    // +64 dword is copied to +68. Our Vertex names: color0@+64, _pad41@+65,
    // lightIdx@+66 (see geometry_types.h). Store faithfully by offset.
    v.lightIdx = R;   // +0x42 (+66)
    v._pad41   = G;   // +0x41 (+65)
    v.color0   = B;   // +0x40 (+64)
    // Packed dword the engine stored at +64 (and mirrored to +68): B | G<<8 | R<<16.
    return (u32)B | ((u32)G << 8) | ((u32)R << 16);
}

// gilde.exe 0x5be668 — VIBE_Floor_TransformTileGeometry poly draw-list append.
i32 AppendTilePolysToDrawList(const Polygon* polys, i32 polyCount,
                              const u32* texSortId, DrawList* out) {
    // Remaining capacity: min(capacity - count, polyCount)  (v44/v80 clamp).
    i32 remain = out->capacity - out->count;
    i32 limit = (remain <= polyCount) ? remain : polyCount;
    i32 appended = 0;
    for (i32 i = 0; i < limit; ++i) {
        const Polygon& poly = polys[i];
        // Only front-facing polys: flags36 high bit set (*(result+36) < 0).
        if ((i8)poly.flags36 >= 0) continue;
        DrawListEntry& e = out->entries[out->count];
        e.sortKey = texSortId[i];   // ((tex - base) >> 7) + 1, or 0 when untextured
        e.poly = const_cast<Polygon*>(&poly);
        ++out->count;
        ++appended;
    }
    return appended;
}

} // namespace guild::render
