// guild::render — CP1251 Cyrillic 5x7 text drawing. See text_cp1251.h.
#include "render/text_cp1251.h"

#include "render/font.h"
#include "render/surface.h"
#include "render/text_raster.h"   // kBuiltinFontBitmap
#include "render/types.h"

#include <cstring>

namespace guild::render {
namespace {

// One authored Cyrillic uppercase glyph: 7 rows of 5 columns ('#' = set). Column 0
// is the leftmost (engine bit 0x10). Order is CP1251 0xC0..0xDF (А..Я) then Ё.
struct GlyphArt { char rows[7][6]; };

// 0xC0..0xDF: А Б В Г Д Е Ж З И Й К Л М Н О П Р С Т У Ф Х Ц Ч Ш Щ Ъ Ы Ь Э Ю Я
const GlyphArt kUpper[32] = {
    {{".###.","#...#","#...#","#####","#...#","#...#","#...#"}}, // А
    {{"#####","#....","#....","####.","#...#","#...#","####."}}, // Б
    {{"####.","#...#","#...#","####.","#...#","#...#","####."}}, // В
    {{"#####","#....","#....","#....","#....","#....","#...."}}, // Г
    {{".###.",".#.#.",".#.#.",".#.#.",".#.#.","#####","#...#"}}, // Д
    {{"#####","#....","#....","####.","#....","#....","#####"}}, // Е
    {{"#.#.#","#.#.#","#.#.#","#####","#.#.#","#.#.#","#.#.#"}}, // Ж
    {{"####.","#...#","...#.","..##.","...#.","#...#","####."}}, // З
    {{"#...#","#..##","#.#.#","#.#.#","##..#","#...#","#...#"}}, // И
    {{".#.#.","..#..","#.#.#","#.#.#","##..#","#...#","#...#"}}, // Й
    {{"#...#","#..#.","#.#..","##...","#.#..","#..#.","#...#"}}, // К
    {{"..###",".#..#",".#..#",".#..#",".#..#",".#..#","#...#"}}, // Л
    {{"#...#","##.##","#.#.#","#.#.#","#...#","#...#","#...#"}}, // М
    {{"#...#","#...#","#...#","#####","#...#","#...#","#...#"}}, // Н
    {{".###.","#...#","#...#","#...#","#...#","#...#",".###."}}, // О
    {{"#####","#...#","#...#","#...#","#...#","#...#","#...#"}}, // П
    {{"####.","#...#","#...#","####.","#....","#....","#...."}}, // Р
    {{".###.","#...#","#....","#....","#....","#...#",".###."}}, // С
    {{"#####","..#..","..#..","..#..","..#..","..#..","..#.."}}, // Т
    {{"#...#","#...#","#...#",".####","....#","#...#",".###."}}, // У
    {{"..#..",".###.","#.#.#","#.#.#","#.#.#",".###.","..#.."}}, // Ф
    {{"#...#","#...#",".#.#.","..#..",".#.#.","#...#","#...#"}}, // Х
    {{"#..#.","#..#.","#..#.","#..#.","#..#.","#####","....#"}}, // Ц
    {{"#...#","#...#","#...#",".####","....#","....#","....#"}}, // Ч
    {{"#.#.#","#.#.#","#.#.#","#.#.#","#.#.#","#.#.#","#####"}}, // Ш
    {{"#.#.#","#.#.#","#.#.#","#.#.#","#.#.#","#####","....#"}}, // Щ
    {{"##...",".#...",".#...",".###.",".#..#",".#..#",".###."}}, // Ъ
    {{"#...#","#...#","#...#","##..#","#.#.#","#.#.#","##..#"}}, // Ы
    {{"#....","#....","#....","####.","#...#","#...#","####."}}, // Ь
    {{".###.","#...#","....#","..###","....#","#...#",".###."}}, // Э
    {{"#.##.","#.#.#","#.#.#","##.##","#.#.#","#.#.#","#.##."}}, // Ю
    {{".####","#...#","#...#",".####","..#.#",".#..#","#...#"}}, // Я
};
// Ё (CP1251 0xA8): Е with a diaeresis.
const GlyphArt kYo = {{"#.#..","#####","#....","####.","#....","#....","#####"}};

// Pack a 7x5 art into 7 bytes (bit 0x10 = column 0).
void Pack(const GlyphArt& a, u8 out[7]) {
    for (int r = 0; r < 7; ++r) {
        u8 b = 0;
        for (int c = 0; c < 5; ++c)
            if (a.rows[r][c] == '#') b |= (u8)(0x10 >> c);
        out[r] = b;
    }
}

const u8* AsciiGlyphMap() {
    static u8 table[256];
    static bool init = false;
    if (!init) { FontInitGlyphTable(table); init = true; }
    return table;
}

inline void PutPx(Surface* s, int x, int y, u8 r, u8 g, u8 b) {
    const int W = s->widthPx ? s->widthPx : s->width;
    if (x < 0 || y < 0 || x >= W || y >= s->height) return;
    if (s->bpp == 32) {
        auto* px = reinterpret_cast<u32*>(
            static_cast<u8*>(s->pixels) + (std::size_t)y * s->pitch) + x;
        *px = 0xFF000000u | ((u32)r << 16) | ((u32)g << 8) | b;
    } else {
        SurfaceSetPixelRgb(s, x, y, r, g, b);
    }
}

} // namespace

bool Glyph5x7Cp1251(u8 ch, u8 out[7]) {
    std::memset(out, 0, 7);
    if (ch == 0x20) return false;                       // space
    if (ch < 0x80) {                                    // ASCII -> engine font ROM
        const u8 gi = AsciiGlyphMap()[ch];
        if (gi == 0) return false;
        std::memcpy(out, kBuiltinFontBitmap + 7 * gi, 7);
        return true;
    }
    // CP1251 Cyrillic (small-caps: lowercase shares the uppercase shape).
    int up = -1;
    if (ch >= 0xC0 && ch <= 0xDF) up = ch - 0xC0;       // А..Я
    else if (ch >= 0xE0 && ch <= 0xFF) up = ch - 0xE0;  // а..я -> uppercase shape
    else if (ch == 0xA8 || ch == 0xB8) { Pack(kYo, out); return true; }  // Ё/ё
    if (up < 0 || up >= 32) return false;
    Pack(kUpper[up], out);
    return true;
}

int DrawTextCp1251(Surface* s, int x, int y, const char* text, u8 r, u8 g, u8 b) {
    if (!s || !s->pixels || !text) return x;
    for (const unsigned char* p = reinterpret_cast<const unsigned char*>(text); *p; ++p) {
        u8 gl[7];
        if (Glyph5x7Cp1251(*p, gl)) {
            for (int row = 0; row < 7; ++row)
                for (int col = 0; col < 5; ++col)
                    if (gl[row] & (0x10 >> col)) PutPx(s, x + col, y + row, r, g, b);
        }
        x += 6;
    }
    return x;
}

} // namespace guild::render
