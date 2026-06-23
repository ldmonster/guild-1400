#include "render/shape_recon_cluster.h"

#include <cstring>

// =============================================================================
// 1:1 reconstruction of the gilde.exe shape / shape-bank / shape-anim cluster.
// See shape_recon_cluster.h for the address->symbol map and record layouts.
//
// Coupled leaves (memory allocator VIBE_Memory_AllocDebug/FreeDebug, the VFS
// file streams, VIBE_Coord_Transform, VIBE_State_Helper, VIBE_FrameData_Process,
// VIBE_Animation_Basic) are NOT reconstructed here — they live elsewhere in the
// live call tree. The PURE shape math (bound scan, RLE span encoding, header
// layout, colour packing/light table, frame slot logic) is reconstructed
// exactly. Where the original allocates a scratch buffer and reallocs into a
// tight blob, we write straight into the caller-provided `out` (the realloc is a
// pure memory-economy step with no observable effect on the blob contents).
// =============================================================================

namespace guild::render {

// ---- helpers ----------------------------------------------------------------

// gilde.exe 0x434F30 — VIBE_Result_Handler_Final.
//   (a2 >> gShiftDown << gShiftUp) | (a1 >> rShiftDown << rShiftUp)
//                                  | (a3 >> bShiftDown << bShiftUp)
// where a1=r(al), a2=g(dl), a3=b(bl).
u16 ColorPack(const ShapeColorState& st, u8 r, u8 g, u8 b) {
    int v = ((int)g >> st.gShiftDown << st.gShiftUp)
          | ((int)r >> st.rShiftDown << st.rShiftUp)
          | ((int)b >> st.bShiftDown << st.bShiftUp);
    return (u16)v;
}

// gilde.exe 0x434F7C — VIBE_Render_UnpackColor.
//   *r = a1 >> rShiftUp << rShiftDown;
//   *b = a1 >> gShiftUp << gShiftDown;   (edx/ecx/ebx outs: a2=r, a3=b? )
//   *g(a3) = (a1 >> bShiftUp) << bShiftDown;
// The original writes: *a2 = r, *a4 = g(v6), *a3 = b(result). We map a2->r,
// a4->g, a3->b to match BuildLightTable's (v5=r-out, v7=b-out, v6=g-out) usage.
void ColorUnpack(const ShapeColorState& st, u16 packed, u8& r, u8& g, u8& b) {
    unsigned a1 = packed;
    r = (u8)(a1 >> st.rShiftUp << st.rShiftDown);              // *a2
    g = (u8)(a1 >> st.gShiftUp << st.gShiftDown);              // *a4 (v6)
    unsigned res = (a1 >> st.bShiftUp) << st.bShiftDown;       // result
    b = (u8)res;                                               // *a3 (v7)
}

// gilde.exe 0x4226BC — VIBE_Color_NotEqualRgb.
bool ColorNotEqualRgb(const u8* a, const u8* b) {
    return a[0] != b[0] || a[1] != b[1] || a[2] != b[2];
}

// gilde.exe 0x4226E0 — VIBE_Color_SetRgb: *r=dl(a2); r[1]=bl(a4); r[2]=cl(a3).
// Called as SetRgb(dst, r, g, b) i.e. (a2=r, a3=g, a4=b) -> stores r, b, g.
void ColorSetRgb(u8* dst, u8 r, u8 g, u8 b) {
    dst[0] = r;
    dst[1] = b;
    dst[2] = g;
}

// ---- 0x5D49A0 VIBE_Shape_BuildLightTable ------------------------------------
int ShapeBuildLightTable(const ShapeColorState& st, u8 a1, u16* out) {
    if (!out) return 0;
    int v2 = 0;
    for (int i = 0; i < 0x10000; ++i) {
        u8 r, g, b;
        // The original passes (i, v5, v7, v6) = (r-out, b-out, g-out).
        ColorUnpack(st, (u16)i, r, g, b);
        // v7[0] is the BLUE channel slot (passed as 3rd arg a3 = ecx = v7).
        // The saturating-add edge cases per the decompile:
        //   if (b < 255 - a1) b += a1; else b = 0xFF;
        if (b < (u8)(255 - a1)) b += a1; else b = 0xFF;
        //   if (g >= 255 - a1) g = 0xFF; else g += a1;
        if (g >= (u8)(255 - a1)) g = 0xFF; else g += a1;
        //   if (r >= 255 - a1) r = 0xFF; else r += a1;
        if (r >= (u8)(255 - a1)) r = 0xFF; else r += a1;
        v2 += 2;
        // *(WORD*)(table + v2 - 2) = Pack(r, g, b)
        out[(v2 - 2) >> 1] = ColorPack(st, r, g, b);
    }
    return 1;
}

// ---- 0x5D4BCC VIBE_Shape_FreeLightTable -------------------------------------
bool ShapeShouldFreeLightTable(bool highColorMode, bool tablePresent) {
    // result = byte_140694B; if (!byte_140694B) { if (dword_64A1C4) free; }
    return !highColorMode && tablePresent;
}

// ---- 0x5D88C0 VIBE_Shape_SetMaskColor ---------------------------------------
u32 ShapeSetMaskColor(const ShapeColorState& st, u8 a1, u8 a2, u32& outMaskReg) {
    // result = VIBE_Result_Handler_Final(a1, a2);  (bl undefined -> 0 path)
    u32 result = ColorPack(st, a1, a2, 0);
    outMaskReg = result | (result << 16);
    return result;
}

// ---- 0x41F4F4 VIBE_Shape_ClassifyType ---------------------------------------
u8 ShapeClassifyType(const u8* a1) {
    if (!a1) return 0;
    // VIBE_Util_StrCmp(aShapbank, blob): a real strcmp that stops at the first
    // NUL in EITHER string. The magic constant aShapbank @0x611278 is the 8
    // bytes "SHAPBANK" followed by a NUL at index 8, so the comparison only ever
    // inspects blob[0..7] — equivalent to memcmp(blob,"SHAPBANK",8)==0. It
    // returns 0 on equality; the original branches on the NONZERO (mismatch)
    // result first.
    static const char kShapBank[8] = {'S','H','A','P','B','A','N','K'};
    bool notBank = std::memcmp(a1, kShapBank, 8) != 0;
    if (notBank) {
        // Non-bank blob (single shape/image): byte[2] is the format/version;
        // ==2 => new-format image (type 17), else unrecognised (0).
        if (a1[2] != 2) return 0;
        return 17;
    }
    // SHAPBANK file: classify by subtype byte[9] and shape count (u16 @ +42).
    u16 shapeCount = (u16)(a1[42] | (a1[43] << 8));
    if (a1[9] || shapeCount <= 1) {
        switch (a1[9]) {
            case 1:  return 4;
            case 2:  return 7;
            case 3:  return 8;
            default: return 1;
        }
    }
    return 5;
}

// ---- 0x5D8294 VIBE_ShapeBank_SetPalette -------------------------------------
int ShapeBankSetPalette(const ShapeColorState& st, const u8* src,
                        u8 palette1024[1024], u16 packedOut[257]) {
    if (!src) return 0;
    std::memcpy(palette1024, src, 0x400);
    int v1 = 0;
    const u8* v2 = src;
    do {
        // word_140621E[++v1] = Pack(v2[0], v2[1], v2[2]);
        ++v1;
        packedOut[v1] = ColorPack(st, v2[0], v2[1], v2[2]);
        // NOTE: the original advances v2 by the palette stride implicitly via
        // the 4*v1 index pattern elsewhere; here the loop reads consecutive
        // quads — v2 advances by 4 per entry to match byte_1406530[4*idx].
        v2 += 4;
    } while (v1 != 256);
    return 1;
}

// ---- 0x5D8F54 VIBE_ShapeBank_SetSequenceData --------------------------------
int ShapeBankSetSequenceData(u8* bank, const u8* src, u16 a3) {
    auto u32at = [&](size_t off) -> u32& { return *reinterpret_cast<u32*>(bank + off); };
    auto u16at = [&](size_t off) -> u16& { return *reinterpret_cast<u16*>(bank + off); };

    if (!u32at(62))
        u32at(62) = u32at(48);
    u8* v4 = bank + u32at(62);
    // qmemcpy(v4, src, 4*((8*a3)>>2)) == 8*a3 bytes.
    std::memcpy(v4, src, (size_t)(4 * (((unsigned)(8 * a3)) >> 2)));
    u32at(48) -= 8u * u16at(67);
    u32 v6 = u32at(48);
    u16at(67) = a3;
    u32at(48) = 8u * (u16)a3 + v6;
    return 8 * a3;
}

// =============================================================================
// The Grab* family.
//
// Common header field write helpers (offsets per shp_hdr).
// =============================================================================
namespace {
inline u32& U32(u8* p, size_t off) { return *reinterpret_cast<u32*>(p + off); }
inline u16& U16(u8* p, size_t off) { return *reinterpret_cast<u16*>(p + off); }
// word index helper: the original uses *((WORD*)out + k); offset = 2*k.
inline u16& W(u8* p, int k) { return *reinterpret_cast<u16*>(p + 2 * k); }
} // namespace

size_t ShapeGrabBit8MaxSize (int w, int h) { return (size_t)(4 * h + 8 * w * h + 50); }
size_t ShapeGrabBit16MaxSize(int w, int h) { return (size_t)(8 * w * h + 4 * h + 50); }
size_t ShapeGrabBit24MaxSize(int w, int h) { return (size_t)(12 * w * h + 4 * h + 50); }

// ---- 0x5D6160 VIBE_Shape_GrabBit8 -------------------------------------------
// a1=x0, a2=y0, a3=h(rows), a4=w(cols), a5=src, a6=typeFlag, a7=stride.
size_t ShapeGrabBit8(const ShapeColorState& st, const u8* palette,
                     int a1, int a2, int a3, int a4, const u8* a5In,
                     u8 a6, u16 a7, u8* out) {
    // The original mutates the source (zeroes mask/anchor pixels). Work on a
    // local mutable copy of the source rectangle's backing? No — the original
    // writes back into a5. To stay faithful (those writes are observable in
    // the original) but keep `src` const for callers, we operate on a copy of
    // the whole source span actually touched. We require a mutable buffer.
    // Faithful translation: cast away const, matching the original which takes
    // a writable a5.
    u8* a5 = const_cast<u8*>(a5In);
    const int h = a3, w = a4;

    // Bounds accumulators. v71=maxX, v8=minX(init w), v72=maxY, v9=minY(init w),
    // (v9/v7 init = a3? — in GrabBit8 v8=a4 init, v9 uninit min...). Track per
    // the decompile: v71=0(maxX), v72=0(maxY); v8=a4(minX), v9=?(minY).
    int maxX = 0, maxY = 0, minX = a4, minY = a4;

    // Pass 1: opaque bounding box (palette-index 0 == transparent).
    for (int row = 0; row < h; ++row) {
        int srcRow = a2 + row;
        for (int col = 0; col < w; ++col) {
            u8 px = a5[col + a1 + srcRow * a7];
            if (px && maxX < col) maxX = col;
            if (px && minX > col) minX = col;
            if (px && row > maxY) maxY = row;
            if (px && row < minY) minY = row;
        }
    }
    if (maxX < minX || maxY < minY) { minX = minY = maxX = maxY = 0; }

    W(out, 4) = (u16)minY;          // out[4] = v9 (minY)
    W(out, 2) = (u16)minX;          // out[2] = v8 (minX)
    W(out, 3) = (u16)(maxX + 1);    // width
    W(out, 7) = (u16)minX;          // out[7]
    W(out, 8) = (u16)minY;          // out[8]
    W(out, 9) = (u16)(maxX + 1);    // out[9]
    W(out, 5) = (u16)(maxY + 1);    // height = maxY+1
    W(out, 10)= (u16)(maxY + 1);

    // Pass 2: zero out mask-coloured pixels.
    //
    // FAITHFUL to gilde.exe 0x5d63d1..0x5d6461: the original walks a per-row
    // pointer v67 = v54 (v54 starts at a5 and is ++'d once per row), and the
    // inner column loop tests/zeroes *v67 WITHOUT ever advancing v67 by the
    // column index. So the binary only ever inspects a5[row] (the first `h`
    // bytes of the source buffer, ignoring a1/a2/stride entirely) and the inner
    // a4-iteration is a redundant (idempotent) repeat. We reproduce this exactly
    // — Hex-Rays shows `*v67` not indexed by v49, the disasm confirms `var_24`
    // (v67) is loaded once per row and never re-indexed in the inner loop.
    for (int row = 0; row < h; ++row) {
        u8* p = &a5[row];                          // v67 = v54 (= a5 + row)
        for (int col = 0; col < w; ++col) {        // inner loop (idempotent)
            const u8* q = &palette[4 * (unsigned)*p];
            if (q[0] == st.maskR && q[1] == st.maskG && q[2] == st.maskB)
                *p = 0;
        }
    }

    // Pass 3: anchor relocation (anchorB -> minX/minY2, anchorA -> max).
    for (int row = 0; row < h; ++row) {
        for (int col = 0; col < w; ++col) {
            int idxA = col + a1 + (a2 + row) * a7;
            u8 v = a5[idxA];
            const u8* q = &palette[4 * v];
            if (q[0] == st.anchorB_r && q[1] == st.anchorB_g && q[2] == st.anchorB_b) {
                minX = col; minY = row; a5[idxA] = 0;
            }
            if (q[0] == st.anchorA_r && q[1] == st.anchorA_g && q[2] == st.anchorA_b) {
                maxX = col; maxY = row; a5[col + a1 + (a2 + row) * a7] = 0;
            }
        }
    }
    W(out, 12) = (u16)minY;
    W(out, 11) = (u16)minX;
    U32(out, 0) = 50;
    W(out, 13) = (u16)maxX;
    W(out, 14) = (u16)maxY;

    // Pass 4: per-row RLE span encoding.
    // rowTable[256] holds the start offset of each row's span block.
    u32 rowTable[864] = {0};
    int rowIdx = 0;
    int rti = 0;     // byte index into rowTable (v53)
    u32 spanTotal = 0;
    int srcRowBase = a2;
    while ((int)W(out, 5) > rowIdx) {
        u32 cursor = U32(out, 0);
        int spanCount = 0;
        u8* rowHdrPtr = out + cursor;       // v52: where the per-row count goes
        U32(out, 0) = cursor + 4;
        u8* sp = out + (cursor + 4);
        *reinterpret_cast<u32*>((u8*)rowTable + rti) = (u32)(cursor);
        int x = 0;
        int rrow = srcRowBase;
        int width = W(out, 3);
        while (x < width) {
            U32(sp, 4) = 0;     // span opaque length
            U32(sp, 0) = 0;     // span transparent (skip) length
            // count transparent run
            for (int i = x + a1; !a5[i + rrow * a7] && x < width; ++i) {
                ++x;
                ++U32(sp, 0);
            }
            // count opaque run
            int base = x + a1;
            while (a5[rrow * a7 + base + U32(sp, 4)]
                   && x + (int)U32(sp, 4) < width)
                ++U32(sp, 4);
            std::memcpy(sp + 8, &a5[a7 * rrow + x + a1], U32(sp, 4));
            u32 olen = U32(sp, 4);
            u32 adv = olen ? olen - 1 : 0;
            U32(out, 46) += olen;
            x += adv + 1;
            u32 grew = olen + 8 + U32(out, 0);
            sp = out + grew;
            U32(out, 0) = grew;
            ++spanTotal;
            ++spanCount;
        }
        srcRowBase += 1;
        rti += 4;
        *reinterpret_cast<u32*>(rowHdrPtr) = (u32)spanCount;
        ++rowIdx;
    }

    // Append the row offset table.
    U32(out, 42) = U32(out, 0);
    U32(out, 0) += 4u * W(out, 5);
    std::memcpy(&out[U32(out, 42)], rowTable, 4u * W(out, 5));
    out[13] = (u8)a6;
    U32(out, 38) = spanTotal;

    (void)rowIdx;
    return U32(out, 0);
}

// ---- 0x5D5908 VIBE_Shape_GrabBit16 ------------------------------------------
// a1=x0, a2=y0, a3=h(rows), a4=w(cols), a5=src(u16*), a6=typeFlag, a7=stride.
size_t ShapeGrabBit16(const ShapeColorState& st,
                      int a1, int a2, int a3, int a4, const u16* a5,
                      u8 a6, u16 a7, u8* out) {
    const int rows = a3, cols = a4;
    // v8=maxX(0), v9=minX, v61=maxY(0), v7=minY(init a3=rows). minX init = a3.
    int maxX = 0, maxY = 0, minX = a3, minY = a3;

    for (int row = 0; row < rows; ++row) {
        int srcRow = a2 + row;
        for (int col = 0; col < cols; ++col) {
            u16 px = a5[col + a1 + srcRow * a7];
            if (px && maxX < col) maxX = col;
            if (px && minX > col) minX = col;
            if (px && row > maxY) maxY = row;
            if (px && row < minY) minY = row;
        }
    }
    if (maxX < minX || maxY < minY) { maxX = minX = maxY = minY = 0; }

    U32(out, 0) = 50;
    W(out, 2) = (u16)minX;
    W(out, 4) = (u16)minY;
    W(out, 7) = (u16)minX;
    W(out, 8) = (u16)minY;
    W(out, 11)= (u16)minX;
    W(out, 12)= (u16)minY;
    W(out, 3) = (u16)(maxX + 1);
    W(out, 5) = (u16)(maxY + 1);
    W(out, 9) = (u16)(maxX + 1);
    W(out, 10)= (u16)(maxY + 1);
    W(out, 14)= (u16)maxY;
    W(out, 13)= (u16)maxX;

    // packed mask colour to compare against (the transparent value).
    u16 maskPacked = ColorPack(st, st.maskR, st.maskG, st.maskB);

    u32 rowTable[864] = {0};
    int rowIdx = 0;
    int rti = 0;
    u32 opaqueRows = 0;   // v55
    int srcRowBase = a2;
    while ((int)W(out, 5) > rowIdx) {
        int x = 0;
        u8* rowHdrPtr = out + U32(out, 0);
        U32(out, 0) += 4;
        int spanCount = 0;
        *reinterpret_cast<u32*>((u8*)rowTable + rti) = (u32)(rowHdrPtr - out);
        u8* sp = out + U32(out, 0);
        int rrow = srcRowBase;
        int width = W(out, 3);
        while (x < width) {
            U32(sp, 4) = 0;
            U32(sp, 0) = 0;
            // transparent run: pixels == maskPacked
            for (int i = x + a1;
                 a5[i + rrow * a7] == maskPacked && x < width; ++i) {
                ++x;
                ++U32(sp, 0);
            }
            U32(sp, 0) *= 2;   // skip stored in bytes (2/pixel)
            // opaque run
            int base = x + a1;
            while (x + (int)U32(sp, 4) < width
                   && a5[base + (int)U32(sp, 4) + rrow * a7] != maskPacked)
                ++U32(sp, 4);
            std::memcpy(sp + 8, &a5[x + a1 + rrow * a7], 2u * U32(sp, 4));
            u32 olen = U32(sp, 4);
            if (olen) x += olen - 1;
            U32(out, 46) += olen;
            ++x;
            u8* base2 = out + U32(out, 0);
            u32 chunk = 2 * olen + 8;
            ++opaqueRows;
            sp = base2 + chunk;
            U32(out, 0) = (u32)(sp - out);
            ++spanCount;
        }
        srcRowBase += 1;
        *reinterpret_cast<u32*>(rowHdrPtr) = (u32)spanCount;
        rti += 4;
        ++rowIdx;
    }

    U32(out, 42) = U32(out, 0);
    u16 nrows = W(out, 5);
    U32(out, 0) += 4u * nrows;
    std::memcpy(&out[U32(out, 42)], rowTable, 4u * (((4u * (unsigned)nrows) >> 2)));
    out[13] = (u8)a6;
    U32(out, 38) = opaqueRows;
    return U32(out, 0);
}

// ---- 0x5D5E7C VIBE_Shape_GrabBit16NoRle -------------------------------------
size_t ShapeGrabBit16NoRle(const ShapeColorState& st,
                           int a1, int a2, int a3, int a4, const u16* a5,
                           u8 a6, u16 a7, u8* out) {
    const int rows = a3, cols = a4;
    int maxX = 0, maxY = 0, minX = a3, minY = a3;
    for (int row = 0; row < rows; ++row) {
        int srcRow = a2 + row;
        for (int col = 0; col < cols; ++col) {
            u16 px = a5[col + a1 + srcRow * a7];
            if (px && col > maxX) maxX = col;
            if (px && col < minX) minX = col;
            if (px && row > maxY) maxY = row;
            if (px && row < minY) minY = row;
        }
    }
    if (maxX < minX || maxY < minY) { minX = minY = maxX = maxY = 0; }

    W(out, 2) = (u16)minX;
    W(out, 4) = (u16)minY;
    W(out, 3) = (u16)(maxX + 1);
    U32(out, 0) = 50;
    W(out, 7) = (u16)minX;
    W(out, 8) = (u16)minY;
    W(out, 9) = (u16)(maxX + 1);
    W(out, 12)= (u16)minY;
    W(out, 5) = (u16)(maxY + 1);
    W(out, 10)= (u16)(maxY + 1);
    W(out, 13)= (u16)maxX;
    W(out, 14)= (u16)maxY;
    W(out, 11)= (u16)minX;
    (void)ColorPack(st, st.maskR, st.maskG, st.maskB);   // computed, discarded

    int row16 = 0;
    int srcRow = a2;
    u8* dst = &out[U32(out, 0)];
    while (row16 < (int)W(out, 5)) {
        u16 width = W(out, 3);
        std::memcpy(dst, &a5[a1 + srcRow * a7], 2u * width);
        dst += 2u * width;
        ++row16;
        U32(out, 46) += width;
        U32(out, 0) += 2u * width;
        ++srcRow;
    }
    U32(out, 42) = 0;
    out[13] = (u8)a6;
    U32(out, 38) = (u32)-1;
    return U32(out, 0);
}

// ---- 0x5D4C20 VIBE_Shape_GrabBit24 ------------------------------------------
// a1=x0, a2=y0, a3=h(rows), a4=w(cols), a5=src(3-byte rgb), a6, a7=stride.
size_t ShapeGrabBit24(const ShapeColorState& st,
                      int a1, int a2, int a3, int a4, u8* a5,
                      u8 a6, u16 a7, u8* out) {
    const int rows = a3, cols = a4;
    u8 mask[3] = { st.maskR, st.maskG, st.maskB };
    u8 anchorA[3] = { st.anchorA_r, st.anchorA_g, st.anchorA_b }; // byte_64A1AC
    u8 anchorB[3] = { st.anchorB_r, st.anchorB_g, st.anchorB_b }; // byte_64A1AF
    // v80/v81 = byte_5D4990 (all zero in image) — local copy of an anchor probe.
    u8 v80[3] = { 0, 0, 0 };

    int maxX = 0, maxY = 0, minX = a4, minY = a3; // v82=maxX,v83=maxY; v7=minY init a4? per decompile v7=a4,v8=a3
    // Per decompile: v7=a4 (minX in 'v7'?), v8=a3. Probe names map to:
    //   v82=maxX(0), v83=maxY(0), v7=minY-ish(a4), v8=minX-ish(a3). Keep the
    //   four-probe pattern exactly:
    int p_maxX = 0, p_maxY = 0, p_minX = a4, p_minY = a3;

    for (int y = 0; y < rows; ++y) {
        int srcY = a2 + y;
        for (int xx = 0; xx < cols; ++xx) {
            const u8* px = a5 + 3 * (xx + a1 + srcY * a7);
            if (ColorNotEqualRgb(px, mask) && p_maxX < xx) p_maxX = xx;
            if (ColorNotEqualRgb(px, mask) && p_minX > xx) p_minX = xx;
            if (ColorNotEqualRgb(px, mask) && p_maxY < y)  p_maxY = y;
            if (ColorNotEqualRgb(px, mask) && p_minY > y)  p_minY = y;
        }
    }
    if (p_maxX < p_minX || p_maxY < p_minY) { p_minX = p_minY = p_maxX = p_maxY = 0; }
    minX = p_minX; minY = p_minY; maxX = p_maxX; maxY = p_maxY;

    W(out, 2) = (u16)minX;
    W(out, 4) = (u16)minY;
    W(out, 3) = (u16)(maxX + 1);
    W(out, 7) = (u16)minX;
    W(out, 9) = (u16)(maxX + 1);
    W(out, 5) = (u16)(maxY + 1);
    W(out, 10)= (u16)(maxY + 1);
    W(out, 8) = (u16)minY;

    // Anchor relocation: only if anchorA(64A1AC) != local probe v80.
    if (ColorNotEqualRgb(anchorA, v80)) {
        for (int y = 0; y < rows; ++y) {
            int srcY = a2 + y;
            for (int xx = 0; xx < cols; ++xx) {
                u8* px = a5 + 3 * (xx + srcY * a7 + a1);
                // anchorB (64A1AF) hit
                if (ColorNotEqualRgb(anchorB, v80)
                    && !ColorNotEqualRgb(px, anchorB)) {
                    minX = xx; minY = y;
                    // SetRgb(px, dl=maskR, cl=maskB, bl=maskG) -> px[0]=R,
                    // px[1]=G, px[2]=B (disasm 0x5d5223: dl=byte_1406946,
                    // cl=SBYTE1(dword_1406947)=maskB, bl=LOBYTE=maskG).
                    ColorSetRgb(px, st.maskR, st.maskB, st.maskG);
                }
                // anchorA (64A1AC) hit
                if (!ColorNotEqualRgb(a5 + 3 * (xx + srcY * a7 + a1), anchorA)) {
                    maxX = xx; maxY = y;
                    ColorSetRgb(a5 + 3 * (xx + srcY * a7 + a1),
                                st.maskR, st.maskB, st.maskG);
                }
            }
        }
    }

    W(out, 12) = (u16)minY;   // v8
    W(out, 11) = (u16)minX;   // v7
    W(out, 13) = (u16)maxX;   // v82
    W(out, 14) = (u16)maxY;   // v83
    U32(out, 0) = 50;

    // Per-row RLE encoding (3 bytes/pixel).
    u32 rowTable[864] = {0};
    int rowIdx = 0;
    int rti = 0;
    u32 spanTotal = 0;
    int srcRowBase = a2;
    while ((int)W(out, 5) > rowIdx) {
        u8* rowHdrPtr = out + U32(out, 0);
        int spanCount = 0;
        U32(out, 0) += 4;
        *reinterpret_cast<u32*>((u8*)rowTable + rti) = (u32)(rowHdrPtr - out);
        u8* sp = out + U32(out, 0);
        int x = 0;
        int rrow = srcRowBase;
        int width = W(out, 3);
        while (x < width) {
            U32(sp, 4) = 0;
            U32(sp, 0) = 0;
            int base = x + a1;
            while (width > x
                   && !ColorNotEqualRgb(a5 + 3 * (base + rrow * a7), mask)) {
                ++base; ++x; ++U32(sp, 0);
            }
            U32(sp, 0) *= 3;
            int obase = x + a1;
            while ((int)U32(sp, 4) + x < width
                   && ColorNotEqualRgb(a5 + 3 * (obase + (int)U32(sp, 4) + rrow * a7), mask))
                ++U32(sp, 4);
            std::memcpy(sp + 8, a5 + 3 * (x + a1 + rrow * a7), 3u * U32(sp, 4));
            u32 olen = U32(sp, 4);
            x += (int)olen - 1;
            U32(out, 46) += olen;
            u32 chunk = 3 * olen + 8;
            u8* cur = out + U32(out, 0);
            ++spanTotal;
            U32(out, 0) += chunk;
            sp = cur + chunk;
            ++spanCount;
            ++x;
        }
        rowTable[0] = rowTable[0]; // no-op keep
        *reinterpret_cast<u32*>(rowHdrPtr) = (u32)spanCount;
        srcRowBase += 1;
        rti += 4;
        ++rowIdx;
    }

    U32(out, 42) = U32(out, 0);
    u16 nrows = W(out, 5);
    U32(out, 0) = 4u * nrows + U32(out, 0);
    std::memcpy(&out[U32(out, 42)], rowTable, 4u * (((4u * (unsigned)nrows) >> 2)));
    out[13] = (u8)a6;
    U32(out, 38) = spanTotal;
    return U32(out, 0);
}

// ---- 0x5D547C VIBE_Shape_GrabBit24NoRle -------------------------------------
size_t ShapeGrabBit24NoRle(const ShapeColorState& st,
                           int a1, int a2, int a3, int a4, u8* a5,
                           u8 a6, u16 a7, u8* out) {
    const int rows = a3, cols = a4;
    u8 mask[3] = { st.maskR, st.maskG, st.maskB };
    u8 anchorA[3] = { st.anchorA_r, st.anchorA_g, st.anchorA_b }; // 64A1AC
    u8 anchorB[3] = { st.anchorB_r, st.anchorB_g, st.anchorB_b }; // 64A1AF
    u8 v41[3] = { 0, 0, 0 };  // byte_5D4993 probe (zero in image)

    // Decompile var map: v43=maxX (init 0, grows on '<'); v45=minX (init a4,
    // shrinks on '>'); v44=maxY (init 0); v8=minY (init a3, shrinks on '<').
    int maxX = 0, maxY = 0, minX = a4, minY = a3;

    for (int y = 0; y < rows; ++y) {
        int srcY = a2 + y;
        for (int xx = 0; xx < cols; ++xx) {
            const u8* px = a5 + 3 * (xx + a1 + srcY * a7);
            if (ColorNotEqualRgb(px, mask) && maxX < xx) maxX = xx;   // v43
            if (ColorNotEqualRgb(px, mask) && minX > xx) minX = xx;   // v45
            if (ColorNotEqualRgb(px, mask) && y > maxY)  maxY = y;    // v44
            if (ColorNotEqualRgb(px, mask) && y < minY)  minY = y;    // v8
        }
    }
    // if ((u16)v43 < (u16)v45 || (u16)v44 < v8) zero all.
    if (maxX < minX || maxY < minY) { minY = minX = maxY = maxX = 0; }

    W(out, 2) = (u16)minX;        // v45
    W(out, 4) = (u16)minY;        // v8
    W(out, 3) = (u16)(maxX + 1);  // v43+1
    W(out, 7) = (u16)minX;        // v45
    W(out, 9) = (u16)(maxX + 1);
    W(out, 5) = (u16)(maxY + 1);  // v44+1
    W(out, 10)= (u16)(maxY + 1);
    W(out, 8) = (u16)minY;        // v8

    if (ColorNotEqualRgb(anchorA, v41)) {
        for (int y = 0; y < rows; ++y) {
            int srcY = a2 + y;
            for (int xx = 0; xx < cols; ++xx) {
                // anchorB hit: v45(minX)=col, v8(minY)=row.
                if (!ColorNotEqualRgb(a5 + 3 * (xx + srcY * a7 + a1), anchorB)) {
                    minX = xx; minY = y;
                    // SetRgb(dl=maskR, cl=maskB, bl=maskG) -> [R,G,B] in memory.
                    ColorSetRgb(a5 + 3 * (a1 + xx + srcY * a7),
                                st.maskR, st.maskB, st.maskG);
                }
                // anchorA hit: v44(maxY)=row, v43(maxX)=col.
                if (!ColorNotEqualRgb(a5 + 3 * (xx + srcY * a7 + a1), anchorA)) {
                    maxY = y; maxX = xx;
                    ColorSetRgb(a5 + 3 * (a1 + xx + srcY * a7),
                                st.maskR, st.maskB, st.maskG);
                }
            }
        }
    }

    U32(out, 0) = 50;
    W(out, 11) = (u16)minX;       // v45
    W(out, 12) = (u16)minY;       // v8
    W(out, 13) = (u16)maxX;       // v43
    W(out, 14) = (u16)maxY;       // v44

    int row = 0;
    int srcRow = a2;
    u8* dst = &out[U32(out, 0)];
    while ((int)W(out, 5) > row) {
        u16 width = W(out, 3);
        std::memcpy(dst, a5 + 3 * (a1 + srcRow * a7), 3u * width);
        dst += 3u * width;
        ++srcRow;
        U32(out, 46) += width;
        ++row;
        U32(out, 0) += 3u * width;
    }
    U32(out, 42) = 0;
    out[13] = (u8)a6;
    U32(out, 38) = (u32)-1;
    return U32(out, 0);
}

// ---- 0x5D68C4 VIBE_Shape_GrabByDepth ----------------------------------------
size_t ShapeGrabByDepth(const ShapeColorState& st, const u8* palette,
                        int x0, int y0, int w, int h,
                        u8 srcDepth, const void* pixels, u16 stride,
                        u8 typeFlag, bool useRle, u8* out) {
    // a3=h(rows), a4=w(cols) at the Grab* boundary; here we pass (h,w) through.
    size_t blob = 0;
    u8 depthCode = 0;
    if (useRle) {
        if (srcDepth >= 0x10) {
            if (srcDepth <= 0x10) {
                blob = ShapeGrabBit16(st, x0, y0, h, w,
                                      static_cast<const u16*>(pixels),
                                      (char)typeFlag, stride, out);
                depthCode = 1;
                if (!blob) return 0;
            } else if (srcDepth == 24) {
                blob = ShapeGrabBit24(st, x0, y0, h, w,
                                      static_cast<u8*>(const_cast<void*>(pixels)),
                                      (char)typeFlag, stride, out);
                depthCode = 2;
                if (!blob) return 0;
            } else {
                return 0;
            }
        } else if (srcDepth == 8) {
            blob = ShapeGrabBit8(st, palette, x0, y0, h, w,
                                 static_cast<const u8*>(pixels),
                                 (char)typeFlag, stride, out);
            depthCode = 0;
            if (!blob) return 0;
        } else {
            return 0;
        }
    } else {
        if (srcDepth < 0x10) {
            return 0;  // 8bpp NoRle has no path in the original
        } else if (srcDepth > 0x10) {
            if (srcDepth == 24) {
                blob = ShapeGrabBit24NoRle(st, x0, y0, h, w,
                                           static_cast<u8*>(const_cast<void*>(pixels)),
                                           (char)typeFlag, stride, out);
                depthCode = 2;
                if (!blob) return 0;
            } else {
                return 0;
            }
        } else {
            blob = ShapeGrabBit16NoRle(st, x0, y0, h, w,
                                       static_cast<const u16*>(pixels),
                                       (char)typeFlag, stride, out);
            depthCode = 1;
            if (!blob) return 0;
        }
    }
    out[12] = depthCode;   // +0x0C
    out[13] = typeFlag;    // +0x0D
    return blob;
}

// ---- 0x5D4AD4 VIBE_Shape_InitColorMasks -------------------------------------
// Pure-state form: derive the hi-bit mask + packed-mask from the shift state and
// optionally (re)build the light table. The sprintf debug log is dropped.
//   word_1406944 = ((1<<(7-bDown))-1)<<bUp | ((1<<(7-gDown))-1)<<gUp |
//                  ((1<<(7-rDown))-1)<<rUp;
void ShapeInitColorMasksImpl(const ShapeColorState& st, bool highColorMode,
                             u8 amount, u16* lightTableOut /*opt*/,
                             u16& hiBitMaskOut, u32& hiBitMaskDwordOut) {
    if (!highColorMode && lightTableOut)
        ShapeBuildLightTable(st, amount, lightTableOut);   // BuildLightTable(0x30)
    u16 m = (u16)((((1 << (7 - st.bShiftDown)) - 1) << st.bShiftUp)
                | (((1 << (7 - st.gShiftDown)) - 1) << st.gShiftUp)
                | (((1 << (7 - st.rShiftDown)) - 1) << st.rShiftUp));
    hiBitMaskOut = m;
    hiBitMaskDwordOut = (u32)m | ((u32)m << 16);
}

// =============================================================================
// Shape-anim slot table.
// =============================================================================
int ShapeAnimDrawAllSlots(const u8 table[272], int drawArg,
                          ShapeAnimDrawFn drawSlot, void* ctx) {
    int result = drawArg;   // the original threads `result@<eax>` (== drawArg)
    int v1 = drawArg;
    for (int i = 0; i != 272; i += 17) {
        const u8* slot = table + i;
        u32 object = *reinterpret_cast<const u32*>(slot + anim_slot::kObject);
        if (object) {
            i16 x = *reinterpret_cast<const i16*>(slot + anim_slot::kX);
            i16 y = *reinterpret_cast<const i16*>(slot + anim_slot::kY);
            u8 shapeNr = slot[anim_slot::kShapeNr];
            result = drawSlot(x, y, object, v1, shapeNr, ctx);
        }
    }
    return result;
}

const u8* ShapeAnimGetSlot(const u8 table[272], int n) {
    return table + 17 * n;
}
u8* ShapeAnimGetSlot(u8 table[272], int n) {
    return table + 17 * n;
}
const u8* ShapeAnimGetSlotTable(const u8 table[272]) {
    return table;
}

} // namespace guild::render
