#include "render/surface_stretch.h"
#include "render/shape_blit.h"
#include "tests/framework/test.h"

#include <cstdint>
#include <vector>

using namespace guild::render;
using guild::u8;
using guild::u16;
using guild::u32;
using guild::i32;

namespace {

// RGB565 masks (R=5@11, G=6@5, B=5@0).
constexpr u32 kR = 0xF800, kG = 0x07E0, kB = 0x001F;

StretchSurfaceDesc Make16(int w, int h, std::vector<u16>& buf) {
    buf.assign(static_cast<size_t>(w) * h, 0);
    StretchSurfaceDesc d;
    d.width = w; d.height = h; d.pitch = w * 2;
    d.pixels = reinterpret_cast<u8*>(buf.data());
    d.bpp = 16; d.indexed = false;
    d.rMask = kR; d.gMask = kG; d.bMask = kB;
    return d;
}

StretchSurfaceDesc Make8(int w, int h, std::vector<u8>& buf, int pitch = -1) {
    if (pitch < 0) pitch = w;
    buf.assign(static_cast<size_t>(pitch) * h, 0);
    StretchSurfaceDesc d;
    d.width = w; d.height = h; d.pitch = pitch;
    d.pixels = buf.data();
    d.bpp = 8; d.indexed = true;
    return d;
}

StretchSurfaceDesc Make24(int w, int h, std::vector<u8>& buf) {
    buf.assign(static_cast<size_t>(w) * 3 * h, 0);
    StretchSurfaceDesc d;
    d.width = w; d.height = h; d.pitch = w * 3;
    d.pixels = buf.data();
    d.bpp = 24; d.indexed = false;
    d.rMask = kR; d.gMask = kG; d.bMask = kB;
    return d;
}

} // namespace

// --- Convert24To16 golden vector ----------------------------------------------
// Binary (0x437814) maps source byte +0 -> R channel, +1 -> G, +2 -> B (verified
// in disasm 0x43790e..0x437954). For RGB565 dest: r=byte0>>3<<11, g=byte1>>2<<5,
// b=byte2>>3. Earlier golden encoded the swapped (B,G,R) interpretation — fixed.
TEST(SurfaceStretch, Convert24To16Golden) {
    std::vector<u8> sbuf;
    std::vector<u16> dbuf;
    StretchSurfaceDesc src = Make24(2, 2, sbuf);
    StretchSurfaceDesc dst = Make16(2, 2, dbuf);
    // source bytes (b0,b1,b2) per pixel -> (R,G,B) channels:
    const u8 px[4][3] = {{10,20,30},{40,50,60},{70,80,90},{100,110,120}};
    for (int i = 0; i < 4; ++i) { sbuf[i*3+0]=px[i][0]; sbuf[i*3+1]=px[i][1]; sbuf[i*3+2]=px[i][2]; }

    u32 last = Convert24To16(dst, src);
    // (10,20,30): 1<<11 | 5<<5 | 3 = 0x08A3 ; (40,50,60): 5<<11|12<<5|7 = 0x2987
    // (70,80,90): 8<<11|20<<5|11 = 0x428B ; (100,110,120):12<<11|27<<5|15 = 0x636F
    CHECK_EQ(dbuf[0], static_cast<u16>(0x08A3));
    CHECK_EQ(dbuf[1], static_cast<u16>(0x2987));
    CHECK_EQ(dbuf[2], static_cast<u16>(0x428B));
    CHECK_EQ(dbuf[3], static_cast<u16>(0x636F));
    CHECK_EQ(last, static_cast<u32>(0x636F));
}

TEST(SurfaceStretch, Convert24To16WhiteBlack) {
    std::vector<u8> sbuf;
    std::vector<u16> dbuf;
    StretchSurfaceDesc src = Make24(2, 1, sbuf);
    StretchSurfaceDesc dst = Make16(2, 1, dbuf);
    sbuf[0]=255; sbuf[1]=255; sbuf[2]=255;  // white
    sbuf[3]=0;   sbuf[4]=0;   sbuf[5]=0;     // black
    Convert24To16(dst, src);
    CHECK_EQ(dbuf[0], static_cast<u16>(0xFFFF));
    CHECK_EQ(dbuf[1], static_cast<u16>(0x0000));
}

