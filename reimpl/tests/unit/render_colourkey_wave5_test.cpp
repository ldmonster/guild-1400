// =============================================================================
// Unit tests for the wave-5 W5-CKEY colour-key fix (the black-tree-backdrop bug).
//
// THE 1:1 MECHANISM (decompile evidence, addresses):
//   * Record +104 bit 2 (render::kTexFlagColourKey) is the transparency flag,
//     set on a >8bpp (24-bit) source with the gate dword_140809C==0 (default)
//     in VIBE_Texture_LoadByName @0x5dad52. (bit 3/0x08 is the "_NM" mip flag,
//     NOT the colour key.)
//   * The colour-key VALUE is pal[0] == BLACK (0,0,0): for a 24-bit source the
//     palette buffer is memset-0 and never rebuilt (the 24-bit arm of
//     VIBE_Bmp_LoadBuffer @0x5f0ce4 skips VIBE_Quant_BuildPalette), so
//     VIBE_Render_LoadAndStretchTexture @0x5dea50 reads v111 = pal[0] = 0
//     (0x5df40a) and the transparent-surface descriptor (+104 bit 2 ->
//     &unk_14080C4) does a DDBLT_KEYSRC blit (&unk_1000000, 0x5df086) keyed on
//     that black value.
//   * The octree quantizer (VIBE_Quant_TreeCollectPalette @0x603180, DFS child
//     bit 7->0) places the all-zero (black) leaf at a HIGH index, NOT index 0,
//     so "skip source index 0" does NOT coincide with black. The faithful
//     software realisation of the DDraw colour-key (render::FillSpanTexturedMasked
//     @0x5F721A in colour-key mode) skips texels whose RESOLVED 16bpp value ==
//     565(black) == 0. progress/colourkey-integration-wave5.md.
// =============================================================================
#include "render/raster.h"
#include "render/raster_textured.h"
#include "render/surface.h"
#include "render/texture.h"
#include "render/texture_bin.h"
#include "render/texture_palettize.h"
#include "test.h"

#include <cstring>
#include <vector>

using namespace guild;
using namespace guild::render;

namespace {

Surface* Make16(int w, int h) {
    Surface* s = SurfaceCreate(w, h, 16);
    std::memset(s->pixels, 0, (size_t)s->pitch * h);
    return s;
}

} // namespace

// --- PRODUCER predicate: kTexFlagColourKey == bit 2 == 0x04, and
//     TextureIsColourKeyed reads it (NOT bit 3 / 0x08, the "_NM" mip flag). ---
TEST(ColourKeyW5, ColourKeyBitIsBit2NotBit3) {
    CHECK_EQ((int)kTexFlagColourKey, 0x04);
    CHECK_EQ((int)kTexFlagNameNM, 0x08);
    Texture t;
    CHECK(!TextureIsColourKeyed(t));            // flags==0
    t.flags = kTexFlagNameNM;                   // _NM mip flag set, NOT keyed
    CHECK(!TextureIsColourKeyed(t));
    t.flags = kTexFlagColourKey;                // bit 2 set -> keyed
    CHECK(TextureIsColourKeyed(t));
    t.flags = (u8)(kTexFlagColourKey | kTexFlagNameNM | kTexFlagAlias);
    CHECK(TextureIsColourKeyed(t));             // bit 2 set among others
}

// --- PRODUCER rule (mirrors buildTextureBind / city_view3d bindFor): a >8bpp
//     source sets bit 2 with the gate off; an 8-bit source never does. The gate
//     dword_140809C defaults 0 (get_global_value) so >8bpp is ALWAYS keyed. ---
TEST(ColourKeyW5, ProducerSetsBit2OnlyForOver8bpp) {
    // VIBE_Texture_LoadByName @0x5dad52: `!dword_140809C && bpp > 8`.
    const int gate = 0;   // dword_140809C default
    auto producerKeyed = [gate](int bpp) {
        Texture T;        // fresh record, flags == 0
        if (!gate && bpp > 8)
            T.flags |= kTexFlagColourKey;
        return TextureIsColourKeyed(T);
    };
    CHECK(!producerKeyed(8));    // 8-bit indexed source -> NOT keyed
    CHECK(producerKeyed(24));    // 24-bit source        -> keyed
    CHECK(producerKeyed(32));    // >8bpp                -> keyed
}

