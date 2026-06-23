#include "tests/framework/test.h"
#include "render/shape_recon_cluster.h"

#include <cstring>
#include <vector>

using namespace guild;
using namespace guild::render;

// Identity colour state: all shift amounts zero. Then ColorPack(r,g,b) =
//   (g) | (r) | (b)  == r|g|b, and ColorUnpack reproduces r=g=b=packed (since
//   each channel is the full value shifted by 0). We exercise this identity.

// ---- ColorPack / ColorUnpack (0x434F30 / 0x434F7C) --------------------------
TEST(ShapeReconColor, PackIdentityIsBitwiseOr) {
    ShapeColorState st;  // all zero shifts
    // r|g|b with no shifts.
    CHECK_EQ((int)ColorPack(st, 0x01, 0x02, 0x04), 0x07);
    CHECK_EQ((int)ColorPack(st, 0x10, 0x20, 0x40), 0x70);
    CHECK_EQ((int)ColorPack(st, 0xFF, 0x00, 0x00), 0xFF);
}

TEST(ShapeReconColor, UnpackIdentity) {
    ShapeColorState st;
    u8 r, g, b;
    ColorUnpack(st, 0x55, r, g, b);
    // With zero shifts every channel == packed value (low byte).
    CHECK_EQ((int)r, 0x55);
    CHECK_EQ((int)g, 0x55);
    CHECK_EQ((int)b, 0x55);
}

TEST(ShapeReconColor, NotEqualRgbAndSetRgb) {
    u8 a[3] = {1, 2, 3}, b[3] = {1, 2, 3}, c[3] = {1, 9, 3};
    CHECK(!ColorNotEqualRgb(a, b));
    CHECK(ColorNotEqualRgb(a, c));
    u8 dst[3] = {0, 0, 0};
    // SetRgb stores {r, b, g} (dl, bl, cl order).
    ColorSetRgb(dst, 0x11, 0x22, 0x33);
    CHECK_EQ((int)dst[0], 0x11);  // r
    CHECK_EQ((int)dst[1], 0x33);  // b
    CHECK_EQ((int)dst[2], 0x22);  // g
}

// ---- BuildLightTable (0x5D49A0) ---------------------------------------------
TEST(ShapeReconLight, BuildLightTableAmountZeroIsIdentity) {
    ShapeColorState st;            // identity shifts
    std::vector<u16> lut(65536);
    int ok = ShapeBuildLightTable(st, 0, lut.data());
    CHECK_EQ(ok, 1);
    // amount 0: unpack(i)->r=g=b=(i&0xFF) (low byte; high bytes collapse under
    // zero shifts), saturating-add 0 leaves them, repack = r|g|b = (i&0xFF).
    // So every entry equals its index's low byte ORed across channels = i&0xFF.
    CHECK_EQ((int)lut[0x00], 0x00);
    CHECK_EQ((int)lut[0x12], 0x12);
    CHECK_EQ((int)lut[0xAB], 0xAB);
    // index 0x1FF -> low byte 0xFF.
    CHECK_EQ((int)lut[0x1FF], 0xFF);
}

TEST(ShapeReconLight, BuildLightTableSaturates) {
    ShapeColorState st;
    std::vector<u16> lut(65536);
    ShapeBuildLightTable(st, 0x30, lut.data());
    // index 0x10: channels each = 0x10, +0x30 = 0x40, repack 0x40.
    CHECK_EQ((int)lut[0x10], 0x40);
    // index 0xF0: 0xF0 >= 255-0x30 (0xCF) -> saturates to 0xFF.
    CHECK_EQ((int)lut[0xF0], 0xFF);
}

TEST(ShapeReconLight, BuildLightTableNullReturnsZero) {
    ShapeColorState st;
    CHECK_EQ(ShapeBuildLightTable(st, 5, nullptr), 0);
}

TEST(ShapeReconLight, ShouldFreeLightTable) {
    CHECK(ShapeShouldFreeLightTable(false, true));    // !highColor && present
    CHECK(!ShapeShouldFreeLightTable(true, true));     // highColor -> keep
    CHECK(!ShapeShouldFreeLightTable(false, false));   // not present -> nothing
}

