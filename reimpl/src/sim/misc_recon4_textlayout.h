#pragma once
// misc_recon4_textlayout.{h,cpp} — VIBE_Property_Set font text layout (gilde.exe)
//
//   0x4159dc VIBE_Property_Set
//
// Despite the placeholder name, 0x4159dc is the engine's right-to-left-advancing
// bitmap-font string drawer. It walks a C string, looks up each glyph's metrics in
// the active font record (dword_62D244, lazily resolved via VIBE_Property_Validate
// /VIBE_State_Update if unset), advances the pen, and draws each glyph through the
// render helpers VIBE_Animation_Basic/Advanced (0x5d85b8/0x5d89bc) and
// VIBE_Velocity_Apply (0x5d883c). The pen-advance / glyph-metric / special-char
// layout logic is GENUINE engine behaviour and is reconstructed 1:1; the actual
// glyph blits are routed through inert hooks (rule 3 render boundary).
//
// Original control flow (a1=startX, a2=string, a3=y, a4=ctx/colour, a5=mode flags):
//   pen = a1
//   if (!font) font = State_Update(Property_Validate("_FONT"))
//   lineHeight (dword_69FFB0) = *(u16*)(font+46)
//   for (i=0; i < strlen(string); ++i):
//       ch = string[i]
//       glyph = Coord_Transform(font, ch)          // glyph record
//       pen  -= *(u16*)(glyph+22)                   // pre-advance by glyph "left"
//       if (ch == '~') { /* skip: no draw, no post-advance */ }
//       else if (ch == ' ') pen += dword_62D270     // space width
//       else {
//           if (a5 & 4) Velocity_Apply(pen+6, a3+6, font, a4, ch)   // shadow/outline
//           Animation_Basic(pen, a3, font, a4, ch)                   // main glyph
//           if (a5 & 2) Animation_Advanced(pen, a3, font, a4, ch)
//           if (a5 & 1) Velocity_Apply(pen, a3, font, a4, ch)
//           pen += dword_62D274 + *(u16*)(glyph+26)  // tracking + glyph advance
//       }
//       if (pen >= (dword_69FFBC >> 16)) break       // hit right clip edge
//   dword_62D27C = lineHeight; dword_62D278 = pen - a1   // publish extent
//   return lineHeight
//
// NOTE the original reads strlen(a2) every iteration (no caching) — preserved.

#include "guild/common/types.h"
#include <cstddef>

namespace guild::sim {

// Glyph metrics the layout needs from a glyph record (via Coord_Transform):
//   +22 leftBearing (u16, subtracted before draw)
//   +26 advance     (u16, added after draw)
struct GlyphMetrics {
    u16 leftBearing = 0;  // *(u16*)(glyph+22)
    u16 advance     = 0;  // *(u16*)(glyph+26)
};

// Inert render/env hooks for the text drawer.
struct TextLayoutEnv {
    // Lazily resolve the active font if unset (returns font record handle).
    // Mirrors the !dword_62D244 branch; default returns the passed-in font.
    void* font = nullptr;
    // Coord_Transform(font, ch) -> glyph metrics.
    GlyphMetrics (*glyphMetrics)(void* font, u8 ch) = nullptr;
    u16  lineHeight = 0;        // dword_69FFB0 (*(u16*)(font+46))
    int  spaceWidth = 0;        // dword_62D270
    int  tracking   = 0;        // dword_62D274
    int  clipRight  = 0;        // dword_69FFBC >> 16

    // Glyph draw callbacks (inert by default). x,y = pen position.
    void (*drawBasic)(void* font, int x, int y, void* ctx, u8 ch) = nullptr;  // Animation_Basic
    void (*drawAdvanced)(void* font, int x, int y, void* ctx, u8 ch) = nullptr;// Animation_Advanced
    void (*drawVelocity)(void* font, int x, int y, void* ctx, u8 ch) = nullptr;// Velocity_Apply
};

struct TextLayoutResult {
    u16 lineHeight = 0;  // dword_62D27C / return value
    int width      = 0;  // dword_62D278 (pen - startX)
};

// gilde.exe 0x4159dc — reproduces the pen-advance/draw loop 1:1. `mode` is a5.
// Returns lineHeight (== the original's eax). `out` (if non-null) gets the extent.
u16 PropertySetDrawText(const TextLayoutEnv& env, int startX, const char* str,
                        int y, void* ctx, unsigned char mode,
                        TextLayoutResult* out);

} // namespace guild::sim
