#include "render/bmp.h"
#include "render/types.h"
#include <cstring>
#include <cstdlib>

namespace guild::render {

namespace {

// HARDENING (wave-11) bounds for the malformed-input guards in BmpLoadBuffer.
constexpr int       kI32Min       = (-2147483647 - 1);  // INT_MIN (abs is UB)
constexpr long long kMaxBmpPixels = 1LL << 28;          // 256M px ceiling (sane)

inline u16 rd16(const u8* p) { return (u16)(p[0] | (p[1] << 8)); }
inline u32 rd32(const u8* p) { return (u32)(p[0] | (p[1] << 8) | (p[2] << 16) | (p[3] << 24)); }
inline void wr16(std::vector<u8>& v, u16 x) { v.push_back((u8)x); v.push_back((u8)(x >> 8)); }
inline void wr32(std::vector<u8>& v, u32 x) {
    v.push_back((u8)x); v.push_back((u8)(x >> 8)); v.push_back((u8)(x >> 16)); v.push_back((u8)(x >> 24));
}

} // namespace

// gilde.exe 0x5f0c10 — VIBE_Bmp_ReadHeaderInfo.
// Original seeks to +0xE and reads the 40-byte BITMAPINFOHEADER. Validates
// planes(==1), bpp(8 or 24) and compression(<=1); returns abs(height).
BmpInfo BmpReadHeaderInfo(const std::vector<u8>& file) {
    BmpInfo info;
    if (file.size() < 0xE + 40)
        return info;
    const u8* h = file.data() + 0xE;       // BITMAPINFOHEADER
    i32 width   = (i32)rd32(h + 4);
    i32 height  = (i32)rd32(h + 8);
    u16 planes  = rd16(h + 12);
    u16 bpp     = rd16(h + 14);
    u32 comp    = rd32(h + 16);
    if (planes != 1)
        return info;
    if (bpp != 8 && bpp != 24)
        return info;
    if (comp > 1)
        return info;
    info.width    = width;
    info.height   = std::abs(height);
    info.bitCount = bpp;
    info.ok       = true;
    return info;
}

// gilde.exe 0x5f1664 — VIBE_Bmp_SaveIndexed (8-bit paletted).
// File: 14B file header (BM, off=1078, size=w*h+1078) + 40B info (bpp=8,
// comp=0, planes=1) + 1024B palette + bottom-up rows of `width` bytes each.
//
// Palette byte layout (HARDEN fix): the decompile's `v26` alias is `v27 - 2`
// and `v17 += 4` executes BEFORE the v26 writes, so entry i is the STANDARD
// 4-byte quad at pal[4i]:
//   pal[4i]   = a5[3i+2] (else i)      v27[v17]
//   pal[4i+1] = a5[3i+1] (else i)      v27[v17+1]
//   pal[4i+2] = a5[3i+0] (else i)      v26[v17+4]   == v27[v17+2]
//   pal[4i+3] = 0                      v26[v17+4+1] == v27[v17+3]
// (The previous transcription put B two bytes BACK — a misread of the alias.)
std::vector<u8> BmpSaveIndexed(int width, int height, const u8* pixels, const u8* palette) {
    std::vector<u8> out;

    // file header (14 bytes)
    wr16(out, 19778);                          // 'BM'
    wr32(out, (u32)(width * height + 1078));    // fileSize
    wr16(out, 0);                               // reserved1
    wr16(out, 0);                               // reserved2
    wr32(out, 1078);                            // dataOffset

    // info header (40 bytes)
    wr32(out, 40);            // size
    wr32(out, (u32)width);    // width
    wr32(out, (u32)height);   // height
    wr16(out, 1);             // planes
    wr16(out, 8);             // bpp
    wr32(out, 0);             // compression (BI_RGB)
    wr32(out, 0);             // imageSize
    wr32(out, 1);             // xPelsPerMeter (v31=1)
    wr32(out, 1);             // yPelsPerMeter (v32=1)
    wr32(out, 0);             // clrUsed
    wr32(out, 0);             // clrImportant

    // palette (1024 bytes) — standard 4-byte entries (see banner: the v26 alias
    // resolves to pal[4i+2]/pal[4i+3]).
    u8 pal[1024];
    std::memset(pal, 0, sizeof(pal));
    int v17 = 0;
    const u8* src = palette;
    for (int i = 0; i < 256; ++i) {
        pal[v17]     = palette ? src[2] : (u8)i;   // v27[v17]
        pal[v17 + 1] = palette ? src[1] : (u8)i;   // v27[v17+1]
        pal[v17 + 2] = palette ? src[0] : (u8)i;   // v26[v17+4]
        pal[v17 + 3] = 0;                          // v26[v17+5]
        v17 += 4;
        if (palette) src += 3;
    }
    out.insert(out.end(), pal, pal + 1024);

    // pixel rows, bottom-up; `width` bytes per row (the original writes width
    // bytes with no DWORD padding — v23 stride is `a2`/width).
    for (int row = height - 1; row >= 0; --row) {
        const u8* p = pixels + (size_t)width * row;
        out.insert(out.end(), p, p + width);
    }
    return out;
}

// gilde.exe 0x5f18f4 — VIBE_Bmp_Save24Bit.
// File: 14B file header + a 44-BYTE info block (the 40-byte BITMAPINFOHEADER
// plus 4 zero pad bytes — `VIBE_Vfs_WriteStream(v29, 44)`), so the pixel data
// starts at 58 and dataOffset = 58 (v45 = 58; fileSize = 3*w*h + 58). Each
// pixel is emitted as B,G,R from the source R,G,B triple (src[2]/src[0] swap).
// Rows are width*3 bytes with no padding (the temp-row is exactly 3*width).
std::vector<u8> BmpSave24Bit(int width, int height, const u8* pixels) {
    std::vector<u8> out;

    wr16(out, 19778);                                 // 'BM'
    wr32(out, (u32)(3 * width * height + 58));         // fileSize (v42)
    wr16(out, 0);
    wr16(out, 0);
    wr32(out, 58);                                     // dataOffset (v45 = 58)

    wr32(out, 40);            // size
    wr32(out, (u32)width);    // width
    wr32(out, (u32)height);   // height
    wr16(out, 1);             // planes
    wr16(out, 24);            // bpp
    wr32(out, 0);             // compression
    wr32(out, 0);             // imageSize
    wr32(out, 1);             // xPelsPerMeter (v34=1)
    wr32(out, 1);             // yPelsPerMeter (v35=1)
    wr32(out, 0);             // clrUsed
    wr32(out, 0);             // clrImportant
    wr32(out, 0);             // 4 zero pad bytes (the 44-byte info write)

    std::vector<u8> rowbuf((size_t)3 * width);
    for (int row = height - 1; row >= 0; --row) {
        const u8* p = pixels + (size_t)3 * width * row;
        for (int x = 0; x < width; ++x) {
            rowbuf[3 * x + 0] = p[3 * x + 2]; // B
            rowbuf[3 * x + 1] = p[3 * x + 1]; // G
            rowbuf[3 * x + 2] = p[3 * x + 0]; // R
        }
        out.insert(out.end(), rowbuf.begin(), rowbuf.end());
    }
    return out;
}

// gilde.exe 0x5f0ce4 — VIBE_Bmp_LoadBuffer (focused: BI_RGB/BI_RLE8 8-bit and
// 24-bit sources; output 8-bit indices or 24-bit RGB). The original's many
// memset/qmemcpy "alignment" idioms and the row-flip swap are collapsed to plain
// loops (behaviour-identical). Flag handling reduced to the two output modes the
// callers in this module use (load-data + optional palette/quantize).
std::vector<u8> BmpLoadBuffer(const std::vector<u8>& file, int wantBpp,
                              int& outWidth, int& outHeight, u8* outPalette) {
    outWidth = outHeight = 0;
    if (file.size() < 0xE + 40)
        return {};
    const u8* h = file.data() + 0xE;
    i32 width   = (i32)rd32(h + 4);
    i32 height  = (i32)rd32(h + 8);    // may be signed: >0 means bottom-up on disk
    u16 planes  = rd16(h + 12);
    u16 bpp     = rd16(h + 14);
    u32 comp    = rd32(h + 16);
    u32 clrUsed = rd32(h + 32);

    if (planes != 1)
        return {};
    bool is24 = (bpp == 24 && comp == 0);
    if (!is24 && (bpp != 8 || comp > 1))
        return {};

    // HARDENING (wave-11): reject degenerate dimensions BEFORE sizing/indexing
    // the decode buffer. The on-disk width/height are untrusted i32s; a negative
    // or absurd value would otherwise (a) make std::abs(INT_MIN) UB, and
    // (b) turn `(size_t)srcStride * absH` into a wild allocation size / OOB index.
    // A real BMP has positive dims; a malformed one fails safe (empty result),
    // which is the original's behavior too (it could not decode such a file).
    if (width <= 0 || height == 0 || height == kI32Min)
        return {};
    int absH = (height < 0) ? -height : height;
    // Cap the pixel count so srcStride*absH cannot overflow the 32-bit math the
    // original used (and matches what any real surface allocation could hold).
    if ((long long)width * absH > kMaxBmpPixels)
        return {};
    outWidth = width;
    outHeight = absH;

    // Read palette (BGRA at 0x36) for 8-bit sources -> RGB triples.
    u8 srcPal[256 * 3];
    std::memset(srcPal, 0, sizeof(srcPal));
    if (!is24) {
        int npal = (int)clrUsed;
        if (npal == 0) npal = 256;
        // HARDENING: srcPal only holds 256 entries; a malformed biClrUsed (huge
        // or negative) must not drive the unpack loop past it. Clamp to [0,256].
        if (npal < 0 || npal > 256) npal = 256;
        if (file.size() < 0x36 + (size_t)4 * npal)
            return {};
        const u8* pp = file.data() + 0x36;
        for (int i = 0; i < npal; ++i) {
            srcPal[3 * i + 0] = pp[4 * i + 2]; // R = src[2]
            srcPal[3 * i + 1] = pp[4 * i + 1]; // G
            srcPal[3 * i + 2] = pp[4 * i + 0]; // B = src[0]
        }
        if (outPalette)
            std::memcpy(outPalette, srcPal, sizeof(srcPal));
    }

    // Decode pixel data into a top-down index/RGB buffer (`buf`), one entry per
    // pixel for 8-bit, three bytes for 24-bit. On-disk rows are bottom-up when
    // height>0, so we place row r at output row (absH-1-r).
    int srcStride = is24 ? 3 * width : width;
    std::vector<u8> raw((size_t)srcStride * absH, 0);

    if (is24) {
        // 24-bit pixel data follows the 54-byte header. On disk B,G,R; convert
        // to R,G,B as the original does (swap first/third of each triple).
        size_t off = 0x36;
        for (int r = 0; r < absH; ++r) {
            if (off + srcStride > file.size())
                return {};
            for (int x = 0; x < width; ++x) {
                u8 b = file[off + 3 * x + 0];
                u8 g = file[off + 3 * x + 1];
                u8 rr = file[off + 3 * x + 2];
                raw[(size_t)srcStride * r + 3 * x + 0] = rr;
                raw[(size_t)srcStride * r + 3 * x + 1] = g;
                raw[(size_t)srcStride * r + 3 * x + 2] = b;
            }
            off += srcStride;
        }
    } else if (comp == 1) {
        // BI_RLE8 decode (original at LABEL_57). Data starts at 0x36 + 4*npal.
        int npal = (int)clrUsed; if (npal == 0) npal = 256;
        size_t off = 0x36 + (size_t)4 * npal;
        int yy = 0, xx = 0;
        while (yy < absH && off + 1 < file.size()) {
            u8 count = file[off++];
            u8 val   = file[off++];
            if (count) { // encoded run
                for (int i = 0; i < count && xx < width; ++i)
                    raw[(size_t)srcStride * yy + xx++] = val;
            } else {
                if (val == 0) { ++yy; xx = 0; }        // end of line
                else if (val == 1) { yy = absH; }       // end of bitmap
                else if (val == 2) {                    // delta
                    if (off + 1 < file.size()) { xx += file[off]; yy += file[off + 1]; off += 2; }
                } else {                                // absolute run
                    for (int i = 0; i < val; ++i) {
                        if (off < file.size() && xx < width)
                            raw[(size_t)srcStride * yy + xx++] = file[off];
                        ++off;
                    }
                    if (val & 1) ++off;                 // pad to word boundary
                }
            }
        }
    } else {
        // BI_RGB 8-bit. The original reads width*height bytes in one shot with
        // NO per-row DWORD padding (matching VIBE_Bmp_SaveIndexed, which writes
        // exactly `width` bytes per row). Reproduce the unpadded layout.
        size_t off = 0x36 + (size_t)4 * ((clrUsed == 0) ? 256 : clrUsed);
        if (off + (size_t)width * absH > file.size())
            return {};
        std::memcpy(raw.data(), file.data() + off, (size_t)width * absH);
    }

    // Flip to top-down when source rows were bottom-up (height>0).
    std::vector<u8> top((size_t)srcStride * absH);
    if (height > 0) {
        for (int r = 0; r < absH; ++r)
            std::memcpy(&top[(size_t)srcStride * (absH - 1 - r)], &raw[(size_t)srcStride * r], srcStride);
    } else {
        top = raw;
    }

    // Produce the requested output format.
    if (wantBpp == 24) {
        if (is24)
            return top;
        // expand 8-bit indices through the palette -> RGB
        std::vector<u8> rgb((size_t)3 * width * absH);
        for (int i = 0; i < width * absH; ++i) {
            u8 idx = top[i];
            rgb[3 * i + 0] = srcPal[3 * idx + 0];
            rgb[3 * i + 1] = srcPal[3 * idx + 1];
            rgb[3 * i + 2] = srcPal[3 * idx + 2];
        }
        return rgb;
    }
    // wantBpp == 8: only meaningful for 8-bit sources here.
    if (is24)
        return {};
    return top;
}

} // namespace guild::render
