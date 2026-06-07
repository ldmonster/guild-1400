#pragma once
#include "guild/common/types.h"
#include "render/surface.h"

// "Paintbox" 2D drawing from gilde.exe (d2_Plot / d2_DrawLine). The originals
// fetched the active paintbox window from global arrays (dword_67EB80[238*win])
// and a global brush size (byte_62D28C); here those are gathered into a small
// PaintboxState so the pixel output is self-contained and testable. The shape /
// sprite blit entry points (DrawShape*) are thin wrappers over the sprite/anim
// module and are NOT reproduced here (see module report).
namespace guild::render {

// Active paintbox: target surface, brush radius (0/1/2), and clip border inset.
struct PaintboxState {
    Surface* surface = nullptr; // the paintbox's backing surface (v->[10])
    u8  brush = 0;              // byte_62D28C: 0 = single pixel, 1/2 = plus/diamond
    int border = 0;            // clip inset (the original reuses brush as border)
};

// gilde.exe 0x41e920 — VIBE_Paintbox_DrawScaledRegion (x@eax, y@edx, g@bl, ...).
// Plot at (x,y) with the current brush, clipped to [border, w-border) x
// [border, h-border). brush 0 => one pixel; 1 => 5-pixel plus; 2 => 13-pixel
// diamond. Colour is passed as r,g,b (the original threaded green in bl and the
// other two through registers it recovered from the colour state).
void PaintboxDrawScaledRegion(PaintboxState& pb, int x, int y, u8 r, u8 g, u8 b);

// gilde.exe 0x41ec40 — VIBE_Paintbox_DrawLine. Bresenham over DrawScaledRegion.
void PaintboxDrawLine(PaintboxState& pb, int x0, int y0, int x1, int y1, u8 r, u8 g, u8 b);

// gilde.exe 0x41ee9c — VIBE_Paintbox_Clear. Zero-fill the paintbox surface.
void PaintboxClear(PaintboxState& pb);

} // namespace guild::render
