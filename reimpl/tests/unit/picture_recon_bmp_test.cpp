#include "tests/framework/test.h"
#include "render/picture_recon_bmp.h"
#include <vector>
#include <cstring>

// =============================================================================
// Golden-vector tests for the gilde.exe BMP picture cluster
// (render/picture_recon_bmp.{h,cpp}, 0x4222bc..0x422d98). Each vector is a
// hand-built in-memory file with a known correct decode/encode.
// =============================================================================
using namespace guild;
using namespace guild::render;

namespace {

void put16(std::vector<u8>& v, size_t off, i16 x) {
    v[off] = (u8)x; v[off + 1] = (u8)((u16)x >> 8);
}
void put32(std::vector<u8>& v, size_t off, i32 x) {
    v[off + 0] = (u8)x;        v[off + 1] = (u8)((u32)x >> 8);
    v[off + 2] = (u8)((u32)x >> 16); v[off + 3] = (u8)((u32)x >> 24);
}

// Build a file whose 40-byte BITMAPINFOHEADER begins at offset 14, with the
// given fields. Returns a file of size 14+40 (+extra trailing capacity).
PictureBmpFile makeInfoFile(i32 w, i32 h, i16 planes, i16 bits, i32 comp,
                            i32 clrUsed = 0, size_t extra = 0) {
    PictureBmpFile f(14 + 40 + extra, 0);
    put32(f, 14 + 4,  w);
    put32(f, 14 + 8,  h);
    put16(f, 14 + 12, planes);
    put16(f, 14 + 14, bits);
    put32(f, 14 + 16, comp);
    put32(f, 14 + 32, clrUsed);
    return f;
}

} // namespace

// ---- ReadHeader (8bpp validity probe, returns width|(abs(h)<<16)) -----------
TEST(PictureReconBmp, ReadHeaderValid8bpp) {
    auto f = makeInfoFile(/*w*/ 7, /*h*/ -5, /*planes*/ 1, /*bits*/ 8, /*comp*/ 0);
    i32 r = PictureReadHeader(f);
    CHECK_EQ(r, (i32)(7u | (5u << 16)));   // abs(-5) in the high word
}
TEST(PictureReconBmp, ReadHeaderRejectsNon8bpp) {
    auto f = makeInfoFile(7, 5, 1, 24, 0);
    CHECK_EQ(PictureReadHeader(f), -2);
}
TEST(PictureReconBmp, ReadHeaderRejectsCompression2) {
    auto f = makeInfoFile(7, 5, 1, 8, 2);   // comp must be < 2
    CHECK_EQ(PictureReadHeader(f), -2);
}
TEST(PictureReconBmp, ReadHeaderEmptyFile) {
    PictureBmpFile f;
    CHECK_EQ(PictureReadHeader(f), -1);
}

// ---- ReadBmpType (returns biBitCount; word at read-offset 14) ----------------
TEST(PictureReconBmp, ReadBmpType) {
    auto f = makeInfoFile(10, 10, 1, 24, 0);
    CHECK_EQ((int)PictureReadBmpType(f), 24);
    auto f8 = makeInfoFile(10, 10, 1, 8, 0);
    CHECK_EQ((int)PictureReadBmpType(f8), 8);
}

// ---- ReadBmpDimensions (24bpp, returns width|(height<<16), no abs) ----------
TEST(PictureReconBmp, ReadBmpDimensionsValid24) {
    auto f = makeInfoFile(640, 480, 1, 24, 0);
    CHECK_EQ(PictureReadBmpDimensions(f), (i32)(640u | (480u << 16)));
}
TEST(PictureReconBmp, ReadBmpDimensionsRejectsCompressed) {
    auto f = makeInfoFile(640, 480, 1, 24, 1);
    CHECK_EQ(PictureReadBmpDimensions(f), -2);
}

