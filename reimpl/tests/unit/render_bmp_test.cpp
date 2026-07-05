// Unit tests for guild::render BMP load/save (render/bmp.{h,cpp}) — the
// VIBE_Bmp_* cluster (0x5f0c10 ReadHeaderInfo, 0x5f0ce4 LoadBuffer,
// 0x5f1664 SaveIndexed, 0x5f18f4 Save24Bit).
//
// Covers a valid round-trip golden plus the wave-11 hardening vectors: empty /
// truncated / bad-dims / bad-bpp / bad-compression / oversized-palette files.
// Every malformed input must fail SAFE (empty result / ok=false) with no OOB
// under ASAN+UBSAN.
#include "test.h"

#include "render/bmp.h"

#include <cstring>
#include <vector>

using namespace guild;
using namespace guild::render;

namespace {

void put16(std::vector<u8>& v, std::size_t off, u16 x) {
    if (off + 2 > v.size()) v.resize(off + 2);
    v[off] = (u8)x; v[off + 1] = (u8)(x >> 8);
}
void put32(std::vector<u8>& v, std::size_t off, u32 x) {
    if (off + 4 > v.size()) v.resize(off + 4);
    v[off] = (u8)x; v[off + 1] = (u8)(x >> 8);
    v[off + 2] = (u8)(x >> 16); v[off + 3] = (u8)(x >> 24);
}

// Build a BMP file: 14-byte file header + 40-byte info header (+optional palette
// at 0x36 and pixel data). `clrUsed`==0 => 256 entries assumed for 8-bit.
std::vector<u8> makeBmp(i32 w, i32 h, u16 planes, u16 bpp, u32 comp, u32 clrUsed,
                        std::size_t paletteBytes, std::size_t pixelBytes) {
    std::vector<u8> f(0x36 + paletteBytes + pixelBytes, 0);
    f[0] = 'B'; f[1] = 'M';
    put32(f, 0x0A, (u32)(0x36 + paletteBytes));  // bfOffBits
    // info header at 0x0E:
    put32(f, 0x0E + 0,  40);
    put32(f, 0x0E + 4,  (u32)w);
    put32(f, 0x0E + 8,  (u32)h);
    put16(f, 0x0E + 12, planes);
    put16(f, 0x0E + 14, bpp);
    put32(f, 0x0E + 16, comp);
    put32(f, 0x0E + 32, clrUsed);
    return f;
}

} // namespace

// ---- valid round-trip golden ------------------------------------------------

TEST(Bmp, ReadHeaderInfoValid) {
    auto f = makeBmp(5, -3, 1, 24, 0, 0, 0, 0);  // negative height -> abs
    BmpInfo info = BmpReadHeaderInfo(f);
    CHECK(info.ok);
    CHECK_EQ(info.width, 5);
    CHECK_EQ(info.height, 3);
    CHECK_EQ(info.bitCount, 24);
}

TEST(Bmp, Save24BinaryLayout) {
    // gilde.exe 0x5f18f4: the info block is written as 44 bytes (40-byte
    // BITMAPINFOHEADER + 4 zero pad), dataOffset = 58, fileSize = 3*w*h + 58,
    // pixel rows bottom-up as B,G,R. (The old test round-tripped through
    // BmpLoadBuffer, but the BINARY's loader reads 24-bit data at 0x36=54 —
    // 4 bytes before where its own saver puts it — so save/load are not a
    // symmetric pair in the original; pin the on-disk layout instead.)
    u8 px[2 * 2 * 3] = {
        10,20,30,  40,50,60,
        70,80,90,  100,110,120,
    };
    std::vector<u8> file = BmpSave24Bit(2, 2, px);
    CHECK_EQ((int)file.size(), 58 + 12);
    auto rd32 = [&](size_t o) {
        return (u32)(file[o] | (file[o+1] << 8) | (file[o+2] << 16) | (file[o+3] << 24));
    };
    CHECK_EQ((int)rd32(2), 3 * 2 * 2 + 58);   // fileSize
    CHECK_EQ((int)rd32(10), 58);              // dataOffset (v45)
    CHECK_EQ((int)rd32(14), 40);              // info size
    CHECK_EQ((int)rd32(54), 0);               // the 4 zero pad bytes
    // First stored row = LAST input row (bottom-up), B,G,R swapped.
    CHECK_EQ((int)file[58], 90);   // B of px row1 pixel0
    CHECK_EQ((int)file[59], 80);   // G
    CHECK_EQ((int)file[60], 70);   // R
    // Second stored row = first input row.
    CHECK_EQ((int)file[64], 30);
    CHECK_EQ((int)file[65], 20);
    CHECK_EQ((int)file[66], 10);
}

TEST(Bmp, SaveIndexedThenLoad8) {
    u8 idx[4] = {0, 1, 254, 255};
    std::vector<u8> file = BmpSaveIndexed(2, 2, idx);
    int w = 0, h = 0;
    std::vector<u8> out = BmpLoadBuffer(file, 8, w, h);
    CHECK_EQ(w, 2);
    CHECK_EQ(h, 2);
    CHECK_EQ((int)out.size(), 4);
    // top-down indices preserved through the bottom-up disk round-trip.
    CHECK_EQ((int)out[0], 0);
    CHECK_EQ((int)out[1], 1);
    CHECK_EQ((int)out[2], 254);
    CHECK_EQ((int)out[3], 255);
}