// --- StretchAverage16: 2x2 -> 1x1 box average ----------------------------------
TEST(SurfaceStretch, Average16Downsample) {
    std::vector<u16> sbuf, dbuf;
    StretchSurfaceDesc src = Make16(2, 2, sbuf);
    StretchSurfaceDesc dst = Make16(1, 1, dbuf);
    // field-packed 565: (31,0,0)(0,63,0)/(0,0,31)(10,20,5)
    sbuf[0] = (31u<<11);
    sbuf[1] = (63u<<5);
    sbuf[2] = (31u);
    sbuf[3] = (10u<<11)|(20u<<5)|5u;
    StretchAverage16(dst, src);
    CHECK_EQ(dbuf[0], static_cast<u16>(0x5289));
}

// Average of identical pixels returns that pixel unchanged.
TEST(SurfaceStretch, Average16Uniform) {
    std::vector<u16> sbuf, dbuf;
    StretchSurfaceDesc src = Make16(4, 4, sbuf);
    StretchSurfaceDesc dst = Make16(2, 2, dbuf);
    const u16 c = (12u<<11)|(34u<<5)|7u;
    for (auto& p : sbuf) p = c;
    StretchAverage16(dst, src);
    for (auto p : dbuf) CHECK_EQ(p, c);
}

// --- StretchSurface8 nearest-neighbour downscale -------------------------------
TEST(SurfaceStretch, Surface8Downsample) {
    std::vector<u8> sbuf, dbuf;
    StretchSurfaceDesc src = Make8(4, 4, sbuf);
    StretchSurfaceDesc dst = Make8(2, 2, dbuf);
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 4; ++c) sbuf[r*4+c] = static_cast<u8>(r*4+c);
    StretchSurface8(dst, src);
    CHECK_EQ(dbuf[0], static_cast<u8>(0));
    CHECK_EQ(dbuf[1], static_cast<u8>(2));
    CHECK_EQ(dbuf[2], static_cast<u8>(8));
    CHECK_EQ(dbuf[3], static_cast<u8>(10));
}

// --- StretchSurface8Up nearest upscale -----------------------------------------
// Binary (0x436488): first arg = eax = DST (larger, write target); second = edx =
// SRC (smaller, read sequentially). Loop bounds are the SOURCE dimensions; each src
// byte scatters to dst col (c*dstW/srcW). Earlier golden called it with reversed
// args and verified the buggy (inverted read/write) reconstruction — fixed.
TEST(SurfaceStretch, Surface8Upscale) {
    std::vector<u8> sbuf, dbuf;
    StretchSurfaceDesc src = Make8(2, 2, sbuf);   // smaller source, read
    StretchSurfaceDesc dst = Make8(4, 2, dbuf);   // larger dest, written
    // src row0: 100,101 ; row1: 110,111
    sbuf[0]=100; sbuf[1]=101;
    sbuf[2]=110; sbuf[3]=111;
    u8 last = StretchSurface8Up(dst, src);
    // dst row = dstH*i/srcH = i ; dst col = c*dstW/srcW = 2c (col0->0, col1->2).
    // row0 -> {100,0,101,0} ; row1 -> {110,0,111,0}
    CHECK_EQ(dbuf[0], static_cast<u8>(100));
    CHECK_EQ(dbuf[1], static_cast<u8>(0));
    CHECK_EQ(dbuf[2], static_cast<u8>(101));
    CHECK_EQ(dbuf[3], static_cast<u8>(0));
    CHECK_EQ(dbuf[4], static_cast<u8>(110));
    CHECK_EQ(dbuf[6], static_cast<u8>(111));
    CHECK_EQ(last, static_cast<u8>(111));         // last src byte read
}

// --- BlitThumbnailToSurface: sequential 160x120 block copy ----------------------
TEST(SurfaceStretch, ThumbnailBlit) {
    const int strideP = 200;
    std::vector<u16> surf(static_cast<size_t>(strideP) * 130, 0);
    std::vector<u16> thumb(160 * 120);
    for (size_t i = 0; i < thumb.size(); ++i) thumb[i] = static_cast<u16>(i & 0xFFFF);
    int rows = BlitThumbnailToSurface(/*x=*/5, /*y=*/3, surf.data(), strideP, thumb.data());
    CHECK_EQ(rows, 120);
    // src index 0 -> (5,3); src index 1 -> (6,3); src index 160 -> (5,4)
    CHECK_EQ(surf[3*strideP + 5], thumb[0]);
    CHECK_EQ(surf[3*strideP + 6], thumb[1]);
    CHECK_EQ(surf[4*strideP + 5], thumb[160]);
    CHECK_EQ(surf[(3+119)*strideP + (5+159)], thumb[160*120 - 1]);
}

