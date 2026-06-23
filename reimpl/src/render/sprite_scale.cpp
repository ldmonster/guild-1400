#include "render/sprite_scale.h"

#include <cmath>
#include <cstring>

namespace guild::render {

namespace {
inline u32 GetU32(const u8* b) { u32 v; std::memcpy(&v, b, 4); return v; }
inline u16 GetU16(const u8* b) { u16 v; std::memcpy(&v, b, 2); return v; }

// Shape header field offsets (see sprite_scale.h).
constexpr size_t kWidth   = 0x06;  // u16
constexpr size_t kHeight  = 0x0A;  // u16
constexpr size_t kRunBase = 0x32;  // first row's runCount (== a3 + 50)
constexpr size_t kRunData = 0x36;  // first row's first run    (== a3 + 54)

// BYTE1 / SBYTE1: bits 8..15 of a 32-bit word, as an unsigned / signed byte. The
// originals index/compare the integer part of an 8.8 fixed-point accumulator.
inline u8  Byte1(int v)  { return static_cast<u8>((static_cast<u32>(v) >> 8) & 0xFF); }
inline i8  SByte1(int v) { return static_cast<i8>(Byte1(v)); }
inline u8  HiByte(i16 v) { return static_cast<u8>((static_cast<u16>(v) >> 8) & 0xFF); }
inline i8  SHiByte(i16 v){ return static_cast<i8>(HiByte(v)); }
} // namespace

// gilde.exe 0x5D72F0 — VIBE_Shape_BlitScaled16.
//   v16 = shape + 50                         ; current source ROW base (byte ptr)
//   v18 = 2*(widthPx*y + x) + base           ; current dest ROW base (byte offset)
//   v22 = (u8)*(shape+6)  width   v21 = (u8)*(shape+10) height
//   v19 = 2*widthPx (dest row stride bytes)  v20 = 2*(u16)width (src row stride bytes)
//   if useColorKey:  per dest pixel sample s = *(u16*)(2*HIBYTE(acc) + rowBase);
//                    if s != 0 write s; acc += xStep (an i16); while SHIBYTE(acc) < w
//   else:            write *(u16*)(2*BYTE1(acc) + rowBase) unconditionally; acc += xStep
//   per row: dst += v19; yAcc += yStep; rowBase = v17 + v20 * BYTE1(yAcc);
//            while SBYTE1(yAcc) < height.  v17 stays the shape's pixel-block base.
u64 ShapeBlitScaled16(int x, int y, const u8* shape, const ColorBlitTarget16& dst,
                      int xStep, int yStep, bool useColorKey) {
    const u8* pixelBlock = shape + kRunBase;       // v16 / v17 (shape + 50)
    const int widthPx    = dst.widthPx;            // *(surface+16)
    // dest byte offset relative to the u16 buffer base; kept as a word index.
    ptrdiff_t destWordOff = static_cast<ptrdiff_t>(widthPx) * y + x;  // v18/2
    u16* base = dst.pixels;

    const u8  w = static_cast<u8>(shape[kWidth]);  // v22  (low byte of width)
    const u8  h = static_cast<u8>(shape[kHeight]); // v21  (low byte of height)
    // v20 = 2*(u16)width — the source row stride in BYTES (rowBase is a byte ptr).
    const ptrdiff_t srcRowStrideBytes = 2 * static_cast<ptrdiff_t>(GetU16(shape + kWidth));

    const u8* rowBase = pixelBlock;                // v16 advances per row
    u64 result = 0;

    if (useColorKey) {
        i16 yAcc = 0;                              // v12 (only BYTE1 used as row idx)
        do {
            i16 xAcc = 0;                          // v13
            u16* d = base + destWordOff;           // v14
            do {
                u16 s = GetU16(rowBase + 2u * HiByte(xAcc));   // *(u16*)(2*HIBYTE+row)
                if (s) *d = s;
                xAcc = static_cast<i16>(xAcc + xStep);
                ++d;
            } while (SHiByte(xAcc) < static_cast<i8>(w));
            destWordOff += widthPx;                // v18 += 2*widthPx (words)
            yAcc = static_cast<i16>(yAcc + yStep); // v12 += yStep
            result = static_cast<u64>(static_cast<u32>(srcRowStrideBytes))
                   * static_cast<u64>(Byte1(yAcc));
            rowBase = pixelBlock + static_cast<ptrdiff_t>(result);  // v16 = v20*BYTE1 + v17 (byte ptr)
        } while (SByte1(yAcc) < static_cast<i8>(h));
    } else {
        i32 yAcc = 0;                              // v8
        do {
            i32 xAcc = 0;                          // v9
            u16* d = base + destWordOff;           // v10
            do {
                *d = GetU16(rowBase + 2u * Byte1(xAcc));        // *(u16*)(2*BYTE1+row)
                xAcc += xStep;
                ++d;
            } while (SByte1(xAcc) < static_cast<i8>(w));
            destWordOff += widthPx;
            yAcc += yStep;
            result = static_cast<u64>(static_cast<u32>(srcRowStrideBytes))
                   * static_cast<u64>(Byte1(yAcc));
            rowBase = pixelBlock + static_cast<ptrdiff_t>(result);
        } while (SByte1(yAcc) < static_cast<i8>(h));
    }
    return result;
}

// gilde.exe 0x5D6A08 — VIBE_Shape_BlitRleScaled.
//   v23 = shape+54 (run cursor)   v19 = shape+50 (this row's runCount cursor)
//   v18 = 2*(x + widthPx*y) + base (dest byte offset)
//   reject if (u16)height + y < clipY0  or  x + (u16)width/scale > clipX1
//                                        or  x + (u16)width      < clipX0
//   v16 = clamp rows: if y + height/scale > clipY1 -> v16 = scale*(clipY1 - y),
//                     else v16 = height
//   v22 = running sub-pixel remainder carried across runs (the fractional X phase).
//   Outer over i in [0,v16): keep the row only when (i % scale)==0.
//     For each of *v19 runs:
//       advance dst by (v22 + (skip>>1)) / scale     ; integer-scaled transparent skip
//       v22 = (skip >> (v22+1)) % scale              ; new fractional phase  (sic)
//       if (i % scale)==0:  emit nPixels/scale pixels, sampling px[scale*k]
//       v23 = next run (skip 2*nPixels + 8 bytes)
//     if (i % scale)==0: dst row base += 2*widthPx
//     v19 = v23 (next row's runCount); v23 = v19 + 4 (skip the runCount dword); ++i
//   The y<clipY0 branch adds a per-pixel "a2 + i/scale >= clipY0" emit gate.
static int RleScaledImpl(int x, int y, const u8* shape, const ColorBlitTarget16& dst,
                         u8 scale, const FrameBlitState& st, bool lightTable) {
    const int widthPx = dst.widthPx;                              // *(a4+16)
    // v23 -> first run, v19 -> first row's runCount (the dword before the run).
    const u8* runCur   = shape + kRunData;                        // v23/v20
    const u8* countCur = shape + kRunBase;                        // v19/v16
    // dest word-index base (v18 is a byte offset; we keep words).
    ptrdiff_t rowBaseWords = static_cast<ptrdiff_t>(widthPx) * y + x;  // v18/2
    u16* destBase = dst.pixels;

    const int height = GetU16(shape + kHeight);                   // (u16)*(a3+10)
    if (height + y < st.clipY0) return 0;

    // BlitRleScaled (0x5D6A08) performs an X-axis clip against clipX0/clipX1 here;
    // BlitRleLightTable (0x5D6D74) does NOT — its prologue reads no width and runs
    // no X-clip (disasm 0x5d6dc6 jumps straight to the Y-clip at 0x5d6de6). Gate the
    // X-clip on !lightTable to match each binary exactly.
    if (!lightTable) {
        const int width = GetU16(shape + kWidth);                 // (u16)*(a3+6)
        if (x + width / static_cast<int>(scale) > st.clipX1 || x + width < st.clipX0)
            return 0;
    }

    int rowLimit;                                                 // v16
    if (y + height / scale > st.clipY1)
        rowLimit = scale * (st.clipY1 - y);
    else
        rowLimit = height;

    const u16* remap = lightTable ? st.remapTable : nullptr;      // dword_64A1C4

    u32 phase = 0;                                                // v22 fractional X
    const bool topClipped = (y < st.clipY0);                     // chooses the gated path

    int i = 0;
    while (i < rowLimit) {
        u16* d = destBase + rowBaseWords;                         // v9 / v14
        const u32 runCount = GetU32(countCur);                    // *v19/*v16
        for (u32 r = 0; r < runCount; ++r) {
            const u32 skip    = GetU32(runCur);                   // *v23 (>>1 etc.)
            const u32 nPixels = GetU32(runCur + 4);              // v23[1]
            d += (phase + (skip >> 1)) / scale;
            // x86 `shr r/m32, cl` masks the shift count to 5 bits (count & 0x1F).
            // `phase` < scale (<= 254), so `phase + 1` can exceed 31 for a large
            // `scale`; a bare C++ `>>` with count >= 32 is UB. Masking to 31 is the
            // faithful translation of the x86 instruction the binary executes (and is
            // byte-identical for the common small-scale case where phase+1 < 32).
            phase = (skip >> ((phase + 1) & 31u)) % scale;
            if ((i % scale) == 0) {
                const u8* px = runCur + 8;                       // v11 / v15 = v23 + 2 (words)
                u32 emitted = 0;                                 // v10 / v24
                const u32 count = (phase + nPixels) / scale;
                while (emitted < count) {
                    if (!topClipped || (y + i / static_cast<int>(scale) >= st.clipY0)) {
                        u16 s = GetU16(px);
                        *d = remap ? remap[s] : s;
                    }
                    ++d;
                    ++emitted;
                    px += 2u * scale;                            // v11 += scale (words)
                }
                phase = (phase + nPixels) % scale;
            }
            runCur = runCur + 2u * nPixels + 8u;                 // next run
        }
        if ((i % scale) == 0)
            rowBaseWords += widthPx;                              // v18 += 2*widthPx
        countCur = runCur;                                       // v19 = v23 (next row's runCount)
        runCur = countCur + 4;                                  // v23 = v19 + 4 (skip runCount dword)
        ++i;
    }
    return 1;
}

int ShapeBlitRleScaled(int x, int y, const u8* shape, const ColorBlitTarget16& dst,
                       u8 scale, const FrameBlitState& st) {
    return RleScaledImpl(x, y, shape, dst, scale, st, /*lightTable=*/false);
}

// gilde.exe 0x5D6D74 — VIBE_Shape_BlitRleLightTable. Identical scaling/clipping to
//   BlitRleScaled; the only difference is the written pixel is remapTable[srcPixel]
//   (dword_64A1C4) instead of the raw source word.
int ShapeBlitRleLightTable(int x, int y, const u8* shape, const ColorBlitTarget16& dst,
                           u8 scale, const FrameBlitState& st) {
    return RleScaledImpl(x, y, shape, dst, scale, st, /*lightTable=*/true);
}

// gilde.exe 0x5D86C4 — VIBE_Shape_ShowFromBankScaled.
//   if (!bank) return bank;
//   if ((u8)index > (i32)*(u16*)(bank+42)) { sprintf error; return 0; }   // +0x2A count
//   shape = bank + *(u32*)(bank + 4*(u8)index + 69);                      // +0x45 table
//   if (!byte_140694B) {                       // global "scaled draw enabled" gate
//       saved = dword_64A1C8; dword_64A1C8 = *(surface+16);              // install stride
//       depth = *(u8*)(shape+12);
//       if (!depth) return 0;
//       if (depth <= 1) {
//           if (!doScaledBlit) return 1;                                  // bit 0x80.. test
//           if (lightTable) return BlitRleLightTable(...);
//           return BlitRleScaled(...);
//       }
//       if (depth == 2) return 1;
//       dword_64A1C8 = saved;
//   }
//   // fall-through: recompute the scaled clip extents and return 1.
int ShapeShowFromBankScaled(int x, int y, const u8* bank, int shapeIndex, u8 scale,
                            bool doScaledBlit, bool lightTable,
                            const ColorBlitTarget16& dst, FrameBlitState& st,
                            bool highColorMode) {
    if (!bank) return 0;

    const int count = static_cast<i32>(GetU16(bank + 0x2A));     // *(u16*)(bank+42)
    if (static_cast<u8>(shapeIndex) > count) {
        // Original: VIBE_Crt_Sprintf_0(buf, "shp_ShowShapeFromBank:Shapenr is
        // invalidate! Bank:%s", bank+10) — a diagnostic into a stack buffer that the
        // original discards. Reproduced as a no-op (no observable state change).
        return 0;
    }

    const u32 shapeOff = GetU32(bank + 4 * static_cast<u8>(shapeIndex) + 0x45);
    const u8* shape = bank + shapeOff;

    // byte_140694B (highColorMode) gates the scaled blit block (disasm 0x5d8704
    // `cmp byte_140694B,0 / jnz`). When clear (default) we run the install+dispatch;
    // when set we fall straight through to the clip-extent recompute below.
    if (!highColorMode) {
        const int savedStride = st.destStridePx;                 // dword_64A1C8
        st.destStridePx = dst.widthPx;                           // = *(surface+16)

        const u8 depth = shape[0x0C];                            // *(u8*)(shape+12)
        if (!depth) return 0;
        if (depth <= 1) {
            if (!doScaledBlit) return 1;                         // (a5 & 0x8000000000)==0
            if (lightTable)
                return ShapeBlitRleLightTable(x, y, shape, dst, scale, st);
            return ShapeBlitRleScaled(x, y, shape, dst, scale, st);
        }
        if (depth == 2) return 1;
        st.destStridePx = savedStride;                          // restore (depth > 2)
    }

    // Fall-through (highColorMode set, or depth > 2): the binary recomputes the
    // scaled clip extents — HIWORD(dword_64A1A2) = width/scale, word_64A1A6 =
    // height/scale (0x5d874f/0x5d876d). dword_64A1A2/word_64A1A6 are owned by other
    // modules (gamelogic_recon's `defaultBorder` et al.) and are not reachable from
    // this signature; this cross-module state write is left as a BOUNDARY rather than
    // faked. The return value (1) is unaffected.
    return 1;
}

// gilde.exe 0x5D861C — VIBE_Shape_ShowFromBank. The unscaled sibling of
//   ShowFromBankScaled: same bank lookup/gate, but the depth<=1 path runs the
//   recolouring blit VIBE_Shape_BlitColored16 (in shape_blit.cpp). Note the count
//   test uses `> count` directly (no (u8) cast — index is already a u8 here).
int ShapeShowFromBank(int x, int y, const u8* bank, int shapeIndex,
                      const ColorBlitTarget16& dst, const ColorFormat& fmt,
                      FrameBlitState& st, bool highColorMode) {
    if (!bank) return 0;

    const int count = static_cast<i32>(GetU16(bank + 0x2A));     // *(u16*)(bank+42)
    if (shapeIndex > count) {
        // diagnostic sprintf into a discarded stack buffer in the original; no-op.
        return 0;
    }

    const u32 shapeOff = GetU32(bank + 4 * shapeIndex + 0x45);
    const u8* shape = bank + shapeOff;

    // byte_140694B (highColorMode) gates the block (disasm 0x5d864c `cmp .,0 / jnz`).
    // When set, the binary returns 1 without installing the stride or blitting.
    if (!highColorMode) {
        const int savedStride = st.destStridePx;                 // dword_64A1C8
        st.destStridePx = dst.widthPx;                           // = *(surface+16)

        const u8 depth = shape[0x0C];                            // *(u8*)(shape+12)
        if (!depth) return 0;
        if (depth <= 1) {
            ShapeBlitColored16(x, y, shape, dst, fmt);           // shape_blit.cpp
            return 1;
        }
        if (depth == 2) return 1;
        st.destStridePx = savedStride;
    }
    return 1;
}

// gilde.exe 0x559D60 — VIBE_Render_EncodeSpriteDrawFlags.
int RenderEncodeSpriteDrawFlags(int mode, int value) {
    if (mode == 1)
        return 0;
    return (value << 22) | 0x8000000;
}

// =============================================================================
// gilde.exe 0x5AC970 — VIBE_Particle_UpdateBillboards (vertex projection arm).
//
//   verts come from the node's billboard set; the original strides 0x50 bytes
//   per record and tests bit7 of +0x4C (signed-byte < 0). Three arms:
//     byte_649D70 && byte_649DD8  -> project + depth fade (alpha @+0x4F)  [0x5AC9A4]
//     byte_649D70 && !byte_649DD8 -> project, NO fade                     [0x5ACB4F]
//     !byte_649D70                -> project + byteOut(+0x42), skip dead   [0x5ACC0B]
//   The first two strictly skip records whose bit7 is clear; the third uses a
//   while-loop that advances over dead records first (same observable effect).
// =============================================================================

namespace {
// VIBE_Coord_ConvertX @0x5C6B08 sets the x87 rounding mode to truncate-toward-
// zero (control word RC field) and frndint's. With that mode `fistp` truncates
// toward zero — i.e. C's float->int conversion. The result is stored as a byte
// (mov al, ...), so only the low 8 bits survive.
inline u8 TruncToByte(double v) { return static_cast<u8>(static_cast<i32>(v)); }
} // namespace

void ProjectBillboardVertices(BillboardVertex* verts, unsigned vertCount,
                              const BillboardParams& p) {
    if (p.enabled) {
        if (p.depthFade) {
            // ---- 0x5AC9A4: project + depth fade ----
            for (unsigned i = 0; i < vertCount; ++i) {
                BillboardVertex& v = verts[i];
                if (!(v.flags() & 0x80)) continue;               // test [edx+4Ch],80h

                const float cx = v.cx(), cy = v.cy(), cz = v.cz();
                const float invZ = 1.0f / cz;                    // fld1; fdiv [edx+8]
                // distSq = cx*cx + cy*cy + cz*cz  (stored to flt_13FC548)
                const float distSq = cy * cy + cx * cx + cz * cz;
                v.invZ() = invZ;                                 // [edx+1Ch]
                v.colorOut() = v.colorSrc();                     // [edx+40h] = [edx+44h]
                // screenX = projScaleX*cx*invZ + centerX ; screenY likewise
                v.screenX() = p.projScaleX * cx * invZ + p.centerX;  // [edx+10h]
                v.screenY() = p.projScaleY * cy * invZ + p.centerY;  // [edx+14h]

                double alpha;
                if (distSq <= p.fadeMinSq) {                     // fcomp flt_13FC544; jbe
                    alpha = 255.0;
                } else {
                    double t = (std::sqrt(static_cast<double>(distSq))
                                - p.fadeNear) * p.fadeScale;     // (sqrt-near)*scale
                    // min(255.0, t): the original clamps t up to 255 before the
                    // 255 - t. (dbl_628074 = 255.0; jnb keeps t, else t = 255.)
                    if (t > 255.0) t = 255.0;                    // 406FE000h = 255.0
                    alpha = 255.0 - t;                           // dbl_628074 - clamp
                }
                v.alphaOut() = TruncToByte(alpha);              // [edx+4Fh]
            }
        } else {
            // ---- 0x5ACB4F: project, NO fade ----
            for (unsigned i = 0; i < vertCount; ++i) {
                BillboardVertex& v = verts[i];
                if (!(v.flags() & 0x80)) continue;

                const float cx = v.cx(), cy = v.cy();
                const float invZ = 1.0f / v.cz();
                v.invZ() = invZ;
                v.colorOut() = v.colorSrc();
                v.screenX() = p.projScaleX * cx * invZ + p.centerX;
                v.screenY() = p.projScaleY * cy * invZ + p.centerY;
            }
        }
        return;
    }

    // ---- 0x5ACC0B: billboards globally disabled — project + byteOut, skip dead ----
    for (unsigned i = 0; i < vertCount; ++i) {
        BillboardVertex& v = verts[i];
        if (!(v.flags() & 0x80)) continue;                       // jnz keeps live ones

        const float cx = v.cx(), cy = v.cy();
        const float invZ = 1.0f / v.cz();
        v.byteOut() = static_cast<u8>(v.byteSrc() >> 2);        // [edx+42h] = [edx+46h]>>2
        v.invZ() = invZ;
        v.screenX() = p.projScaleX * cx * invZ + p.centerX;
        v.screenY() = p.projScaleY * cy * invZ + p.centerY;
    }
}

// =============================================================================
// gilde.exe 0x5ACAB0 — VIBE_Particle_UpdateBillboards (node-level effect-tint arm).
//
//   Runs on the billboards-enabled branch (byte_649D70), AFTER both project arms
//   and BEFORE the quad visibility pass, gated on the node type byte +0x215:
//
//     al = *(a1+0x215);
//     if (al < 5)  goto quad-pass;               // jl loc_5ACAE0 — no tint
//     if (al == 8) esi = 0x1F1FFF;               // mov esi, 1F1FFFh
//     else { ... pack esi from the node tint floats ... }   // loc_5ACBBC
//     // broadcast loop (loc_5ACAC7):
//     eax = *(a1+0x1CC);  eax = *eax;            // verts base = *v2
//     for (edx = 0; edx < count; ++edx) {
//         eax += 0x50;  *(eax-0x10) = esi;       // vertex[i] + 0x40 = packed tint
//     }
//
//   The packing block (loc_5ACBBC) reads three floats and narrows each to a byte
//   with VIBE_Coord_ConvertX (x87 frndint truncate-toward-zero) then fistp + an
//   8-bit move:
//     R = (u8)trunc(*(a1+0x5C))   ->  shl 16
//     G = (u8)trunc(*(a1+0x60))   ->  shl 8
//     B = (u8)trunc(*(a1+0x64))
//     esi = (R<<16) | (G<<8) | B
//   (Stack trace: fld f60, fld f5C, ConvertX, fxch, ConvertX, fxch, fistp(f5C)->al
//    ->dl, fistp(f60)->al; edx = dl<<16 | (al&0xFF)<<8; fld f64, ConvertX, fistp,
//    esi = (al&0xFF) | edx.)  The constant 0x1F1FFF decodes the same way:
//    R=0x1F, G=0x1F, B=0xFF.
// =============================================================================

bool BillboardEffectTintColor(u8 nodeType, float tintR, float tintG, float tintB,
                              u32& outColor) {
    if (nodeType < 5) return false;                 // cmp al,5; jl loc_5ACAE0
    if (nodeType == 8) {                             // cmp al,8; jnz loc_5ACBBC
        outColor = 0x1F1FFFu;                        // mov esi, 1F1FFFh
        return true;
    }
    // loc_5ACBBC: pack from the three node tint floats (R=+0x5C, G=+0x60, B=+0x64).
    const u32 r = TruncToByte(static_cast<double>(tintR));   // (u8)trunc, then <<16
    const u32 g = TruncToByte(static_cast<double>(tintG));   // (u8)trunc, then <<8
    const u32 b = TruncToByte(static_cast<double>(tintB));   // (u8)trunc, low byte
    outColor = (r << 16) | (g << 8) | b;            // or edx,eax / or esi,edx
    return true;
}

void BillboardApplyEffectTint(BillboardVertex* verts, unsigned vertCount,
                              u8 nodeType, float tintR, float tintG, float tintB) {
    u32 packed = 0;
    if (!BillboardEffectTintColor(nodeType, tintR, tintG, tintB, packed))
        return;                                     // nodeType < 5 -> no broadcast
    // loc_5ACAC7: broadcast into every vertex's colorOut (+0x40); the loop is
    // unconditional over the count (no per-vertex live-bit test, unlike project).
    for (unsigned i = 0; i < vertCount; ++i)
        verts[i].colorOut() = packed;               // mov [eax-10h], esi
}

// gilde.exe 0x5ACAE0 — per-quad back-face / visibility pass.
void BillboardQuadVisibilityPass(BillboardQuad* quads, unsigned quadCount) {
    for (unsigned i = 0; i < quadCount; ++i) {
        BillboardQuad& q = quads[i];
        const u8 f = q.flags;                                    // [edx+24h]
        if (!(f & 0x80)) continue;                               // signed < 0 test

        if (f & 0x10) {                                          // bit4: force-keep
            q.flags = static_cast<u8>(f | 0x40);                 // set bit6
            continue;
        }
        // Projected winding test (0x5ACC92). The binary loads the screen coords as
        // floats but keeps the two subtractions AND products on the x87 stack in
        // 80-bit precision, comparing them with `fcompp` (no intermediate store to a
        // 32-bit float). Model the products in `double` so the comparison is not
        // perturbed by an intermediate round-to-float. eax=v0, edi=v1, esi=v2:
        //   product1 = (v0.sx - v1.sx)*(v0.sy - v2.sy)   [== rhs below]
        //   product2 = (v0.sx - v2.sx)*(v0.sy - v1.sy)   [== lhs below]
        // `fcompp; jbe` skips the cull when product2 <= product1, so cull happens
        // when product2 > product1, i.e. when `lhs > rhs` here.
        BillboardVertex* v0 = q.v0;
        BillboardVertex* v1 = q.v1;
        BillboardVertex* v2 = q.v2;
        const double lhs = (double(v0->screenX_c()) - double(v2->screenX_c()))
                         * (double(v0->screenY_c()) - double(v1->screenY_c()));
        const double rhs = (double(v0->screenX_c()) - double(v1->screenX_c()))
                         * (double(v0->screenY_c()) - double(v2->screenY_c()));
        if (lhs > rhs && (q.flags2 & 0x04) == 0)                 // [edx+26h] bit2
            q.flags = static_cast<u8>(f & 0x7F);                 // clear bit7
    }
}

} // namespace guild::render