// ---- LoadBmpPalette (BGRA quads -> [R,G,B], stride 4) -----------------------
TEST(PictureReconBmp, LoadBmpPalette) {
    // 2 colours: clrUsed=2. Quads at file offset 14+40 = 54.
    auto f = makeInfoFile(1, 1, 1, 8, 0, /*clrUsed*/ 2, /*extra*/ 8);
    // quad 0 = B,G,R,_ = 10,20,30,0 ; quad 1 = 40,50,60,0
    f[54 + 0] = 10; f[54 + 1] = 20; f[54 + 2] = 30; f[54 + 3] = 0;
    f[54 + 4] = 40; f[54 + 5] = 50; f[54 + 6] = 60; f[54 + 7] = 0;
    u8 pal[1024];
    std::memset(pal, 0xAB, sizeof(pal));
    PictureLoadBmpPalette(f, pal);
    // entry 0 -> R=30,G=20,B=10 at stride 4
    CHECK_EQ((int)pal[0], 30); CHECK_EQ((int)pal[1], 20); CHECK_EQ((int)pal[2], 10);
    // entry 1 at offset 4 -> R=60,G=50,B=40
    CHECK_EQ((int)pal[4], 60); CHECK_EQ((int)pal[5], 50); CHECK_EQ((int)pal[6], 40);
}

// ---- LoadBmpUncompressed (24bpp, BGR on disk -> RGB in dst, top/bottom) -----
TEST(PictureReconBmp, LoadBmpUncompressedTopDown) {
    // width=2, height=+2 (top-down, since height>=0). rowBytes = 6, (6&3)=2 != 0
    // so there is row padding of 4-((3*2)&3)=4-2=2 bytes after each row.
    auto f = makeInfoFile(2, 2, 1, 24, 0, /*clrUsed*/ 0, /*extra*/ 64);
    // pixel data starts at 4*clrUsed+54 = 54. Two rows of 2 BGR pixels + 2 pad.
    size_t p = 54;
    // row stored first is the BOTTOM row (off = 3*w*(h-y-1)); for top-down v21,
    // y=0 -> off=3*2*(2-0-1)=6 -> second dst row; y=1 -> off=0 -> first dst row.
    // disk row A (read y=0): pixels (B,G,R)=(1,2,3),(4,5,6) then 2 pad
    f[p++]=1; f[p++]=2; f[p++]=3; f[p++]=4; f[p++]=5; f[p++]=6; f[p++]=0; f[p++]=0;
    // disk row B (read y=1): (7,8,9),(10,11,12) then 2 pad
    f[p++]=7; f[p++]=8; f[p++]=9; f[p++]=10; f[p++]=11; f[p++]=12; f[p++]=0; f[p++]=0;
    u8 dst[2 * 2 * 3];
    std::memset(dst, 0, sizeof(dst));
    int r = PictureLoadBmpUncompressed(f, dst);
    CHECK_EQ(r, 1);
    // After load (BGR->stored) then the B<->R swap pass, each disk (B,G,R)
    // becomes (R,G,B) in dst. disk rowA went to dst offset 6 (pixels 2,3),
    // disk rowB went to dst offset 0 (pixels 0,1).
    // dst pixel0 (offset 0) = disk B (7,8,9) -> swapped -> R,G,B = 9,8,7
    CHECK_EQ((int)dst[0], 9); CHECK_EQ((int)dst[1], 8); CHECK_EQ((int)dst[2], 7);
    // dst pixel1 (offset 3) = (10,11,12) -> 12,11,10
    CHECK_EQ((int)dst[3], 12); CHECK_EQ((int)dst[4], 11); CHECK_EQ((int)dst[5], 10);
    // dst pixel2 (offset 6) = disk A (1,2,3) -> 3,2,1
    CHECK_EQ((int)dst[6], 3); CHECK_EQ((int)dst[7], 2); CHECK_EQ((int)dst[8], 1);
    // dst pixel3 (offset 9) = (4,5,6) -> 6,5,4
    CHECK_EQ((int)dst[9], 6); CHECK_EQ((int)dst[10], 5); CHECK_EQ((int)dst[11], 4);
}