// --- StretchSurfaceDispatch: equal-size 16bpp -> per-row copy -------------------
TEST(SurfaceStretch, DispatchEqual16Copy) {
    std::vector<u16> sbuf, dbuf;
    StretchSurfaceDesc src = Make16(3, 2, sbuf);
    StretchSurfaceDesc dst = Make16(3, 2, dbuf);
    for (size_t i = 0; i < sbuf.size(); ++i) sbuf[i] = static_cast<u16>(0x1000 + i);
    u8 st = StretchSurfaceDispatch(dst, src);
    CHECK_EQ(st, static_cast<u8>(3)); // loop bound = dst.width
    for (size_t i = 0; i < dbuf.size(); ++i) CHECK_EQ(dbuf[i], sbuf[i]);
}

// Depth mismatch -> no draw; dst untouched. The return register (al) still holds
// (u8)src.bpp from the function entry `mov eax,[edx+54h]` — NOT 0 (verified disasm).
TEST(SurfaceStretch, DispatchDepthMismatch) {
    std::vector<u16> dbuf;
    std::vector<u8> sbuf;
    StretchSurfaceDesc dst = Make16(2, 2, dbuf);
    StretchSurfaceDesc src = Make8(2, 2, sbuf);   // bpp 8 != dst bpp 16
    u8 st = StretchSurfaceDispatch(dst, src);
    CHECK_EQ(st, static_cast<u8>(8));              // (u8)src.bpp, the eax passthrough
    for (auto p : dbuf) CHECK_EQ(p, static_cast<u16>(0));
}

// Dispatch 8bpp downscale routes to StretchSurface8.
TEST(SurfaceStretch, Dispatch8Downscale) {
    std::vector<u8> sbuf, dbuf;
    StretchSurfaceDesc src = Make8(4, 4, sbuf);
    StretchSurfaceDesc dst = Make8(2, 2, dbuf);
    for (int i = 0; i < 16; ++i) sbuf[i] = static_cast<u8>(i);
    StretchSurfaceDispatch(dst, src);
    CHECK_EQ(dbuf[0], static_cast<u8>(0));
    CHECK_EQ(dbuf[3], static_cast<u8>(10));
}

// --- BlitConvertDispatch: same-size 24->16 convert ------------------------------
TEST(SurfaceStretch, ConvertDispatch24To16) {
    std::vector<u8> sbuf;
    std::vector<u16> dbuf;
    StretchSurfaceDesc src = Make24(2, 1, sbuf);
    StretchSurfaceDesc dst = Make16(2, 1, dbuf);
    sbuf[0]=10; sbuf[1]=20; sbuf[2]=30;
    sbuf[3]=40; sbuf[4]=50; sbuf[5]=60;
    u32 r = BlitConvertDispatch(dst, /*status=*/0, src, /*pal=*/nullptr);
    // byte0->R, byte1->G, byte2->B (see Convert24To16Golden): {10,20,30}=0x08A3,
    // {40,50,60}=0x2987. Returns Convert24To16's last pixel.
    CHECK_EQ(dbuf[0], static_cast<u16>(0x08A3));
    CHECK_EQ(dbuf[1], static_cast<u16>(0x2987));
    CHECK_EQ(r, static_cast<u32>(0x2987));
}

// Same-size, same-depth 16bpp -> per-row memcpy, returns height.
TEST(SurfaceStretch, ConvertDispatchEqualCopy) {
    std::vector<u16> sbuf, dbuf;
    StretchSurfaceDesc src = Make16(3, 2, sbuf);
    StretchSurfaceDesc dst = Make16(3, 2, dbuf);
    for (size_t i = 0; i < sbuf.size(); ++i) sbuf[i] = static_cast<u16>(0x2000 + i);
    u32 r = BlitConvertDispatch(dst, 0, src, nullptr);
    CHECK_EQ(r, static_cast<u32>(2)); // height
    for (size_t i = 0; i < dbuf.size(); ++i) CHECK_EQ(dbuf[i], sbuf[i]);
}

// Mismatched size -> passthrough status.
TEST(SurfaceStretch, ConvertDispatchSizeMismatch) {
    std::vector<u16> sbuf, dbuf;
    StretchSurfaceDesc src = Make16(3, 2, sbuf);
    StretchSurfaceDesc dst = Make16(4, 2, dbuf);
    u32 r = BlitConvertDispatch(dst, /*status=*/0x77, src, nullptr);
    CHECK_EQ(r, static_cast<u32>(0x77));
}
