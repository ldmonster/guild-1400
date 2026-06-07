#include "render/picture_io.h"
#include <cstring>
#include <cstddef>

// =============================================================================
// guild::render — Picture image I/O (implementation). All pixel arithmetic is
// transcribed from the Hex-Rays pseudocode of the listed gilde.exe functions.
// The 18-byte on-disk header is a TGA image header:
//   +0  idLength            +1  colorMapType        +2  imageType
//   +3..+7  colorMapSpec    +8..+9 xOrigin          +10..+11 yOrigin
//   +12..+13 width          +14..+15 height         +16 bpp   +17 descriptor
// The framebuffer pixel format is RGB555 little-endian (see picture_io.h).
// =============================================================================
namespace guild::render {

namespace {

inline u16 rd16(const u8* p) { return (u16)(p[0] | (p[1] << 8)); }
inline void wr16(std::vector<u8>& v, u16 x) { v.push_back((u8)x); v.push_back((u8)(x >> 8)); }
inline void wr32(std::vector<u8>& v, u32 x) {
    v.push_back((u8)x); v.push_back((u8)(x >> 8)); v.push_back((u8)(x >> 16)); v.push_back((u8)(x >> 24));
}

// Pack an 8-bit-per-channel RGB triple to RGB555, exactly as the originals do:
//   ((b>>3)&0x1F) | (((g>>3)&0x1F)<<5) | (((r>>3)&0x1F)<<10)
inline u16 Pack555(u8 r, u8 g, u8 b) {
    return (u16)((((int)b >> 3) & 0x1F)
               | (32 * (((int)g >> 3) & 0x1F))
               | ((((int)r >> 3) & 0x1F) << 10));
}

} // namespace

// gilde.exe 0x421B58 — VIBE_Picture_SwapRowBytes.
// Original: v2 = 2*a2-2; walk byte pairs swapping [k] and [k+1] in place. The
// loop bound is 2*pixelCount-2, so it swaps every full u16 (the last byte pair).
void PictureSwapRowBytes(u16* pixels, int pixelCount) {
    u8* p = (u8*)pixels;
    int limit = 2 * pixelCount - 2;
    for (int i = 0; i < limit; i += 2) {
        u8 t = p[i];
        p[i]     = p[i + 1];
        p[i + 1] = t;
    }
}

// gilde.exe 0x421B88 — VIBE_Picture_LoadTga.
bool PictureLoadTga(const PictureFile& file, u16* fb, int fbWidth,
                    int& outW, int& outH) {
    outW = outH = 0;
    if (file.size() < 18)
        return false;
    const u8* h = file.data();
    int width  = rd16(h + 12);
    int height = rd16(h + 14);
    u8  bpp    = h[16];                 // v11
    u8  descr  = h[17];                 // v12
    outW = width; outH = height;

    // 16bpp TGA pixel rows follow the header (no colour map / id). Each row is
    // `width` u16s copied verbatim into the framebuffer.
    size_t off = 18;
    for (int i = 0; i < height; ++i) {
        int dstRow = (descr == 32) ? i : (height - i - 1);
        const u8* srcRow = h + off + (size_t)2 * width * i;
        if (srcRow + 2 * width > h + file.size())
            break;
        std::memcpy(fb + (size_t)dstRow * fbWidth, srcRow, (size_t)2 * width);
    }
    (void)bpp;
    return true;
}

// gilde.exe 0x421CA4 — VIBE_Picture_SaveTga.
PictureFile PictureSaveTga(const u16* fb, int fbWidth, int w, int h) {
    PictureFile out;
    // 18-byte TGA header (the original hard-codes these fields):
    u8 hdr[18];
    std::memset(hdr, 0, sizeof(hdr));
    hdr[2]  = 2;                  // byte_75BFE2 imageType = 2 (uncompressed RGB)
    hdr[12] = (u8)(w & 0xFF);     // width lo  (HIWORD(dword_75BFEA)=320 in orig)
    hdr[13] = (u8)((w >> 8) & 0xFF);
    hdr[14] = (u8)(h & 0xFF);     // height lo (word_75BFEE=240 in orig)
    hdr[15] = (u8)((h >> 8) & 0xFF);
    hdr[16] = 16;                 // byte_75BFF0 bpp = 16
    hdr[17] = 32;                 // byte_75BFF1 descriptor = 32 (top-down)
    out.insert(out.end(), hdr, hdr + 18);

    // The original swaps the framebuffer byte order in place via SwapRowBytes
    // over fbWidth*h pixels (which leaves the VERY LAST u16 unswapped — the
    // 2*count-2 off-by-one), writes the rows, then swaps back. We reproduce that
    // exactly on a copy of the written region so the byte stream is identical,
    // including the unswapped final pixel.
    size_t total = (size_t)fbWidth * h;
    std::vector<u16> tmp(fb, fb + total);
    PictureSwapRowBytes(tmp.data(), (int)total);   // swaps all but the last u16
    for (int row = 0; row < h; ++row) {
        const u8* src = (const u8*)(tmp.data() + (size_t)row * fbWidth);
        for (int x = 0; x < w; ++x) {
            out.push_back(src[2 * x + 0]);
            out.push_back(src[2 * x + 1]);
        }
    }
    return out;
}

// gilde.exe 0x421DC4 — VIBE_Picture_LoadBmp24.
bool PictureLoadBmp24(const PictureFile& file, u16* fb, int fbWidth,
                      int& outW, int& outH) {
    outW = outH = 0;
    if (file.size() < 18)
        return false;
    const u8* h = file.data();
    int width  = rd16(h + 12);
    int height = rd16(h + 14);
    u8  bpp    = h[16];           // byte_75CC82
    u8  descr  = h[17];           // byte_75CC83
    outW = width; outH = height;
    if (bpp == 16)
        return PictureLoadTga(file, fb, fbWidth, outW, outH);

    // Rows of `width` BGR triples follow the header. Packed to RGB555.
    size_t off = 18;
    for (int i = 0; i < height; ++i) {
        const u8* srcRow = h + off + (size_t)3 * width * i;
        if (srcRow + 3 * width > h + file.size())
            break;
        int dstRow = (descr == 32) ? i : (height - i - 1);
        u16* d = fb + (size_t)dstRow * fbWidth;
        for (int x = 0; x < width; ++x) {
            u8 b = srcRow[3 * x + 0];   // byte_75C072
            u8 g = srcRow[3 * x + 1];   // byte_75C073
            u8 r = srcRow[3 * x + 2];   // byte_75C074
            d[x] = Pack555(r, g, b);
        }
    }
    return true;
}

// gilde.exe 0x421F2C — VIBE_Picture_LoadBmp32. Off-by-one row count preserved:
// the original loops `i < (height>>16) - 1`, stopping one row short.
bool PictureLoadBmp32(const PictureFile& file, u16* fb, int fbWidth,
                      int& outW, int& outH) {
    outW = outH = 0;
    if (file.size() < 18)
        return false;
    const u8* h = file.data();
    int width  = rd16(h + 12);
    int height = rd16(h + 14);
    u8  bpp    = h[16];           // byte_75DD14
    u8  descr  = h[17];           // byte_75DD15
    outW = width; outH = height;
    if (bpp == 16)
        return PictureLoadTga(file, fb, fbWidth, outW, outH);

    size_t off = 18;
    for (int i = 0; i < height - 1; ++i) {   // faithful: `< height - 1`
        const u8* srcRow = h + off + (size_t)4 * width * i;
        if (srcRow + 4 * width > h + file.size())
            break;
        int dstRow = (descr == 32) ? i : (height - i - 1);
        u16* d = fb + (size_t)dstRow * fbWidth;
        for (int x = 0; x < width; ++x) {
            u8 b = srcRow[4 * x + 0];
            u8 g = srcRow[4 * x + 1];
            u8 r = srcRow[4 * x + 2];
            d[x] = Pack555(r, g, b);
        }
    }
    return true;
}

// gilde.exe 0x422094 — VIBE_Picture_BlitRegion.
void PictureBlitRegion(const u16* srcFb, int srcX, int srcY, int w, int h,
                       u16* dstFb, int dstX, int dstY, int fbWidth) {
    if (w < 1 || h < 1)
        return;
    for (int row = 0; row < h; ++row) {
        const u16* s = srcFb + (size_t)(srcY + row) * fbWidth + srcX;
        u16* d       = dstFb + (size_t)(dstY + row) * fbWidth + dstX;
        std::memcpy(d, s, (size_t)2 * w);    // qmemcpy(.., 2*w)
    }
}

// gilde.exe 0x42210C — VIBE_Picture_FillRows.
void PictureFillRows(u16* fb, int x, int y, int w, int rows, int fbWidth) {
    if (rows <= 0)
        return;
    for (int row = y; row < y + rows; ++row)
        std::memset(fb + (size_t)row * fbWidth + x, 0, (size_t)2 * w);
}

// gilde.exe 0x422148 — VIBE_Picture_DrawBorder. White = 0x00FF (the original
// writes the literal 255). Vertical edges first (left at x, right at x+w),
// each clipped to fbHeight; then the top (row y) and bottom (row y+h) edges.
void PictureDrawBorder(u16* fb, int x, int y, int w, int h,
                       int fbWidth, int fbHeight) {
    if (w < 1 || h < 1)
        return;
    // left edge: rows [y, y+h), clipped to fbHeight (the `if(v9>=v6) break`).
    for (int row = y; row < y + h; ++row) {
        if (row >= fbHeight) break;
        fb[(size_t)row * fbWidth + x] = 255;
    }
    // right edge: x + w column.
    for (int row = y; row < y + h; ++row) {
        if (row >= fbHeight) break;
        fb[(size_t)row * fbWidth + (x + w)] = 255;
    }
    // top edge: row y, w pixels (only if y < fbHeight).
    if (fbHeight > y) {
        u16* d = fb + (size_t)y * fbWidth + x;
        for (int i = 0; i < w; ++i) d[i] = 255;
    }
    // bottom edge: row y+h, w pixels (only if y+h < fbHeight).
    if (y + h < fbHeight) {
        u16* d = fb + (size_t)(y + h) * fbWidth + x;
        for (int i = 0; i < w; ++i) d[i] = 255;
    }
}

// gilde.exe 0x422B58 — VIBE_Picture_SaveBmp24.
PictureFile PictureSaveBmp24(int width, int height, const u8* rgb) {
    PictureFile out;
    // 14-byte file header (v28..): "BM", fileSize=3*w*h+58, dataOffset=58.
    wr16(out, 19778);                              // 'BM'
    wr32(out, (u32)(3 * width * height + 58));      // v29
    wr16(out, 0);                                   // v30
    wr16(out, 0);                                   // v31
    wr32(out, 58);                                  // v32 dataOffset
    // 40-byte info header (v16..). height stored negative (top-down).
    wr32(out, 40);                                  // v16[0]
    wr32(out, (u32)width);                          // v16[1]
    wr32(out, (u32)(-height));                      // v16[2] = -a3
    wr16(out, 1);                                   // v17 planes
    wr16(out, 24);                                  // v18 bpp
    wr32(out, 0);                                   // v19 compression
    wr32(out, 0);                                   // v20 imageSize
    wr32(out, 1);                                   // v21 xPelsPerMeter
    wr32(out, 1);                                   // v22 yPelsPerMeter
    wr32(out, 0);                                   // v23 clrUsed
    wr32(out, 0);                                   // v24 clrImportant
    // The original's info block is 0x2C=44 bytes (40 header + a trailing 4-byte
    // gap), which is why dataOffset=58 (14+44). Emit the gap so pixels start at 58.
    wr32(out, 0);

    // Rows top-down (height stored negative); each pixel emitted B,G,R (the
    // original swaps src[0]<->src[2] of every triple in the temp row).
    std::vector<u8> rowbuf((size_t)3 * width);
    for (int row = 0; row < height; ++row) {
        const u8* p = rgb + (size_t)3 * width * row;
        for (int x = 0; x < width; ++x) {
            rowbuf[3 * x + 0] = p[3 * x + 2];   // B
            rowbuf[3 * x + 1] = p[3 * x + 1];   // G
            rowbuf[3 * x + 2] = p[3 * x + 0];   // R
        }
        out.insert(out.end(), rowbuf.begin(), rowbuf.end());
    }
    return out;
}

} // namespace guild::render
