#include "test.h"
#include "render/sprite_scale.h"
#include "render/shape_blit.h"        // ShapeBlitColored16 (real sibling)
#include "render/animation_decode.h"
#include "render/colorformat.h"
#include <cstring>
#include <vector>

using namespace guild;
using namespace guild::render;

// =============================================================================
// Cross-module: the bank dispatchers (sprite_scale.cpp) driving the REAL pixel
// blitters — VIBE_Shape_BlitColored16 from shape_blit.cpp (unscaled path) and
// VIBE_Shape_BlitRleScaled/LightTable from this module (scaled path). The bank
// blob is built byte-for-byte to the original layout (count @+0x2A, offset table
// @+0x45, shape index 1 -> a 6x3 RLE shape with depth=1).
// =============================================================================

static void PutU16(std::vector<u8>& b, u16 v) { b.push_back((u8)v); b.push_back((u8)(v >> 8)); }
static void PutU32(std::vector<u8>& b, u32 v) {
    for (int i = 0; i < 4; ++i) b.push_back((u8)(v >> (8 * i)));
}

// Bank: count(+0x2A)=5, offset[1](+0x49)=0x4D -> a 6x3 depth-1 RLE shape
//   row0 {[R,G]} row1 {skip1,[B]} row2 {[Y,W,M]}.  Built byte-for-byte to layout.
static std::vector<u8> BuildBank() {
    const u16 R = 63488, G = 2016, B = 31, Y = 65504, W = 65535, M = 63519;
    std::vector<u8> shape(0x32, 0);
    shape[0x06] = 6; shape[0x0A] = 3; shape[0x0C] = 1;
    auto run = [&](u32 skipPixels, std::vector<u16> px) {
        PutU32(shape, (u32)(skipPixels << 1));
        PutU32(shape, (u32)px.size());
        for (u16 p : px) PutU16(shape, p);
    };
    PutU32(shape, 1); run(0, {R, G});
    PutU32(shape, 1); run(1, {B});
    PutU32(shape, 1); run(0, {Y, W, M});
    for (int i = 0; i < 16; ++i) shape.push_back(0);

    const u32 shapeOff = 0x45 + 4 * 2;     // 0x4D
    std::vector<u8> bank(shapeOff, 0);
    bank[0x2A] = 5;                          // shapeCount
    std::memcpy(&bank[0x45 + 4], &shapeOff, 4);
    bank.insert(bank.end(), shape.begin(), shape.end());
    return bank;
}

// ShapeShowFromBank (unscaled) -> ShapeBlitColored16: each opaque pixel becomes a
// grey luma value (matching the shape_blit.cpp golden weights r*.2+b*.59+g*.2).
TEST(RenderSpriteScaleItest, ShowFromBankColored) {
    auto bank = BuildBank();
    u16 buf[48]; std::memset(buf, 0, sizeof(buf));
    ColorBlitTarget16 dst{8, buf};
    FrameBlitState st;
    int rv = ShapeShowFromBank(2, 1, bank.data(), 1, dst, Format565(), st);
    CHECK_EQ(rv, 1);
    CHECK_EQ(st.destStridePx, 8);   // installed from dst.widthPx
    static const u16 gold[48] = {
        0,0,0,0,0,0,0,0, 0,0,12678,12678,0,0,0,0, 0,0,0,38034,0,0,0,0,
        0,0,25388,63422,50712,0,0,0, 0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0 };
    for (int i = 0; i < 48; ++i) CHECK_EQ((int)buf[i], (int)gold[i]);
}

// Out-of-range index (> count @+0x2A) -> 0, nothing drawn.
TEST(RenderSpriteScaleItest, ShowFromBankOutOfRange) {
    auto bank = BuildBank();
    u16 buf[48]; std::memset(buf, 0, sizeof(buf));
    ColorBlitTarget16 dst{8, buf};
    FrameBlitState st;
    int rv = ShapeShowFromBank(0, 0, bank.data(), 99, dst, Format565(), st);
    CHECK_EQ(rv, 0);
    for (int i = 0; i < 48; ++i) CHECK_EQ((int)buf[i], 0);
    CHECK_EQ(ShapeShowFromBank(0, 0, nullptr, 1, dst, Format565(), st), 0);
}

// ShapeShowFromBankScaled (doScaledBlit) -> ShapeBlitRleScaled at scale=1 (identity).
TEST(RenderSpriteScaleItest, ShowFromBankScaledIdentity) {
    auto bank = BuildBank();
    u16 buf[48]; std::memset(buf, 0, sizeof(buf));
    ColorBlitTarget16 dst{8, buf};
    FrameBlitState st;
    st.clipX0 = 0; st.clipX1 = 8; st.clipY0 = 0; st.clipY1 = 6;
    int rv = ShapeShowFromBankScaled(2, 1, bank.data(), 1, /*scale=*/1,
                                     /*doScaledBlit=*/true, /*lightTable=*/false, dst, st);
    CHECK_EQ(rv, 1);
    static const u16 gold[48] = {
        0,0,0,0,0,0,0,0, 0,0,63488,2016,0,0,0,0, 0,0,0,31,0,0,0,0,
        0,0,65504,65535,63519,0,0,0, 0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0 };
    for (int i = 0; i < 48; ++i) CHECK_EQ((int)buf[i], (int)gold[i]);
}

// ShowFromBankScaled with doScaledBlit=false short-circuits to success without drawing.
TEST(RenderSpriteScaleItest, ShowFromBankScaledNoBlit) {
    auto bank = BuildBank();
    u16 buf[48]; std::memset(buf, 0, sizeof(buf));
    ColorBlitTarget16 dst{8, buf};
    FrameBlitState st;
    st.clipX1 = 8; st.clipY1 = 6;
    int rv = ShapeShowFromBankScaled(2, 1, bank.data(), 1, 1,
                                     /*doScaledBlit=*/false, false, dst, st);
    CHECK_EQ(rv, 1);
    for (int i = 0; i < 48; ++i) CHECK_EQ((int)buf[i], 0);
}

// ShowFromBankScaled (lightTable) -> ShapeBlitRleLightTable through the remap table.
TEST(RenderSpriteScaleItest, ShowFromBankScaledLightTable) {
    auto bank = BuildBank();
    std::vector<u16> remap(65536, 0);
    for (u16 v : {63488, 2016, 31, 65504, 65535, 63519})
        remap[v] = (u16)((v + 1000) & 0xFFFF);
    u16 buf[48]; std::memset(buf, 0, sizeof(buf));
    ColorBlitTarget16 dst{8, buf};
    FrameBlitState st;
    st.clipX1 = 8; st.clipY1 = 6;
    st.remapTable = remap.data();
    int rv = ShapeShowFromBankScaled(2, 1, bank.data(), 1, 1,
                                     /*doScaledBlit=*/true, /*lightTable=*/true, dst, st);
    CHECK_EQ(rv, 1);
    static const u16 gold[48] = {
        0,0,0,0,0,0,0,0, 0,0,64488,3016,0,0,0,0, 0,0,0,1031,0,0,0,0,
        0,0,968,999,64519,0,0,0, 0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0 };
    for (int i = 0; i < 48; ++i) CHECK_EQ((int)buf[i], (int)gold[i]);
}
