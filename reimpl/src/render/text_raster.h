#pragma once
#include "guild/common/types.h"
#include "render/colorformat.h"
#include "render/surface_present.h"

// =============================================================================
// guild::render — built-in 5x7 bitmap-font rasterization to a LOCKED back buffer.
//
// This is the layer BELOW the rich-text engine (gui/text/*): once the upper layer
// has resolved a literal ASCII byte string and a screen position/colour, these two
// functions stamp each glyph's 5x7 cells straight into the locked framebuffer.
//
// FUNCTIONS RECOVERED (faithful 1:1 of the Hex-Rays pseudocode)
// -----------------------------------------------------------------------------
//   0x434D0C  VIBE_Render_DrawGlyph  — blit one glyph (5 wide x 7 tall) of a fixed
//                                       packed colour at (x,y) into the lock target.
//   0x434E18  VIBE_Render_DrawText   — pack an 8-bit RGB triple to a native pixel,
//                                       then DrawGlyph each non-space char at 6px
//                                       advance; brackets the run with the present
//                                       Acquire/Unlock of the back buffer.
//
// THE GLYPH SOURCE DATA (gilde.exe)
// -----------------------------------------------------------------------------
//   byte_75FB50[256]  ASCII-code -> glyph-index map, built at runtime by
//                     VIBE_Font_InitGlyphTable (render/font.cpp, FontInitGlyphTable).
//   unk_62D59C[91*7]  the 5x7 glyph bitmaps: 7 bytes per glyph, one byte per row;
//                     within a row the columns are tested MSB-first from mask 0x10
//                     (16) down 5 steps, so bit4=leftmost..bit0=rightmost column.
//
// THE LOCK TARGET (gilde.exe present-state block 0x7626xx, modelled by PresentGlobals)
// -----------------------------------------------------------------------------
//   targetBase (dword_7626F0)   byte base of the locked pixels
//   pitchExtra (dword_7626F4)   bytes per pixel (bpp>>3) = the x step in bytes
//   pitchBytes (dword_7626FC)   row pitch in bytes = the y step
//   lockBitDepth (dword_762714) bits per pixel (only 16 and 32 are rasterized)
//   dibStride (dword_7626E0)    framebuffer width  (clip:  x+5 <= width)
//   screenHeight (dword_7626DC) framebuffer height (clip:  y+7 <= height)
// =============================================================================
namespace guild::render {

// The packed 91-glyph 5x7 bitmap font recovered from gilde.exe @0x62D59C
// (91 glyphs * 7 rows = 637 bytes). Exposed so a golden-vector test can index it.
extern const u8 kBuiltinFontBitmap[637];

// gilde.exe 0x42E350 — VIBE_Font_InitGlyphTable lives in render/font.cpp
// (FontInitGlyphTable). Reused here; not redefined.

// gilde.exe 0x434D0C — VIBE_Render_DrawGlyph
//   (__usercall: al=ch, edx=x, ecx=packedColor, ebx=y).
// Look up the glyph row block for ASCII byte `ch` via `glyphMap` (the 256-byte
// table FontInitGlyphTable fills), clip against (x+5<=width, y+7<=height) using
// g.dibStride / g.screenHeight, then write the packed pixel `color` to every set
// bit of the glyph's 7 rows x 5 columns. Only 16bpp and 32bpp targets draw; any
// other depth is a no-op (matches the original's `>=16 && <=16` / `==32` gates).
// Returns the original's `eax`: dword_762714 (depth) when a row block was reached,
// else the failing clip/extent value. `glyphMap` is byte_75FB50, `color` already
// packed (DrawText packs it); `g` is the locked present-state block.
u32 DrawGlyph(u8 ch, int x, int color, int y, const u8* glyphMap,
              const PresentGlobals& g);

// gilde.exe 0x434E18 — VIBE_Render_DrawText
//   (__userpurge: al=x, edx=y, ebx=text, [stack] g, [stack] r).
// Acquire the back buffer; if that fails, return 0. Otherwise pack (r,g,b) to a
// native pixel through `fmt`, then for each non-NUL char advance x by 6px and
// DrawGlyph it (space is skipped — it only advances). Finally release the lock
// (the original's inlined UnlockBackBuffer tail). Returns the last sub-result.
// `glyphMap` is byte_75FB50; `g`/`fmt` model the present-state block + channel
// shifts. The DDraw Acquire/Unlock route through PresentGlobals (shim boundary).
u8 DrawText(int x, int y, const u8* text, u8 r, u8 g_, u8 b,
            const u8* glyphMap, PresentGlobals& g, const ColorFormat& fmt);

} // namespace guild::render
