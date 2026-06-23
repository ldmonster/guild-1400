#include "render/scene_recon5_blit.h"
#include "test.h"
#include <vector>
#include <cstdint>

using namespace guild::render;

// Build RGB565-style LUTs: R -> bits[11..15], G -> bits[5..10], B -> bits[0..4].
static void buildLuts(std::vector<guild::u32>& r, std::vector<guild::u32>& g,
                      std::vector<guild::u32>& b) {
    r.resize(256); g.resize(256); b.resize(256);
    for (int i = 0; i < 256; ++i) {
        r[i] = (guild::u32)((i >> 3) << 11);   // 5-bit R in high bits
        g[i] = (guild::u32)((i >> 2) << 5);    // 6-bit G
        b[i] = (guild::u32)(i >> 3);           // 5-bit B
    }
}

// --- Fast (point-sample) path: all four taps equal --------------------------
TEST(SceneRecon5, ScaleBlitFastPath) {
    std::vector<guild::u32> rl, gl, bl;
    buildLuts(rl, gl, bl);
    ChannelLuts luts{rl.data(), gl.data(), bl.data()};

    // 2x2 source of RGB triples (3 bytes each), one row, point-sampled.
    // srcColStride = 3 bytes (advance one triple per output column).
    guild::u8 src[2 * 3] = {
        /*px0*/ 248, 252, 248,    // R=248,G=252,B=248 -> full bits
        /*px1*/ 0,   0,   0,
    };
    BlitStrides strides{};
    strides.srcColStride = 3;       // per-tap advance
    strides.weightAdvance = 0;      // single row, no row advance needed
    strides.dstRowStride = 2 * 2;   // bytes per dst row (2 px)
    strides.srcRowStride = 0;

    BlitTaps taps{};
    taps.tap[0] = taps.tap[1] = taps.tap[2] = taps.tap[3] = src; // all equal => fast
    // dim=2 => 2 rows x 2 cols. weightAdvance=0 so row 2 re-reads the same src row;
    // dst advances dstRowStride(4 bytes = 2 px) per row -> dst[0..1] then dst[2..3].
    guild::u16 dst2[4] = {0, 0, 0, 0};
    ScaleBlitMip(dst2, nullptr, 2, taps, luts, strides);
    // px0: R565(248) = (248>>3)<<11 | (252>>2)<<5 | (248>>3)
    guild::u16 exp0 = (guild::u16)(((248 >> 3) << 11) + ((252 >> 2) << 5) + (248 >> 3));
    CHECK_EQ(dst2[0], exp0);
    CHECK_EQ(dst2[1], (guild::u16)0);   // black px -> 0
    CHECK_EQ(dst2[2], exp0);            // row 2 identical (weightAdvance=0)
    CHECK_EQ(dst2[3], (guild::u16)0);
}

// --- General bilinear path: distinct taps, explicit weights -----------------
TEST(SceneRecon5, ScaleBlitBilinear) {
    std::vector<guild::u32> rl, gl, bl;
    buildLuts(rl, gl, bl);
    ChannelLuts luts{rl.data(), gl.data(), bl.data()};

    // Four distinct 1-pixel taps (3 bytes each). Weight quad = (64,64,64,64)
    // packed little-endian = 0x40404040 -> each weight 64, sum*64 then >>8.
    guild::u8 t0[3] = {200, 200, 200};
    guild::u8 t1[3] = {100, 100, 100};
    guild::u8 t2[3] = {40,  40,  40};
    guild::u8 t3[3] = {16,  16,  16};
    guild::u32 weight = 0x40404040u;   // w0=w1=w2=w3=64

    BlitStrides strides{};
    strides.srcColStride = 0;     // single output px -> no advance needed
    strides.weightAdvance = 0;
    strides.dstRowStride = 2;
    strides.srcRowStride = 0;

    guild::u16 dst[1] = {0};
    BlitTaps taps{};
    taps.tap[0] = t0; taps.tap[1] = t1; taps.tap[2] = t2; taps.tap[3] = t3;
    ScaleBlitMip(dst, &weight, /*dim*/1, taps, luts, strides);

    // each channel = (64*200 + 64*100 + 64*40 + 64*16) >> 8
    //              = (64*(200+100+40+16)) >> 8 = (64*356)>>8 = 22784>>8 = 89
    int c = (64 * (200 + 100 + 40 + 16)) >> 8;   // = 89
    CHECK_EQ(c, 89);
    guild::u16 exp = (guild::u16)(((89 >> 3) << 11) + ((89 >> 2) << 5) + (89 >> 3));
    CHECK_EQ(dst[0], exp);
}

// --- General path: zero weights produce a black pixel -----------------------
TEST(SceneRecon5, ScaleBlitZeroWeights) {
    std::vector<guild::u32> rl, gl, bl;
    buildLuts(rl, gl, bl);
    ChannelLuts luts{rl.data(), gl.data(), bl.data()};
    // four DISTINCT full RGB triples (so the general path is taken), each 3 bytes.
    guild::u8 t0[3] = {255, 255, 255};
    guild::u8 t1[3] = {200, 200, 200};
    guild::u8 t2[3] = {100, 100, 100};
    guild::u8 t3[3] = {50, 50, 50};
    guild::u32 weight = 0;     // all weights 0
    BlitStrides strides{};
    strides.dstRowStride = 2;
    guild::u16 dst[1] = {0xFFFF};
    BlitTaps taps{};
    taps.tap[0] = t0; taps.tap[1] = t1; taps.tap[2] = t2; taps.tap[3] = t3;
    // taps not all-equal -> general path; weights 0 -> channels 0 -> px 0.
    ScaleBlitMip(dst, &weight, 1, taps, luts, strides);
    CHECK_EQ(dst[0], (guild::u16)0);
}