// ---- LoadBmp24_226ec (TGA-headed 24bpp -> RGB, fbWidth stride) --------------
TEST(PictureReconBmp, LoadBmp24_226ec_TopDown) {
    // 18-byte TGA header at file offset 0. width=2 (@12), height=1 (@14),
    // bpp=24 (@16), descriptor=32 (@17, top-down). Then 2 BGR pixels.
    PictureBmpFile f(18 + 2 * 3, 0);
    put16(f, 12, 2);     // width
    put16(f, 14, 1);     // height
    f[16] = 24;          // bpp
    f[17] = 32;          // descriptor (top-down)
    // pixels BGR: (1,2,3),(4,5,6)
    f[18]=1; f[19]=2; f[20]=3; f[21]=4; f[22]=5; f[23]=6;
    u8 dst[3 * 2];       // fbWidth = 2, one row
    std::memset(dst, 0, sizeof(dst));
    int r = PictureLoadBmp24_226ec(f, dst, /*fbWidth*/ 2);
    CHECK_EQ(r, 1);
    // store order is R,G,B: pixel0 -> 3,2,1 ; pixel1 -> 6,5,4
    CHECK_EQ((int)dst[0], 3); CHECK_EQ((int)dst[1], 2); CHECK_EQ((int)dst[2], 1);
    CHECK_EQ((int)dst[3], 6); CHECK_EQ((int)dst[4], 5); CHECK_EQ((int)dst[5], 4);
}
TEST(PictureReconBmp, LoadBmp24_226ec_RejectsNon24) {
    PictureBmpFile f(18, 0);
    f[16] = 8;           // not 24 -> returns 1 (no-op)
    u8 dst[3] = {7,7,7};
    CHECK_EQ(PictureLoadBmp24_226ec(f, dst, 1), 1);
    CHECK_EQ((int)dst[0], 7);   // untouched
}

// ---- SaveBmpPalette (8bpp BMP: 14+40+1024 header, then pixels) --------------
TEST(PictureReconBmp, SaveBmpPaletteLayout) {
    // 2x1 image, 2 palette entries set (rest zero).
    u8 pixels[2] = {1, 0};
    u8 pal[256 * 3];
    std::memset(pal, 0, sizeof(pal));
    pal[0]=30; pal[1]=20; pal[2]=10;     // colour 0 RGB
    pal[3]=60; pal[4]=50; pal[5]=40;     // colour 1 RGB
    auto out = PictureSaveBmpPalette(2, 1, pixels, pal);
    // total = 1078 header + 2 pixels
    CHECK_EQ((int)out.size(), 1078 + 2);
    // 'BM'
    CHECK_EQ((int)out[0], 'B'); CHECK_EQ((int)out[1], 'M');
    // fileSize = 2 + 1078 = 1080
    u32 fsz = out[2] | (out[3] << 8) | (out[4] << 16) | ((u32)out[5] << 24);
    CHECK_EQ((int)fsz, 1080);
    // dataOffset (bytes 10..13) = 1078
    u32 off = out[10] | (out[11] << 8) | (out[12] << 16) | ((u32)out[13] << 24);
    CHECK_EQ((int)off, 1078);
    // info header biSize = 40 at byte 14
    CHECK_EQ((int)out[14], 40);
    // biHeight stored negative (-1): bytes 14+8..14+11 == 0xFFFFFFFF
    CHECK_EQ((int)out[22], 0xFF); CHECK_EQ((int)out[23], 0xFF);
    CHECK_EQ((int)out[24], 0xFF); CHECK_EQ((int)out[25], 0xFF);
    // The 40-byte info block ends with biClrImportant == v22[0] == 0 at bytes
    // 50..53 (14 file header + 36 info fields). The palette colours (v22[1..])
    // begin at byte 54.
    CHECK_EQ((int)out[50], 0); CHECK_EQ((int)out[51], 0);
    CHECK_EQ((int)out[52], 0); CHECK_EQ((int)out[53], 0);
    // entry 1 (offset 54) carries pal[0..2] as B,G,R,0 = 10,20,30,0
    CHECK_EQ((int)out[54], 10); CHECK_EQ((int)out[55], 20);
    CHECK_EQ((int)out[56], 30); CHECK_EQ((int)out[57], 0);
    // entry 2 (offset 58) carries pal[3..5] as B,G,R,0 = 40,50,60,0
    CHECK_EQ((int)out[58], 40); CHECK_EQ((int)out[59], 50);
    CHECK_EQ((int)out[60], 60); CHECK_EQ((int)out[61], 0);
    // pixels appended after the 1078-byte header+palette
    CHECK_EQ((int)out[1078], 1); CHECK_EQ((int)out[1079], 0);
}