// ---- SetMaskColor (0x5D88C0) ------------------------------------------------
TEST(ShapeReconMask, SetMaskColorPacksAndDuplicates) {
    ShapeColorState st;  // identity
    u32 reg = 0;
    u32 packed = ShapeSetMaskColor(st, 0x12, 0x34, reg);
    // ColorPack(0x12, 0x34, 0) = 0x12|0x34 = 0x36.
    CHECK_EQ((int)packed, 0x36);
    CHECK_EQ((unsigned)reg, 0x00360036u);
}

// ---- ClassifyType (0x41F4F4) ------------------------------------------------
// `byte2` is only meaningful for non-bank blobs (it is the format/version at
// offset 2). For "SHAPBANK" blobs offset 2 is the magic byte 'A', so we keep the
// magic intact there and ignore byte2.
static void mk(u8* b, const char* magic, u8 byte2, u8 byte9, u16 cnt) {
    std::memset(b, 0, 64);
    std::memcpy(b, magic, 8);
    if (std::memcmp(magic, "SHAPBANK", 8) != 0)
        b[2] = byte2;     // only override version for non-bank blobs
    b[9] = byte9;
    b[42] = (u8)(cnt & 0xFF);
    b[43] = (u8)(cnt >> 8);
}

TEST(ShapeReconClassify, NullIsZero) {
    CHECK_EQ((int)ShapeClassifyType(nullptr), 0);
}

TEST(ShapeReconClassify, NonBankNewImageIs17) {
    // NOT "SHAPBANK"; byte[2]==2 -> new-format single image -> 17.
    u8 b[64];
    mk(b, "XXXXXXXX", 2, 0, 0);
    CHECK_EQ((int)ShapeClassifyType(b), 17);
}

TEST(ShapeReconClassify, NonBankUnknownIs0) {
    // NOT "SHAPBANK"; byte[2] != 2 -> unrecognised -> 0.
    u8 b[64];
    mk(b, "XXXXXXXX", 1, 0, 0);
    CHECK_EQ((int)ShapeClassifyType(b), 0);
}

TEST(ShapeReconClassify, ShapBankSubtypeSwitch) {
    // IS "SHAPBANK" -> classify by byte[9] / shape count.
    u8 b[64];
    mk(b, "SHAPBANK", 0, 1, 5);  CHECK_EQ((int)ShapeClassifyType(b), 4);
    mk(b, "SHAPBANK", 0, 2, 5);  CHECK_EQ((int)ShapeClassifyType(b), 7);
    mk(b, "SHAPBANK", 0, 3, 5);  CHECK_EQ((int)ShapeClassifyType(b), 8);
    mk(b, "SHAPBANK", 0, 9, 5);  CHECK_EQ((int)ShapeClassifyType(b), 1);  // default
}

TEST(ShapeReconClassify, ShapBankMultiFrameIs5) {
    u8 b[64];
    // IS "SHAPBANK", byte9 == 0 AND shapeCount > 1 -> 5.
    mk(b, "SHAPBANK", 0, 0, 4);
    CHECK_EQ((int)ShapeClassifyType(b), 5);
    // byte9==0 but count<=1 -> default switch -> 1.
    mk(b, "SHAPBANK", 0, 0, 1);
    CHECK_EQ((int)ShapeClassifyType(b), 1);
}

