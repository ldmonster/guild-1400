#pragma once
#include "guild/common/types.h"

// Bitmap-font glyph table init from gilde.exe (the only Font.* function in the
// surface/2D bottom of the renderer; the GDI TextOut path is the present layer).
namespace guild::render {

// gilde.exe 0x42e350 — VIBE_Font_InitGlyphTable.
// Fills a 256-byte ASCII-code -> glyph-index translation table: zero it, then
// assign glyph slots for space, digits, and (re)map A-Z / a-z and punctuation.
// `table` must point to at least 256 bytes. Returns 0 (matches the original).
int FontInitGlyphTable(u8* table);

} // namespace guild::render
