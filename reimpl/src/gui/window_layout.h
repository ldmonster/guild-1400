#pragma once
// guild::gui — window resize / auto-fit-height layout (the geometry-reflow model).
//
//   VIBE_Window_Resize        @0x41a0f8  resize a window + reflow its children
//   VIBE_Window_AutoFitHeight @0x41a500  grow a window's height to bound its content
//   VIBE_Window_LayoutScrollContent @0x41536c  — DEFERRED (renderer glyph-layout, see .cpp)
//
// These operate on the already-translated Window/Widget records (gui/types.h). Window
// geometry lives as plain int16 pixel words at +4/+6/+8/+10 (x,y,w,h); the originals read
// them via misaligned `dword >> 16` tricks that name the SAME words. The MODEL parts —
// the content-extent scan over children, the child clip-bound reflow, and the backing
// widget's size/clip update — are translated here. The surface re-creation (Surface_Create
// / blit) and the scrollbar-tile snap (flags 0x400 / 0x4, which need the renderer's 84-byte
// gfx-metric table) are DEFERRED to the renderer cluster.

#include "gui/types.h"

namespace guild::gui {

// gilde.exe 0x41a0f8 — VIBE_Window_Resize  (w@ax, h@dx, winSlot@ebx)
// Resizes window `winSlot` to (w,h): scans children to expand the requested height to bound
// the lowest child, applies the width/height delta to the window words (+8/+10) and the
// backing widget (+20/+22), clamps to the screen extent, and reflows every child's clip
// bounds (+30/+34). Returns a handle (the original returns the backing-widget byte address).
// Surface re-creation and the scrollbar-tile snap are deferred (see header). Returns 0 when
// the window slot is free.
int Window_Resize(i16 w, i16 h, int winSlot);

// gilde.exe 0x41a500 — VIBE_Window_AutoFitHeight  (winSlot@eax, minHeight@edi)
// Walks the window's children; for each, computes its bottom (y + height) relative to the
// window top and, when it exceeds `minHeight`, grows `minHeight` to it + 8. Then calls
// Window_Resize(curWidth, fittedHeight, winSlot). When the window has the text-buffer flag
// (0x10) the starting height is taken from the scroll thumb extent (+580). Returns the
// Window_Resize result.
int Window_AutoFitHeight(int winSlot, int minHeight);

} // namespace guild::gui
