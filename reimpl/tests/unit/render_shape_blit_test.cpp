#include "test.h"
#include "render/shape_blit.h"
#include "render/animation_decode.h"
#include "render/colorformat.h"
#include "render/shape.h"
#include <cstring>
#include <vector>

using namespace guild;
using namespace guild::render;

// ---------------------------------------------------------------------------
// Golden shape (computed by /tmp/gold.py — see report). 3 RLE rows blitted at
// (x=2, y=1) into an 8x6 surface. Hex of the shape blob and the expected pixel
// images are reproduced here as the golden vectors.
// ---------------------------------------------------------------------------
static const char* kShapeHex =
    "000000000000000000000300000000000000000000000000000000000000000000000000000000000000000000000000000001000000000000000200000000f8e0070200000002000000010000001f000000000001000000e0ff010000000400000001000000ffff";

static std::vector<u8> HexToBytes(const char* h) {
    std::vector<u8> out;
    for (const char* p = h; p[0] && p[1]; p += 2) {
        auto nyb = [](char c) -> int {
            if (c >= '0' && c <= '9') return c - '0';
            if (c >= 'a' && c <= 'f') return c - 'a' + 10;
            return c - 'A' + 10;
        };
        out.push_back((u8)((nyb(p[0]) << 4) | nyb(p[1])));
    }
    return out;
}

static const u16 kGoldShape[48] = {
    0,0,0,0,0,0,0,0, 0,0,63488,2016,0,0,0,0, 0,0,0,31,65504,0,0,0,
    0,0,0,0,65535,0,0,0, 0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0
};
static const u16 kGoldColored[48] = {
    0,0,0,0,0,0,0,0, 0,0,12678,12678,0,0,0,0, 0,0,0,38034,25388,0,0,0,
    0,0,0,0,63422,0,0,0, 0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0
};

TEST(RenderShapeBlit, DecodeRleGolden) {
    std::vector<u8> shape = HexToBytes(kShapeHex);
    u16 buf[48];
    std::memset(buf, 0, sizeof(buf));
    BlitTarget16 dst{8, buf};
    ShapeDecodeRle(2, 1, shape.data(), dst);
    for (int i = 0; i < 48; ++i)
        CHECK_EQ((int)buf[i], (int)kGoldShape[i]);
}

TEST(RenderShapeBlit, BlitColored16Golden) {
    std::vector<u8> shape = HexToBytes(kShapeHex);
    u16 buf[48];
    std::memset(buf, 0, sizeof(buf));
    ColorBlitTarget16 dst{8, buf};
    u16 h = ShapeBlitColored16(2, 1, shape.data(), dst, Format565());
    CHECK_EQ((int)h, 3);   // returns the shape height (3 RLE rows)
    for (int i = 0; i < 48; ++i)
        CHECK_EQ((int)buf[i], (int)kGoldColored[i]);
}

// Per-pixel luma golden values from gold.py:
//   p=63488(R)->L=49, p=2016(G)->L=50, p=31(B)->L=146, p=65504(Y)->L=100,
//   p=65535(W)->L=246. (Original weights: r*0.2 + b*0.59 + g*0.2, truncated.)
TEST(RenderShapeBlit, ColoredLumaSinglePixels) {
    struct Case { u16 src; u16 expect; };
    Case cases[] = {
        {63488, 12678}, {2016, 12678}, {31, 38034}, {65504, 25388}, {65535, 63422},
    };
    for (auto& c : cases) {
        // one-row, one-run, one-pixel shape
        std::vector<u8> shape(0x32, 0);
        u16 height = 1;
        std::memcpy(shape.data() + 0x0A, &height, 2);
        u32 runCount = 1; for (int i = 0; i < 4; ++i) shape.push_back((u8)(runCount >> (8*i)));
        u32 skip = 0;      for (int i = 0; i < 4; ++i) shape.push_back((u8)(skip >> (8*i)));
        u32 npix = 1;      for (int i = 0; i < 4; ++i) shape.push_back((u8)(npix >> (8*i)));
        shape.push_back((u8)c.src); shape.push_back((u8)(c.src >> 8));
        u16 buf[4] = {0,0,0,0};
        ColorBlitTarget16 dst{2, buf};
        ShapeBlitColored16(0, 0, shape.data(), dst, Format565());
        CHECK_EQ((int)buf[0], (int)c.expect);
    }
}

