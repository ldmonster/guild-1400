#include "render/shape_blit.h"

#include <cstring>

namespace guild::render {

namespace {
inline u32 GetU32(const u8* b) { u32 v; std::memcpy(&v, b, 4); return v; }
inline u16 GetU16(const u8* b) { u16 v; std::memcpy(&v, b, 2); return v; }

// gilde.exe luma weights (dbl_629478 / dbl_629480), recovered byte-for-byte:
//   0x3FC999999999999A = 0.2   0x3FE2E147AE147AE1 = 0.59
constexpr double kWLow  = 0.2;   // dbl_629478 (applied to r AND g)
constexpr double kWHigh = 0.59;  // dbl_629480 (applied to b)

// VIBE_Coord_ConvertX @0x5c6b08: round the FPU value toward zero (chop). The
// original frndint's under a truncating control word; trunc(double) == (int)
// for the in-range non-negative luma here.
inline int ChopToInt(double v) { return static_cast<int>(v); }
} // namespace

// gilde.exe 0x5D7164 — VIBE_Shape_BlitColored16.
//   v26 = shape + 54           ; -> current row's first run
//   v24 = shape + 50           ; -> current row's runCount
//   v23 = pixels + (widthPx*y + x)        ; current scanline dst (byte offset *2)
//   for each row in [0, height@+10):
//     v7 = v23                            ; reset dst to row start
//     for (i = 0; i < *v24; advance v26 past the run):
//       v7 += 2*(*v26 >> 1)               ; skip transparent pixels (>>1 pixels)
//       for (k = 0; k < v26[1]; ++k):
//         unpack src px -> (r,g,b); L = chop(r*0.2 + b*0.59 + g*0.2); write pack(L,L,L)
//       v26 += 2*v26[1] + 8               ; next run
//     v23 += 2*widthPx ; v24 = v26 ; ++v26 ; ++row
//   return height
u16 ShapeBlitColored16(int x, int y, const u8* shape, const ColorBlitTarget16& dst,
                       const ColorFormat& fmt) {
    u16* rowStart = dst.pixels + (static_cast<ptrdiff_t>(dst.widthPx) * y + x);

    const u8* runCountPtr = shape + 0x32;   // v24
    const u8* run         = shape + 0x36;   // v26

    u16 height = 0;
    int rowIdx = 0;
    while (true) {
        height = GetU16(shape + 0x0A);      // *(shape+10), the loop bound + return
        if (height <= rowIdx)
            break;

        u16* d = rowStart;                  // v7 = v23
        const u32 runs = GetU32(runCountPtr);
        for (u32 i = 0; i < runs; ++i) {
            const u32 skip    = GetU32(run);
            const u32 nPixels = GetU32(run + 4);
            d += (skip >> 1);
            const u8* px = run + 8;
            for (u32 k = 0; k < nPixels; ++k) {
                u8 r, g, b;
                UnpackColor(fmt, GetU16(px), r, g, b);
                double luma = static_cast<double>(r) * kWLow
                            + static_cast<double>(b) * kWHigh
                            + static_cast<double>(g) * kWLow;
                int L = ChopToInt(luma);
                *d++ = static_cast<u16>(PackColor(fmt, static_cast<u8>(L),
                                                  static_cast<u8>(L),
                                                  static_cast<u8>(L)));
                px += 2;
            }
            run = run + 2u * nPixels + 8u;
        }
        runCountPtr = run;
        run = runCountPtr + 4;

        rowStart += dst.widthPx;
        ++rowIdx;
    }
    return height;
}

// gilde.exe 0x43768C — VIBE_Render_Convert8To16Indexed.
//   For each channel mask: count trailing zero bits (field position `pos`) and the
//   number of set bits (`bits`); the precision drop is (8 - bits). Then per source
//   index `idx`, palette entry = {R@+0, G@+1, B@+2}; the packed pixel is
//     (G>>(8-gBits)<<gPos) | (R>>(8-rBits)<<rPos) | (B>>(8-bBits)<<bPos)
//   exactly mirroring the original's expression (note: green is computed first/
//   leftmost in the OR, matching the decompiled order).
u32 Convert8To16Indexed(const ConvertSurf16& dst, const ConvertSurf8& src,
                        const u8* palette) {
    // Derive field positions (i,k,n) and set-bit counts (j,m,ii) per mask.
    auto field = [](u32 mask, int& pos, int& bits) {
        pos = 0;
        u32 v = mask;
        for (; (v & 1) == 0 && v != 0; ++pos) v >>= 1;   // trailing zeros
        bits = 0;
        for (; v; ++bits) v >>= 1;                        // set bits (mask is contiguous)
    };
    int rPos, rBits, gPos, gBits, bPos, bBits;
    field(dst.rMask, rPos, rBits);   // i / j   (a1[22])
    field(dst.gMask, gPos, gBits);   // k / m   (a1[23])
    field(dst.bMask, bPos, bBits);   // n / ii  (a1[24])

    const int rDrop = 8 - rBits;     // v19
    const int gDrop = 8 - gBits;     // v16
    const int bDrop = 8 - bBits;     // v17

    u32 result = 0;
    for (int row = 0; row < src.height; ++row) {          // v15 < a2[2]
        u16* d = dst.pixels + (dst.widthPx * row + dst.base);   // v10 + a1[9]
        const u8* s = src.pixels + (src.widthPx * row);   // a2[9] + a2[4]*v15
        for (int col = 0; col < src.width; ++col) {       // v11 < a2[3]
            const u8 idx = s[col];
            const u8 R = palette[4 * idx + 0];
            const u8 G = palette[4 * idx + 1];
            const u8 B = palette[4 * idx + 2];
            result = (static_cast<u32>(G >> gDrop) << gPos)
                   | (static_cast<u32>(R >> rDrop) << rPos)
                   | (static_cast<u32>(B >> bDrop) << bPos);
            d[col] = static_cast<u16>(result);
        }
    }
    return result;
}

} // namespace guild::render
