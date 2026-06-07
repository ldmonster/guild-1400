#pragma once
#include "guild/common/types.h"
#include "render/gfx_archive.h"

// =============================================================================
// guild::render — reusable 3-slice button + window-frame paint helpers.
//
// These compose REAL decoded gilde.gfx art into an arbitrary-width button or a
// bordered window frame, on a 32bpp ARGB (0xAARRGGBB, A=0xFF opaque) framebuffer
// — the same pixel packing menu_assets / gui::Argb8888 use.  All drawing clips
// to the destination and honours per-pixel transparency (A==0 skipped).
//
// THE 3-SLICE BUTTON (confirmed from the binary)
// -----------------------------------------------------------------------------
// The menu buttons are gfx record #174 `_BUTTON_RED`.  The main-menu build
// VIBE_Menu_RunMainMenu @0x529d08 adds them with
//   VIBE_Widget_AddSpriteToWindow(x, y, /*gfx*/174, win)
// i.e. ONE sprite-widget bound to record 174.  The draw path
//   VIBE_Animation_Basic @0x5d85b8 -> VIBE_FrameData_Process @0x5d781c
//   -> VIBE_FrameTable_Index @0x5fbb24
// blits ONE depth-2 shape; the multi-shape stretch is the record layout itself.
//
// Record #174 is a SHAPBANK of SIX depth-2 shapes (dims read from the real
// gilde.gfx — widths 12,12,100,12,12,100, all height 33):
//   shape 0  12x33   left  cap   | state A (normal/up)
//   shape 1  12x33   right cap   |
//   shape 2 100x33   centre face | <- the stretchable middle
//   shape 3  12x33   left  cap   | state B (pressed/hover/down)
//   shape 4  12x33   right cap   |
//   shape 5 100x33   centre face |
// A button of width W is painted: left cap at x, the centre face tiled/clamped
// to span (W - capL - capR), right cap flush at x+W-capR.  Caps are 12px.
//
// THE WINDOW FRAME
// -----------------------------------------------------------------------------
// Two real frame records are supported:
//   * `_MAIN_MENU_RAHMEN` #1776 — a single 300x320 decorated panel shape.  Its
//     border ring is reused: corners blitted 1:1, the four edge strips sampled
//     from the shape's border and repeated to span the requested w/h.
//   * `_WIN_BORDER` #0 — a true tiled border (8x8 corners + 2px edge tiles).
//     Corners blitted at the four corners, edges tiled along each side.
// DrawWindowFrame auto-detects: a 1-shape record uses the panel-ring path; a
// multi-shape record uses the tiled-corner/edge path.
//
// FALLBACK (inert): when the archive isn't ok / the record is absent / decode
// fails, the helpers draw a plain filled rect + 1px outline so callers always
// get *something* drawn, and the return value reports inert (false) vs real.
// =============================================================================
namespace guild::render {

// gfx record index of _BUTTON_RED (record index == gfx id in the archive).
constexpr int kGfxButtonRed      = 174;
constexpr int kGfxMainMenuRahmen = 1776;
constexpr int kGfxWinBorder      = 0;

// _BUTTON_RED cap width (px) — the fixed left/right end-cap slice width.
constexpr int kButtonCapWidth = 12;

struct ButtonSliceInfo {
    bool real     = false;   // true => drew real decoded slices; false => inert rect
    int  capLeft  = 0;       // px width of the left cap actually drawn
    int  capRight = 0;       // px width of the right cap actually drawn
    int  center   = 0;       // px span of the stretched centre region
    int  height   = 0;       // px height actually used
};

struct FrameDrawInfo {
    bool real      = false;  // true => drew real border art; false => inert outline
    int  thickness = 0;      // px border thickness drawn (max of edge slice sizes)
    bool tiled     = false;  // true => multi-shape tiled border path used
};

// Draw a 3-slice button of total width `widthPx` at (x,y) into the WxH 32bpp ARGB
// buffer `dst`.  `gfxRecord` is normally kGfxButtonRed.  `pressed` selects the
// down-state slices (shapes 3/4/5) instead of the up-state (0/1/2).  Returns the
// slice geometry actually drawn (real flag false when it fell back to a rect).
ButtonSliceInfo DrawThreeSliceButton(u32* dst, int W, int H, int x, int y,
                                     int widthPx, const GfxArchive& arc,
                                     int gfxRecord, bool pressed);

// Draw a bordered window frame of size (w,h) at (x,y).  `frameRecord` is normally
// kGfxMainMenuRahmen (single panel) or kGfxWinBorder (tiled).  Returns the frame
// geometry actually drawn (real flag false when it fell back to a plain outline).
FrameDrawInfo DrawWindowFrame(u32* dst, int W, int H, int x, int y, int w, int h,
                              const GfxArchive& arc, int frameRecord);

} // namespace guild::render
