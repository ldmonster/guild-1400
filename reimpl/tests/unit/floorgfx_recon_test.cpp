// Golden-vector unit tests for the floorgfx_recon gfx surface-blit leaves.
// Values are computed directly from the gilde.exe 1:1 reconstructions
// (0x5fc4ec / 0x5fc554 / 0x5fc5a4 / 0x5fc6a8 / 0x5d9078 / 0x5c2a9c tile math).

#include "test.h"

#include "render/floorgfx_recon.h"

#include <cstdint>
#include <vector>

using namespace guild;
using namespace guild::render;

// --- reference implementations mirroring the decompile, used to cross-check ---
namespace {

u16 ref_blend565(u16 bg, u16 fg, u8 alpha) {
    u8 inv = static_cast<u8>(0xFF - alpha);
    u8 AADC[64], AB1C[64];
    for (int i = 0; i < 64; ++i) {
        AADC[i] = static_cast<u8>(((static_cast<u16>(inv) * static_cast<u8>(i)) >> 8) + 1);
        AB1C[i] = static_cast<u8>((static_cast<u16>(alpha) * static_cast<u8>(i)) >> 8);
    }
    u8 blue  = static_cast<u8>(AADC[bg & 0x1F]        + AB1C[fg & 0x1F]);
    u8 green = static_cast<u8>(AADC[(bg >> 5) & 0x3F] + AB1C[(fg >> 5) & 0x3F]);
    u8 red   = static_cast<u8>(AADC[bg >> 11]         + AB1C[(fg >> 11) & 0x1F]);
    return static_cast<u16>(blue | ((u16(green) & 0x3F) << 5) | ((u16(red) << 11) & 0xF800));
}

u16 ref_blend555(u16 bg, u16 fg, u8 alpha) {
    u8 inv = static_cast<u8>(0xFF - alpha);
    u8 A[64], B[64];
    for (int i = 0; i < 64; ++i) {
        A[i] = static_cast<u8>(((static_cast<u16>(inv) * static_cast<u8>(i)) >> 8) + 1);
        B[i] = static_cast<u8>((static_cast<u16>(alpha) * static_cast<u8>(i)) >> 8);
    }
    u8 blue  = static_cast<u8>(A[bg & 0x1F]         + B[fg & 0x1F]);
    u8 green = static_cast<u8>(A[(bg >> 5) & 0x1F]  + B[(fg >> 5) & 0x1F]);
    u8 red   = static_cast<u8>(A[(bg >> 10) & 0x1F] + B[(fg >> 10) & 0x1F]);
    return static_cast<u16>(blue | ((u16(green) & 0x1F) << 5) | ((u16(red) << 10) & 0xFC00));
}

} // namespace

// ---------------------------------------------------------------------------
TEST(FloorGfxReconDarken, HalvesChannelsWithMask) {
    // Single row, 4 pixels, stride==width so no row padding.
    FloorGfxState().darkenRemapStridePx = 4;
    // The 32-bit fast path masks 0x3DEFBDEF; the odd-pixel path masks 0x3DEF.
    // Build an even-width buffer so only the 32-bit body runs (reads a1 in place).
    std::vector<u16> buf = {0xFFFF, 0x8421, 0x7C00, 0x001F};
    std::vector<u16> src = buf; // src only consulted for odd preamble (width even -> unused)
    VIBE_Gfx_DarkenSurface(buf.data(), 4, 1, src.data());
    // Expected: pairs masked by 0x3DEFBDEF after >>1.
    // pixel0,1 packed = 0x8421FFFF -> >>1 = 0x4210FFFF & 0x3DEFBDEF = 0x0000BDEF
    //   low word 0xBDEF, high word 0x0000
    CHECK_EQ((unsigned)buf[0], 0xBDEFu);
    CHECK_EQ((unsigned)buf[1], 0x0000u);
    // pixel2,3 packed = 0x001F7C00 -> >>1 = 0x000FBE00 & 0x3DEFBDEF = 0x000FBC00
    CHECK_EQ((unsigned)buf[2], 0xBC00u);
    CHECK_EQ((unsigned)buf[3], 0x000Fu);
}