// ===========================================================================
// HARDENING (wave-11): malformed BMP inputs — fail safe, no OOB.
// ===========================================================================

TEST(BmpHarden, EmptyAndTruncatedHeader) {
    int w, h;
    for (std::size_t n = 0; n < 0x0E + 40; ++n) {
        std::vector<u8> f(n, 0xCD);
        CHECK(BmpLoadBuffer(f, 24, w, h).empty());
        CHECK(!BmpReadHeaderInfo(f).ok);
    }
}

TEST(BmpHarden, BadPlanesBppCompression) {
    int w, h;
    // planes != 1
    CHECK(BmpLoadBuffer(makeBmp(2, 2, 2, 24, 0, 0, 0, 12), 24, w, h).empty());
    // bpp neither 8 nor 24
    CHECK(BmpLoadBuffer(makeBmp(2, 2, 1, 16, 0, 0, 0, 12), 24, w, h).empty());
    CHECK(BmpLoadBuffer(makeBmp(2, 2, 1, 4,  0, 0, 0, 12), 24, w, h).empty());
    // 8-bit with compression > 1 (invalid)
    CHECK(BmpLoadBuffer(makeBmp(2, 2, 1, 8, 3, 0, 1024, 12), 8, w, h).empty());
    // header-info validator agrees
    CHECK(!BmpReadHeaderInfo(makeBmp(2, 2, 1, 16, 0, 0, 0, 0)).ok);
    CHECK(!BmpReadHeaderInfo(makeBmp(2, 2, 1, 8, 2, 0, 0, 0)).ok);
}

TEST(BmpHarden, NegativeAndZeroDimensions) {
    int w, h;
    // negative width must be rejected before sizing the decode buffer.
    CHECK(BmpLoadBuffer(makeBmp(-4, 4, 1, 24, 0, 0, 0, 0x1000), 24, w, h).empty());
    // zero width / zero height rejected.
    CHECK(BmpLoadBuffer(makeBmp(0, 4, 1, 24, 0, 0, 0, 0), 24, w, h).empty());
    CHECK(BmpLoadBuffer(makeBmp(4, 0, 1, 24, 0, 0, 0, 0), 24, w, h).empty());
}

TEST(BmpHarden, OversizedDimensionsRejected) {
    int w, h;
    // width*height beyond the sane ceiling must be rejected, not allocate wild.
    CHECK(BmpLoadBuffer(makeBmp(0x10000, 0x10000, 1, 24, 0, 0, 0, 0), 24, w, h)
              .empty());
}

TEST(BmpHarden, OversizedPaletteClamped) {
    int w, h;
    // A huge biClrUsed must not drive the palette unpack past the 256-entry
    // scratch (it is clamped to 256). The file is too small for that many quads,
    // so the load fails safe — but with NO OOB write into srcPal.
    auto f = makeBmp(2, 2, 1, 8, 0, 0x40000000u, 16, 4);
    CHECK(BmpLoadBuffer(f, 8, w, h).empty());
    // A negative-looking clrUsed (top bit set, smaller) is likewise clamped.
    auto f2 = makeBmp(2, 2, 1, 8, 0, 0x80000001u, 16, 4);
    CHECK(BmpLoadBuffer(f2, 8, w, h).empty());
}

TEST(BmpHarden, Truncated24BitPixelData) {
    int w, h;
    // Header says 8x8 24-bit but only one row of pixels is present: the per-row
    // EOF check must break, returning a (zero-filled) buffer without over-reading.
    auto f = makeBmp(8, 8, 1, 24, 0, 0, 0, /*pixelBytes*/ 3 * 8);
    std::vector<u8> out = BmpLoadBuffer(f, 24, w, h);
    // Either empty (rejected) or a valid-sized buffer; never OOB. If non-empty it
    // is exactly w*h*3 bytes.
    if (!out.empty())
        CHECK_EQ((int)out.size(), 8 * 8 * 3);
}

TEST(BmpHarden, Rle8DegenerateStreams) {
    int w, h;
    // BI_RLE8 8-bit with a stream that is mostly absent / contains delta + abs
    // escapes near EOF. Must terminate without reading past the buffer.
    auto f = makeBmp(4, 4, 1, 8, 1, 256, 1024, /*pixelBytes*/ 6);
    // craft a few RLE bytes after the palette (0x36 + 1024):
    std::size_t d = 0x36 + 1024;
    f[d + 0] = 0;  f[d + 1] = 2;   // delta escape (then expects 2 more bytes...)
    f[d + 2] = 7;  f[d + 3] = 9;   // delta dx=7 dy=9 (jumps past dims) -> safe
    f[d + 4] = 0;  f[d + 5] = 1;   // end of bitmap
    std::vector<u8> out = BmpLoadBuffer(f, 8, w, h);
    if (!out.empty())
        CHECK_EQ((int)out.size(), 4 * 4);
}
