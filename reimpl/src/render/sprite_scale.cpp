#include "render/sprite_scale.h"

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

    const int width = GetU16(shape + kWidth);                     // (u16)*(a3+6)
    if (x + width / static_cast<int>(scale) > st.clipX1 || x + width < st.clipX0)
        return 0;

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
            phase = (skip >> (phase + 1)) % scale;
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
                            const ColorBlitTarget16& dst, FrameBlitState& st) {
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

    // byte_140694B gates the scaled blit path; when clear (the common case) we run it.
    const int savedStride = st.destStridePx;                     // dword_64A1C8
    st.destStridePx = dst.widthPx;                               // = *(surface+16)

    const u8 depth = shape[0x0C];                                // *(u8*)(shape+12)
    if (!depth) return 0;
    if (depth <= 1) {
        if (!doScaledBlit) return 1;
        if (lightTable)
            return ShapeBlitRleLightTable(x, y, shape, dst, scale, st);
        return ShapeBlitRleScaled(x, y, shape, dst, scale, st);
    }
    if (depth == 2) return 1;
    st.destStridePx = savedStride;                              // restore (depth > 2)
    return 1;
}

// gilde.exe 0x5D861C — VIBE_Shape_ShowFromBank. The unscaled sibling of
//   ShowFromBankScaled: same bank lookup/gate, but the depth<=1 path runs the
//   recolouring blit VIBE_Shape_BlitColored16 (in shape_blit.cpp). Note the count
//   test uses `> count` directly (no (u8) cast — index is already a u8 here).
int ShapeShowFromBank(int x, int y, const u8* bank, int shapeIndex,
                      const ColorBlitTarget16& dst, const ColorFormat& fmt,
                      FrameBlitState& st) {
    if (!bank) return 0;

    const int count = static_cast<i32>(GetU16(bank + 0x2A));     // *(u16*)(bank+42)
    if (shapeIndex > count) {
        // diagnostic sprintf into a discarded stack buffer in the original; no-op.
        return 0;
    }

    const u32 shapeOff = GetU32(bank + 4 * shapeIndex + 0x45);
    const u8* shape = bank + shapeOff;

    const int savedStride = st.destStridePx;                     // dword_64A1C8
    st.destStridePx = dst.widthPx;                               // = *(surface+16)

    const u8 depth = shape[0x0C];                                // *(u8*)(shape+12)
    if (!depth) return 0;
    if (depth <= 1) {
        ShapeBlitColored16(x, y, shape, dst, fmt);               // shape_blit.cpp
        return 1;
    }
    if (depth == 2) return 1;
    st.destStridePx = savedStride;
    return 1;
}

// gilde.exe 0x559D60 — VIBE_Render_EncodeSpriteDrawFlags.
int RenderEncodeSpriteDrawFlags(int mode, int value) {
    if (mode == 1)
        return 0;
    return (value << 22) | 0x8000000;
}

} // namespace guild::render