// ---------------------------------------------------------------------------
// Convert8To16Indexed golden (gold.py): palette[1..4] = R,G,B,(200,100,50);
// src indices {1,2,3,4,0,1} -> {63488,2016,31,52006,0,63488}.
// ---------------------------------------------------------------------------
TEST(RenderShapeBlit, Convert8To16IndexedGolden) {
    u8 pal[256 * 4] = {0};
    auto setp = [&](int i, u8 r, u8 g, u8 b) { pal[4*i]=r; pal[4*i+1]=g; pal[4*i+2]=b; };
    setp(1,255,0,0); setp(2,0,255,0); setp(3,0,0,255); setp(4,200,100,50);

    u8 src[6] = {1,2,3,4,0,1};
    u16 dst[6] = {0};
    ConvertSurf8 s{3, 3, 2, src};
    ConvertSurf16 d{3, 0, 0xF800, 0x07E0, 0x001F, dst};
    Convert8To16Indexed(d, s, pal);

    u16 expect[6] = {63488, 2016, 31, 52006, 0, 63488};
    for (int i = 0; i < 6; ++i)
        CHECK_EQ((int)dst[i], (int)expect[i]);
}

// ---------------------------------------------------------------------------
// FrameDataInterpolate (uncompressed block) golden (gold.py). Frame 3x2 mode 0,
// pixels {R,0,G, B,Y,0}, blitted at (1,1) into a 6x4 surface; transparent (0) is
// skipped. FB = unclipped, FBC = X-clipped to [2,5).
// ---------------------------------------------------------------------------
static const char* kFrameHex =
    "0000000000000300000002000000000000000000000000000000000000000000000000000000"
    "ffffffff000000000000000000f80000e0071f00e0ff0000";

TEST(RenderAnimDecode, FrameInterpolateUnclipped) {
    std::vector<u8> frame = HexToBytes(kFrameHex);
    u16 buf[24]; std::memset(buf, 0, sizeof(buf));
    FrameBlitState st;
    st.dest = buf; st.destStridePx = 6;
    st.clipX0 = 0; st.clipY0 = 0; st.clipX1 = 6; st.clipY1 = 4;
    u16 h = FrameDataInterpolate(1, 1, frame.data(), st);
    CHECK_EQ((int)h, 2);
    u16 fb[24] = {0,0,0,0,0,0, 0,63488,0,2016,0,0, 0,31,65504,0,0,0, 0,0,0,0,0,0};
    for (int i = 0; i < 24; ++i) CHECK_EQ((int)buf[i], (int)fb[i]);
}

TEST(RenderAnimDecode, FrameInterpolateXClipped) {
    std::vector<u8> frame = HexToBytes(kFrameHex);
    u16 buf[24]; std::memset(buf, 0, sizeof(buf));
    FrameBlitState st;
    st.dest = buf; st.destStridePx = 6;
    st.clipX0 = 2; st.clipY0 = 0; st.clipX1 = 5; st.clipY1 = 4;
    FrameDataInterpolate(1, 1, frame.data(), st);
    u16 fbc[24] = {0,0,0,0,0,0, 0,0,0,2016,0,0, 0,0,65504,0,0,0, 0,0,0,0,0,0};
    for (int i = 0; i < 24; ++i) CHECK_EQ((int)buf[i], (int)fbc[i]);
}

// AnimationBasic out-of-range / null-bank guards (0x5D85B8).
TEST(RenderAnimDecode, AnimationBasicGuards) {
    FrameBlitState st;
    CHECK_EQ(AnimationBasic(0, 0, nullptr, 0, st), 0);   // null bank
    // bank with shapeCount(+0x2A) = 0; asking shape n=5 (> count) returns 0.
    std::vector<u8> bank(0x100, 0);
    u16 count = 0; std::memcpy(bank.data() + 0x2A, &count, 2);
    CHECK_EQ(AnimationBasic(0, 0, bank.data(), 5, st), 0);
}
