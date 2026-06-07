#include "render/surface_stretch.h"
#include "render/shape_blit.h"
#include "tests/framework/test.h"

#include <cstdint>
#include <vector>

using namespace guild::render;
using guild::u8;
using guild::u16;
using guild::u32;

namespace {

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
StretchSurfaceDesc Make8(int w, int h, std::vector<u8>& buf) {
    buf.assign(static_cast<size_t>(w) * h, 0);
    StretchSurfaceDesc d;
    d.width = w; d.height = h; d.pitch = w;
    d.pixels = buf.data();
    d.bpp = 8; d.indexed = true;
    return d;
}

} // namespace

// End-to-end: an 8bpp indexed source is converted to native 16bpp through a
// palette (BlitConvertDispatch -> Convert8To16Indexed), exactly mirroring the
// renderer's "grab indexed surface, encode to display format" path.
TEST(SurfaceStretchE2E, IndexedToNative16ViaDispatch) {
    std::vector<u8> sbuf;
    std::vector<u16> dbuf;
    StretchSurfaceDesc src = Make8(2, 2, sbuf);
    StretchSurfaceDesc dst = Make16(2, 2, dbuf);

    // 4 palette entries (R,G,B,X) — index i used at pixel i.
    u8 pal[256 * 4] = {0};
    auto setPal = [&](int i, u8 r, u8 g, u8 b){ pal[i*4]=r; pal[i*4+1]=g; pal[i*4+2]=b; };
    setPal(0, 255, 0, 0);     // red
    setPal(1, 0, 255, 0);     // green
    setPal(2, 0, 0, 255);     // blue
    setPal(3, 255, 255, 255); // white
    sbuf = {0, 1, 2, 3};
    src.pixels = sbuf.data();

    u32 r = BlitConvertDispatch(dst, /*status=*/0, src, pal);
    // Convert8To16Indexed returns the last packed pixel (white index 3 -> 0xFFFF).
    CHECK_EQ(r, static_cast<u32>(0xFFFF));
    // Verify the packed pixels: red, green, blue, white in 565.
    // red:  R=255>>3<<11 = 0xF800 ; green: G=255>>2<<5 = 0x07E0 ; blue: 0x001F ; white 0xFFFF
    CHECK_EQ(dbuf[0], static_cast<u16>(0xF800));
    CHECK_EQ(dbuf[1], static_cast<u16>(0x07E0));
    CHECK_EQ(dbuf[2], static_cast<u16>(0x001F));
    CHECK_EQ(dbuf[3], static_cast<u16>(0xFFFF));
}

// End-to-end grab+resample: render a gradient into a 16bpp "back buffer", grab a
// 160x120 thumbnail region into a packed buffer (BlitThumbnailToSurface is the
// reverse — copy a packed thumb back into a surface), then box-average down to a
// smaller preview and confirm a uniform region averages to itself.
TEST(SurfaceStretchE2E, ThumbnailThenAverageDownscale) {
    // packed 160x120 thumbnail, a single colour.
    const u16 c = (9u<<11)|(18u<<5)|4u;
    std::vector<u16> thumb(160 * 120, c);

    // destination surface 200-wide.
    const int strideP = 200;
    std::vector<u16> surf(static_cast<size_t>(strideP) * 130, 0);
    int rows = BlitThumbnailToSurface(0, 0, surf.data(), strideP, thumb.data());
    CHECK_EQ(rows, 120);

    // Wrap the 160x120 written region as a stretch source (pitch = strideP*2 bytes)
    StretchSurfaceDesc src;
    src.width = 160; src.height = 120; src.pitch = strideP * 2;
    src.pixels = reinterpret_cast<u8*>(surf.data());
    src.bpp = 16; src.indexed = false;
    src.rMask = kR; src.gMask = kG; src.bMask = kB;

    std::vector<u16> dbuf;
    StretchSurfaceDesc dst = Make16(80, 60, dbuf);
    StretchAverage16(dst, src);
    // Uniform source -> every averaged output equals the source colour.
    for (auto p : dbuf) CHECK_EQ(p, c);
}

// End-to-end dispatch chain: equal-size copy then 8bpp downscale, verifying the
// dispatcher's size-relative routing end to end.
TEST(SurfaceStretchE2E, DispatchRoutingChain) {
    // 1) equal-size 8bpp copy
    std::vector<u8> a, b;
    StretchSurfaceDesc src = Make8(4, 4, a);
    StretchSurfaceDesc mid = Make8(4, 4, b);
    for (int i = 0; i < 16; ++i) a[i] = static_cast<u8>(i + 1);
    u8 st = StretchSurfaceDispatch(mid, src);
    CHECK_EQ(st, static_cast<u8>(4));
    for (int i = 0; i < 16; ++i) CHECK_EQ(b[i], a[i]);

    // 2) downscale the copy 4x4 -> 2x2
    std::vector<u8> c;
    StretchSurfaceDesc out = Make8(2, 2, c);
    StretchSurfaceDispatch(out, mid);
    CHECK_EQ(c[0], static_cast<u8>(1));  // src(0,0)
    CHECK_EQ(c[1], static_cast<u8>(3));  // src(0,2)
    CHECK_EQ(c[2], static_cast<u8>(9));  // src(2,0)
    CHECK_EQ(c[3], static_cast<u8>(11)); // src(2,2)
}
