#include "test.h"
#include "render/sprite_scale.h"
#include "render/shape_blit.h"
#include "render/animation_decode.h"
#include "render/colorformat.h"
#include <cstring>
#include <vector>

using namespace guild;
using namespace guild::render;

// ---------------------------------------------------------------------------
// Golden vectors computed by /tmp/sprite_gold.py — a byte-faithful Python model
// of the gilde.exe scaled-sprite blitters (verified against the IDA disassembly
// of the run-table walk: countCur=runCur; runCur=countCur+4 per row). Colours:
//   R=63488 G=2016 B=31 Y=65504 W=65535 M=63519 (RGB565).
// ---------------------------------------------------------------------------
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

// 6x3 RLE shape: row0 {[R,G]} row1 {skip1,[B]} row2 {[Y,W,M]}.
static const char* kShape1Hex =
    "000000000000060000000300010000000000000000000000000000000000000000000000000000000000000000000000000001000000000000000200000000f8e0070100000002000000010000001f00010000000000000003000000e0ffffff1ff800000000000000000000000000000000";
// 4x4 RLE shape: every row {[100,200,300,400]}.
static const char* kShape2Hex =
    "00000000000004000000040001000000000000000000000000000000000000000000000000000000000000000000000000000100000000000000040000006400c8002c0190010100000000000000040000006400c8002c0190010100000000000000040000006400c8002c0190010100000000000000040000006400c8002c01900100000000000000000000000000000000";
// 3x2 uncompressed 16bpp block: row0 [R,G,B] row1 [Y,W,M]; depth=2.
static const char* kBlockHex =
    "000000000000030000000200020000000000000000000000000000000000000000000000000000000000000000000000000000f8e0071f00e0ffffff1ff800000000000000000000000000000000";

static FrameBlitState ClipState(int x0, int x1, int y0, int y1) {
    FrameBlitState st;
    st.clipX0 = x0; st.clipX1 = x1; st.clipY0 = y0; st.clipY1 = y1;
    return st;
}

// VIBE_Shape_BlitRleScaled at scale=1 = identity blit (every column/row kept).
TEST(RenderSpriteScale, RleScale1Identity) {
    auto shape = HexToBytes(kShape1Hex);
    u16 buf[48]; std::memset(buf, 0, sizeof(buf));
    ColorBlitTarget16 dst{8, buf};
    FrameBlitState st = ClipState(0, 8, 0, 6);
    int rv = ShapeBlitRleScaled(2, 1, shape.data(), dst, 1, st);
    CHECK_EQ(rv, 1);
    static const u16 gold[48] = {
        0,0,0,0,0,0,0,0, 0,0,63488,2016,0,0,0,0, 0,0,0,31,0,0,0,0,
        0,0,65504,65535,63519,0,0,0, 0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0 };
    for (int i = 0; i < 48; ++i) CHECK_EQ((int)buf[i], (int)gold[i]);
}

// scale=2 down-scales a 4x4 source: only columns 0,2 (100,300) and row 0 survive.
TEST(RenderSpriteScale, RleScale2Downscale) {
    auto shape = HexToBytes(kShape2Hex);
    u16 buf[48]; std::memset(buf, 0, sizeof(buf));
    ColorBlitTarget16 dst{8, buf};
    FrameBlitState st = ClipState(0, 8, 0, 6);
    int rv = ShapeBlitRleScaled(1, 0, shape.data(), dst, 2, st);
    CHECK_EQ(rv, 1);
    static const u16 gold[48] = {
        0,100,300,0,0,0,0,0, 0,100,300,0,0,0,0,0, 0,0,0,0,0,0,0,0,
        0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0 };
    for (int i = 0; i < 48; ++i) CHECK_EQ((int)buf[i], (int)gold[i]);
}

// VIBE_Shape_BlitRleLightTable remaps each opaque pixel through st.remapTable.
TEST(RenderSpriteScale, RleLightTableRemap) {
    auto shape = HexToBytes(kShape1Hex);
    std::vector<u16> remap(65536, 0);
    for (u16 v : {63488, 2016, 31, 65504, 65535, 63519})
        remap[v] = (u16)((v + 1000) & 0xFFFF);
    u16 buf[48]; std::memset(buf, 0, sizeof(buf));
    ColorBlitTarget16 dst{8, buf};
    FrameBlitState st = ClipState(0, 8, 0, 6);
    st.remapTable = remap.data();
    int rv = ShapeBlitRleLightTable(2, 1, shape.data(), dst, 1, st);
    CHECK_EQ(rv, 1);
    static const u16 gold[48] = {
        0,0,0,0,0,0,0,0, 0,0,64488,3016,0,0,0,0, 0,0,0,1031,0,0,0,0,
        0,0,968,999,64519,0,0,0, 0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0 };
    for (int i = 0; i < 48; ++i) CHECK_EQ((int)buf[i], (int)gold[i]);
}

