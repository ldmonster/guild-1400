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

// =============================================================================
// WAVE-10 HARDENING — large-scale shift UB + degenerate inputs (ASAN+UBSAN).
//
// The RLE scaled blitter computes `phase = (skip >> (phase + 1)) % scale`. Because
// `phase < scale` and `scale` can be up to 255, `phase + 1` can exceed 31; a bare
// C++ `u32 >> count` with count >= 32 is UB. The original x86 `shr r/m32, cl`
// masks the count to 5 bits — so the faithful translation masks `(phase+1) & 31`.
// These tests drive a large scale with multi-run rows so phase grows large and the
// masked shift path is exercised under UBSAN (which would trap on the unmasked >>).
// =============================================================================

// Build a 2-run-per-row RLE shape so `phase` carries across runs into the shift.
// Layout per row: runCount(u32) then {skip(u32), nPixels(u32), px[nPixels](u16)}.
static std::vector<u8> MakeTwoRunShape(u16 width, u16 height) {
    std::vector<u8> s(0x32, 0);
    std::memcpy(&s[6], &width, 2);
    std::memcpy(&s[0x0A], &height, 2);
    s[0x0C] = 1;                              // depth 1 -> RLE path
    auto putU32 = [&](u32 v) {
        s.push_back((u8)v); s.push_back((u8)(v >> 8));
        s.push_back((u8)(v >> 16)); s.push_back((u8)(v >> 24));
    };
    auto putU16 = [&](u16 v) { s.push_back((u8)v); s.push_back((u8)(v >> 8)); };
    for (int row = 0; row < height; ++row) {
        putU32(2);                            // 2 runs this row
        // run A: a big skip (forces a large phase via skip>>1 % scale) + 1 pixel
        putU32(500); putU32(1); putU16(63488);
        // run B: small skip + 1 pixel (its shift count is the carried large phase+1)
        putU32(2);   putU32(1); putU16(2016);
    }
    return s;
}

// Large scale (64): phase can reach up to 63, so the second run's shift count
// (phase+1) reaches 64 — the masked-shift path. Must not trap UBSAN; must clip
// cleanly and return without OOB on the destination buffer.
TEST(RenderSpriteScale, HardenLargeScaleShiftNoUB) {
    auto shape = MakeTwoRunShape(/*width*/200, /*height*/200);
    u16 buf[64]; std::memset(buf, 0, sizeof(buf));
    ColorBlitTarget16 dst{8, buf};
    // Clip tightly so the down-scaled output stays inside the 8x8 buffer.
    FrameBlitState st = ClipState(0, 8, 0, 8);
    int rv = ShapeBlitRleScaled(0, 0, shape.data(), dst, /*scale*/64, st);
    CHECK_EQ(rv, 1);                          // not fully rejected
    // No assertion on exact pixels (the gold for scale=64 is not the point); the
    // value here is that ASAN/UBSAN exercise the masked shift + dest writes.
    CHECK(true);
}

// An even larger scale (255) with the light-table path, to push phase+1 toward
// 255 (the worst-case shift count) through the remap arm too.
TEST(RenderSpriteScale, HardenMaxScaleLightTableNoUB) {
    auto shape = MakeTwoRunShape(/*width*/255, /*height*/255);
    std::vector<u16> remap(65536, 0);
    remap[63488] = 1; remap[2016] = 2;
    u16 buf[64]; std::memset(buf, 0, sizeof(buf));
    ColorBlitTarget16 dst{8, buf};
    FrameBlitState st = ClipState(0, 8, 0, 8);
    st.remapTable = remap.data();
    int rv = ShapeBlitRleLightTable(0, 0, shape.data(), dst, /*scale*/255, st);
    CHECK_EQ(rv, 1);
    CHECK(true);
}

// ShowFromBankScaled / ShowFromBank null-bank guards (no deref of a null bank).
TEST(RenderSpriteScale, HardenNullBankGuards) {
    ColorBlitTarget16 dst{8, nullptr};
    FrameBlitState st = ClipState(0, 8, 0, 8);
    CHECK_EQ(ShapeShowFromBankScaled(0, 0, nullptr, 0, 1, true, false, dst, st), 0);
    ColorFormat fmt{};
    CHECK_EQ(ShapeShowFromBank(0, 0, nullptr, 0, dst, fmt, st), 0);
}

// VIBE_Render_EncodeSpriteDrawFlags leaf.
TEST(RenderSpriteScale, EncodeSpriteDrawFlags) {
    CHECK_EQ(RenderEncodeSpriteDrawFlags(1, 7), 0);
    CHECK_EQ(RenderEncodeSpriteDrawFlags(0, 0), 0x8000000);
    CHECK_EQ(RenderEncodeSpriteDrawFlags(0, 1), (1 << 22) | 0x8000000);
    CHECK_EQ(RenderEncodeSpriteDrawFlags(2, 3), (3 << 22) | 0x8000000);
}
