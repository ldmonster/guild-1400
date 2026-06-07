#include "render/tile_lighting.h"

#include "util/coord.h"   // ConvertX (x87 truncate-toward-zero)

#include <cmath>

namespace guild::render {

// File-scope constants recovered byte-for-byte from gilde.exe .rdata (get_bytes):
//   flt_628878 = -4.0   (0x628878: 00 00 80 C0)   X-slope normal scale base
//   flt_628874 =  2.0   (0x628874: 00 00 00 40)   Z-slope normal scale base
//   dbl_62887C = -1023.0 (0x62887c: 00 00 00 00 00 F8 8F C0) falloff-LUT index scale
//   flt_62886C =  0.5   (0x62886c: 00 00 00 3F)   falloff-scale half (light[37]*0.5)
//   flt_628868 =  0.5   (0x628868: 00 00 00 3F)   circle r^2 round bias
static constexpr double kFalloffIndexScale = -1023.0;  // dbl_62887C

// ---------------------------------------------------------------------------
// gilde.exe 0x5c4718 — VIBE_Heightmap_ComputeTileIllumination (per-tile lookup).
//   cell = (mask & x) + size * (mask & y);  t = types[cell];
//   if (t & 0x80) return 0;  else return illum.value[t];
// (the once-per-floor name->pattern build of `illum` is deferred — see header.)
// ---------------------------------------------------------------------------
u8 ComputeTileIllumination(const u8* types, i32 size, i32 x, i32 y,
                           const TileIlluminationTable& illum) {
    i32 mask = size - 1;                        // ecx = *a3 - 1
    i32 cell = (mask & x) + size * (mask & y);  // edx = (mask&x) + size*(mask&y)
    u8 t = types[cell];                         // *(a3[5] + cell)
    if (t & 0x80u)                              // test byte, 0x80 -> jnz: return 0
        return 0;
    // byte_1405100[type byte] — type byte (0..7) selects the light source.
    return illum.value[t & 7u];
}

// ---------------------------------------------------------------------------
// gilde.exe 0x5bc294 — VIBE_Floor_StampLightCircle (type-grid circle stamp).
// ---------------------------------------------------------------------------
void StampLightCircle(u8* types, i32 size, i32 cx, i32 cy, i32 r, i32 r2cap) {
    if (r <= 1) {
        // r <= 1: stamp only the centre cell (the `*(... size*cy + cx ...) |= 0x80`
        // single-cell path the original takes when the radius rounds below 1).
        types[(i32)size * cy + cx] |= 0x80u;
        return;
    }
    // Row span: [cy-r, cy+r] clamped to [0, size-1].
    i32 rowStart = cy - r;
    if (rowStart < 0) rowStart = 0;
    i32 rowEnd = cy + r;
    if (rowEnd > size - 1) rowEnd = size - 1;   // v11-style (*v3 - 1) clamp
    for (i32 row = rowStart; row <= rowEnd; ++row) {
        // Column span: [cx-r, cx+r] clamped to [0, size-1].
        i32 colEnd = cx + r;
        if (colEnd > size - 1) colEnd = size - 1;   // v11 = *v3 - 1; if (>=) clamp
        i32 colStart = cx - r;
        if (colStart < 0) colStart = 0;             // if (cx-r < 0) -> 0
        for (i32 col = colStart; col <= colEnd; ++col) {
            // (row-cy)^2 + (col-cx)^2 < r2cap  ->  OR the lit bit
            i32 dy = row - cy, dx = col - cx;
            if (dy * dy + dx * dx < r2cap)
                types[(i32)size * row + col] |= 0x80u;
        }
    }
}

// ---------------------------------------------------------------------------
// gilde.exe 0x5c47dc — VIBE_Heightmap_BuildLitTileGeometry, height->elevation byte.
//   v28 = ((double)(int16)h * scaleH + originY - tileMinY) * invScaleY;
//   elev = (int)v28   (ConvertX truncate toward zero), stored as a byte.
// ---------------------------------------------------------------------------
u8 BuildTileElevationByte(u8 h, float scaleH, float originY, float tileMinY,
                          float invScaleY) {
    // (int16)h: the engine sign-extends the height byte through a 16-bit load
    // before the int->double convert (movsx). For a 0..255 byte this is just h.
    double v28 = ((double)(short)(unsigned short)h * (double)scaleH
                  + (double)originY - (double)tileMinY) * (double)invScaleY;
    return (u8)(int)util::ConvertX(v28);
}

// gilde.exe 0x5c47dc — VIBE_Heightmap_BuildLitTileGeometry, 2x2 box-average mip.
//   out[i][j] = (a + b + c + d) >> 2   (the 4-sample sum / 4, matching v36>>2).
i32 MipDownsample(const u8* src, i32 n, u8* dst) {
    i32 m = n >> 1;                       // v98 = v94 >> 1
    for (i32 i = 0; i < m; ++i) {
        for (i32 j = 0; j < m; ++j) {
            i32 a = src[(2 * i) * n + (2 * j)];
            i32 b = src[(2 * i) * n + (2 * j + 1)];
            i32 c = src[(2 * i + 1) * n + (2 * j)];
            i32 d = src[(2 * i + 1) * n + (2 * j + 1)];
            dst[i * m + j] = (u8)((unsigned)(a + b + c + d) >> 2);
        }
    }
    return m;
}

// ---------------------------------------------------------------------------
// gilde.exe 0x5bc45c — VIBE_Floor_BuildTilePolys, slope-based dynamic-light stamp.
//   For each interior cell the engine builds a surface normal from the wrapped
//   4-neighbour height differences, normalises it, dots it with the light dir, and
//   when negative (facing the light) ORs a 0..127 falloff intensity into the cell's
//   light-accumulator byte. Matches the decompiled inner double-loop 1:1.
// ---------------------------------------------------------------------------
void StampSlopeLight(u8* accum, const u8* heights, i32 size,
                     const SlopeLightParams& p, const float* falloffLut) {
    i32 mask = size - 1;                       // v61 = v59 - 1
    for (i32 y = 0; y < size; ++y) {           // v51 loop
        for (i32 x = 0; x < size; ++x) {       // v14 loop
            // dx = h[x+1] - h[x-1]   (wrapped)        v45 - v71
            i32 hxp = heights[(mask & y) * size + (mask & (x + 1))];
            i32 hxm = heights[(mask & y) * size + (mask & (x - 1))];
            // dz = h[y+1] - h[y-1]   (wrapped)
            i32 hzp = heights[(mask & (y + 1)) * size + (mask & x)];
            i32 hzm = heights[(mask & (y - 1)) * size + (mask & x)];

            float nx = (float)(hxp - hxm) * p.normalScaleX;  // v34 = (dx) * v54
            float ny = p.normalScaleY;                       // v35 = v55 (constant)
            float nz = (float)(hzp - hzm) * p.normalScaleZ;  // v36 = (dz) * v52

            // inv = 1 / sqrt(nx^2 + ny^2 + nz^2)
            float inv = 1.0f / std::sqrt(nx * nx + ny * ny + nz * nz);
            nx *= inv;
            ny = p.normalScaleY * inv;          // v35 = v55 * v31
            nz *= inv;

            // d = nx*L0 + ny*L1 + nz*L2   (dot with rotated light dir)
            float d = nx * p.lightDir[0] + ny * p.lightDir[1] + nz * p.lightDir[2];
            if (d < 0.0f) {
                // idx = (int)(d * -1023.0)   (ConvertX truncate); LUT lookup
                int idx = (int)util::ConvertX((double)d * kFalloffIndexScale);
                float lit = p.falloffScale * falloffLut[idx];   // v53 * lut[idx]
                int v = (int)lit;                                // ConvertX
                if ((unsigned)v > 0x7F) v = 127;                 // clamp to 0..127
                accum[(i32)size * y + x] |= (u8)v;               // *v17 |= v62
            }
        }
    }
}

// ---------------------------------------------------------------------------
// gilde.exe 0x5bc45c — VIBE_Floor_BuildTilePolys, quad poly-visibility cascade.
// Exact branch tree from the disasm at 0x5bca69..0x5bcb30 (bl=h0, dl=h1, dh=h2,
// bh=h3). loc_5BC9A2 = OR 0x80 (Hidden); loc_5BCA8D/loc_5BCAB6/loc_5BCB07 =
// AND 0x7F (Visible); fall-through to loc_5BC9A6 = no change.
// ---------------------------------------------------------------------------
QuadPolyAction QuadPolyVisible(bool h0, bool h1, bool h2, bool h3) {
    // Block A (5bca69): h0 && !h2 && !h3 && !h1 -> HIDDEN
    if (h0 && !h2 && !h3 && !h1) return QuadPolyAction::Hidden;
    // Block A2 (5bca7d): h0 && !h2 && h3 && h1 -> VISIBLE
    if (h0 && !h2 && h3 && h1) return QuadPolyAction::Visible;
    // Block A3 (5bca96): !h0 && h2 && !h3 && h1 -> VISIBLE
    if (!h0 && h2 && !h3 && h1) return QuadPolyAction::Visible;
    // Block A4 (5bcaa6): h0 && h2 && !h3 && h1 -> VISIBLE
    if (h0 && h2 && !h3 && h1) return QuadPolyAction::Visible;
    // Block A5 (5bcabf): !h0 && !h2 && h3 && !h1 -> VISIBLE
    if (!h0 && !h2 && h3 && !h1) return QuadPolyAction::Visible;
    // Block A6 (5bcacf): h0 && h2 && h3 && !h1 -> HIDDEN
    if (h0 && h2 && h3 && !h1) return QuadPolyAction::Hidden;
    // Block A7 (5bcae3): !h0 && !h2 && !h3 && h1 -> HIDDEN
    if (!h0 && !h2 && !h3 && h1) return QuadPolyAction::Hidden;
    // Block A8 (5bcaf7): h0 && !h2 && !h3 && h1 -> VISIBLE
    if (h0 && !h2 && !h3 && h1) return QuadPolyAction::Visible;
    // Block A9 (5bcb10): !h0 && h2 && h3 && !h1 -> HIDDEN
    if (!h0 && h2 && h3 && !h1) return QuadPolyAction::Hidden;
    // Every other corner pattern leaves the poly flag unchanged.
    return QuadPolyAction::Unchanged;
}

} // namespace guild::render