// ---- ShapeBankSetSequenceData (0x5D8F54) ------------------------------------
TEST(ShapeReconBank, SetSequenceDataLazyInitAndCursor) {
    std::vector<u8> bank(4096, 0);
    auto u32at = [&](size_t o){ return *reinterpret_cast<u32*>(bank.data()+o); };
    auto u16at = [&](size_t o){ return *reinterpret_cast<u16*>(bank.data()+o); };
    // writeCursor(+48) starts at 1000, seqOffset(+62)=0 (lazy), prevCount(+67)=0.
    *reinterpret_cast<u32*>(bank.data()+48) = 1000;
    u8 seq[16];
    for (int i = 0; i < 16; ++i) seq[i] = (u8)(i + 1);
    int ret = ShapeBankSetSequenceData(bank.data(), seq, 2);  // 2 records = 16 bytes
    CHECK_EQ(ret, 16);
    CHECK_EQ((unsigned)u32at(62), 1000u);   // seqOffset lazily = old cursor
    CHECK_EQ((int)u16at(67), 2);            // prevCount updated
    // cursor: 1000 - 8*0 + 8*2 = 1016.
    CHECK_EQ((unsigned)u32at(48), 1016u);
    // bytes copied at offset 1000.
    CHECK_EQ((int)bank[1000], 1);
    CHECK_EQ((int)bank[1015], 16);

    // Second call with count=3: cursor 1016 - 8*2 + 8*3 = 1024.
    int ret2 = ShapeBankSetSequenceData(bank.data(), seq, 3);
    CHECK_EQ(ret2, 24);
    CHECK_EQ((unsigned)u32at(48), 1024u);
    CHECK_EQ((int)u16at(67), 3);
}

// ---- ShapeBankSetPalette (0x5D8294) -----------------------------------------
TEST(ShapeReconBank, SetPaletteCopiesAndPacks) {
    ShapeColorState st;  // identity pack
    std::vector<u8> src(1024);
    for (int i = 0; i < 256; ++i) {
        src[4*i+0] = (u8)i;      // r
        src[4*i+1] = (u8)0;      // g
        src[4*i+2] = (u8)0;      // b
        src[4*i+3] = 0xFF;       // a
    }
    u8 pal[1024];
    u16 packed[257] = {0};
    int ok = ShapeBankSetPalette(st, src.data(), pal, packed);
    CHECK_EQ(ok, 1);
    CHECK_EQ((int)pal[4*5+0], 5);
    // packed is 1-based: packed[1] is the first entry (palette index 0).
    CHECK_EQ((int)packed[1], 0);          // r=0
    CHECK_EQ((int)packed[256], 0xFF);     // last entry r=0xFF (index 255)
    CHECK_EQ(ShapeBankSetPalette(st, nullptr, pal, packed), 0);
}

// ---- Anim slot table (0x5D8E30 / 0x5D8F1C / 0x5D8F3C) -----------------------
static int g_drawCalls = 0;
static i16 g_lastX, g_lastY;
static u32 g_lastObj;
static u8  g_lastShape;
static int drawCb(i16 x, i16 y, u32 object, int drawArg, u8 shapeNr, void* ctx) {
    (void)drawArg; (void)ctx;
    ++g_drawCalls;
    g_lastX = x; g_lastY = y; g_lastObj = object; g_lastShape = shapeNr;
    return (int)object;  // distinguishable result
}

TEST(ShapeReconAnim, DrawAllSlotsFiresOnlyActive) {
    u8 table[272];
    std::memset(table, 0, sizeof(table));
    // Activate slot 0 and slot 2.
    u8* s0 = ShapeAnimGetSlot(table, 0);
    *reinterpret_cast<u32*>(s0 + anim_slot::kObject) = 0xCAFE;
    *reinterpret_cast<i16*>(s0 + anim_slot::kX) = (i16)100;
    *reinterpret_cast<i16*>(s0 + anim_slot::kY) = (i16)-50;
    s0[anim_slot::kShapeNr] = 7;

    u8* s2 = ShapeAnimGetSlot(table, 2);
    *reinterpret_cast<u32*>(s2 + anim_slot::kObject) = 0xBEEF;
    s2[anim_slot::kShapeNr] = 3;

    g_drawCalls = 0;
    int r = ShapeAnimDrawAllSlots(table, 999, drawCb, nullptr);
    CHECK_EQ(g_drawCalls, 2);
    // Last fired slot is slot 2 (0xBEEF), result = its object.
    CHECK_EQ((unsigned)r, 0xBEEFu);
    CHECK_EQ((int)g_lastShape, 3);

    // No active slots -> returns the initial accumulator (drawArg).
    std::memset(table, 0, sizeof(table));
    g_drawCalls = 0;
    int r2 = ShapeAnimDrawAllSlots(table, 777, drawCb, nullptr);
    CHECK_EQ(g_drawCalls, 0);
    CHECK_EQ(r2, 777);
}