TEST(FloorGfxReconDarken, OddWidthUsesSrcForLeadingPixel) {
    FloorGfxState().darkenRemapStridePx = 3;
    std::vector<u16> dst = {0x0000, 0x1111, 0x2222};
    std::vector<u16> src = {0xFFFE, 0xAAAA, 0x5555};
    // width=3 (odd): leading pixel = (src[0]>>1)&0x3DEF; then one 32-bit body pair
    // reads dst[1..2] in place.
    VIBE_Gfx_DarkenSurface(dst.data(), 3, 1, src.data());
    CHECK_EQ((unsigned)dst[0], (unsigned)((0xFFFE >> 1) & 0x3DEF)); // 0x3DEF
    // dst[1],dst[2] packed=0x22221111 ->>1=0x11110888 & 0x3DEFBDEF = 0x11010888
    CHECK_EQ((unsigned)dst[1], 0x0888u);
    CHECK_EQ((unsigned)dst[2], 0x1101u);
}

// ---------------------------------------------------------------------------
TEST(FloorGfxReconRemap, AppliesPaletteTablePerPixel) {
    std::vector<u16> table(65536);
    for (int i = 0; i < 65536; ++i) table[i] = static_cast<u16>((i * 3 + 7) & 0xFFFF);
    FloorGfxState().remapTable = table.data();
    FloorGfxState().darkenRemapStridePx = 4;

    std::vector<u16> buf = {0x0000, 0x0001, 0x00FF, 0xFFFF};
    VIBE_Gfx_RemapSurfacePalette(buf.data(), 4, 1);
    CHECK_EQ((unsigned)buf[0], (unsigned)table[0x0000]);
    CHECK_EQ((unsigned)buf[1], (unsigned)table[0x0001]);
    CHECK_EQ((unsigned)buf[2], (unsigned)table[0x00FF]);
    CHECK_EQ((unsigned)buf[3], (unsigned)table[0xFFFF]);
}

TEST(FloorGfxReconRemap, MultiRowAdvancesByStride) {
    std::vector<u16> table(65536);
    for (int i = 0; i < 65536; ++i) table[i] = static_cast<u16>(i ^ 0xABCD);
    FloorGfxState().remapTable = table.data();
    FloorGfxState().darkenRemapStridePx = 3;  // stride 3, width 2 -> 1 pad word/row

    std::vector<u16> buf = {1, 2, 99, /*pad*/ 3, 4, 99};
    VIBE_Gfx_RemapSurfacePalette(buf.data(), 2, 2);
    CHECK_EQ((unsigned)buf[0], (unsigned)table[1]);
    CHECK_EQ((unsigned)buf[1], (unsigned)table[2]);
    CHECK_EQ((unsigned)buf[2], 99u);                 // padding untouched
    CHECK_EQ((unsigned)buf[3], (unsigned)table[3]);
    CHECK_EQ((unsigned)buf[4], (unsigned)table[4]);
}

// ---------------------------------------------------------------------------
TEST(FloorGfxReconFade565, AlphaZeroIsBackgroundPlusBias) {
    FloorGfxState().fadeStridePx = 2;
    FloorGfxState().fadeWidth = 2;
    FloorGfxState().fadeHeight = 1;
    FloorGfxState().fadeAlpha = 0;        // pure background (with +1 channel bias)

    u16 bg[2] = {0xFFFF, 0x0000};
    u16 fg[2] = {0x0000, 0xFFFF};
    u16 dst[2] = {0, 0};
    i32 adv = VIBE_Gfx_FadeBlend565(dst, bg, fg);
    CHECK_EQ((unsigned)dst[0], (unsigned)ref_blend565(0xFFFF, 0x0000, 0));
    CHECK_EQ((unsigned)dst[1], (unsigned)ref_blend565(0x0000, 0xFFFF, 0));
    CHECK_EQ(adv, 2 * (2 - 2));            // stride==width -> 0
}

