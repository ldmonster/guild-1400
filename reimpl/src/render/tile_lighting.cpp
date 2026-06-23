#include "render/tile_lighting.h"

#include "render/heightmap.h"   // Heightmap (BuildLitTileGeometry target)
#include "util/coord.h"         // ConvertX (x87 truncate-toward-zero)

#include <cctype>
#include <cmath>
#include <cstring>

namespace guild::render {

// File-scope constants recovered byte-for-byte from gilde.exe .rdata (get_bytes):
//   flt_628878 = -4.0   (0x628878: 00 00 80 C0)   X-slope normal scale base
//   flt_628874 =  2.0   (0x628874: 00 00 00 40)   Z-slope normal scale base
//   dbl_62887C = -1023.0 (0x62887c: 00 00 00 00 00 F8 8F C0) falloff-LUT index scale
//   flt_62886C =  0.5   (0x62886c: 00 00 00 3F)   falloff-scale half (light[37]*0.5)
//   flt_628868 =  0.5   (0x628868: 00 00 00 3F)   circle r^2 round bias
static constexpr double kFalloffIndexScale = -1023.0;  // dbl_62887C

// ---------------------------------------------------------------------------
// gilde.exe 0x5c4690 — the 15 terrain-class match patterns (9-byte stride),
// dumped byte-for-byte from the binary (get_bytes(0x5c4690, 9*15)).
// ---------------------------------------------------------------------------
const char kTerrainTypePatterns[15][9] = {
    "_ill*", "_unk*", "SAND", "ERDE", "WIESE", "MOOR", "PFLASTER", "KIESEL",
    "FELS", "EIS", "WASSER", "WEG", "WEG", "_ill*", "",
};

// ---------------------------------------------------------------------------
// gilde.exe 0x5c4718 — VIBE_Heightmap_ComputeTileIllumination, the once-per-floor
// name -> terrain-class table build (the @0x5c475c..0x5c479e loop):
//   byte_1405100[i] = 1;                                      (@0x5c475c)
//   if (name[0]) for k in [0,15):                             (@0x5c477f)
//       if (strstr(StrToUpper(name), &aIll[9*k])) {           (@0x5cb930)
//           byte_1405100[i] = k; break;                       (@0x5c4791)
//       }
// loc_5CB930 is a plain strstr (empty needle -> returns the haystack, i.e. a
// match — pattern 14 is the empty string and therefore the always-match tail).
// VIBE_Util_StrToUpper @0x5e9f50's body was not captured; the uppercase-the-name
// semantics follow from its name + call shape (inert for the shipped names,
// which are already uppercase).
// ---------------------------------------------------------------------------
TileIlluminationTable BuildTileIlluminationTable(const TileLightSource names[8]) {
    TileIlluminationTable t{};
    for (int i = 0; i < 8; ++i) {                    // edi slot loop
        t.value[i] = 1;                              // byte_1405100[edi] = 1
        const char* name = names[i].name;
        if (!name[0])                                // test bl,bl -> next slot
            continue;
        char upper[64];                              // StrToUpper @0x5e9f50
        for (int c = 0; c < 64; ++c) {
            upper[c] = (char)std::toupper((unsigned char)name[c]);
            if (!name[c]) break;
        }
        upper[63] = '\0';
        for (int k = 0; k < 15; ++k) {               // ebx pattern loop
            // loc_5CB930 strstr: empty needle matches (returns the haystack).
            if (std::strstr(upper, kTerrainTypePatterns[k]) != nullptr) {
                t.value[i] = (u8)k;                  // mov byte_1405100[edi], bl
                break;                               // jmp loc_5C4797
            }
        }
    }
    return t;
}

// ---------------------------------------------------------------------------
// gilde.exe 0x5c4718 — VIBE_Heightmap_ComputeTileIllumination (per-tile lookup).
//   cell = (mask & x) + size * (mask & y);  t = types[cell];
//   if (t & 0x80) return 0;  else return illum.value[t];
// ---------------------------------------------------------------------------
u8 ComputeTileIllumination(const u8* types, i32 size, i32 x, i32 y,
                           const TileIlluminationTable& illum) {
    i32 mask = size - 1;                        // ecx = *a3 - 1
    i32 cell = (mask & x) + size * (mask & y);  // edx = (mask&x) + size*(mask&y)
    u8 t = types[cell];                         // *(a3[5] + cell)
    if (t & 0x80u)                              // test byte ptr [eax],80h -> jnz: return 0
        return 0;
    // HARDEN render_08: the binary indexes `byte_1405100[t]` with the FULL byte t
    // (disasm @0x5c47b7..0x5c47be: mov al,[eax]; and eax,0FFh; mov al,byte_1405100[eax])
    // — NO `& 7` mask. The 0x80 gate above guarantees t in [0,127]; the engine's
    // type grid only ever stores texture-slot indices 0..7 (8 slots @Floor+0x1A64),
    // so on the entire REACHABLE domain t == (t & 7) and the lookup is identical.
    // The `& 7` here is a behavior-identical memory-safety pin keeping the 8-entry
    // table in-bounds for malformed t in [8,127] (which the binary would read OOB).
    return illum.value[t & 7u];
}

// ---------------------------------------------------------------------------
// gilde.exe 0x5bc294 — VIBE_Floor_StampLightCircle (type-grid circle stamp).
// ---------------------------------------------------------------------------
// HARDEN render_08: disasm-verified @0x5bc294. The integer stamp body (clamps,
// distance test, single-cell path) is byte-for-byte 1:1 (row clamp @0x5bc3b8/0x5bc3da
// to [0,size-1], col clamp @0x5bc3e5, dist `(row-cy)^2+(col-cx)^2 < r2cap` @0x5bc419,
// single cell `types[size*cy+cx]` @0x5bc44b). BOUNDARY: the binary chooses the
// single-cell vs circle path on the PRE-truncation FLOAT radius (`v15 > 1.0`
// @0x5bc371), where r==(int)ConvertX(v15) and r2cap==(int)ConvertX(v15*v15+0.5).
// With r/r2cap arriving already truncated this API folds that to `r <= 1`, which
// agrees with the binary for every radius except the float interval (1.0, 2.0)
// (binary: circle path with r==1; here: single cell). The float radius derivation
// (bone-chain transform + pick) is the deferred outer walk, so the float-branch
// distinction is not reconstructible from the integer inputs alone.
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
// gilde.exe 0x5c47dc — VIBE_Heightmap_BuildLitTileGeometry, the COMPLETE driver
// (see the header for the captured-decompile derivation of every line).
// ---------------------------------------------------------------------------
u32 BuildLitTileGeometry(const LitFloorView& fl, Heightmap* hm,
                         const TileIlluminationTable& illum) {
    if (!hm || !hm->heights || !hm->entries || fl.size <= 0 || !fl.heights ||
        !fl.texGrid || hm->size <= 0 || hm->scaleY == 0.0f)
        return 0;

    const float invScaleY = (float)(1.0 / (double)hm->scaleY);  // v4 -> v106 (float)
    const u32 N     = (u32)fl.size;     // v89 = *(_DWORD*)a1
    const u32 S     = (u32)hm->size;    // v93 = *(a2+32)
    const u32 maskS = S - 1;            // v86
    u8* hh = hm->heights;               // *(a2+40)
    u8* he = hm->entries;               // *(a2+36), 24-byte stride, class byte +0
    u32 r = S / N;                      // v94 (unsigned)
    u32 result = 0;

    if (r) {
        // ---- branch 1 (@0x5c4ba4..0x5c4ceb): direct per-floor-cell fill ------
        for (u32 ty = 0; ty < N; ++ty) {            // v70 outer
            for (u32 tx = 0; tx < N; ++tx) {        // v76/v32 inner (N iterations)
                const u8 h = fl.heights[ty * N + tx];          // *(a1+16 + v76)
                // v28 = ((double)(i16)h * *(a1+196) + *(a1+148) - *(a2+4)) * v106
                const u8 elev = BuildTileElevationByte(h, fl.scaleH, fl.originY,
                                                       hm->originY, invScaleY);
                const u32 cell = (ty * r) * S + tx * r;
                // heights store @0x5c4c79 (index register clobbered by ConvertX in
                // the decompile; reconstructed from the entries-cursor symmetry
                // v27 = 24*S*(ty*r) stepping v64 = 24*r).
                hh[cell] = elev;
                // entries store @0x5c4c94: ComputeTileIllumination(tx, ty, floor).
                he[24u * cell] = ComputeTileIllumination(fl.texGrid, fl.size,
                                                         (i32)tx, (i32)ty, illum);
            }
            result = N;                              // result = v51 (@0x5c4cd4)
        }

        // ---- in-place midpoint pyramid (@0x5c4cfb..0x5c5078) -----------------
        while (r > 1) {
            const u32 half  = r >> 1;               // v98
            const u32 cells = S / r;                // v92
            result = cells;                         // @0x5c4d15
            for (u32 cy = 0; cy < cells; ++cy) {    // v79
                const u32 rowA = (cy * r) * S;                  // v99
                const u32 rowB = ((cy * r + r) & maskS) * S;    // v90 = (v86&v75)*S
                const u32 rowH = (cy * r + half) * S;           // S * v82
                for (u32 cx = 0; cx < cells; ++cx) {            // v105
                    const u32 x  = cx * r;                      // v100
                    const u32 xr = (cx * r + r) & maskS;        // v86 & v103
                    const u32 A  = rowA + x;                    // v104
                    const u32 a = hh[A];                        // v50
                    const u32 b = hh[rowB + x];                 // *(v35 + v34)
                    const u32 c = hh[rowA + xr];                // C (x+r, y)
                    const u32 d = hh[rowB + xr];                // v107 first read
                    const i32 sum  = (i32)(b + (a + d) + c);    // v36
                    const i32 avg4 = sum >> 2;                  // v37 (signed /4)
                    // top-edge midpoint @0x5c4f11: (avg4 + 2*(C+A)) / 5u
                    hh[A + half] = (u8)((u32)(avg4 + 2 * (i32)(c + a)) / 5u);
                    // left-edge midpoint @0x5c4f6f: (avg4 + 2*(B+A)) / 5u
                    hh[rowH + x] = (u8)((u32)(avg4 + 2 * (i32)(b + a)) / 5u);
                    // centre @0x5c4f86: avg4
                    hh[rowH + x + half] = (u8)avg4;
                    // entries: all three midpoints copy entries[A] (@0x5c4fb5..)
                    const u8 cls = he[24u * A];                 // *(v43 + v33)
                    he[24u * (A + half)]          = cls;        // v102 cursor
                    he[24u * (rowH + x)]          = cls;        // 24*v41
                    he[24u * (rowH + x + half)]   = cls;        // 24*v42
                }
            }
            r >>= 1;                                 // v94 >>= 1 (@0x5c5078)
        }
    } else {
        // ---- branch 2 (@0x5c4842..0x5c4b7c): floor.N > hm.size downsample ----
        const u32 ratio  = N / S;                    // v62
        const u32 ratio2 = ratio * ratio;            // v63
        for (u32 cy = 0; cy < S; ++cy) {             // v61
            for (u32 cx = 0; cx < S; ++cx) {         // v74
                // height box average: sum of ConvertX-truncated projected
                // elevations over the ratio^2 block, / ratio^2 (@0x5c49ab..0x5c4a12).
                // HARDEN render_08: the accumulator `edi` is a 32-bit register and
                // each sample is added via `fistp [qword]; mov eax, dword[low]; add
                // edi, eax` (@0x5c49cf..0x5c49d8) — only the LOW 32 bits of the i64
                // ConvertX result feed a 32-bit UNSIGNED accumulator, and the final
                // divide is a 32-bit `div ebx` (@0x5c49fb), i.e. UNSIGNED 32-bit.
                // (Was `i64 sum` + signed 64-bit `/` — a divergence for negative
                // elevations / 32-bit wraparound; corrected to u32 + unsigned div.)
                u32 sum = 0;                          // v6 (edi, 32-bit)
                for (u32 fy = cy * ratio; fy < (cy + 1) * ratio; ++fy) {  // v5
                    for (u32 fx = cx * ratio; fx < (cx + 1) * ratio; ++fx) {
                        const u8 h = fl.heights[fy * N + fx];   // *v7
                        const double e = ((double)(short)h * (double)fl.scaleH
                                          + (double)fl.originY
                                          - (double)hm->originY) * (double)invScaleY;
                        sum += (u32)(i32)util::ConvertX(e);     // add edi, low32(fistp)
                    }
                }
                hh[cy * S + cx] = (u8)(sum / ratio2);           // v12 = v6 / v63 (unsigned div)

                // illumination histogram over 15 bins (@0x5c4a10..0x5c4a8e).
                u32 bins[15] = {0};                   // v45, memset(0, 60)
                for (u32 fy = cy * ratio; fy < (cy + 1) * ratio; ++fy)
                    for (u32 fx = cx * ratio; fx < (cx + 1) * ratio; ++fx)
                        ++bins[ComputeTileIllumination(fl.texGrid, fl.size,
                                                       (i32)fx, (i32)fy, illum)];
                // first-max vote (@0x5c4a90..0x5c4aad): v22=-1, v25=0.
                i32 best = -1;
                u32 bestN = 0;
                for (u32 k = 0; k < 15; ++k) {
                    if (bestN < bins[k]) { best = (i32)k; bestN = bins[k]; }
                }
                he[24u * (cy * S + cx)] = (best == -1) ? (u8)0 : (u8)best;
            }
            result = cy + 1;                          // @0x5c4b71
        }
    }
    return result;
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
                // WAVE-10 memory-safety guard (ASAN caught a heap-buffer-overflow on
                // falloffLut[idx]): the engine's `d` is the dot of a NORMALISED surface
                // normal with a UNIT rotated light direction, so d in [-1,0) -> idx in
                // [0,1023], always inside the documented 1024-entry LUT. Clamp idx to
                // that LUT range so float rounding past -1.0 (idx==1024) or a malformed
                // (non-unit) light dir can't read OOB. The in-range path (every valid
                // engine input) is byte-identical; only the would-be OOB index is pinned.
                if (idx < 0) idx = 0;
                else if (idx > 1023) idx = 1023;
                float lit = p.falloffScale * falloffLut[idx];   // v53 * lut[idx]
                // The engine stores `v42 = (int)v19` with a BARE `fistp` (NO
                // VIBE_Coord_ConvertX before it — verified disasm @0x5bc7b3), so it
                // rounds to nearest-even under the default x87 control word, NOT
                // truncate-toward-zero. std::lrint honours the FE rounding mode
                // (round-to-nearest-even by default), matching the fistp 1:1.
                int v = (int)std::lrint(lit);
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
    // EXACT 1:1 translation of the disasm goto-cascade @0x5bc982..0x5bcb30,
    // verified instruction-by-instruction (HARDEN render_08). Register map from
    // the corner setup @0x5bc96e..0x5bc97e:  bl = h0, dl = h2's-hole? No —
    //   bl == h0, dl == h1, dh == h2, bh == h3, and cl == dl (mov dl,cl @0x5bc97c),
    // so every `test cl,cl` is `test h1`. Block result mapping:
    //   loc_5BC9A2 (or 0x80)   -> Hidden;   loc_5BC9A6 (fall-through) -> Unchanged;
    //   loc_5BCA8D/5BCAB6/5BCB07 (and 0x7F) -> Visible.
    // NB: the PRIOR structured form here was a DIVERGENCE — its truth table was
    // [0,2,1,0,1,0,2,2,2,1,0,1,0,1,2,0]; the disasm-exact table (below, golden
    // recomputed in the test) is [0,2,2,1,1,0,0,1,1,0,0,1,2,2,2,0]. Disasm wins.
    const bool b = h0, d = h1, e = h2, f = h3;   // bl, dl(==cl), dh, bh

    // ---- G1 @0x5bc982: ch=bl; the four guard tests fall into 5bc9a2 (Hidden)
    //      only when !b && e && f && d; otherwise branch to loc_5BCA69.
    if (!b && e && f && d)
        return QuadPolyAction::Hidden;           // loc_5BC9A2

    // ---- loc_5BCA69: test bl jz A7D / test dh jnz A7D / test bh jnz A7D /
    //                  test dl jz -> 5bc9a2 (Hidden); else A7D.
    if (b && !e && !f && !d)
        return QuadPolyAction::Hidden;           // loc_5BC9A2 via 5bca77

    // ---- loc_5BCA7D: bl jz A96 / dh jnz A96 / bh jz A96 / dl jz A96;
    //                  fall -> 5bca8d (Visible).
    if (b && !e && f && d)
        return QuadPolyAction::Visible;          // loc_5BCA8D

    // ---- loc_5BCA96: bl jnz AA6 / dh jz AA6 / bh jnz AA6 / dl jz -> 5bca8d (Visible).
    if (!b && e && !f && !d)
        return QuadPolyAction::Visible;          // loc_5BCA8D via 5bcaa4

    // ---- loc_5BCAA6: bl jz ABF / dh jz ABF / bh jnz ABF / dl jz ABF;
    //                  fall -> 5bcab6 (Visible).
    if (b && e && !f && d)
        return QuadPolyAction::Visible;          // loc_5BCAB6

    // ---- loc_5BCABF: bl jnz ACF / dh jnz ACF / bh jz ACF / dl jz -> 5bcab6 (Visible).
    if (!b && !e && f && !d)
        return QuadPolyAction::Visible;          // loc_5BCAB6 via 5bcacd

    // ---- loc_5BCACF: bl jz AE3 / dh jz AE3 / bh jz AE3 / dl jz -> 5bc9a2 (Hidden).
    if (b && e && f && !d)
        return QuadPolyAction::Hidden;           // loc_5BC9A2 via 5bcadd

    // ---- loc_5BCAE3: bl jnz AF7 / dh jnz AF7 / bh jnz AF7 / dl jnz -> 5bc9a2 (Hidden).
    if (!b && !e && !f && d)
        return QuadPolyAction::Hidden;           // loc_5BC9A2 via 5bcaf1

    // ---- loc_5BCAF7: bl jz B10 / dh jnz B10 / bh jnz B10 / dl jz B10;
    //                  fall -> 5bcb07 (Visible).
    if (b && !e && !f && d)
        return QuadPolyAction::Visible;          // loc_5BCB07

    // ---- loc_5BCB10: bl jnz / dh jz / bh jz / dl jnz -> 5bc9a6 (Unchanged);
    //                  else 5bcb30 jmp 5bc9a2 (Hidden).
    if (!b && e && f && !d)
        return QuadPolyAction::Hidden;           // loc_5BC9A2 via 5bcb30
    return QuadPolyAction::Unchanged;            // loc_5BC9A6
}

} // namespace guild::render