TEST(ShapeReconAnim, GetSlotAndTablePointers) {
    u8 table[272];
    CHECK(ShapeAnimGetSlotTable(table) == table);
    CHECK(ShapeAnimGetSlot((const u8*)table, 3) == table + 51);  // 17*3
    CHECK(ShapeAnimGetSlot(table, 15) == table + 255);           // 17*15
}

// ---- GrabBit16NoRle (0x5D5E7C) — raw-row blob ------------------------------
TEST(ShapeReconGrab, Bit16NoRleBoundsAndRows) {
    ShapeColorState st;
    // 4x4 source, stride 4. Put opaque (nonzero) pixels in a 2x2 block at
    // cols 1..2, rows 1..2. Everything else 0 (transparent).
    const int W = 4, H = 4;
    u16 src[16];
    std::memset(src, 0, sizeof(src));
    src[1*4 + 1] = 0x1111;
    src[1*4 + 2] = 0x2222;
    src[2*4 + 1] = 0x3333;
    src[2*4 + 2] = 0x4444;

    std::vector<u8> out(ShapeGrabBit16MaxSize(W, H) + 64, 0);
    // a3=h=4 rows, a4=w=4 cols.
    size_t sz = ShapeGrabBit16NoRle(st, 0, 0, 4, 4, src, /*type*/0x55, /*stride*/4,
                                    out.data());
    CHECK(sz > 0);
    auto W16 = [&](int k){ return *reinterpret_cast<u16*>(out.data() + 2*k); };
    // minX(out[2]) = 1, minY(out[4]) = 1, width(out[3]) = maxX+1 = 3,
    // height(out[5]) = maxY+1 = 3.
    CHECK_EQ((int)W16(2), 1);
    CHECK_EQ((int)W16(4), 1);
    CHECK_EQ((int)W16(3), 3);
    CHECK_EQ((int)W16(5), 3);
    // type flag stored at +13.
    CHECK_EQ((int)out[13], 0x55);
    // span count field (+38) = -1 for No-Rle variants.
    CHECK_EQ((int)*reinterpret_cast<int*>(out.data() + 38), -1);
    // First copied row = source row 0 of the bounding box (full width=3 from
    // x=0): copies src[0..2] of row 0 = {0,0,0}. Payload begins at offset 50.
    u16* payload = reinterpret_cast<u16*>(out.data() + 50);
    CHECK_EQ((int)payload[0], 0x0000);  // row0 col0
    // Row 1 (second copied row), col1/col2 carry the opaque values.
    CHECK_EQ((int)payload[3 + 1], 0x1111);
    CHECK_EQ((int)payload[3 + 2], 0x2222);
}

// ---- GrabBit8 (0x5D6160) — RLE round-trip on a known sprite -----------------
TEST(ShapeReconGrab, Bit8RleHeaderAndOpaqueCount) {
    ShapeColorState st;            // mask colour {0,0,0}; anchors default.
    // Build a 1024-byte palette where index 0 maps to {0,0,0} (== mask) and
    // index 5 maps to a non-mask, non-anchor colour {10,20,30}.
    u8 pal[1024];
    std::memset(pal, 0, sizeof(pal));
    pal[4*5+0] = 10; pal[4*5+1] = 20; pal[4*5+2] = 30;
    // 4x4 source: a horizontal run of index 5 at row 1, cols 1..2.
    u8 src[16];
    std::memset(src, 0, sizeof(src));
    src[1*4 + 1] = 5;
    src[1*4 + 2] = 5;

    std::vector<u8> out(ShapeGrabBit8MaxSize(4, 4) + 64, 0);
    size_t sz = ShapeGrabBit8(st, pal, 0, 0, 4, 4, src, 0x01, 4, out.data());
    CHECK(sz > 0);
    auto W16 = [&](int k){ return *reinterpret_cast<u16*>(out.data() + 2*k); };
    // bounds: minX=1, minY=1, width(out[3])=maxX+1=3, height(out[5])=maxY+1=2.
    CHECK_EQ((int)W16(2), 1);   // minX
    CHECK_EQ((int)W16(4), 1);   // minY
    CHECK_EQ((int)W16(3), 3);   // maxX(2)+1
    CHECK_EQ((int)W16(5), 2);   // maxY(1)+1
    // opaque pixel count (+46) = 2 (the two index-5 pixels).
    CHECK_EQ((int)*reinterpret_cast<int*>(out.data() + 46), 2);
    // depth/type: GrabBit8 stores type flag at +13 (depth is stamped by
    // GrabByDepth, not here).
    CHECK_EQ((int)out[13], 0x01);
}