// --- PALETTIZE producer (option a): a 24-bit source whose backdrop is exact
//     (0,0,0) gets BLACK reserved at palette index 0, source-black pixels mapped
//     to index 0, and index 0 pinned to exact black — so the masked span's
//     index-0 rule reproduces the DDraw source-black key. ---------------------
TEST(ColourKeyW5, PalettizeReservesBlackAtIndex0) {
    // 4x4 24-bit source: a blue blob on a black (0,0,0) backdrop.
    render::DecodedBmp bmp;
    bmp.ok = true; bmp.bpp = 24; bmp.width = bmp.height = 4; bmp.square = true;
    bmp.rgba.assign(4 * 4 * 4, 0);
    for (int i = 0; i < 16; ++i) {
        bool blob = (i == 5 || i == 6 || i == 9 || i == 10);  // centre 2x2
        bmp.rgba[(std::size_t)i * 4 + 0] = blob ? 0 : 0;      // R
        bmp.rgba[(std::size_t)i * 4 + 1] = blob ? 0 : 0;      // G
        bmp.rgba[(std::size_t)i * 4 + 2] = blob ? 200 : 0;    // B (blob blue, else black)
        bmp.rgba[(std::size_t)i * 4 + 3] = 255;
    }
    CHECK(render::PalettizeDecodedBmp(bmp));
    CHECK_EQ((int)bmp.indices.size(), 16);
    // index 0 is pinned to EXACT black (0,0,0).
    CHECK_EQ((int)bmp.palette[0], 0);
    CHECK_EQ((int)bmp.palette[1], 0);
    CHECK_EQ((int)bmp.palette[2], 0);
    // Every source-black pixel now carries index 0 (the masked span key); the
    // blue blob pixels carry a non-zero index.
    for (int i = 0; i < 16; ++i) {
        bool blob = (i == 5 || i == 6 || i == 9 || i == 10);
        if (blob) CHECK(bmp.indices[(std::size_t)i] != 0);
        else      CHECK_EQ((int)bmp.indices[(std::size_t)i], 0);
    }
}

// --- A 24-bit source WITHOUT any exact-black pixel is left untouched by the
//     reservation (no spurious index-0 remap). -----------------------------
TEST(ColourKeyW5, PalettizeNoBlackLeavesIndicesAsQuantized) {
    render::DecodedBmp bmp;
    bmp.ok = true; bmp.bpp = 24; bmp.width = bmp.height = 4; bmp.square = true;
    bmp.rgba.assign(4 * 4 * 4, 0);
    for (int i = 0; i < 16; ++i) {           // uniform (100,50,25), no black
        bmp.rgba[(std::size_t)i * 4 + 0] = 100;
        bmp.rgba[(std::size_t)i * 4 + 1] = 50;
        bmp.rgba[(std::size_t)i * 4 + 2] = 25;
        bmp.rgba[(std::size_t)i * 4 + 3] = 255;
    }
    CHECK(render::PalettizeDecodedBmp(bmp));
    // No source-black -> index 0 NOT pinned to black; the quantized bucket
    // centre (100,52,28) is the resolved colour of every texel.
    u8 i0 = bmp.indices[0];
    CHECK_EQ((int)bmp.palette[(std::size_t)i0 * 3 + 0], 100);
    CHECK_EQ((int)bmp.palette[(std::size_t)i0 * 3 + 1], 52);
    CHECK_EQ((int)bmp.palette[(std::size_t)i0 * 3 + 2], 28);
}