TEST(FloorGfxReconFade565, MidAlphaBlendMatchesReference) {
    FloorGfxState().fadeStridePx = 3;
    FloorGfxState().fadeWidth = 3;
    FloorGfxState().fadeHeight = 1;
    FloorGfxState().fadeAlpha = 128;

    u16 bg[3] = {0x1234, 0xF800, 0x07E0};
    u16 fg[3] = {0xABCD, 0x001F, 0xFFFF};
    u16 dst[3] = {0, 0, 0};
    VIBE_Gfx_FadeBlend565(dst, bg, fg);
    for (int i = 0; i < 3; ++i)
        CHECK_EQ((unsigned)dst[i], (unsigned)ref_blend565(bg[i], fg[i], 128));
}

TEST(FloorGfxReconFade565, RowAdvanceFromStride) {
    FloorGfxState().fadeStridePx = 5;
    FloorGfxState().fadeWidth = 2;
    FloorGfxState().fadeHeight = 2;
    FloorGfxState().fadeAlpha = 64;

    // dst large enough for 2 rows at stride 5.
    std::vector<u16> dst(2 * 5, 0xDEAD);
    u16 bg[4] = {0x1111, 0x2222, 0x3333, 0x4444};
    u16 fg[4] = {0x5555, 0x6666, 0x7777, 0x8888};
    i32 adv = VIBE_Gfx_FadeBlend565(dst.data(), bg, fg);
    CHECK_EQ(adv, 2 * (5 - 2)); // 6 bytes -> 3 words pad
    // Row 0 written to dst[0..1], row 1 to dst[5..6] (after 3-word pad).
    CHECK_EQ((unsigned)dst[0], (unsigned)ref_blend565(bg[0], fg[0], 64));
    CHECK_EQ((unsigned)dst[1], (unsigned)ref_blend565(bg[1], fg[1], 64));
    CHECK_EQ((unsigned)dst[2], 0xDEADu); // pad untouched
    CHECK_EQ((unsigned)dst[5], (unsigned)ref_blend565(bg[2], fg[2], 64));
    CHECK_EQ((unsigned)dst[6], (unsigned)ref_blend565(bg[3], fg[3], 64));
}

// ---------------------------------------------------------------------------
TEST(FloorGfxReconFade555, MidAlphaBlendMatchesReference) {
    FloorGfxState().fadeStridePx = 4;
    FloorGfxState().fadeWidth = 4;
    FloorGfxState().fadeHeight = 1;
    FloorGfxState().fadeAlpha = 200;

    u16 bg[4] = {0x7FFF, 0x0000, 0x7C00, 0x03E0};
    u16 fg[4] = {0x0000, 0x7FFF, 0x001F, 0x7C1F};
    u16 dst[4] = {0, 0, 0, 0};
    VIBE_Gfx_FadeBlend555(dst, bg, fg);
    for (int i = 0; i < 4; ++i)
        CHECK_EQ((unsigned)dst[i], (unsigned)ref_blend555(bg[i], fg[i], 200));
}

// ---------------------------------------------------------------------------
TEST(FloorGfxReconSetFadeParams, Dispatch565WhenFormatEleven) {
    FloorGfxState().pixelFormat565 = 11;
    FloorGfxState().fadeStridePx = 2;
    u16 bg[2] = {0xF800, 0x07E0};
    u16 fg[2] = {0x001F, 0xFFFF};
    u16 dst[2] = {0, 0};
    VIBE_Gfx_SetFadeParams(dst, bg, fg, /*width*/2, /*height*/1, /*alpha*/100);
    CHECK_EQ((unsigned)dst[0], (unsigned)ref_blend565(0xF800, 0x001F, 100));
    CHECK_EQ((unsigned)dst[1], (unsigned)ref_blend565(0x07E0, 0xFFFF, 100));
    // State globals installed:
    CHECK_EQ(FloorGfxState().fadeWidth, 2);
    CHECK_EQ(FloorGfxState().fadeHeight, 1);
    CHECK_EQ((int)FloorGfxState().fadeAlpha, 100);
}

