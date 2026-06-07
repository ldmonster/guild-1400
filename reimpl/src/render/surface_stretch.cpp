#include "render/surface_stretch.h"

#include "render/shape_blit.h"   // Convert8To16Indexed / Convert24To16 descriptors

#include <cstdint>
#include <cstring>

namespace guild::render {

namespace {

// Field position of a contiguous channel mask = number of trailing zero bits.
// Mirrors the original's `for (pos=0; (m&1)==0; ++pos) m>>=1;` loops verbatim.
inline int MaskPos(u32 mask) {
    int pos = 0;
    for (; (mask & 1) == 0 && mask != 0; ++pos) mask >>= 1;
    return pos;
}
// Set-bit count of the (already low-shifted) remainder; the originals run a second
// loop `while (m) { ++bits; m >>= 1; }` after the trailing-zero pass.
inline int MaskBits(u32 mask) {
    for (; (mask & 1) == 0 && mask != 0; ) mask >>= 1;  // skip trailing zeros
    int bits = 0;
    for (; mask; ++bits) mask >>= 1;
    return bits;
}

} // namespace

// gilde.exe 0x435D88 — VIBE_Render_StretchSurface8
//   v2=dst(a1), a2=src. DST-driven: for each dst row i in [0,dst.height): src row
//   = src.height*i/dst.height; for each dst col v5 in [0,dst.width): src col =
//   v5*src.width/dst.width. Byte copy.
u8 StretchSurface8(const StretchSurfaceDesc& dst, const StretchSurfaceDesc& src) {
    u8 last = 0;
    for (u32 i = 0; i < static_cast<u32>(dst.height); ++i) {            // i < v2[2]
        u8* d = dst.pixels + dst.pitch * i;                            // v2[9] + v2[4]*i
        // src.pitch * (src.height*i/dst.height) + src.base
        const u8* srcRow = src.pixels
            + src.pitch * (static_cast<u32>(src.height) * i / static_cast<u32>(dst.height));
        for (u32 c = 0; c < static_cast<u32>(dst.width); ++c) {        // v5 < v2[3]
            last = srcRow[c * static_cast<u32>(src.width) / static_cast<u32>(dst.width)];
            *d++ = last;                                               // *v3 = al
        }
    }
    return last;
}

// gilde.exe 0x436488 — VIBE_Render_StretchSurface8Up
//   v2=src(a1), a2=dst. SRC-driven: for each src row i in [0,dst? ] ... NOTE the
//   original loops `i < a2[2]` (dst.height) and reads `v2[...]` (src). dst row dst
//   base = a2[9] + a2[4]*i; src row = v2[4]*(v2[2]*i/a2[2]) + v2[9]; for each col j
//   in [0,dst.width): dst col = j*v2[3]/a2[3]; write src byte sequentially.
u8 StretchSurface8Up(const StretchSurfaceDesc& src, const StretchSurfaceDesc& dst) {
    u8 last = 0;
    for (u32 i = 0; i < static_cast<u32>(dst.height); ++i) {           // i < a2[2]
        u8* dRow = dst.pixels + dst.pitch * i;                         // a2[9] + a2[4]*i
        const u8* s = src.pixels
            + src.pitch * (static_cast<u32>(src.height) * i / static_cast<u32>(dst.height)); // v2[4]*(v2[2]*i/a2[2]) + v2[9]
        for (u32 j = 0; j < static_cast<u32>(dst.width); ++j) {        // v5 < a2[3]
            last = *s;                                                 // al = *v3
            dRow[j * static_cast<u32>(src.width) / static_cast<u32>(dst.width)] = *s; // scatter
            ++s;
        }
    }
    return last;
}

// gilde.exe 0x435E00 — VIBE_Render_StretchAverage16
//   Box-average down-sample. i/j/k = trailing-zero positions of src R/G/B masks.
//   For each dst pixel, average all src pixels in its [rowSpan)x[colSpan) footprint.
u16* StretchAverage16(const StretchSurfaceDesc& dst, const StretchSurfaceDesc& src) {
    const int i = MaskPos(src.rMask);   // a2[22]
    const int j = MaskPos(src.gMask);   // a2[23]
    const int k = MaskPos(src.bMask);   // a2[24]

    u16* result = reinterpret_cast<u16*>(dst.pixels);
    u32 rowEnd = 0;                                          // v13
    for (u32 v14 = 0; v14 < static_cast<u32>(dst.height); ++v14) {
        const u32 rowStart = rowEnd;                         // v15
        rowEnd = static_cast<u32>(src.height) * (v14 + 1) / static_cast<u32>(dst.height);
        const u32 rowSpan = rowEnd - rowStart;               // v16
        u16* dpix = reinterpret_cast<u16*>(dst.pixels + dst.pitch * v14); // v19
        u32 colEnd = 0;                                      // v18
        for (u32 v20 = 0; v20 < static_cast<u32>(dst.width); ++v20) {
            const u32 colStart = colEnd;                     // v21 / v7
            colEnd = static_cast<u32>(src.width) * (v20 + 1) / static_cast<u32>(dst.width);
            const u32 colSpan = colEnd - colStart;           // v23

            u32 accR = 0, accG = 0, accB = 0;                // v9 / v30 / v8
            u32 count = 0;                                   // v29
            if (rowSpan) {
                const u8* base = src.pixels + src.pitch * rowStart; // v25 = a2[4]*v15 + a2[9]
                for (u32 r = 0; r < rowSpan; ++r) {
                    if (colSpan) {
                        const u16* sp = reinterpret_cast<const u16*>(
                            base + 2 * colStart);            // 2*v21 + v25
                        const u16* spEnd = sp + colSpan;
                        for (; sp < spEnd; ++sp) {
                            const u16 px = *sp;
                            accR += (static_cast<u32>(px) & src.rMask) >> i;
                            accG += (static_cast<u32>(px) & src.gMask) >> j;
                            accB += (static_cast<u32>(px) & src.bMask) >> k;
                            ++count;
                        }
                    }
                    base += src.pitch;                       // v25 += v22
                }
            }
            result = dpix;
            *dpix++ = static_cast<u16>(((accG / count) << j)
                                     | ((accR / count) << i)
                                     | ((accB / count) << k));
        }
    }
    return result;
}

// gilde.exe 0x437814 — VIBE_Render_Convert24To16
//   24bpp source bytes are (B,G,R) in memory order: *result=B, +1=G, +2=R. Dest
//   field positions/precisions derived from the DEST masks.
u32 Convert24To16(const StretchSurfaceDesc& dst, const StretchSurfaceDesc& src) {
    const int rPos  = MaskPos(dst.rMask), rBits = MaskBits(dst.rMask);  // i / j
    const int gPos  = MaskPos(dst.gMask), gBits = MaskBits(dst.gMask);  // k / m
    const int bPos  = MaskPos(dst.bMask), bBits = MaskBits(dst.bMask);  // n / ii

    const int rDrop = 8 - rBits;          // (8 - j) — computed inline in original
    const int gDrop = 8 - gBits;          // v13
    const int bDrop = 8 - bBits;          // v14

    u32 result = 0;
    for (u32 row = 0; row < static_cast<u32>(src.height); ++row) {      // v11 < a2[2]
        u16* d = reinterpret_cast<u16*>(dst.pixels + dst.pitch * row);  // a1[4]*v11 + a1[9]
        const u8* s = src.pixels + src.pitch * row;                     // a2[4]*v11 + a2[9]
        for (u32 col = 0; col < static_cast<u32>(src.width); ++col) {   // v9 < a2[3]
            const u8 B = s[0];
            const u8 G = s[1];
            const u8 R = s[2];
            result = (static_cast<u32>(R >> rDrop) << rPos)
                   | (static_cast<u32>(G >> gDrop) << gPos)
                   | (static_cast<u32>(B >> bDrop) << bPos);
            *d++ = static_cast<u16>(result);
            s += 3;
        }
    }
    return result;
}

namespace {

// Per-row pitch-aware byte copy (the original's aligned qmemcpy decomposition,
// head + 4*words + tail, collapses to a plain memcpy of `rowBytes`).
inline void CopyRows(const StretchSurfaceDesc& dst, const StretchSurfaceDesc& src,
                     int rows, int rowBytes) {
    for (int r = 0; r < rows; ++r) {
        std::memcpy(dst.pixels + dst.pitch * r,
                    src.pixels + src.pitch * r,
                    static_cast<std::size_t>(rowBytes));
    }
}

// Map a raw surface descriptor onto the shape_blit ConvertSurf* views. The
// existing Convert8To16Indexed models the dest as pixel-stride/base; for a tight
// block at (0,0) the pixel stride is pitch/2 (16bpp) and base 0.
ConvertSurf8 AsSrc8(const StretchSurfaceDesc& s) {
    return ConvertSurf8{ s.pitch, s.width, s.height, s.pixels };
}
ConvertSurf16 AsDst16(const StretchSurfaceDesc& d) {
    ConvertSurf16 c;
    c.widthPx = d.pitch / 2;   // pixel stride
    c.base    = 0;
    c.rMask   = d.rMask;
    c.gMask   = d.gMask;
    c.bMask   = d.bMask;
    c.pixels  = reinterpret_cast<u16*>(d.pixels);
    return c;
}

} // namespace

// gilde.exe 0x437530 — VIBE_Render_StretchSurfaceDispatch
//   Depth match gate, then size-relative dispatch. Only 8bpp/16bpp paths are
//   reconstructed; 24/32bpp average/interpolate leaves are deferred.
u8 StretchSurfaceDispatch(StretchSurfaceDesc& dst, const StretchSurfaceDesc& src) {
    u8 status = 0;
    if (static_cast<u32>(src.bpp) != static_cast<u32>(dst.bpp))   // bpp != a1[21]
        return status;
    // Gate: indexed OR 16/24/32 bpp.
    if (!(src.indexed || src.bpp == 16 || src.bpp == 24 || src.bpp == 32))
        return status;

    if (static_cast<u32>(dst.width) < static_cast<u32>(src.width)) {
        // down-sample
        if (src.indexed)
            return StretchSurface8(dst, src);
        if (src.bpp == 16)
            return static_cast<u8>(reinterpret_cast<std::uintptr_t>(StretchAverage16(dst, src)));
        // 24/32 average leaves deferred
        return status;
    } else if (dst.width == src.width) {
        // straight per-row copy; rowBytes depends on bpp (indexed = width bytes)
        int rowBytes;
        if (src.indexed)            rowBytes = src.width;
        else if (src.bpp == 16)     rowBytes = 2 * dst.width;
        else if (src.bpp == 24)     rowBytes = 3 * dst.width;
        else /* 32 */               rowBytes = 4 * dst.width;
        CopyRows(dst, src, dst.width, rowBytes);  // loop bound is a1[3]=dst.width
        return static_cast<u8>(dst.width);
    } else {
        // up-sample
        if (src.indexed)
            return StretchSurface8Up(src, dst);
        // 16/24/32 interpolate leaves deferred
        return status;
    }
}

// gilde.exe 0x437980 — VIBE_Render_BlitConvertDispatch
//   Same-size convert/copy. `palette8` is needed only for the 8bpp->16bpp route.
u32 BlitConvertDispatch(StretchSurfaceDesc& dst, u32 status,
                        const StretchSurfaceDesc& src, const u8* palette8) {
    u32 result = status;
    if (src.width != dst.width || src.height != dst.height)   // a3[12]==a1[12] && a3[8]==a1[8]
        return result;

    if (src.bpp == dst.bpp || (src.indexed && dst.indexed)) {
        // per-row copy
        int rowBytes;
        if (src.indexed)            rowBytes = src.width;     // a3[12]
        else if (src.bpp == 16)     rowBytes = 2 * src.width;
        else if (src.bpp == 24)     rowBytes = 3 * src.width;
        else if (src.bpp == 32)     rowBytes = 4 * src.width;
        else                        return result;            // default: return
        for (int r = 0; r < src.height; ++r) {                // result < a3[8]
            std::memcpy(dst.pixels + dst.pitch * r,
                        src.pixels + src.pitch * r,
                        static_cast<std::size_t>(rowBytes));
        }
        return static_cast<u32>(src.height);
    } else if (src.indexed && dst.bpp == 16) {
        ConvertSurf16 d = AsDst16(dst);
        ConvertSurf8  s = AsSrc8(src);
        return Convert8To16Indexed(d, s, palette8);
    } else if (src.bpp == 24 && dst.bpp == 16) {
        return Convert24To16(dst, src);
    }
    return result;
}

// gilde.exe 0x56D6D4 — VIBE_Render_BlitThumbnailToSurface
//   Fixed 160x120 16bpp block, source read sequentially.
int BlitThumbnailToSurface(int x, int y, u16* surfPixels, int surfStridePx,
                           const u16* thumb) {
    int srcIdx = 0;                       // v6 / v9 (running word index)
    int row = y;                          // v13
    const int rowEnd = y + 120;           // v11 = v5 + 120
    do {
        int col = x;                      // v7 = a1
        do {
            u16* d = surfPixels + (col + surfStridePx * row); // surf+28 + 2*(v7 + surf[16]*v13)
            *d = thumb[srcIdx];           // *(word_13CED78 + v9)
            ++srcIdx;
            ++col;
        } while (col != x + 160);         // v7 != a1 + 160
        ++row;
    } while (row != rowEnd);
    return row - y;                       // 120
}

} // namespace guild::render
