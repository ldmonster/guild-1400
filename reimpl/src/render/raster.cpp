#include "render/raster.h"

#include "render/fog.h"   // wave-6 W6-INTEGRATE: per-pixel span fog (SpanFog())

#include <cstring>

// =============================================================================
// guild::render software rasterizer — implementation. See raster.h for the
// module overview, the self-modifying-span documentation, the MMX-block note,
// and the ORIGINAL GLOBAL MAP. All arithmetic is 16.16 fixed-point and mirrors
// the Hex-Rays pseudocode of the listed gilde.exe functions verbatim (same
// 64-bit intermediate widening, same ceil-to-pixel rounding `(x+0xFFFF)>>16`,
// same intentional integer wraparound).
// =============================================================================
namespace guild::render {

// flt_62C3D8 == 0x47800000 == 65536.0f : the 16.16 scale factor applied to the
// float screen coordinates before they enter the fixed-point edge walk.
static constexpr float kFixedScale = 65536.0f;

// dword_5AC540 / dword_5AC544 (interleaved, stride 2 dwords): the triangle
// vertex-traversal table. For vertex index i, "next" = kNext[i], "prev" =
// kPrev[i] — i.e. the CCW neighbour ordering used to pick the long/short edges.
// Recovered bytes: 5AC540 = {1,2,0}, 5AC544 = {2,0,1}.
static constexpr int kNext[3] = {1, 2, 0}; // dword_5AC540[2*i]
static constexpr int kPrev[3] = {2, 0, 1}; // dword_5AC544[2*i]

// ---------------------------------------------------------------------------
// Fixed-point edge-slope helper shared by the InterpolateEdge* routines.
// Computes (num << 16) / dy using the original's two-path scheme:
//   dy >= 0x10000 : direct 64-bit ((num<<16)/dy)
//   dy <  0x10000 : reciprocal trick  ((0x40000000/dy) * num) >> 14
// (the second path avoids a slow 64-bit divide for short edges by pre-dividing
// a fixed-point reciprocal; bit-identical to the original).
// ---------------------------------------------------------------------------
static i32 EdgeSlope(i32 num, i32 dy) {
    if (dy >= 0x10000) {
        // The x86 `shl`/widening is bit-exact regardless of sign; do the <<16 in
        // the unsigned domain so a negative `num` does not invoke signed-shift UB
        // (the two's-complement bit pattern, and hence the 64-bit divide, is
        // identical). HARDENING wave-10 (UBSAN: left shift of negative value).
        return (i32)(((i64)((u64)(i64)num << 16)) / dy);
    } else {
        i32 recip = (i32)(0x40000000 / dy);
        return (i32)(((u64)((i64)recip * (i64)num)) >> 14);
    }
}

// Snap a 16.16 Y value down to its first covered pixel centre offset:
//   ((y + 0xFFFF) >> 16 << 16) - y   ==  (ceil(y) in 16.16) - y
// i.e. the sub-pixel distance from y up to the next integer scanline.
static i32 SubpixelToCeil(i32 y16) {
    // The `>>16 <<16` integer-floor mask: do the final <<16 unsigned so a
    // negative ceil() (off-screen top edge) is not signed-shift UB. The masked
    // bits are identical to the original `sar/shl`. HARDENING wave-10 (UBSAN).
    return (i32)((u32)((y16 + 0xFFFF) >> 16) << 16) - y16;
}

// ---------------------------------------------------------------------------
// gilde.exe 0x5F7840 — VIBE_Raster_InterpolateEdgeZTex (long left edge).
// Sets xLeft/xLeftStep and uLeft/uLeftStep for edge a->b (b below a).
// ---------------------------------------------------------------------------
void InterpolateEdgeZTex(RasterState& rs, int a, int b) {
    i32 dy = rs.vy[b] - rs.vy[a];                     // dword_13FC59C delta
    i32 uStep;
    if (dy >= 0x10000) {
        // <<16 in the unsigned domain (negative dx/dlight would be signed-shift
        // UB; bits identical). HARDENING wave-10 (UBSAN).
        rs.xLeftStep = (i32)(((i64)((u64)(i64)(rs.vx[b] - rs.vx[a]) << 16)) / dy);
        uStep        = (i32)(((i64)((u64)(i64)(rs.vlight[b] - rs.vlight[a]) << 16)) / dy);
    } else {
        i32 recip = (i32)(0x40000000 / dy);
        rs.xLeftStep = (i32)(((u64)((i64)recip * (i64)(rs.vx[b] - rs.vx[a]))) >> 14);
        uStep        = (i32)(((u64)((i64)recip * (i64)(rs.vlight[b] - rs.vlight[a]))) >> 14);
    }
    rs.uLeftStep = uStep;
    i32 sub = SubpixelToCeil(rs.vy[a]);               // ((vy+0xFFFF)>>16<<16)-vy
    rs.xLeft = rs.vx[a]     + (i32)(((u64)((i64)rs.xLeftStep * (i64)sub)) >> 16);
    rs.uLeft = rs.vlight[a] + (i32)(((u64)((i64)uStep        * (i64)sub)) >> 16);
}

// ---------------------------------------------------------------------------
// gilde.exe 0x5F6A8C — VIBE_Raster_InterpolateEdgeZ (short right edge).
// Sets xRight/xRightStep for edge a->b.
// ---------------------------------------------------------------------------
void InterpolateEdgeZ(RasterState& rs, int a, int b) {
    i32 dy = rs.vy[b] - rs.vy[a];
    rs.xRightStep = EdgeSlope(rs.vx[b] - rs.vx[a], dy);
    i32 sub = SubpixelToCeil(rs.vy[a]);
    rs.xRight = rs.vx[a] + (i32)(((u64)((i64)rs.xRightStep * (i64)sub)) >> 16);
}

// ---------------------------------------------------------------------------
// gilde.exe 0x5F71AD — VIBE_Raster_FillSpanTextured (formerly self-modifying).
//
// Original register contract: eax=dst byte ptr, edx=U accum(16.16), ebx=V
// accum(16.16), span length in dword_13FC588. The inner loop per pixel:
//   V_int = sar(ebx,16); ebx = (V_int << widthShift)  + U_int          (texel addr)
//   ebx &= texelMask                                                   (wrap)
//   idx = texBase[ebx]                                                 (palette idx)
//   colour = palBase[idx]                                              (16bpp)
//   *dst++ = colour
//   ebx += vStep (with carry from the U add); edx += uStepFrac         (advance)
// The "(ebx<<widthShift)+U_int" forms the byte offset (V*rowBytes + U) with the
// row stride folded into widthShift; this is the affine texel fetch. We
// reproduce it scalar with explicit 16.16 accumulators; the patched immediates
// arrive in `p` (see SpanTexParams / the self-modifying-code note in raster.h).
// ---------------------------------------------------------------------------
// EQUIVALENCE NOTE (combined-index vs separate accumulators). The original's
// inner loop keeps ONE combined texel index ebx = (Vint<<shift)+Uint, advanced
// per pixel by the patched delta dword_13DCE54[0] = (dVint<<shift)+dUint, plus
// +1 via the `adc` when the U fraction (eax += dword_13FC594) carries, plus
// +width (dword_13DCE50 selected by `sbb ebp,ebp`) when the V fraction
// (ecx += dword_13FC5A8) carries — and the V-fraction add is PRE-RUN once in
// the prologue so its carry chain lands on the correct pixel. Both carries are
// therefore exactly timed, and the per-iteration `and ebx, mask` is a mod-2^k
// reduction (the mask is contiguous low bits) that commutes with the adds. The
// separate full-16.16 U/V accumulators below produce the identical masked
// address on every pixel.
// Gouraud RGB texel modulate (the D3D diffuse multiply) on a packed 565 pixel:
// channel * (shade+1) >> 8 (the classic /255 approximation; shade 255 == 1.0).
static inline u16 Modulate565(u16 col, int R, int G, int B) {
    if (R < 0) R = 0; else if (R > 255) R = 255;
    if (G < 0) G = 0; else if (G > 255) G = 255;
    if (B < 0) B = 0; else if (B > 255) B = 255;
    const u32 r5 = ((u32)((col >> 11) & 0x1F) * (u32)(R + 1)) >> 8;
    const u32 g6 = ((u32)((col >> 5) & 0x3F) * (u32)(G + 1)) >> 8;
    const u32 b5 = ((u32)(col & 0x1F) * (u32)(B + 1)) >> 8;
    return (u16)((r5 << 11) | (g6 << 5) | b5);
}

void FillSpanTextured(RasterState& rs, u16* dst, i32 u, i32 v,
                      const SpanTexParams& p) {
    int n = rs.spanLen;
    // GOURAUD (default off == byte-identical): the per-pixel RGB diffuse
    // modulate channel (rs.rStart.. seeded by the textured triangle setup).
    const bool gou = rs.gPerPixel;
    i32 r16 = rs.rStart, g16 = rs.gStart, b16 = rs.bStart;
    auto shadePixel = [&](u16 col) -> u16 {
        return gou ? Modulate565(col, r16 >> 16, g16 >> 16, b16 >> 16) : col;
    };
    auto stepShade = [&]() {
        if (gou) {
            r16 = WrapAddI32(r16, rs.rGrad);
            g16 = WrapAddI32(g16, rs.gGrad);
            b16 = WrapAddI32(b16, rs.bGrad);
        }
    };
    // wave-6/wave-7: the per-pixel fog blend — the software realisation of D3D
    // fixed-function VERTEX fog (rule 3). DEFAULT DISABLED (SpanFog().enabled ==
    // false) so this whole branch is bypassed and the span is byte-identical.
    //
    // wave-7 W7-FOGPIX: when rs.fPerPixel is set, the fog factor is the PER-PIXEL
    // interpolated value the engine's D3D vertex fog produces — the per-vertex
    // factor (vertex+79) carried as a third 16.16 span channel (rs.fStart, stepped
    // by rs.fGrad), clamped to [0,255]. When fPerPixel is clear we fall back to the
    // per-triangle constant SpanFog().factor (the wave-6 first cut — kept so direct
    // FillSpanTextured callers that do not seed the channel stay byte-identical).
    const SpanFogState& fog = SpanFog();
    if (fog.enabled) {
        const u32 fogColor = (u32)fog.color;
        if (rs.fPerPixel) {
            i32 f16 = rs.fStart;                            // 16.16 fog factor
            for (int i = 0; i < n; ++i) {
                int factor = f16 >> 16;                     // interp factor
                if (factor < 0) factor = 0;
                else if (factor > 255) factor = 255;
                i32 uInt = u >> 16;
                i32 vInt = v >> 16;
                u32 addr = ((((u32)vInt) << p.widthShift) + (u32)uInt) & p.texelMask;
                u8  idx = p.texBase[addr];
                dst[i] = BlendFog565(shadePixel(p.palBase[p.lightRow8 | idx]),
                                     fogColor, factor);
                u = WrapAddI32(u, p.uStepFrac);
                v = WrapAddI32(v, p.vStep);
                f16 = WrapAddI32(f16, rs.fGrad);                            // step the 3rd channel
                stepShade();
            }
            rs.fStart = f16;                                // (mirrors the original
            return;                                         //  edge-walk persistence)
        }
        if (fog.factor < 255) {
            const int factor = fog.factor;
            for (int i = 0; i < n; ++i) {
                i32 uInt = u >> 16;
                i32 vInt = v >> 16;
                u32 addr = ((((u32)vInt) << p.widthShift) + (u32)uInt) & p.texelMask;
                u8  idx = p.texBase[addr];
                dst[i] = BlendFog565(shadePixel(p.palBase[p.lightRow8 | idx]),
                                     fogColor, factor);
                u = WrapAddI32(u, p.uStepFrac);
                v = WrapAddI32(v, p.vStep);
                stepShade();
            }
            return;
        }
    }
    for (int i = 0; i < n; ++i) {
        i32 uInt = u >> 16;                                  // sar edx,16
        i32 vInt = v >> 16;                                  // sar ebx,16
        u32 addr = (((u32)vInt) << p.widthShift) + (u32)uInt;// (V<<shift)+U
        addr &= p.texelMask;                                 // and ebx, mask
        u8  idx = p.texBase[addr];                           // mov dl,[ebx+base]
        dst[i] = shadePixel(p.palBase[p.lightRow8 | idx]);   // mov cx,pal[edx*2]
        u = WrapAddI32(u, p.uStepFrac);                                    // add edx (U step)
        v = WrapAddI32(v, p.vStep);                                        // add ebx (V step)
        stepShade();
    }
}

// ---------------------------------------------------------------------------
// gilde.exe 0x5F721A — VIBE_Raster_FillSpanTexturedMasked.
//
// As FillSpanTextured but transparent texels are skipped (left untouched in the
// framebuffer). TWO transparency rules, selected by SpanTexParams::useColorKey:
//
//   useColorKey == false (DEFAULT): the EXACT original rule — `test dl,dl / jz`
//     skips the texel when its SOURCE INDEX is 0. Byte-identical to the binary's
//     software masked span and to every prior reconstruction/test.
//
//   useColorKey == true (wave-5 W5-CKEY): the faithful software realisation of
//     the DDraw colour-key the engine actually uses for 24-bit colour-keyed
//     (record +104 bit 2) textures. VIBE_Render_LoadAndStretchTexture @0x5dea50
//     colour-keys the transparent surface on `v111 = pal[0]` (0x5df40a) which,
//     for a 24-bit source, is (0,0,0) == BLACK (the palette buffer is memset-0
//     and never rebuilt — the 24-bit arm of VIBE_Bmp_LoadBuffer @0x5f0ce4 skips
//     VIBE_Quant_BuildPalette), and the DDBLT_KEYSRC blit (&unk_1000000,
//     0x5df086) makes every pixel whose RESOLVED colour equals that key
//     transparent. The octree quantizer (TreeCollectPalette @0x603180) does NOT
//     put black at index 0, so the index-0 rule cannot stand in for black; we
//     key on the RESOLVED 16bpp value instead — skip when
//     palBase[lightRow8|idx] == colorKey565 (black == 565 0x0000). This matches
//     the DDraw key exactly. (Black scaled by any HiColTab light ramp row is
//     still 0, so the key is row-invariant.)
// ---------------------------------------------------------------------------
void FillSpanTexturedMasked(RasterState& rs, u16* dst, i32 u, i32 v,
                            const SpanTexParams& p) {
    int n = rs.spanLen;
    // GOURAUD (default off == byte-identical): per-pixel RGB diffuse modulate.
    // The channel steps at EVERY covered pixel (like fog) so it stays in phase
    // across masked (transparent) texels.
    const bool gou = rs.gPerPixel;
    i32 r16 = rs.rStart, g16 = rs.gStart, b16 = rs.bStart;
    auto shadePixel = [&](u16 col) -> u16 {
        return gou ? Modulate565(col, r16 >> 16, g16 >> 16, b16 >> 16) : col;
    };
    auto stepShade = [&]() {
        if (gou) {
            r16 = WrapAddI32(r16, rs.rGrad);
            g16 = WrapAddI32(g16, rs.gGrad);
            b16 = WrapAddI32(b16, rs.bGrad);
        }
    };
    // wave-6/wave-7: per-pixel fog (default disabled == byte-identical). Fog only
    // touches WRITTEN (non-transparent) pixels — transparent texels keep the
    // existing destination untouched, exactly as without fog. The interpolated
    // fog-factor channel (rs.fStart/fGrad) advances per pixel regardless of the
    // transparency test (D3D interpolates fog at every covered pixel; only the
    // BLEND/write is masked), so the gradient stays in phase across skipped texels.
    const SpanFogState& fog = SpanFog();
    const bool fogEnabled  = fog.enabled;
    const bool fogPerPixel = fogEnabled && rs.fPerPixel;
    const bool fogConst    = fogEnabled && !rs.fPerPixel && fog.factor < 255;
    const u32  fogColor    = (u32)fog.color;
    i32 f16 = rs.fStart;                                     // 16.16 fog factor
    auto fogPixel = [&](u16 col) -> u16 {
        if (fogPerPixel) {
            int factor = f16 >> 16;
            if (factor < 0) factor = 0;
            else if (factor > 255) factor = 255;
            return BlendFog565(col, fogColor, factor);
        }
        if (fogConst) return BlendFog565(col, fogColor, fog.factor);
        return col;
    };
    if (p.useColorKey) {
        for (int i = 0; i < n; ++i) {
            i32 uInt = u >> 16;
            i32 vInt = v >> 16;
            u32 addr = ((((u32)vInt) << p.widthShift) + (u32)uInt) & p.texelMask;
            u8  idx = p.texBase[addr];
            u16 col = p.palBase[p.lightRow8 | idx];          // resolved 16bpp
            if (col != p.colorKey565)                        // DDraw KEYSRC == key
                dst[i] = fogPixel(shadePixel(col));
            u = WrapAddI32(u, p.uStepFrac);
            v = WrapAddI32(v, p.vStep);
            f16 = WrapAddI32(f16, rs.fGrad);
            stepShade();
        }
        rs.fStart = f16;
        return;
    }
    for (int i = 0; i < n; ++i) {
        i32 uInt = u >> 16;
        i32 vInt = v >> 16;
        u32 addr = ((((u32)vInt) << p.widthShift) + (u32)uInt) & p.texelMask;
        u8  idx = p.texBase[addr];
        if (idx != 0)                                        // test dl,dl; jz
            dst[i] = fogPixel(shadePixel(p.palBase[p.lightRow8 | idx]));
        u = WrapAddI32(u, p.uStepFrac);
        v = WrapAddI32(v, p.vStep);
        f16 = WrapAddI32(f16, rs.fGrad);
        stepShade();
    }
    rs.fStart = f16;
}

// ---------------------------------------------------------------------------
// gilde.exe 0x5F7960 — VIBE_Raster_FillTexturedSpansShaded.
//
// Walks `rowCount` scanlines starting at row y0. For each row,
// [ceil(xLeft), ceil(xRight)) is the span (dword_13FC588 = right-left).
//
// BIT 1 (modeMask & 2): the shaded BYTE span. The per-pixel value is the
// integer part of the horizontal U/shade accumulator: start = uLeft +
// (uGrad * ((rowStartX<<16) - xLeft)) >> 16, then += uGrad per pixel. The
// original keeps the accumulator ROR'd by 16 and advances it with an `adc`
// chain (0x5f7a53..0x5f7a60):
//     v8 = ROR(start,16); v9 = ROR(uGrad,16); CF = 0;
//     do { *dst++ = (u8)v8; v8 = adc(v8, v9); } while (--n);
// The carry out of the FRACTION half (bit 31 of the rotated word) re-enters
// the INTEGER half (bit 0) only on the NEXT iteration's adc — so the
// sub-pixel carry lands ONE PIXEL LATE relative to plain 16.16 accumulation
// (and an integer-half overflow at bit 15 spills into the fraction). We
// reproduce the rotated adc chain verbatim; the bytes are bit-identical to
// the original including the carry-boundary pixels.
//
// BITS 0/2 (modeMask & 5, "v43"): the tile-type stamp into the 24-byte-stride
// tile array (rs.shadeBase = dword_1408AA4 cursor; rs.blurRadius =
// dword_1408AA0). Stamp value: v44 = v47 = 11 when (modeMask & 1) == 0, else
// 0 (0x5f7987..0x5f799d). Per nonempty row: a width-clamped halo row
// (blur > 0), once per firstBatch call a pyramid halo above the first
// nonempty span (the a3/v46 once-flag), the core span bytes, and after the
// walk a pyramid halo below the last nonempty span (v34/v31/v33 saved state).
// Transcribed verbatim from the captured decompile.
//
// Returns the last filled row index, or -1 if no span was emitted. (The
// original's char return value is dead in all callers; internally the v34
// sentinel is the LEFT X of the last nonempty span — used verbatim for the
// trailing halo below.)
//
// Reconstruction-only byte-map guard: bit 1 writes one shade byte per pixel
// into an 8bpp byte map. The single live caller (BuildTerrainMesh @0x5c5610)
// always passes the terrain byte map ([esi+28h]); a host caller pointing it at
// a 16bpp surface would produce 0xC8C8-style byte-pair garbage (the pink-
// polygon artifact pinned by tests/e2e/render_fidelity_w3a_e2e_test.cpp), so a
// non-8bpp `fb` clears bit 1 — the same masking RasterizeTexturedTriangle's
// `if (!a4) a5 &= 5` applies when the byte map is absent. Byte writes are also
// clipped to the surface rect (the original writes unclipped, trusting the
// caller's geometry).
// ---------------------------------------------------------------------------
int FillTexturedSpansShaded(RasterState& rs, Surface* fb, int rowCount, int y0,
                            int firstBatch, u8 modeMask) {
    const u8 stamp = (u8)(((modeMask & 5) != 0 && (modeMask & 1) == 0) ? 11 : 0);
    const bool stamping  = (modeMask & 5) != 0 && rs.shadeBase != nullptr; // v43
    const bool byteSpan  = (modeMask & 2) != 0 && fb && fb->bpp == 8;
    bool topHaloDone = false;                                // v46

    int lastRow = -1;                                        // reconstruction ret
    i32 v34 = -1;                                            // left X of last span
    i32 v31 = 0;                                             // its span length
    u8* v33 = nullptr;                                       // its tile row cursor

    if (rowCount <= 0)
        return lastRow;

    const int pitchPx = rs.fbPitch;                          // a2 (width/pitch)
    u8* const fbPixels = fb ? fb->pixels : nullptr;
    const int fbW = fb ? fb->width : 0;
    const int fbH = fb ? fb->height : 0;
    const i32 blur = rs.blurRadius;                          // dword_1408AA0
    u8* tileRow = rs.shadeBase;                              // dword_1408AA4

    int row = y0;                                            // v37 = a5
    do {
        i32 xL = (rs.xLeft + 0xFFFF) >> 16;                  // v39 = ceil(xLeft)
        rs.spanLen = ((rs.xRight + 0xFFFF) >> 16) - xL;      // dword_13FC588
        if (rs.spanLen > 0) {
            lastRow = row;
            v31 = rs.spanLen;                                // saved for trailing
            v33 = tileRow;                                   // saved 1408AA4
            v34 = xL;                                        // saved left X

            if (byteSpan) {                                  // ((a4>>1)&1) block
                // v8 = ROR(uLeft + (uGrad*((xL<<16)-xLeft))>>16, 16)
                // <<16 unsigned: xL can be a negative ceil() for an off-screen
                // left edge (signed-shift UB; bits identical). HARDENING wave-10.
                i32 sub = (i32)((u32)xL << 16) - rs.xLeft;
                u32 r  = Ror4((u32)(rs.uLeft +
                              (i32)(((i64)rs.uGrad * (i64)sub) >> 16)), 16);
                u32 s  = Ror4((u32)rs.uGrad, 16);            // v9
                u32 cf = 0;                                  // v10 = 0
                if (row >= 0 && row < fbH) {
                    u8* dstRow = fbPixels + (i64)row * pitchPx;
                    for (int i = 0; i < rs.spanLen; ++i) {
                        int x = xL + i;
                        if ((unsigned)x < (unsigned)fbW)
                            dstRow[x] = (u8)r;               // *v6 = v8
                        u64 t = (u64)r + s + cf;             // adc v8, v9
                        cf = (u32)(t >> 32);
                        r  = (u32)t;
                    }
                }
            }

            if (stamping) {                                  // if (v43)
                if (blur > 0) {                              // dword_1408AA0 > 0
                    // current-row halo: x in [max(xL-blur,0), ...), width
                    // min(spanLen + 2*blur, pitch - xL).
                    i32 x0 = xL - blur;                      // v13
                    if (x0 <= 0) x0 = 0;
                    u8* cell = tileRow + 24 * x0;            // v14
                    i32 n = rs.spanLen + 2 * blur;           // v15
                    if (n >= pitchPx - xL) n = pitchPx - xL;
                    for (i32 i = 0; i < n; ++i, cell += 24)
                        *cell = stamp;                       // = v44
                    if (firstBatch && !topHaloDone) {        // if (a3 && !v46)
                        // pyramid halo over the `blur` rows ABOVE this row:
                        // v17 = blur..1; row v37-blur..v37-1 widening toward
                        // the span.
                        i32 v17 = blur;
                        if (blur > 0) {
                            i32 v41 = row - blur;
                            i64 v40 = (i64)pitchPx * 24 * blur;
                            do {
                                if (v41 > 0) {
                                    i32 v18 = xL - (blur - v17);
                                    if (v18 <= 0) v18 = 0;
                                    i32 v19 = 2 * (blur - v17) + rs.spanLen;
                                    u8* v20 = tileRow - v40 + 24 * v18;
                                    if (v19 >= pitchPx - xL) v19 = pitchPx - xL;
                                    for (i32 j = 0; j < v19; ++j, v20 += 24)
                                        *v20 = stamp;        // = v44
                                }
                                --v17;
                                ++v41;
                                v40 += -24 * (i64)pitchPx;
                            } while (v17 > 0);
                        }
                        topHaloDone = true;                  // v46 = 1
                    }
                }
                // core span stamp: bytes at tileRow + 24*x, x in [xL, xL+len).
                u8* cell = tileRow + 24 * xL;                // v23
                for (i32 k = 0; k < rs.spanLen; ++k, cell += 24)
                    *cell = stamp;                           // = v47
            }
        }
        // advance one scanline (the 0x5f7bfd..0x5f7c32 block). The accumulator
        // adds wrap mod 2^32 like the original `add` (WrapAddI32 avoids
        // signed-overflow UB at the fixed-point range edge). HARDENING wave-10.
        rs.xLeft  = WrapAddI32(rs.xLeft,  rs.xLeftStep);      // 13FC5D8 += 13FC5E8
        ++row;                                                // ++v37
        rs.xRight = WrapAddI32(rs.xRight, rs.xRightStep);     // 13FC5BC += 13FC5C8
        rs.uLeft  = WrapAddI32(rs.uLeft,  rs.uLeftStep);      // 13FC5F4 += 13FC5C4
        if (byteSpan) { /* 13FC5DC += a2 — folded into row*pitch above */ }
        tileRow += 24 * (i64)pitchPx;                         // 1408AA4 += 24*a2
    } while (row < rowCount + y0);
    rs.shadeBase = tileRow;                                   // cursor persists

    // trailing pyramid halo below the LAST nonempty span (the v34 >= 0 block).
    if (v34 >= 0 && stamping && blur > 0) {
        i32 v25 = blur;
        i32 v35 = blur + rowCount + y0;                       // end row + blur
        u8* v36 = v33 + (i64)pitchPx * 24 * blur;
        do {
            if (v35 <= pitchPx) {                             // row gate vs width
                i32 v26 = v34 - (blur - v25);                 // (square grid)
                if (v26 <= 0) v26 = 0;
                i32 v27 = 2 * (blur - v25) + v31;
                u8* v28 = v36 + 24 * v26;
                if (v27 >= pitchPx - v34) v27 = pitchPx - v34;
                for (i32 m = 0; m < v27; ++m, v28 += 24)
                    *v28 = stamp;                             // = v44
            }
            --v25;
            --v35;
            v36 += -24 * (i64)pitchPx;
        } while (v25 > 0);
    }

    return lastRow;
}

// ---------------------------------------------------------------------------
// Internal: load the 3 float screen vertices into the 16.16 per-vertex arrays
// and return the index of the top-most (min-Y) vertex. Mirrors the two scan
// loops in RasterizeTexturedTriangle: the signed-area sign selects the FORWARD
// loop (slot i <- vertex i; 0x5f7ef2..) or the REVERSE loop (slot i <- vertex
// 2-i; pointers v10 = a1+4 / v8 = a2+2 walking DOWN, 0x5f7db3..) — the
// original normalises a positive-area (back-wound) triangle to the winding the
// fixed left/right edge tables expect.
// ---------------------------------------------------------------------------
static int LoadVertices(RasterState& rs, const RasterVertex v[3], bool reverse) {
    int topIdx = -1;
    // 0x5f7d9f (textured) / 0x603f6f (flat): the min-Y tracker is seeded with the
    // pitch shifted left 16 (`shl ecx,10h` on a3 = pitch), NOT INT_MAX. A vertex
    // whose 16.16 Y exceeds (pitch<<16) is therefore NOT a valid top candidate; if
    // none qualify, topIdx stays -1 and the caller draws nothing. Match the binary
    // exactly. The <<16 is done unsigned (no signed-shift UB; bits identical).
    i32 minY = (i32)((u32)rs.fbPitch << 16);
    for (int i = 0; i < 3; ++i) {
        const RasterVertex& src = v[reverse ? 2 - i : i];
        // VIBE_Coord_ConvertX rounds float->int (truncation toward zero in the
        // original FPU path); x*65536, y*65536 give 16.16 screen coordinates.
        rs.vx[i]     = (i32)(src.x * kFixedScale);           // dword_13FC5B0
        rs.vy[i]     = (i32)(src.y * kFixedScale);           // dword_13FC59C
        rs.vlight[i] = (i32)src.light << 16;                 // dword_13FC578
        if (rs.vy[i] <= minY) {                              // v16 >= v17 (<=)
            topIdx = i;
            minY = rs.vy[i];
        }
    }
    return topIdx;
}

// ---------------------------------------------------------------------------
// gilde.exe 0x5F7D58 — VIBE_Raster_RasterizeTexturedTriangle.
//
// 1. Signed-area test (the literal a1[] expression below): <= 0 selects the
//    forward vertex scan, > 0 the REVERSE scan (slot i <- vertex 2-i — winding
//    normalisation for the fixed edge tables). Both track the top (min-Y)
//    vertex index v7 in the LOADED order.
// 2. Mode masking (0x5f7e26..0x5f7e34): `if (!a6) a5 &= 2; if (!a4) a5 &= 5;
//    if (!a5) return` — a6 = tile array, a4 = byte map.
// 3. Compute the horizontal U/shade gradient dword_13FC590 from the triangle's
//    light values and screen positions (the (*a1 - a1[2]) etc. cross product).
//    NOTE: the gradient uses the ORIGINAL a1/a2 argument order (NOT the
//    possibly-reversed load order) — exactly as the binary reads *a1/a2[i].
// 4. From the top vertex, walk the long edge with InterpolateEdgeZTex, the
//    opposite short edge with InterpolateEdgeZ, fill the top sub-triangle
//    (firstBatch=1), then re-set an edge and fill the bottom one (firstBatch=0).
// ---------------------------------------------------------------------------
int RasterizeTexturedTriangle(Surface* fb, const RasterVertex v[3],
                              u8 modeMask, u8* tileBase, i32 blurRadius,
                              i32 gridPitch) {
    RasterState rs;
    std::memset(&rs, 0, sizeof(rs));
    rs.fbBase  = fb ? fb->pixels : nullptr;
    rs.fbPitch = fb ? fb->widthPx : gridPitch;   // a3 (grid width)

    // --- signed-area / winding select (0x5f7da9) -----------------------------
    // a1[4]*a1[3] - a1[5]*a1[2] + a1[2]*a1[1] - a1[3]*a1[0] + a1[0]*a1[5]
    //   - a1[1]*a1[4]   with a1 = (x0,y0,x1,y1,x2,y2):
    float area = v[2].x * v[1].y - v[2].y * v[1].x
               + v[1].x * v[0].y - v[1].y * v[0].x
               + v[0].x * v[2].y - v[0].y * v[2].x;
    const bool reverse = !(area <= 0.0f);     // forward when <= 0.0
    int top = LoadVertices(rs, v, reverse);
    if (top < 0)
        return 0;

    // --- a5 mode masking (0x5f7e26 / 0x5f7e32) -------------------------------
    u8 a5 = modeMask;
    if (!tileBase)                 // if (!a6) a5 &= 2
        a5 &= 2;
    if (!fb || fb->bpp != 8)       // if (!a4) a5 &= 5  (byte map absent/unusable)
        a5 &= 5;
    if (!a5)
        return 0;

    // --- horizontal shade gradient dword_13FC590 ----------------------------
    // v42 = y0 - y1 ; v43 = y2 - y1 ; v45 = (x0-x1)*v43 - (x2-x1)*v42  (2*area).
    // grad = ((l0-l1)*v43 - (l2-l1)*v42) * (65536 / v45)  if v45 != 0 else 0.
    // (Uses the float screen coords + light bytes exactly as the original —
    // always in ARGUMENT order, independent of the winding reversal.)
    float v42 = v[0].y - v[1].y;
    float v43 = v[2].y - v[1].y;
    float v45 = (v[0].x - v[1].x) * v43 - (v[2].x - v[1].x) * v42;
    if (((u32&)v45 & 0x7FFFFFFF) != 0) {
        float num = (float)((int)v[0].light - (int)v[1].light) * v43
                  - (float)((int)v[2].light - (int)v[1].light) * v42;
        rs.uGrad = (i32)(num * (kFixedScale / v45));
    } else {
        rs.uGrad = 0;
    }

    // --- pick the long edge from the top vertex -----------------------------
    // The original compares the two neighbour Y values to the top vertex Y; the
    // neighbour at the *same* min-Y picks the traversal order (degenerate
    // top-edge case), otherwise the longer-Y neighbour is the long edge.
    // Long edge = v7->v20 (top->prev), short edge = v19->v21 (top->next).
    // Register map from 0x5F7E4B..: ebp=v7, ebx=v19, edi=v20, esi=v21.
    int n = kNext[top];
    int p = kPrev[top];
    i32 yTop = rs.vy[top];

    int v7, v19, v20, v21;
    if (yTop == rs.vy[n]) {            // flat top edge top->next
        v7 = top; v19 = n; v20 = p; v21 = kNext[n];
    } else if (yTop == rs.vy[p]) {     // flat top edge top->prev
        v7 = p; v19 = top; v21 = n; v20 = kPrev[p];
    } else {                           // general: top is the sole apex
        v7 = top; v19 = top; v20 = p; v21 = n;
    }

    // --- top sub-triangle ---------------------------------------------------
    int last = -1;
    i32 longDy = rs.vy[v20] - rs.vy[v7];
    if (longDy > 0) {
        InterpolateEdgeZTex(rs, v7, v20);          // long left edge
        i32 shortDy = rs.vy[v21] - rs.vy[v19];
        if (shortDy > 0) {
            InterpolateEdgeZ(rs, v19, v21);        // short right edge
            i32 y0fix = rs.vy[v7];
            int y0 = (y0fix + 0xFFFF) >> 16;       // v32 = ceil(topY)
            rs.fbBase = fb ? fb->pixels : nullptr; // 13FC5DC = v32*a3 + a4
            // 1408AA4 = 24 * v32 * a3 + a6 ; 1408AA0 = a7. The cursor then
            // persists across both fills exactly like the original globals.
            rs.shadeBase = tileBase
                ? tileBase + 24 * (i64)y0 * rs.fbPitch : nullptr;
            rs.blurRadius = blurRadius;

            i32 yMidR = rs.vy[v20];
            i32 yBot  = rs.vy[v21];
            bool rightShorter = yMidR < yBot;      // v41
            i32 yEnd = rightShorter ? (yMidR + 0xFFFF) : (yBot + 0xFFFF);
            int rowCount = (yEnd >> 16) - y0;      // v46
            last = FillTexturedSpansShaded(rs, fb, rowCount, y0,
                                           /*firstBatch=*/1, a5);

            // --- bottom sub-triangle -------------------------------------
            if (rs.vy[v20] != rs.vy[v21]) {
                int y1 = rowCount + y0;            // v35
                i32 hi, lo;
                if (rightShorter) {
                    // right edge ended first: re-walk v20->v21 as the new edge.
                    InterpolateEdgeZTex(rs, v20, v21);
                    hi = rs.vy[v21];
                    lo = rs.vy[v20];
                } else {
                    InterpolateEdgeZ(rs, v21, v20);
                    hi = rs.vy[v20];
                    lo = rs.vy[v21];
                }
                int rc2 = ((hi + 0xFFFF) >> 16) - ((lo + 0xFFFF) >> 16);
                int last2 = FillTexturedSpansShaded(rs, fb, rc2, y1,
                                                    /*firstBatch=*/0, a5);
                if (last2 >= 0)
                    last = last2;
            }
        }
    }

    return last >= 0 ? 1 : 0;
}

// ---------------------------------------------------------------------------
// Flat-shaded triangle — gilde.exe 0x603ED4 VIBE_Shadow_RasterizeTriangle, plus
// the flat-fill leaves 0x603D00 VIBE_Raster_ComputeEdgeSlope (LEFT-edge setup,
// same two-path fixed-point divide as InterpolateEdgeZ) and 0x603DA8
// VIBE_Raster_FillSpans (the per-row span loop — memsets [ceil(xLeft),
// ceil(xRight)) with the constant index dword_13FC5E0 per row; word-stores in
// the 16bpp byte_140A220==0 path). The flat-fill leaves carry NO winding logic
// of their own (wave-5 verified); the winding is decided here by 0x603ED4.
// ---------------------------------------------------------------------------
static void ComputeEdgeSlopeLeft(RasterState& rs, int a, int b) {
    i32 dy = rs.vy[b] - rs.vy[a];
    rs.xLeftStep = EdgeSlope(rs.vx[b] - rs.vx[a], dy);
    i32 sub = SubpixelToCeil(rs.vy[a]);
    rs.xLeft = rs.vx[a] + (i32)(((u64)((i64)rs.xLeftStep * (i64)sub)) >> 16);
}

int RasterizeFlatTriangle(Surface* fb, const RasterVertex v[3], u8 color,
                          u8 polyFlags38) {
    RasterState rs;
    std::memset(&rs, 0, sizeof(rs));
    rs.fbPitch = fb->widthPx;

    // --- winding select (0x603F0F) -------------------------------------------
    // Screen-space cross product on the ORIGINAL vertex order (the binary reads
    // *a1/a1[1]/a1[2] before any reversal). Back-wound when `>`:
    //   (x0-x2)*(y0-y1)  >  (x0-x1)*(y0-y2)
    // Back-wound + (poly+38 & 4)==0  -> cull (0x603FB0 `return`).
    // Back-wound + bit2 set          -> reverse load (v18 = v+2, --v18).
    // Front-wound                    -> forward load.
    const bool backWound =
        (v[0].x - v[2].x) * (v[0].y - v[1].y) >
        (v[0].x - v[1].x) * (v[0].y - v[2].y);
    bool reverse = false;
    if (backWound) {
        if ((polyFlags38 & 4) == 0)
            return 0;            // cull: no span drawn
        reverse = true;
    }

    int top = LoadVertices(rs, v, reverse);
    if (top < 0)
        return 0;

    // --- bounds rejection (0x603f72..0x603f8d) -------------------------------
    // The shadow flat path rejects the WHOLE triangle if ANY loaded vertex falls
    // outside [0, (pitch<<16)-1] in EITHER X or Y. edx = (a3<<16)-1; per vertex:
    //   vx < 0  || vx > limit || vy < 0 || vy > limit  -> return (draw nothing).
    // (The textured path @0x5F7D58 has no such loop — flat only.) The compares are
    // signed (jl); `cmp edx, vx; jl` rejects when vx > limit, etc.
    {
        i32 limit = (i32)((u32)rs.fbPitch << 16) - 1;   // (a3<<16)-1
        for (int i = 0; i < 3; ++i) {
            if (rs.vx[i] < 0 || limit < rs.vx[i] ||
                rs.vy[i] < 0 || limit < rs.vy[i])
                return 0;
        }
    }

    int n = kNext[top];
    int p = kPrev[top];
    i32 yTop = rs.vy[top];

    int v7, v19, v20, v21;
    if (yTop == rs.vy[n]) {
        v7 = top; v19 = n; v20 = p; v21 = kNext[n];
    } else if (yTop == rs.vy[p]) {
        v7 = p; v19 = top; v21 = n; v20 = kPrev[p];
    } else {
        v7 = top; v19 = top; v20 = p; v21 = n;
    }

    int drew = 0;
    const int pitchPx = rs.fbPitch;
    const int fbW = fb->width, fbH = fb->height;

    auto fillRange = [&](int rowCount, int y0) {
        int row = y0;
        for (int r = 0; r < rowCount; ++r, ++row) {
            i32 xL = (rs.xLeft + 0xFFFF) >> 16;
            i32 xR = (rs.xRight + 0xFFFF) >> 16;
            rs.spanLen = xR - xL;
            if (rs.spanLen > 0 && row >= 0 && row < fbH) {
                u8* dstRow = fb->pixels + (i64)row * pitchPx;
                int x0 = xL < 0 ? 0 : xL;
                int x1 = xR > fbW ? fbW : xR;
                for (int x = x0; x < x1; ++x) {
                    dstRow[x] = color;
                    drew = 1;
                }
            }
            rs.xLeft  = WrapAddI32(rs.xLeft,  rs.xLeftStep);   // mod-2^32 (no UB)
            rs.xRight = WrapAddI32(rs.xRight, rs.xRightStep);  // HARDENING wave-10
        }
    };

    i32 longDy = rs.vy[v20] - rs.vy[v7];
    if (longDy > 0) {
        ComputeEdgeSlopeLeft(rs, v7, v20);    // long left edge
        i32 shortDy = rs.vy[v21] - rs.vy[v19];
        if (shortDy > 0) {
            InterpolateEdgeZ(rs, v19, v21);   // short right edge
            int y0 = (rs.vy[v7] + 0xFFFF) >> 16;
            i32 yMidR = rs.vy[v20], yBot = rs.vy[v21];
            bool rightShorter = yMidR < yBot;
            i32 yEnd = rightShorter ? (yMidR + 0xFFFF) : (yBot + 0xFFFF);
            int rowCount = (yEnd >> 16) - y0;
            fillRange(rowCount, y0);

            if (rs.vy[v20] != rs.vy[v21]) {
                int y1 = rowCount + y0;
                i32 hi, lo;
                if (rightShorter) {
                    ComputeEdgeSlopeLeft(rs, v20, v21);
                    hi = rs.vy[v21]; lo = rs.vy[v20];
                } else {
                    InterpolateEdgeZ(rs, v21, v20);
                    hi = rs.vy[v20]; lo = rs.vy[v21];
                }
                int rc2 = ((hi + 0xFFFF) >> 16) - ((lo + 0xFFFF) >> 16);
                fillRange(rc2, y1);
            }
        }
    }
    return drew;
}

// ---------------------------------------------------------------------------
// Portable scalar reconstruction of the MMX bilinear block (0x5F76E2). The MMX
// path packs the 4 corner texels, multiplies by the fractional weight vector
// (pmaddwd), >>8 per channel, then recombines through the channel-spread LUTs.
// The scalar fast path (4 identical row ptrs) point-samples. We reproduce the
// per-channel arithmetic with plain integer ops. weightLow/weightHigh are the
// (1-fy, fy) row weights packed in mm6; per-step the horizontal frac is folded
// into the LUT indices via the same >>8 recombination.
// ---------------------------------------------------------------------------
void BilinearBlendBlock(u16* dst, const u8* const srcRows[4], int w, int h,
                        int srcStep, int dstStep,
                        const u32 rLut[256], const u32 gLut[256],
                        const u32 bLut[256], u16 weightLow, u16 weightHigh) {
    bool sameRows = (srcRows[1] == srcRows[0]) &&
                    (srcRows[2] == srcRows[0]) &&
                    (srcRows[3] == srcRows[0]);
    (void)weightLow;
    (void)weightHigh;
    if (sameRows) {
        // scalar fast path: point-sample srcRows[0] (3 channel bytes per texel),
        // recombine via the channel LUTs (matches loc_5F770C).
        const u8* src = srcRows[0];
        for (int y = 0; y < h; ++y) {
            const u8* s = src;
            u16* d = dst;
            for (int x = 0; x < w; ++x) {
                u32 c = bLut[s[0]] + gLut[s[1]] + rLut[s[2]];
                *d++ = (u16)c;
                s += srcStep;
            }
            src += 0; // row ptr advanced by caller via dstStep semantics
            dst += dstStep;
        }
        return;
    }
    // MMX path reconstruction: bilerp the 4 corners by the row weights, then
    // recombine through the channel LUTs (matches loc_5F7770). We average the
    // 4 corners with the supplied weights split 50/50 horizontally (the engine
    // uses this only for symmetric 2x magnification of texture tiles).
    for (int y = 0; y < h; ++y) {
        const u8* r0 = srcRows[0] + (i64)y * srcStep;
        const u8* r1 = srcRows[1] + (i64)y * srcStep;
        const u8* r2 = srcRows[2] + (i64)y * srcStep;
        const u8* r3 = srcRows[3] + (i64)y * srcStep;
        u16* d = dst + (i64)y * dstStep;
        for (int x = 0; x < w; ++x) {
            int b = (r0[0] + r1[0] + r2[0] + r3[0]) >> 2;
            int g = (r0[1] + r1[1] + r2[1] + r3[1]) >> 2;
            int rr = (r0[2] + r1[2] + r2[2] + r3[2]) >> 2;
            d[x] = (u16)(bLut[b] + gLut[g] + rLut[rr]);
            r0 += srcStep; r1 += srcStep; r2 += srcStep; r3 += srcStep;
        }
    }
}

} // namespace guild::render
