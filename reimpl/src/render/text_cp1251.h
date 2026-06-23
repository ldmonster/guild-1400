#pragma once
// =============================================================================
// guild::render — CP1251 (Cyrillic) text drawing for the bitmap glyph font.
//
// The engine renders menu/form text with a built-in 5x7 bitmap font (the glyph
// ROM kBuiltinFontBitmap @0x62D59C + the FontInitGlyphTable index map). The IDA
// binary we reverse is a Latin build, so the reconstructed font only carries the
// ASCII glyphs — but the shipped install's localized text (e.g. the ChooseCity
// city descriptions, _STADTAUSWAHL_<city>_BESCHR) is Russian CP1251.
//
// This module ADDS a CP1251 Cyrillic 5x7 glyph set in the SAME row format the
// engine font uses (bit 0x10 = column 0, 7 rows) so that real localized text
// renders in the reconstructed font's style. ASCII bytes (< 0x80) fall through to
// the engine's own glyph ROM, so Latin text is byte-identical to render::DrawText.
// Lowercase Cyrillic (0xE0..0xFF, plus ё 0xB8) is rendered with the uppercase
// shapes (small-caps) — a legible 5x7 reduction, not a separate lowercase set.
//
// Not a 1:1 reconstruction of a specific shipped Cyrillic font (that font is not
// in the reversed Latin binary); it is the host glyph set that lets the REAL
// localized data render, matching how the reimpl already substitutes the engine's
// bitmap font for the original GDI/bitmap text path.
// =============================================================================
#include "guild/common/types.h"

namespace guild::render {

struct Surface;

// Fill `out` (7 bytes, bit 0x10 = column 0) with the 5x7 glyph for byte `ch`:
// ASCII printable -> the engine font ROM; CP1251 Cyrillic -> this module's set.
// Returns false (and leaves `out` zeroed) for bytes with no glyph (e.g. space).
bool Glyph5x7Cp1251(u8 ch, u8 out[7]);

// Draw `text` (CP1251 bytes) at (x,y) onto a 16/32 bpp surface in colour (r,g,b),
// advancing 6 px per character (the engine's fixed advance), space = blank. 32 bpp
// is written raw 0xAARRGGBB (matching the scene/overlay convention). Returns the x
// past the last glyph.
int DrawTextCp1251(Surface* s, int x, int y, const char* text, u8 r, u8 g, u8 b);

} // namespace guild::render