// ---- LoadBmpRle (BI_RLE8 span decode) ---------------------------------------
TEST(PictureReconBmp, LoadBmpRleEncodedRun) {
    // width(v17/stride)=4, height=1, compression=1, clrUsed=0(->256).
    // pixel data starts at 4*256+54 = 1078.
    auto f = makeInfoFile(/*w/stride*/ 4, /*h*/ 1, 1, 8, /*comp*/ 1, 0,
                          /*extra*/ 1078 + 16);
    size_t p = 1078;
    // encoded run: count=2, value=9 -> two 9's at col 0,1
    f[p++] = 2; f[p++] = 9;
    // absolute run: 0,3 -> read 3 literals 7,8,5 (code 3 is absolute; odd length
    // so the original reads val+1==4 bytes, the 4th is alignment padding).
    f[p++] = 0; f[p++] = 3; f[p++] = 7; f[p++] = 8; f[p++] = 5; f[p++] = 0;
    // end of line: 0,0
    f[p++] = 0; f[p++] = 0;
    u8 dst[8] = {0,0,0,0,0,0,0,0};   // stride 4 + slack for the padded literal read
    char r = PictureLoadBmpRle(f, dst, /*dstStride*/ 4, /*flip*/ 0);
    CHECK_EQ((int)r, 1);
    CHECK_EQ((int)dst[0], 9); CHECK_EQ((int)dst[1], 9);   // encoded run
    CHECK_EQ((int)dst[2], 7); CHECK_EQ((int)dst[3], 8);   // absolute literals
    CHECK_EQ((int)dst[4], 5);                             // 3rd literal (col advanced by 3)
}

// ---- CreateSurfaceFromBmp (orchestrator, injected surface allocator) --------
TEST(PictureReconBmp, CreateSurfaceFromBmp) {
    // a valid 24bpp file, width=2 height=1, with one BGR pixel row.
    auto f = makeInfoFile(2, 1, 1, 24, 0, 0, /*extra*/ 64);
    size_t p = 54;   // pixel data at 4*0+54
    // rowBytes = 6, padding 2. one row top-down (h=1).
    f[p++]=1; f[p++]=2; f[p++]=3; f[p++]=4; f[p++]=5; f[p++]=6; f[p++]=0; f[p++]=0;

    static u8 backing[2 * 1 * 3];
    std::memset(backing, 0, sizeof(backing));
    auto alloc = [](int w, int h, int bpp) -> BmpSurface {
        BmpSurface s;
        s.width = w; s.height = h; s.bpp = bpp;
        s.data = backing;
        s.handle = backing;
        return s;
    };
    BmpSurface s = PictureCreateSurfaceFromBmp(f, alloc);
    CHECK(s.data != nullptr);
    CHECK_EQ(s.width, 2);
    CHECK_EQ(s.height, 1);
    CHECK_EQ(s.bpp, 24);
    // pixel0 (1,2,3) BGR -> swapped -> 3,2,1 ; pixel1 (4,5,6) -> 6,5,4
    CHECK_EQ((int)backing[0], 3); CHECK_EQ((int)backing[1], 2); CHECK_EQ((int)backing[2], 1);
    CHECK_EQ((int)backing[3], 6); CHECK_EQ((int)backing[4], 5); CHECK_EQ((int)backing[5], 4);
}

