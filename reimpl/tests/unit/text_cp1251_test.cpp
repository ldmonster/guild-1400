// unit: CP1251 Cyrillic 5x7 glyph lookup + draw advance.
#include "test.h"
#include "render/text_cp1251.h"
#include "render/text_raster.h"   // kBuiltinFontBitmap
#include "render/font.h"
#include "render/surface.h"
#include "render/types.h"
#include <cstdint>
#include <cstring>
using namespace guild;

TEST(TextCp1251, AsciiMatchesBuiltinFont) {
    u8 table[256]; render::FontInitGlyphTable(table);
    for (char c : {'A','Z','a','5',':','#'}) {
        u8 got[7]; CHECK(render::Glyph5x7Cp1251((u8)c, got));
        const u8* want = render::kBuiltinFontBitmap + 7 * table[(u8)c];
        CHECK(std::memcmp(got, want, 7) == 0);   // ASCII falls through to the engine ROM
    }
}

TEST(TextCp1251, SpaceHasNoGlyph) {
    u8 g[7]; CHECK(!render::Glyph5x7Cp1251(0x20, g));
    for (int i = 0; i < 7; ++i) CHECK_EQ((int)g[i], 0);
}

TEST(TextCp1251, CyrillicPresentAndSmallCaps) {
    // Every CP1251 uppercase (0xC0..0xDF) + Ё have a non-empty glyph.
    for (int b = 0xC0; b <= 0xDF; ++b) {
        u8 g[7]; CHECK(render::Glyph5x7Cp1251((u8)b, g));
        int set = 0; for (int i = 0; i < 7; ++i) set += __builtin_popcount(g[i]);
        CHECK(set > 0);
    }
    u8 yo[7]; CHECK(render::Glyph5x7Cp1251(0xA8, yo));   // Ё
    // Lowercase shares the uppercase shape (small-caps): а(0xE0) == А(0xC0).
    u8 lo[7], up[7];
    CHECK(render::Glyph5x7Cp1251(0xE0, lo));
    CHECK(render::Glyph5x7Cp1251(0xC0, up));
    CHECK(std::memcmp(lo, up, 7) == 0);
}

TEST(TextCp1251, DrawAdvancesAndPaints) {
    render::Surface* fb = render::SurfaceCreate(80, 16, 32); CHECK(fb);
    render::SurfaceColorFill(fb, 0, 0, 0);
    // "Аб" (0xC0 0xE1) -> 2 glyphs, advance 12 px, paints some pixels.
    const char s[] = {(char)0xC0, (char)0xE1, 0};
    int endx = render::DrawTextCp1251(fb, 2, 2, s, 255, 255, 255);
    CHECK_EQ(endx, 2 + 12);
    int lit = 0;
    for (int y = 0; y < 16; ++y) {
        auto* row = reinterpret_cast<std::uint32_t*>(
            static_cast<std::uint8_t*>(fb->pixels) + (std::size_t)y * fb->pitch);
        for (int x = 0; x < 80; ++x) if ((row[x] & 0xFFFFFF) != 0) ++lit;
    }
    CHECK(lit > 10);
    render::SurfaceDestroy(fb);
}
