// Unit tests for the built-in 5x7 bitmap-font rasterizer (render/text_raster).
// Golden vectors: a deterministic glyph bitmap -> exact output pixels in a small
// in-memory framebuffer. The reference grids were derived directly from the
// recovered font bytes (render/text_raster.cpp kBuiltinFontBitmap) and the runtime
// glyph map (render/font.cpp FontInitGlyphTable).
#include "test.h"
#include "render/text_raster.h"
#include "render/font.h"
#include <vector>
#include <cstring>

using namespace guild;
using namespace guild::render;

// Build a PresentGlobals pointing at a freshly-allocated 16bpp framebuffer.
namespace {

struct Fb16 {
    int w, h;
    std::vector<u16> px;
    PresentGlobals g;
    Fb16(int width, int height) : w(width), h(height), px(width * height, 0) {
        g.mode         = PresentBackend::GdiBitBlt;
        g.targetBase   = reinterpret_cast<std::uintptr_t>(px.data());
        g.pitchExtra   = 2;                // bytes per pixel (16bpp)
        g.pitchBytes   = width * 2;        // row pitch in bytes
        g.lockBitDepth = 16;
        g.dibStride    = width;            // == screen width
        g.screenHeight = height;
    }
    u16 at(int x, int y) const { return px[y * w + x]; }
};

// Render one glyph cell to an ASCII grid for comparison ("#" = pixel set to color,
// "." = background 0). Reads back the 5x7 cell drawn at (x0,y0).
void readGrid(const Fb16& fb, int x0, int y0, u16 color, char out[7][6]) {
    for (int row = 0; row < 7; ++row) {
        for (int col = 0; col < 5; ++col)
            out[row][col] = (fb.at(x0 + col, y0 + row) == color) ? '#' : '.';
        out[row][5] = '\0';
    }
}

bool gridEquals(const char got[7][6], const char* const expect[7]) {
    for (int r = 0; r < 7; ++r)
        if (std::strcmp(got[r], expect[r]) != 0)
            return false;
    return true;
}

// Shared runtime glyph map (filled once via FontInitGlyphTable).
const u8* glyphMap() {
    static u8 map[256];
    static bool init = [] { FontInitGlyphTable(map); return true; }();
    (void)init;
    return map;
}

} // namespace

TEST(TextRaster, GlyphA_16bpp_GoldenGrid) {
    Fb16 fb(32, 16);
    const u16 color = 0xF81F;          // magenta in 565 — distinct from background
    u32 ret = DrawGlyph((u8)'A', 3, color, 4, glyphMap(), fb.g);
    CHECK_EQ(ret, 16u);                // depth returned on the drawn path

    char got[7][6];
    readGrid(fb, 3, 4, color, got);
    const char* expect[7] = {
        ".###.",
        "#...#",
        "#...#",
        "#...#",
        "#####",
        "#...#",
        "#...#",
    };
    CHECK(gridEquals(got, expect));
}

TEST(TextRaster, GlyphHash_16bpp_GoldenGrid) {
    Fb16 fb(16, 16);
    const u16 color = 0x07E0;
    DrawGlyph((u8)'#', 1, color, 1, glyphMap(), fb.g);
    char got[7][6];
    readGrid(fb, 1, 1, color, got);
    const char* expect[7] = {
        ".#.#.",
        ".#.#.",
        "#####",
        ".#.#.",
        "#####",
        ".#.#.",
        ".#.#.",
    };
    CHECK(gridEquals(got, expect));
}

TEST(TextRaster, GlyphDigit0_16bpp_GoldenGrid) {
    Fb16 fb(16, 16);
    const u16 color = 0xFFFF;
    DrawGlyph((u8)'0', 2, color, 2, glyphMap(), fb.g);
    char got[7][6];
    readGrid(fb, 2, 2, color, got);
    const char* expect[7] = {
        ".###.",
        "#...#",
        "#..##",
        "#.#.#",
        "##..#",
        "#...#",
        ".###.",
    };
    CHECK(gridEquals(got, expect));
}

// Background pixels (the '.' cells) must remain untouched (the original only writes
// set bits; transparent cells keep their prior value).
TEST(TextRaster, GlyphLeavesBackgroundUntouched) {
    Fb16 fb(16, 16);
    for (auto& p : fb.px) p = 0x1234;          // non-zero background
    const u16 color = 0xBEEF;
    DrawGlyph((u8)'A', 1, color, 1, glyphMap(), fb.g);
    // top-left cell of 'A' (row0,col0) is '.', must still be background
    CHECK_EQ(fb.at(1, 1), (u16)0x1234);
    // row0,col1 is '#', must be color
    CHECK_EQ(fb.at(2, 1), color);
    // a pixel well outside the 5x7 cell is untouched
    CHECK_EQ(fb.at(10, 10), (u16)0x1234);
}

// 32bpp path writes 4-byte pixels.
TEST(TextRaster, GlyphA_32bpp_GoldenGrid) {
    int w = 16, h = 16;
    std::vector<u32> px(w * h, 0);
    PresentGlobals g;
    g.targetBase   = reinterpret_cast<std::uintptr_t>(px.data());
    g.pitchExtra   = 4;
    g.pitchBytes   = w * 4;
    g.lockBitDepth = 32;
    g.dibStride    = w;
    g.screenHeight = h;

    const u32 color = 0xDEADBEEF;
    u32 ret = DrawGlyph((u8)'A', 1, (int)color, 1, glyphMap(), g);
    CHECK_EQ(ret, 32u);
    // row4 (the crossbar of 'A') is fully set: cols 0..4 all == color
    for (int c = 0; c < 5; ++c)
        CHECK_EQ(px[(1 + 4) * w + (1 + c)], color);
    // row0 col0 is '.' -> 0
    CHECK_EQ(px[(1 + 0) * w + (1 + 0)], 0u);
}

// Clip: a glyph whose right edge exceeds width draws NOTHING and returns the
// failing extent (x+5).
TEST(TextRaster, ClipRightEdgeRejected) {
    Fb16 fb(6, 16);                    // width 6
    u32 ret = DrawGlyph((u8)'A', 2, 0xFFFF, 0, glyphMap(), fb.g); // 2+5=7 > 6
    CHECK_EQ(ret, 7u);
    for (u16 v : fb.px) CHECK_EQ(v, (u16)0);   // untouched
}

// Clip: bottom edge beyond height draws nothing, returns y+7.
TEST(TextRaster, ClipBottomEdgeRejected) {
    Fb16 fb(16, 6);                    // height 6
    u32 ret = DrawGlyph((u8)'A', 0, 0xFFFF, 1, glyphMap(), fb.g); // 1+7=8 > 6
    CHECK_EQ(ret, 8u);
    for (u16 v : fb.px) CHECK_EQ(v, (u16)0);
}

// A non-16/32 depth is a no-op (returns depth) — the original gates on 16/32 only.
TEST(TextRaster, UnsupportedDepthNoOp) {
    Fb16 fb(16, 16);
    fb.g.lockBitDepth = 24;            // not rasterized
    u32 ret = DrawGlyph((u8)'A', 1, 0xFFFF, 1, glyphMap(), fb.g);
    CHECK_EQ(ret, 24u);
    for (u16 v : fb.px) CHECK_EQ(v, (u16)0);
}