// Bottom-clip: clipY1=3, y=1 -> rowLimit = 1*(3-1) = 2, so the last source row drops.
TEST(RenderSpriteScale, RleBottomClip) {
    auto shape = HexToBytes(kShape1Hex);
    u16 buf[48]; std::memset(buf, 0, sizeof(buf));
    ColorBlitTarget16 dst{8, buf};
    FrameBlitState st = ClipState(0, 8, 0, 3);
    int rv = ShapeBlitRleScaled(2, 1, shape.data(), dst, 1, st);
    CHECK_EQ(rv, 1);
    static const u16 gold[48] = {
        0,0,0,0,0,0,0,0, 0,0,63488,2016,0,0,0,0, 0,0,0,31,0,0,0,0,
        0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0 };
    for (int i = 0; i < 48; ++i) CHECK_EQ((int)buf[i], (int)gold[i]);
}

// Full reject: height+y < clipY0 -> returns 0 and writes nothing.
TEST(RenderSpriteScale, RleRejectAboveClip) {
    auto shape = HexToBytes(kShape1Hex);
    u16 buf[48]; std::memset(buf, 0, sizeof(buf));
    ColorBlitTarget16 dst{8, buf};
    FrameBlitState st = ClipState(0, 8, 5, 6);
    int rv = ShapeBlitRleScaled(2, 1, shape.data(), dst, 1, st);
    CHECK_EQ(rv, 0);
    for (int i = 0; i < 48; ++i) CHECK_EQ((int)buf[i], 0);
}

// VIBE_Shape_BlitScaled16: 1.0 step (0x100) copies the 3x2 block verbatim.
TEST(RenderSpriteScale, Scaled16Identity) {
    auto blk = HexToBytes(kBlockHex);
    u16 buf[24]; std::memset(buf, 0, sizeof(buf));
    ColorBlitTarget16 dst{6, buf};
    ShapeBlitScaled16(1, 1, blk.data(), dst, 0x100, 0x100, /*useColorKey=*/false);
    static const u16 gold[24] = {
        0,0,0,0,0,0, 0,63488,2016,31,0,0, 0,65504,65535,63519,0,0, 0,0,0,0,0,0 };
    for (int i = 0; i < 24; ++i) CHECK_EQ((int)buf[i], (int)gold[i]);
}

// Color-key path (no zero pixels here -> same as identity but written at origin).
TEST(RenderSpriteScale, Scaled16ColorKey) {
    auto blk = HexToBytes(kBlockHex);
    u16 buf[24]; std::memset(buf, 0, sizeof(buf));
    ColorBlitTarget16 dst{6, buf};
    ShapeBlitScaled16(0, 0, blk.data(), dst, 0x100, 0x100, /*useColorKey=*/true);
    static const u16 gold[24] = {
        63488,2016,31,0,0,0, 65504,65535,63519,0,0,0, 0,0,0,0,0,0, 0,0,0,0,0,0 };
    for (int i = 0; i < 24; ++i) CHECK_EQ((int)buf[i], (int)gold[i]);
}

// Color-key leaves the destination pixel untouched where the source is 0.
TEST(RenderSpriteScale, Scaled16ColorKeyTransparent) {
    // 2x1 block: [0, R]; depth=2.
    std::vector<u8> blk(0x32, 0);
    u16 w = 2, h = 1; std::memcpy(&blk[6], &w, 2); std::memcpy(&blk[0x0A], &h, 2);
    blk[0x0C] = 2;
    u16 z = 0, R = 63488;
    blk.push_back((u8)z); blk.push_back((u8)(z >> 8));
    blk.push_back((u8)R); blk.push_back((u8)(R >> 8));
    u16 buf[2] = {111, 222};
    ColorBlitTarget16 dst{2, buf};
    ShapeBlitScaled16(0, 0, blk.data(), dst, 0x100, 0x100, /*useColorKey=*/true);
    CHECK_EQ((int)buf[0], 111);   // source 0 -> destination preserved
    CHECK_EQ((int)buf[1], 63488); // source R -> written
}

// VIBE_Render_EncodeSpriteDrawFlags leaf.
TEST(RenderSpriteScale, EncodeSpriteDrawFlags) {
    CHECK_EQ(RenderEncodeSpriteDrawFlags(1, 7), 0);
    CHECK_EQ(RenderEncodeSpriteDrawFlags(0, 0), 0x8000000);
    CHECK_EQ(RenderEncodeSpriteDrawFlags(0, 1), (1 << 22) | 0x8000000);
    CHECK_EQ(RenderEncodeSpriteDrawFlags(2, 3), (3 << 22) | 0x8000000);
}
