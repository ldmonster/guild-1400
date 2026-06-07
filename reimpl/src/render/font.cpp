#include "render/font.h"
#include <cstring>

namespace guild::render {

// gilde.exe 0x42e350 — VIBE_Font_InitGlyphTable.
// The original's leading three memset calls are the MSVC "fill 256 bytes with
// alignment fixup" idiom; collapsed to one memset (behaviour-identical). Then it
// assigns individual entries and two qmemcpy literal blocks. Order matters: the
// qmemcpy at result+33 (32 bytes) overwrites entries 33..64, so the explicit
// A..Z/a..d/space/digit assignments below 65..100 and 32 are applied first by
// the original exactly as transcribed here.
int FontInitGlyphTable(u8* table) {
    if (!table)
        return 0;
    std::memset(table, 0, 256);

    table[32] = 1;            // space
    // 'A'..'Z' -> 2..27
    for (int i = 0; i < 26; ++i)
        table[65 + i] = (u8)(2 + i);
    // 'a'..'d' -> 28..31
    table[97]  = 28;
    table[98]  = 29;
    table[99]  = 30;
    table[100] = 31;

    // result+101: " !\"#$%&'()*+,-./012345" (22 bytes) -> glyph indices for the
    // ASCII bytes of that literal stored at 'e'.. (faithful copy).
    std::memcpy(table + 101, " !\"#$%&'()*+,-./012345", 22);

    table[92] = 55;
    table[91] = 64;
    table[93] = 65;

    // result+33: 32-byte remap block overwriting entries 33..64.
    std::memcpy(table + 33, "EHIJKB8>?L<:;96PQRSTUVWXY=GCMDFN", 32);

    table[95]  = 79;
    table[126] = 90;
    return 0;
}

} // namespace guild::render
