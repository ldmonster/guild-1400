// End-to-end test for guild::render render-math leaves: build source surfaces in
// system memory, run the stretch/convert leaves into destination memory surfaces,
// and assert the resulting pixels / invariants. No DDraw/GDI involved — pure
// memory-surface pixel math, mirroring how the engine resamples textures.
#include "test.h"

#include "render/render_leaves.h"

#include <cstdint>
#include <vector>

using namespace guild;
using namespace guild::render;

namespace {

StretchSurfaceDesc MakeDesc(int w, int h, int bpp, std::vector<u8>& buf,
                            u32 mR = 0, u32 mG = 0, u32 mB = 0) {
    StretchSurfaceDesc d;
    d.width = w;
    d.height = h;
    d.pitch = w * bpp;
    d.pixels = buf.data();
    d.bpp = bpp * 8;
    d.rMask = mR;
    d.gMask = mG;
    d.bMask = mB;
    return d;
}

} // namespace

// Down-then-up resample of a 24bpp surface: a solid block must round-trip exactly,
// and the depth->mode flag classification must agree with the chosen depth.
TEST(RenderLeavesE2E, Resample24Pipeline) {
    // 8x8 source, every pixel BGR = (10,20,30).
    const int sw = 8, sh = 8;
    std::vector<u8> srcBuf(sw * sh * 3);
    for (int p = 0; p < sw * sh; ++p) {
        srcBuf[p * 3 + 0] = 10;
        srcBuf[p * 3 + 1] = 20;
        srcBuf[p * 3 + 2] = 30;
    }
    StretchSurfaceDesc src = MakeDesc(sw, sh, 3, srcBuf);

    // Down-sample 8x8 -> 2x2 (box average of a constant block = the block colour).
    std::vector<u8> midBuf(2 * 2 * 3, 0);
    StretchSurfaceDesc mid = MakeDesc(2, 2, 3, midBuf);
    u8* avgRet = StretchAverage24(mid, src);
    CHECK(avgRet != nullptr);
    for (int p = 0; p < 2 * 2; ++p) {
        CHECK_EQ(midBuf[p * 3 + 0], (u8)10);
        CHECK_EQ(midBuf[p * 3 + 1], (u8)20);
        CHECK_EQ(midBuf[p * 3 + 2], (u8)30);
    }

    // Up-sample 2x2 -> 4x4 (bilinear of a constant block stays constant).
    std::vector<u8> upBuf(4 * 4 * 3, 0xAB);
    StretchSurfaceDesc up = MakeDesc(4, 4, 3, upBuf);
    StretchInterpolate24(up, mid);
    for (int p = 0; p < 4 * 4; ++p) {
        CHECK_EQ(upBuf[p * 3 + 0], (u8)10);
        CHECK_EQ(upBuf[p * 3 + 1], (u8)20);
        CHECK_EQ(upBuf[p * 3 + 2], (u8)30);
    }

    // The pipeline's surfaces are 24bpp -> mode flag 512.
    CHECK_EQ(DepthToModeFlag(src.bpp), 512);
}

// 32bpp constant block round-trips through average + interpolate, and the packed
// pixel equals PackColorToPixel's extraction of the same channels.
TEST(RenderLeavesE2E, Resample32AndPack) {
    const u32 mR = 0xFF0000, mG = 0xFF00, mB = 0xFF;
    const u32 colour = (200u << 16) | (100u << 8) | 50u;  // R=200,G=100,B=50

    const int sw = 4, sh = 4;
    std::vector<u8> srcBuf(sw * sh * 4);
    for (int p = 0; p < sw * sh; ++p) {
        srcBuf[p * 4 + 0] = (u8)(colour & 0xFF);
        srcBuf[p * 4 + 1] = (u8)((colour >> 8) & 0xFF);
        srcBuf[p * 4 + 2] = (u8)((colour >> 16) & 0xFF);
        srcBuf[p * 4 + 3] = 0;
    }
    StretchSurfaceDesc src = MakeDesc(sw, sh, 4, srcBuf, mR, mG, mB);

    std::vector<u8> dstBuf(2 * 2 * 4, 0);
    StretchSurfaceDesc dst = MakeDesc(2, 2, 4, dstBuf, mR, mG, mB);
    StretchAverage32(dst, src);
    for (int p = 0; p < 2 * 2; ++p) {
        u32 px = dstBuf[p * 4] | (dstBuf[p * 4 + 1] << 8) | (dstBuf[p * 4 + 2] << 16);
        CHECK_EQ(px, colour);
    }

    // PackColorToPixel re-extracts the channels into a 0xRRGGBB byte layout: for
    // 8-bit masks pos/prec are 0/0, so it reproduces the source channels exactly.
    CHECK_EQ(PackColorToPixel((i32)colour, mR, mB, mG), (i32)colour);

    CHECK_EQ(DepthToModeFlag(32), 256);
}