TEST(FloorGfxReconSetFadeParams, Dispatch555WhenFormatNotEleven) {
    FloorGfxState().pixelFormat565 = 0;   // -> 555 path
    FloorGfxState().fadeStridePx = 2;
    u16 bg[2] = {0x7C00, 0x03E0};
    u16 fg[2] = {0x001F, 0x7FFF};
    u16 dst[2] = {0, 0};
    VIBE_Gfx_SetFadeParams(dst, bg, fg, 2, 1, 100);
    CHECK_EQ((unsigned)dst[0], (unsigned)ref_blend555(0x7C00, 0x001F, 100));
    CHECK_EQ((unsigned)dst[1], (unsigned)ref_blend555(0x03E0, 0x7FFF, 100));
}

// ---------------------------------------------------------------------------
TEST(FloorGfxReconMinimapStep, OverridePassesThrough) {
    CHECK_EQ(VIBE_Floor_MinimapTileStep(5, 0x00), 5);
    CHECK_EQ(VIBE_Floor_MinimapTileStep(1, 0xFF), 1);
}

TEST(FloorGfxReconMinimapStep, DerivedFromFlags) {
    // flags & 0x1C == 0 -> step 1
    CHECK_EQ(VIBE_Floor_MinimapTileStep(0, 0x00), 1);
    CHECK_EQ(VIBE_Floor_MinimapTileStep(0, 0x03), 1); // low bits only -> &0x1C==0
    // flags & 0x1C != 0 -> (u8)(8*flags) >> 5
    // flags=0x04: 8*4=0x20 (u8)=0x20 >>5 = 1
    CHECK_EQ(VIBE_Floor_MinimapTileStep(0, 0x04), 1);
    // flags=0x08: 8*8=0x40 >>5 = 2
    CHECK_EQ(VIBE_Floor_MinimapTileStep(0, 0x08), 2);
    // flags=0x10: 8*0x10=0x80 >>5 = 4
    CHECK_EQ(VIBE_Floor_MinimapTileStep(0, 0x10), 4);
    // flags=0x1C: 8*0x1C=0xE0 (u8) >>5 = 7
    CHECK_EQ(VIBE_Floor_MinimapTileStep(0, 0x1C), 7);
    // flags=0x14: 8*0x14=0xA0 >>5 = 5
    CHECK_EQ(VIBE_Floor_MinimapTileStep(0, 0x14), 5);
}

TEST(FloorGfxReconMinimapSpan, BoundsMatchDecompile) {
    i32 row = -1, col = -1;
    // cells=16, step=2, interior tile.
    VIBE_Floor_MinimapTileSpan(16, 2, /*lastRow*/false, /*lastCol*/false, &row, &col);
    // base = 16/2+1 = 9; v9=9, v10=9; -1 each.
    CHECK_EQ(row, 8);
    CHECK_EQ(col, 8);

    // last column reduces col by 4/step.
    VIBE_Floor_MinimapTileSpan(16, 2, false, true, &row, &col);
    // v10 = 9 - 4/2 = 7; row v9=9. minus 1 each.
    CHECK_EQ(row, 8);
    CHECK_EQ(col, 6);

    // last row reduces row.
    VIBE_Floor_MinimapTileSpan(16, 4, true, false, &row, &col);
    // base=16/4+1=5; v9=5-4/4=4; v10=5. minus 1.
    CHECK_EQ(row, 3);
    CHECK_EQ(col, 4);
}