// --- The masked span DEFAULT rule stays the exact index-0 key (byte-identical
//     to the binary's software masked span and every prior test). ------------
TEST(ColourKeyW5, MaskedSpanDefaultKeysIndex0) {
    // 4-wide texture row: indices 0,1,2,3 — palette maps idx -> 0x100+idx so the
    // resolved value is never the (unset) colorKey565=0 except where idx==0.
    Texture t;
    TextureSetSize(t, 4);
    for (int i = 0; i < 16; ++i) t.texels[i] = (u8)(i & 3);
    std::vector<u16> pal(256);
    for (int i = 0; i < 256; ++i) pal[i] = (u16)(0x100 + i);

    SpanTexParams p{};
    p.texBase = t.texels.data();
    p.palBase = pal.data();
    p.texelMask = t.texelMask;
    p.widthShift = t.widthShift;
    p.uStepFrac = 1 << 16;     // one texel per pixel, U
    p.vStep = 0;
    // useColorKey defaults false -> index-0 rule.

    u16 dst[4];
    for (int i = 0; i < 4; ++i) dst[i] = 0xBEEF;   // sentinel "untouched"
    RasterState rs{};
    rs.spanLen = 4;
    FillSpanTexturedMasked(rs, dst, 0, 0, p);
    // idx 0 -> skipped (sentinel kept); idx 1..3 -> palette value written.
    CHECK_EQ(dst[0], (u16)0xBEEF);          // index 0 transparent
    CHECK_EQ(dst[1], (u16)(0x100 + 1));
    CHECK_EQ(dst[2], (u16)(0x100 + 2));
    CHECK_EQ(dst[3], (u16)(0x100 + 3));
}

// --- The plain span is unchanged for the SAME inputs (no skipping at all). ---
TEST(ColourKeyW5, PlainSpanNeverSkips) {
    Texture t;
    TextureSetSize(t, 4);
    for (int i = 0; i < 16; ++i) t.texels[i] = (u8)(i & 3);
    std::vector<u16> pal(256);
    for (int i = 0; i < 256; ++i) pal[i] = (u16)(0x100 + i);
    SpanTexParams p{};
    p.texBase = t.texels.data();
    p.palBase = pal.data();
    p.texelMask = t.texelMask;
    p.widthShift = t.widthShift;
    p.uStepFrac = 1 << 16;
    u16 dst[4];
    for (int i = 0; i < 4; ++i) dst[i] = 0xBEEF;
    RasterState rs{};
    rs.spanLen = 4;
    FillSpanTextured(rs, dst, 0, 0, p);
    CHECK_EQ(dst[0], (u16)(0x100 + 0));     // even index 0 is written
    CHECK_EQ(dst[1], (u16)(0x100 + 1));
    CHECK_EQ(dst[2], (u16)(0x100 + 2));
    CHECK_EQ(dst[3], (u16)(0x100 + 3));
}

// --- COLOUR-KEY rule: skip texels whose RESOLVED 16bpp value == colorKey565
//     (black == 0), regardless of the source INDEX. This is the DDraw
//     KEYSRC-on-pal[0] behaviour the 24-bit foliage relies on. -------------
TEST(ColourKeyW5, MaskedSpanKeysResolvedBlack) {
    // Texture row indices 0,1,2,3. Put BLACK (565 0) at index 2 (NOT index 0),
    // exactly as the octree quantizer would (black at a non-zero index). Index 0
    // resolves to a non-black colour. The key must skip index 2 (resolved black)
    // and KEEP index 0 (resolved non-black).
    Texture t;
    TextureSetSize(t, 4);
    for (int i = 0; i < 16; ++i) t.texels[i] = (u8)(i & 3);
    std::vector<u16> pal(256, (u16)0x1234);
    pal[0] = 0x07E0;   // index 0 -> green (NOT black)
    pal[1] = 0x001F;   // index 1 -> blue
    pal[2] = 0x0000;   // index 2 -> BLACK (the colour key)
    pal[3] = 0xF800;   // index 3 -> red

    SpanTexParams p{};
    p.texBase = t.texels.data();
    p.palBase = pal.data();
    p.texelMask = t.texelMask;
    p.widthShift = t.widthShift;
    p.uStepFrac = 1 << 16;
    p.useColorKey = true;
    p.colorKey565 = 0x0000;     // black

    u16 dst[4];
    for (int i = 0; i < 4; ++i) dst[i] = 0xBEEF;
    RasterState rs{};
    rs.spanLen = 4;
    FillSpanTexturedMasked(rs, dst, 0, 0, p);
    CHECK_EQ(dst[0], (u16)0x07E0);          // resolved green -> written
    CHECK_EQ(dst[1], (u16)0x001F);          // resolved blue  -> written
    CHECK_EQ(dst[2], (u16)0xBEEF);          // resolved BLACK -> SKIPPED (key)
    CHECK_EQ(dst[3], (u16)0xF800);          // resolved red   -> written
}