// ---- GrabBit24 (0x5D4C20) anchor writeback writes [maskR,maskG,maskB] -------
// gilde.exe 0x5d5223/0x5d52ca: the anchor relocation overwrites the hit pixel
// with the mask colour via VIBE_Color_SetRgb(px, dl=byte_1406946 (maskR),
// cl=SBYTE1(dword_1406947) (maskB), bl=LOBYTE(dword_1406947) (maskG)), so the
// 3 bytes land in memory as [maskR, maskG, maskB]. Use an asymmetric mask so
// the byte order is observable (regression guard for the SetRgb arg order).
TEST(ShapeReconGrab, Bit24AnchorWritebackByteOrder) {
    ShapeColorState st;
    st.maskR = 0x10; st.maskG = 0x20; st.maskB = 0x30;   // asymmetric mask
    // anchorA default {0,0,0xFF}, anchorB default {0xFF,0,0}.
    // 4x4 RGB source: one anchorA pixel and one anchorB pixel; rest mask colour.
    u8 src[4 * 4 * 3];
    for (int i = 0; i < 16; ++i) {
        src[3*i+0] = st.maskR; src[3*i+1] = st.maskG; src[3*i+2] = st.maskB;
    }
    // anchorB {0xFF,0,0} at (col=1,row=1); anchorA {0,0,0xFF} at (col=2,row=2).
    int b = 1*4 + 1; src[3*b+0]=0xFF; src[3*b+1]=0x00; src[3*b+2]=0x00;
    int a = 2*4 + 2; src[3*a+0]=0x00; src[3*a+1]=0x00; src[3*a+2]=0xFF;

    std::vector<u8> out(ShapeGrabBit24MaxSize(4, 4) + 64, 0);
    size_t sz = ShapeGrabBit24(st, 0, 0, 4, 4, src, 0x02, 4, out.data());
    CHECK(sz > 0);
    // After relocation, both anchor pixels were overwritten with the mask colour
    // in [R,G,B] memory order.
    CHECK_EQ((int)src[3*b+0], 0x10);
    CHECK_EQ((int)src[3*b+1], 0x20);
    CHECK_EQ((int)src[3*b+2], 0x30);
    CHECK_EQ((int)src[3*a+0], 0x10);
    CHECK_EQ((int)src[3*a+1], 0x20);
    CHECK_EQ((int)src[3*a+2], 0x30);
}

// ---- GrabByDepth dispatch (0x5D68C4) ----------------------------------------
TEST(ShapeReconGrab, ByDepthStampsDepthCodeAndType) {
    ShapeColorState st;
    u16 src[16];
    std::memset(src, 0, sizeof(src));
    src[1*4 + 1] = 0xABCD;
    std::vector<u8> out(ShapeGrabBit16MaxSize(4, 4) + 64, 0);
    // srcDepth=16, useRle=false -> GrabBit16NoRle, depthCode=1.
    size_t sz = ShapeGrabByDepth(st, nullptr, 0, 0, 4, 4, 16, src, 4,
                                 /*type*/0x07, /*useRle*/false, out.data());
    CHECK(sz > 0);
    CHECK_EQ((int)out[12], (int)kDepthCode16);  // +0x0C
    CHECK_EQ((int)out[13], 0x07);               // +0x0D
}

TEST(ShapeReconGrab, ByDepthRejectsUnknownDepth) {
    ShapeColorState st;
    u8 dummy[16] = {0};
    std::vector<u8> out(4096, 0);
    // depth 4 (not 8/16/24) -> 0.
    CHECK_EQ(ShapeGrabByDepth(st, nullptr, 0, 0, 4, 4, 4, dummy, 4, 0, true,
                              out.data()), (size_t)0);
}