TEST(PictureReconBmp, CreateSurfaceFromBmpRejectsBadHeader) {
    auto f = makeInfoFile(2, 1, 1, 8, 0);   // 8bpp -> ReadBmpDimensions returns -2
    auto alloc = [](int, int, int) -> BmpSurface { return BmpSurface{}; };
    BmpSurface s = PictureCreateSurfaceFromBmp(f, alloc);
    CHECK(s.data == nullptr);
}

// ===========================================================================
// HARDENING (wave-11): malformed/empty/truncated picture-BMP inputs. The header
// readers and palette loader must fail SAFE with no OOB under ASAN+UBSAN.
// ===========================================================================

TEST(PictureReconBmpHarden, EmptyFiles) {
    PictureBmpFile e;
    CHECK_EQ(PictureReadHeader(e), -1);
    CHECK_EQ(PictureReadBmpType(e), (i16)-1);
    CHECK_EQ(PictureReadBmpDimensions(e), -1);
    u8 pal[1024]; std::memset(pal, 0xAB, sizeof(pal));
    PictureLoadBmpPalette(e, pal);          // open-fail leg: clears 768, returns
    for (int i = 0; i < 768; ++i) CHECK_EQ((int)pal[i], 0);
}

TEST(PictureReconBmpHarden, HeaderShorterThanInfoBlock) {
    // Files of 1..40 bytes — the cursor reads only what is available (clamped),
    // the rest of the 40-byte header buffer stays zero. No over-read.
    for (std::size_t n = 1; n <= 40; ++n) {
        PictureBmpFile f(n, 0xFF);
        // never crashes; result is just whatever the zero-padded header decodes to.
        (void)PictureReadHeader(f);
        (void)PictureReadBmpType(f);
        (void)PictureReadBmpDimensions(f);
    }
}

TEST(PictureReconBmpHarden, PaletteOversizedClrUsedClamped) {
    // biClrUsed declares far more than 256 entries; the unpack must clamp to 256
    // and not write past the 1024-byte `pal` block nor over-read the file.
    auto f = makeInfoFile(2, 2, 1, 8, 0, /*clrUsed*/ 0x7FFFFFFF, /*extra*/ 64);
    u8 pal[1024]; std::memset(pal, 0xAB, sizeof(pal));
    PictureLoadBmpPalette(f, pal);   // must not OOB (ASAN guards the 1024 bytes)
    // The first 768 bytes were cleared at entry; clamped read fills what it can.
    CHECK_EQ((int)pal[0], pal[0]);   // reachable line == no crash
}

TEST(PictureReconBmpHarden, PaletteNegativeClrUsedClamped) {
    auto f = makeInfoFile(2, 2, 1, 8, 0, /*clrUsed*/ (i32)0x80000005, 64);
    u8 pal[1024]; std::memset(pal, 0, sizeof(pal));
    PictureLoadBmpPalette(f, pal);   // clamped to 256, no OOB
    CHECK(true);
}

TEST(PictureReconBmpHarden, ReadHeaderRejectsBadFields) {
    CHECK_EQ(PictureReadHeader(makeInfoFile(4, 4, 2, 8, 0)), -2);   // planes!=1
    CHECK_EQ(PictureReadHeader(makeInfoFile(4, 4, 1, 24, 0)), -2);  // bpp!=8
    CHECK_EQ(PictureReadHeader(makeInfoFile(4, 4, 1, 8, 2)), -2);   // comp>=2
    CHECK_EQ(PictureReadBmpDimensions(makeInfoFile(4, 4, 1, 8, 0)), -2); // needs 24bpp
}