// --- Light-row invariance: a HiColTab ramp scales black to black at every row,
//     so the key still fires when lightRow8 selects a ramp row. ------------
TEST(ColourKeyW5, ColourKeyIsLightRowInvariant) {
    Texture t;
    TextureSetSize(t, 4);
    for (int i = 0; i < 16; ++i) t.texels[i] = 0;   // every texel index 0
    // palBase modelling a ramp: row 0 entry 0 == black, row 5 entry 0 == black
    // (a ramp scales each channel by L/62 -> black stays black). Build a block
    // big enough for lightRow8 = 5<<8 = 0x500.
    std::vector<u16> pal((size_t)0x600, (u16)0xABCD);
    pal[0x000] = 0x0000;       // row 0, index 0 == black
    pal[0x500] = 0x0000;       // row 5, index 0 == black (ramp-scaled)
    SpanTexParams p{};
    p.texBase = t.texels.data();
    p.palBase = pal.data();
    p.texelMask = t.texelMask;
    p.widthShift = t.widthShift;
    p.uStepFrac = 1 << 16;
    p.useColorKey = true;
    p.colorKey565 = 0x0000;
    p.lightRow8 = 5u << 8;     // select ramp row 5
    u16 dst[4];
    for (int i = 0; i < 4; ++i) dst[i] = 0xBEEF;
    RasterState rs{};
    rs.spanLen = 4;
    FillSpanTexturedMasked(rs, dst, 0, 0, p);
    for (int i = 0; i < 4; ++i) CHECK_EQ(dst[i], (u16)0xBEEF);  // all skipped
}

