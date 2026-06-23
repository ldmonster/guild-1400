#include "render/picture_recon_bmp.h"
#include <cstring>
#include <cstdlib>

// =============================================================================
// guild::render — Picture BMP I/O cluster (implementation).
// All header parsing, RLE span decode and channel/row maths are transcribed
// directly from the Hex-Rays pseudocode of gilde.exe 0x4222bc..0x422d98.
//
// VFS modelling. The original streamed bytes through the file shim. Here the
// file is a flat byte vector and a tiny cursor reproduces the exact semantics
// used by these functions:
//   Seek(off, 0)  -> absolute set                 (all calls use whence 0/SEEK_SET)
//   Seek(off, 1)  -> relative from current        (the row-pad seek, whence 1)
//   Read(buf,sz,n)-> copy sz*n bytes, advance cursor, return elements read.
// Reads past EOF copy what is available and zero nothing extra (the original's
// buffered reader returns fewer elements; we clamp the copy length identically).
// =============================================================================
namespace guild::render {

namespace {

// abs32 — the original's abs32() helper (two's-complement absolute of an i32).
inline int abs32(int v) { return v < 0 ? -v : v; }

inline i16 rd16(const u8* p) { return (i16)(p[0] | (p[1] << 8)); }
inline i32 rd32(const u8* p) {
    return (i32)((u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24));
}
inline void wr16(PictureBmpFile& v, u16 x) {
    v.push_back((u8)x); v.push_back((u8)(x >> 8));
}
inline void wr32(PictureBmpFile& v, u32 x) {
    v.push_back((u8)x); v.push_back((u8)(x >> 8));
    v.push_back((u8)(x >> 16)); v.push_back((u8)(x >> 24));
}

// Minimal in-memory stand-in for the VFS stream the original opened.
struct Cursor {
    const u8* base;
    size_t    size;
    size_t    pos;
    explicit Cursor(const PictureBmpFile& f) : base(f.data()), size(f.size()), pos(0) {}
    // VIBE_File_Seek(off, whence): whence 0 = SEEK_SET, 1 = SEEK_CUR.
    void seek(long off, int whence) {
        long np = (whence == 1) ? (long)pos + off : off;
        if (np < 0) np = 0;
        pos = (size_t)np;
    }
    // VIBE_File_Read(buf, elemSize, count) -> elements actually read.
    int read(void* buf, int elemSize, int count) {
        long want = (long)elemSize * (long)count;
        if (want <= 0) return 0;
        long avail = (pos <= size) ? (long)(size - pos) : 0;
        long got = want < avail ? want : avail;
        if (got > 0) std::memcpy(buf, base + pos, (size_t)got);
        pos += (size_t)got;
        return elemSize ? (int)(got / elemSize) : 0;
    }
};

} // namespace

// gilde.exe 0x4222bc — VIBE_Picture_ReadHeader.
i32 PictureReadHeader(const PictureBmpFile& file) {
    if (file.empty())
        return -1;                 // VIBE_File_OpenStream failed (!v2)
    Cursor c(file);
    c.seek(14, 0);                 // VIBE_File_Seek(14, 0)
    u8 hdr[40] = {0};
    c.read(hdr, 1, 40);            // VIBE_File_Read(.., 1, 40)
    // Stack fields v5..v9 map onto the 40-byte read at +0:
    i32 width   = rd32(hdr + 4);   // v5  (biWidth)
    i32 height  = rd32(hdr + 8);   // v6  (biHeight)
    i16 planes  = rd16(hdr + 12);  // v7  (biPlanes)
    i16 bitcnt  = rd16(hdr + 14);  // v8  (biBitCount)
    u32 comp    = (u32)rd32(hdr + 16); // v9 (biCompression)
    if (planes == 1 && bitcnt == 8 && comp < 2u)
        return (i32)((u32)width | ((u32)abs32(height) << 16)); // v5 | (abs32(v6)<<16)
    return -2;
}

// gilde.exe 0x4228a0 — VIBE_Picture_ReadBmpType.
i16 PictureReadBmpType(const PictureBmpFile& file) {
    if (file.empty())
        return -1;
    Cursor c(file);
    c.seek(14, 0);
    u8 buf[40] = {0};              // original: char v4[14]; i16 v5 right after
    c.read(buf, 1, 40);
    return rd16(buf + 14);         // v5 lands at read-offset 14 (biBitCount)
}

// gilde.exe 0x4228fc — VIBE_Picture_ReadBmpDimensions.
i32 PictureReadBmpDimensions(const PictureBmpFile& file) {
    if (file.empty())
        return -1;
    Cursor c(file);
    c.seek(14, 0);
    u8 hdr[40] = {0};
    c.read(hdr, 1, 40);
    i32 width  = rd32(hdr + 4);    // v5
    i32 height = rd32(hdr + 8);    // v6
    i16 planes = rd16(hdr + 12);   // v7
    i16 bitcnt = rd16(hdr + 14);   // v8
    i32 comp   = rd32(hdr + 16);   // v9
    if (planes == 1 && bitcnt == 24 && comp == 0)
        return (i32)((u32)width | ((u32)height << 16)); // v5 | (v6<<16) (no abs)
    return -2;
}

// gilde.exe 0x42234c — VIBE_Picture_LoadBmpPalette.
void PictureLoadBmpPalette(const PictureBmpFile& file, u8* pal) {
    // SetGrayColorThunk(0, 768, pal) == memset(pal, 0, 768). NOTE: only 768
    // bytes are cleared though entries are written with a 4-byte stride below
    // (256 entries -> up to 1024 bytes touched); this matches the original.
    std::memset(pal, 0, 768);
    if (file.empty())
        return;                    // !result (open failed)
    Cursor c(file);
    c.seek(14, 0);                 // VIBE_File_Seek(14, 0)
    // Original reads the 40-byte info header into stack v9[32]; biClrUsed
    // (read-offset 32) lands in the adjacent DWORD v10 (stack ebp-18). We read
    // into a full 40-byte buffer so offset 32 is well-defined.
    u8 info[40] = {0};
    c.read(info, 1, 40);           // VIBE_File_Read(.., 1, 40)
    u32 clrUsed = (u32)rd32(info + 32);   // v10 = biClrUsed
    if (clrUsed == 0) clrUsed = 256;      // if(!v10) v10 = 256;
    // HARDENING (wave-11): an 8bpp BMP palette is physically at most 256 entries,
    // and the caller's `pal` block holds exactly 256 (the unpack writes pal+4*i).
    // A malformed biClrUsed (huge/negative) would otherwise write past `pal` and
    // allocate a wild `quads` buffer. Clamp to 256 — valid files are unaffected.
    if (clrUsed > 256u) clrUsed = 256;
    // VIBE_File_Read(.., 4, v10): clrUsed BGRA quads at the current cursor.
    std::vector<u8> quads((size_t)4 * clrUsed, 0);
    c.read(quads.data(), 4, (int)clrUsed);
    // Unpack [B,G,R,_] -> pal[R,G,B] (3 bytes per entry); result += 4 per step.
    for (u32 i = 0; i < clrUsed; ++i) {
        u8* o = pal + 4 * i;
        o[0] = quads[4 * i + 2];          // *result   = v8[4*v7+2]  (R)
        o[1] = quads[4 * i + 1];          // result[1] = v8[4*v7+1]  (G)
        o[2] = quads[4 * i + 0];          // result[2] = v8[4*v7]    (B)
    }
}

// gilde.exe 0x422418 — VIBE_Picture_LoadBmpRle.
char PictureLoadBmpRle(const PictureBmpFile& file, u8* dst, int dstStride,
                       char flipVertical) {
    (void)dstStride;
    if (file.empty())
        return 0;                  // open failed
    Cursor c(file);
    c.seek(14, 0);                 // VIBE_File_Seek(14, 0)
    u8 hdr[40] = {0};
    c.read(hdr, 1, 40);            // 40-byte info header into v16[4]/v17/v18/...
    // Field offsets within the 40-byte read (stack v16@-44 .. v20@-24):
    int stride = rd32(hdr + 4);    // v17 = biWidth (used as the row stride/bytes)
    int height = rd32(hdr + 8);    // v18 = biHeight
    int comp   = rd32(hdr + 16);   // v19 = biCompression
    int clrUsed = rd32(hdr + 32);  // v20 = biClrUsed
    if (clrUsed == 0) clrUsed = 256;          // if(!v20) v20 = 256;
    c.seek(4 * clrUsed + 54, 0);              // VIBE_File_Seek(4*v20+54, 0)

    if (comp == 1) {               // BI_RLE8
        int col = 0;               // a4 (column offset within row)
        int row = 0;               // v8 (row index)
        while (row < abs32(height)) {           // while(v8 < abs32(v18))
            u8 pair[2] = {0,0};
            c.read(pair, 1, 2);                 // VIBE_File_Read(.., 1, 2)
            u8 count = pair[0];                 // v23
            u8 val   = pair[1];                 // v24
            if (count) {                        // if(v23)
                // run of `count` pixels = `val`
                std::memset(&dst[col + row * stride], val, count); // SetGrayColorThunk(v24,v23,..)
                col += count;                   // a4 += v23
            } else if (val) {                   // else if(v24)
                if (val > 1u) {                 // if(v24 > 1u)
                    if (val == 2) {             // delta
                        // VIBE_File_Read overwrites v23/v24 with dx,dy. The
                        // binary then does a4 += v23 (new dx) and v8 += v24
                        // (NEW dy, var_13 @0x422528 add ebp,eax), NOT +2.
                        u8 d[2] = {0,0};
                        c.read(d, 1, 2);        // VIBE_File_Read(.., 1, 2)
                        col += d[0];            // a4 += v23 (dx)
                        row += d[1];            // v8 += v24 (dy)  @0x422524/0x422528
                    } else {                    // absolute run of `val` literals
                        u8* p = &dst[col + row * stride];
                        if ((val & 1) == 1)     // (v24 & 1)==1 -> read val+1 padded
                            c.read(p, val + 1, 1);
                        else
                            c.read(p, val, 1);
                        col += val;             // a4 += v24
                    }
                } else {                        // v24 == 1 -> end of bitmap
                    row = height;               // v8 = v18
                }
            } else {                            // count==0 && val==0 -> end of line
                ++row;                          // ++v8
                col = 0;                        // a4 = 0
            }
        }
    } else {
        c.read(dst, height, stride);            // VIBE_File_Read(.., v18, v17)
    }

    // VIBE_Vfs_CloseAndFreeEntry — no-op in the in-memory model.

    if (flipVertical && height > 0) {           // if(v10 && v18 > 0)
        // Reverse the rows in place using a stride-sized scratch buffer.
        u8* tmp = (u8*)std::malloc((size_t)stride);  // VIBE_Memory_AllocDebug
        int top = 0;                            // v12
        int half = height / 2;                  // v21 = v18/2
        for (int bot = height - 1; top < half; --bot) {  // for(i=v18-1; v12<v21; --i)
            std::memcpy(tmp, &dst[top * stride], (size_t)stride);
            std::memcpy(&dst[top * stride], &dst[bot * stride], (size_t)stride);
            ++top;
            std::memcpy(&dst[bot * stride], tmp, (size_t)stride);
        }
        std::free(tmp);                         // VIBE_Memory_FreeDebug
    }
    return 1;
}

// gilde.exe 0x422980 — VIBE_Picture_LoadBmpUncompressed.
int PictureLoadBmpUncompressed(const PictureBmpFile& file, u8* dst) {
    if (file.empty())
        return 0;                  // open failed -> returns result(==0)
    Cursor c(file);
    c.seek(14, 0);                 // VIBE_File_Seek(14, 0)
    u8 hdr[40] = {0};
    c.read(hdr, 1, 40);
    int width  = rd32(hdr + 4);    // v15
    int height = rd32(hdr + 8);    // v16
    i16 planes = rd16(hdr + 12);   // v17
    i16 bitcnt = rd16(hdr + 14);   // v18
    int comp   = rd32(hdr + 16);   // v19
    int clrUsed = rd32(hdr + 32);  // v20
    c.seek(4 * clrUsed + 54, 0);   // VIBE_File_Seek(4*v20+54, 0)

    if (comp == 0 && planes == 1 && bitcnt == 24) {  // !v19 && v17==1 && v18==24
        bool topDown = height >= 0;                  // v21 = v16 >= 0
        height = abs32(height);                      // v16 = abs32(v16)
        int rowBytes = 3 * width;                    // v22 = 3*v15
        if (height > 0) {
            int pad = 4 - ((rowBytes) & 3);          // v23 = 4 - ((3*(u8)v15)&3)
            // NOTE: original uses (3*(_BYTE)v15)&3 — the low byte of width. For a
            // faithful low-byte reproduction we mask the same way:
            pad = 4 - ((3 * (u8)width) & 3);
            for (int y = 0; y < height; ++y) {
                int off;
                if (topDown)                         // if(v21): bottom row first
                    off = 3 * width * (height - y - 1); // v8 = 3*v15*(v16-v7-1)
                else
                    off = 3 * y * width;                // v8 = 3*v7*v15
                c.read(dst + off, 1, rowBytes);         // Read(.., 1, 3*v15)
                if (((rowBytes) & 3) != 0)              // if((v22 & 3) != 0)
                    c.seek(pad, 1);                     // Seek(v23, 1)  (SEEK_CUR)
            }
        }
        // BGR<->RGB swap pass (0x422a8e): for each of width*height pixels swap
        // bytes [0] and [2].
        int n = 0;                                   // v10
        u8* p = dst;                                 // v11 = a1
        while (n < width * height) {                 // while(v10 < v15*v16)
            p += 3;                                   // v11 += 3
            u8 b0 = p[-3];                            // v12 = *(v11-3)
            u8 b1 = p[-2];                            // v13 = *(v11-2)
            p[-3] = p[-1];                            // *(v11-3) = *(v11-1)
            p[-2] = b1;                               // *(v11-2) = v13
            ++n;
            p[-1] = b0;                               // *(v11-1) = v12
        }
    }
    // VIBE_Vfs_CloseAndFreeEntry — no-op here.
    return 1;                       // both header-ok and header-bad paths return 1
}

// gilde.exe 0x4226ec — VIBE_Picture_LoadBmp24_226ec.
int PictureLoadBmp24_226ec(const PictureBmpFile& file, u8* dst, int fbWidth) {
    // SetGrayColorThunk(0, 0xC00, byte_75DD96) clears the 3072-byte row scratch;
    // not observable on `dst`, so omitted.
    if (file.empty())
        return 0;                  // open failed -> sprintf "Cannot open.." ; return 0
    Cursor c(file);
    c.seek(0, 0);                  // VIBE_File_Seek(0, 0)
    u8 hdr[18] = {0};
    c.read(hdr, 0x12, 1);          // VIBE_File_Read(.., 0x12, 1)
    u8 bpp = hdr[16];              // byte_75E9A6
    if (bpp != 24)
        return 1;                  // close + return 1 (success no-op)
    int width  = rd16(hdr + 12);   // dword_75E9A0 >> 16
    int height = rd16(hdr + 14);   // (dword_75E9A0+2) >> 16
    u8 descr   = hdr[17];          // byte_75E9A7
    std::vector<u8> row((size_t)3 * (width > 0 ? width : 0), 0); // byte_75DD96 scratch
    for (int i = 0; i < height; ++i) {                // for(i=0; i<height; ++i)
        c.read(row.data(), width, 3);                 // Read(.., dword_75E9A0>>16, 3)
        for (int x = 0; x < width; ++x) {
            int j = 3 * x;
            u8 B = row[j + 0];                         // v15 = byte_75DD96[j]
            u8 G = row[j + 1];                         // v13 = byte_75DD97[j]
            u8 R = row[j + 2];                         // v14 = byte_75DD98[j]
            int dstRow = (descr == 32) ? i : (height - i - 1);
            int v11 = 3 * (x + fbWidth * dstRow);
            dst[v11 + 0] = R;                          // *(v11+a2)   = v14
            dst[v11 + 1] = G;                          // *(v11+a2+1) = v13
            dst[v11 + 2] = B;                          // *(v11+a2+2) = v15
        }
    }
    return 1;
}

// gilde.exe 0x422d98 — VIBE_Picture_SaveBmpPalette.
PictureBmpFile PictureSaveBmpPalette(int width, int height,
                                     const u8* pixels, const u8* pal) {
    PictureBmpFile out;
    int pixBytes = width * height;          // v7 = a1 * a3
    // 14-byte BITMAPFILEHEADER (v23 'BM', size, reserved, dataOffset):
    wr16(out, 19778);                        // v23 = 19778 ('BM')
    wr32(out, (u32)(pixBytes + 1078));       // v24 = v7 + 1078
    wr16(out, 0);                            // v25 = 0
    wr16(out, 0);                            // v26 = 0
    wr32(out, 1078);                         // v27 = 1078 (dataOffset)
    // 40-byte BITMAPINFOHEADER (v14[0]=40, ...):
    wr32(out, 40);                           // v14[0]
    wr32(out, (u32)width);                   // v14[1] = a1
    wr32(out, (u32)(-height));               // v14[2] = -a3  (top-down)
    wr16(out, 1);                            // v15 = 1 (planes)
    wr16(out, 8);                            // v16 = 8 (bitcount)
    wr32(out, 0);                            // v17 = 0 (compression)
    wr32(out, 0);                            // v18 = 0 (imageSize)
    wr32(out, 1);                            // v19 = 1 (xPelsPerMeter)
    wr32(out, 1);                            // v20 = 1 (yPelsPerMeter)
    wr32(out, 0);                            // v21 = 0 (clrUsed)
    // The original buffers v14..v22 (a 36-byte info tail + 257 palette dwords)
    // and writes 0xE bytes (file header) + 0x428==1064 bytes in one go. The
    // info header thus ends with biClrImportant == v22[0] == 0, immediately
    // followed by the 256-entry palette. Exact reproduction of the layout:
    //   v22[0]            = 0                       (biClrImportant)
    //   loop i=0..254:    v22[i+1] = {B,G,R,0}      from pal (a5[2],a5[1],a5[0])
    //   v22[256]          = whatever was in the buffer (here emitted as 0)
    // (the loop `for(i=0;i!=255;...)` with the inner ++i fills v22[1..255]).
    {
        // v22[0] = biClrImportant = 0:
        wr32(out, 0);
        const u8* a5 = pal;                  // a5 walks pal in 3-byte steps
        for (int i = 0; i < 255; ++i) {      // fills v22[1..255]
            out.push_back(a5[2]);            // LOBYTE = a5[2]  (B)
            out.push_back(a5[1]);            // BYTE1  = a5[1]  (G)
            out.push_back(a5[0]);            // BYTE2  = *a5    (R)
            out.push_back(0);                // HIBYTE = 0
            a5 += 3;                         // a5 += 3
        }
        // v22[256]: the 257th dword in the 0x428 write (never touched by the
        // fill loop). Emitted zero so the byte stream length matches exactly.
        wr32(out, 0);
    }
    // Totals: file header 14 + (info 36 + 257*4 palette) 1064 = 1078 == dataOffset.
    // Pixel rows: a3*a1 bytes copied verbatim (8bpp indices, top-down).
    out.insert(out.end(), pixels, pixels + (size_t)pixBytes);
    return out;
}

// gilde.exe 0x422ae4 — VIBE_Picture_CreateSurfaceFromBmp.
BmpSurface PictureCreateSurfaceFromBmp(const PictureBmpFile& file,
                                       const BmpSurfaceCreate& createSurface) {
    int dims = PictureReadBmpDimensions(file);   // BmpDimensions
    if (dims < 0)
        return BmpSurface{};                     // if(BmpDimensions < 0) return 0
    u16 w = (u16)dims;                            // v5 = (u16)BmpDimensions
    int h = dims >> 16;                           // v6 = BmpDimensions >> 16
    if (h < 0)
        h = abs32(h);                             // if(v6<0) v6 = abs32(v6)
    // v10 layout: v10[1]=width, v10[2]=height, byte v11=24 (bitcount) -> create.
    BmpSurface surf = createSurface((int)w, h, 24);  // VIBE_Surface_Create(.., 24bpp)
    if (!surf.data)
        return BmpSurface{};                      // if(!result) return result(==0)
    PictureLoadBmpUncompressed(file, surf.data);  // fill *(result+28)
    return surf;                                  // return v9
}

} // namespace guild::render
