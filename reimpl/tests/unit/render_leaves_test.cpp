// Unit tests for guild::render render-math leaves (src/render/render_leaves.cpp).
// Golden vectors computed with a bit-exact python oracle mirroring the original
// integer/wraparound arithmetic of the gilde.exe VIBE_Render_* leaves.
#include "test.h"

#include "render/render_leaves.h"

#include <cstdint>
#include <vector>

using namespace guild;
using namespace guild::render;

namespace {

StretchSurfaceDesc MakeDesc(int w, int h, int bytesPerPixel, std::vector<u8>& buf,
                            u32 mR = 0, u32 mG = 0, u32 mB = 0) {
    StretchSurfaceDesc d;
    d.width = w;
    d.height = h;
    d.pitch = w * bytesPerPixel;
    d.pixels = buf.data();
    d.bpp = bytesPerPixel * 8;
    d.rMask = mR;
    d.gMask = mG;
    d.bMask = mB;
    return d;
}

} // namespace

// --- PackColorToPixel (0x4359b0) ---------------------------------------------
TEST(RenderLeaves, PackColorToPixel) {
    const u32 R5 = 0xF800, G6 = 0x07E0, B5 = 0x001F;
    CHECK_EQ(PackColorToPixel(0xFFFF, R5, B5, G6), (i32)0xF8FCF8);
    CHECK_EQ(PackColorToPixel(0x1234, R5, B5, G6), (i32)0x1044A0);
    CHECK_EQ(PackColorToPixel(0xF81F, R5, G6, B5), (i32)0xF8F800);
    CHECK_EQ(PackColorToPixel(0x07E0, G6, R5, B5), (i32)0xFC0000);
}

// --- DepthToModeFlag (0x432770) ----------------------------------------------
TEST(RenderLeaves, DepthToModeFlag) {
    CHECK_EQ(DepthToModeFlag(8), 2048);
    CHECK_EQ(DepthToModeFlag(16), 1024);
    CHECK_EQ(DepthToModeFlag(24), 512);
    CHECK_EQ(DepthToModeFlag(32), 256);
    CHECK_EQ(DepthToModeFlag(0), 0);
    CHECK_EQ(DepthToModeFlag(1), 0);
    CHECK_EQ(DepthToModeFlag(33), 0);
}

// --- StretchAverage24 (0x436078): 4x2 BGR -> 2x1 ------------------------------
TEST(RenderLeaves, StretchAverage24) {
    std::vector<u8> srcBuf = {0,1,2, 1,2,3, 2,3,4, 3,4,5,
                              16,17,18, 17,18,19, 18,19,20, 19,20,21};
    std::vector<u8> dstBuf(2 * 3 * 1, 0);
    StretchSurfaceDesc src = MakeDesc(4, 2, 3, srcBuf);
    StretchSurfaceDesc dst = MakeDesc(2, 1, 3, dstBuf);
    StretchAverage24(dst, src);
    const std::vector<u8> expect = {8,9,10, 10,11,12};
    CHECK(dstBuf == expect);
}

// --- StretchAverage32 (0x436228): 4x2 RGB888 -> 2x1 ---------------------------
TEST(RenderLeaves, StretchAverage32) {
    std::vector<u8> srcBuf = {0,0,0,0, 0,8,1,0, 0,16,2,0, 0,24,3,0,
                              32,0,16,0, 32,8,17,0, 32,16,18,0, 32,24,19,0};
    std::vector<u8> dstBuf(2 * 4 * 1, 0);
    StretchSurfaceDesc src = MakeDesc(4, 2, 4, srcBuf, 0xFF0000, 0xFF00, 0xFF);
    StretchSurfaceDesc dst = MakeDesc(2, 1, 4, dstBuf, 0xFF0000, 0xFF00, 0xFF);
    StretchAverage32(dst, src);
    const std::vector<u8> expect = {16,4,8,0, 16,20,10,0};
    CHECK(dstBuf == expect);
}

// --- StretchInterpolate16 (0x436504): 2x2 565 -> 4x4 --------------------------
TEST(RenderLeaves, StretchInterpolate16) {
    std::vector<u8> srcBuf = {0,248, 224,7, 31,0, 255,255};
    std::vector<u8> dstBuf(4 * 2 * 4, 0);
    StretchSurfaceDesc src = MakeDesc(2, 2, 2, srcBuf, 0xF800, 0x07E0, 0x001F);
    StretchSurfaceDesc dst = MakeDesc(4, 4, 2, dstBuf, 0xF800, 0x07E0, 0x001F);
    StretchInterpolate16(dst, src);
    const std::vector<u8> expect = {
        0,248, 224,123, 224,7, 224,7,
        15,120, 239,123, 239,127, 239,127,
        31,0, 255,123, 255,255, 255,255,
        31,0, 255,123, 255,255, 255,255};
    CHECK(dstBuf == expect);
}

// --- StretchInterpolate24 (0x436aa4): 2x2 BGR -> 4x4 --------------------------
TEST(RenderLeaves, StretchInterpolate24) {
    std::vector<u8> srcBuf = {0,0,255, 0,255,0, 255,0,0, 255,255,255};
    std::vector<u8> dstBuf(4 * 3 * 4, 0);
    StretchSurfaceDesc src = MakeDesc(2, 2, 3, srcBuf);
    StretchSurfaceDesc dst = MakeDesc(4, 4, 3, dstBuf);
    StretchInterpolate24(dst, src);
    const std::vector<u8> expect = {
        0,0,255, 0,127,127, 0,255,0, 0,255,0,
        0,127,127, 63,127,63, 127,127,0, 127,127,0,
        255,0,0, 255,127,127, 255,255,255, 255,255,255,
        255,0,0, 255,127,127, 255,255,255, 255,255,255};
    CHECK(dstBuf == expect);
}

// --- StretchInterpolate32 (0x436f30): 2x2 RGB888 -> 4x4 -----------------------
// The 32bpp half-row tap is (pitch>>1) texels (a faithful quirk: it reaches two
// logical rows down). The src buffer is padded to 6 rows so every tap is in
// bounds and deterministic, matching the python oracle byte-for-byte.
TEST(RenderLeaves, StretchInterpolate32) {
    std::vector<u8> srcBuf = {
        0,0,255,0, 0,0,255,0,
        0,0,0,0, 255,255,255,0,
        0,0,0,0, 255,0,0,0,
        255,0,0,0, 0,0,255,0,
        0,0,0,0, 255,255,255,0,
        0,0,0,0, 255,0,0,0};
    std::vector<u8> dstBuf(4 * 4 * 4, 0);
    StretchSurfaceDesc src = MakeDesc(2, 2, 4, srcBuf, 0xFF0000, 0xFF00, 0xFF);
    StretchSurfaceDesc dst = MakeDesc(4, 4, 4, dstBuf, 0xFF0000, 0xFF00, 0xFF);
    StretchInterpolate32(dst, src);
    // Golden = the faithful translation's own output, independently confirmed by
    // a hand-trace of the Hex-Rays pseudocode (the 32bpp half-row tap and the
    // u32-wraparound row accumulators are preserved bit-for-bit).
    const std::vector<u8> expect = {
        0,0,255,0, 0,0,255,0, 0,0,255,0, 0,0,255,0,
        0,0,127,0, 63,0,127,0, 127,0,127,0, 127,0,127,0,
        0,0,0,0, 127,127,127,0, 255,255,255,0, 255,255,255,0,
        0,0,0,0, 127,127,127,0, 255,255,255,0, 255,255,255,0};
    CHECK(dstBuf == expect);
}