// --- End-to-end through the RGBZ triangle leaf: a black-background texture
//     rendered through the KEYED variant leaves the cleared backdrop showing,
//     while the OPAQUE variant overwrites it. Pins the transparent pixel count.
TEST(ColourKeyW5, RgbzKeyedTrianglePunchesBlackBackground) {
    // 8x8 texture: a non-black foreground blob in the centre, black elsewhere
    // (the foliage-with-black-backdrop case). Index 0 == foreground colour,
    // index 1 == black -> the key skips index-1 texels by RESOLVED black.
    Texture t;
    TextureSetSize(t, 8);
    for (int y = 0; y < 8; ++y)
        for (int x = 0; x < 8; ++x) {
            bool blob = (x >= 3 && x <= 4 && y >= 3 && y <= 4);
            t.texels[(size_t)y * 8 + x] = blob ? 0 : 1;   // 0=fg, 1=black
        }
    t.mipWidth = 8;
    std::vector<u16> pal(256, (u16)0xFFFF);
    pal[0] = 0x07E0;   // foreground green
    pal[1] = 0x0000;   // black (the key colour)

    const int W = 16, H = 16;
    RgbzVertex tri[3] = {
        {0.0f, 0.0f, 0.0f, 0.0f, 0},
        {(float)W, 0.0f, 1.0f, 0.0f, 0},
        {0.0f, (float)H, 0.0f, 1.0f, 0},
    };
    // UVs are texel coords (caller multiplies by width): scale 0..1 -> 0..8.
    for (auto& v : tri) { v.u *= 8.0f; v.v *= 8.0f; }

    // OPAQUE render over a magenta backdrop: black texels overwrite to black.
    Surface* op = Make16(W, H);
    SurfaceColorFill(op, 0xFF, 0x00, 0xFF);              // magenta backdrop
    RasterizeTexturedTriangleRgbz(op, tri, t, pal.data());
    // KEYED render over the same magenta backdrop: black texels are SKIPPED.
    Surface* kf = Make16(W, H);
    SurfaceColorFill(kf, 0xFF, 0x00, 0xFF);
    RasterizeTexturedTriangleRgbzMasked(kf, tri, t, pal.data(), /*polyFlags38=*/0,
                                        /*colorKey565=*/0x0000);

    const u16* opx = (const u16*)op->pixels;
    const u16* kpx = (const u16*)kf->pixels;
    const u16 magenta = (u16)(((0xFF & 0xF8) << 8) | ((0x00 & 0xFC) << 3) | (0xFF >> 3));
    int opBlack = 0, kBlack = 0, kBackdrop = 0;
    for (int i = 0; i < W * H; ++i) {
        if (opx[i] == 0x0000) ++opBlack;          // opaque: black written
        if (kpx[i] == 0x0000) ++kBlack;           // keyed: black should be rare
        if (kpx[i] == magenta) ++kBackdrop;       // keyed: backdrop shows through
    }
    // The opaque render writes many black texels into the covered half-triangle.
    CHECK(opBlack > 8);
    // The keyed render writes NO black (every covered black texel was skipped;
    // the backdrop is magenta, never black).
    CHECK_EQ(kBlack, 0);
    // The backdrop materially shows through under the keyed render (the black
    // texels left it untouched) where the opaque render had overwritten it.
    CHECK(kBackdrop > opBlack - 4);
}

// --- The colour key skips texels by VALUE even when index 0 is a real colour,
//     proving option (b) (resolve-then-key), not the index-0 fallback. -------
TEST(ColourKeyW5, KeyByValueNotIndexZero) {
    Texture t;
    TextureSetSize(t, 2);
    t.texels[0] = 0; t.texels[1] = 0; t.texels[2] = 0; t.texels[3] = 0;
    t.mipWidth = 2;
    // index 0 resolves to BLACK -> under the key it is transparent EVEN THOUGH
    // the default rule "skip index 0" would also skip it. Make index 0 black and
    // verify the colour-key path also skips (consistent), then flip to non-black.
    std::vector<u16> palBlack(256, (u16)0x0001);
    palBlack[0] = 0x0000;
    SpanTexParams p{};
    p.texBase = t.texels.data();
    p.palBase = palBlack.data();
    p.texelMask = t.texelMask;
    p.widthShift = t.widthShift;
    p.uStepFrac = 1 << 16;
    p.useColorKey = true;
    p.colorKey565 = 0x0000;
    u16 dst[2] = {0xBEEF, 0xBEEF};
    RasterState rs{};
    rs.spanLen = 2;
    FillSpanTexturedMasked(rs, dst, 0, 0, p);
    CHECK_EQ(dst[0], (u16)0xBEEF);   // index 0 -> black -> skipped
    CHECK_EQ(dst[1], (u16)0xBEEF);

    // Now index 0 resolves to a NON-black colour: the colour-key path must NOT
    // skip it (the index-0 fallback WOULD have — proving we key by value).
    std::vector<u16> palGreen(256, (u16)0x0001);
    palGreen[0] = 0x07E0;            // index 0 -> green
    p.palBase = palGreen.data();
    dst[0] = 0xBEEF; dst[1] = 0xBEEF;
    rs.spanLen = 2;
    FillSpanTexturedMasked(rs, dst, 0, 0, p);
    CHECK_EQ(dst[0], (u16)0x07E0);   // index 0 -> green -> WRITTEN (not keyed)
    CHECK_EQ(dst[1], (u16)0x07E0);
}
