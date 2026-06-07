#pragma once
// guild::gui — Window lifecycle/management run-loop leaves (position / teardown /
// scroll), complementing window.cpp (create/destroy core) and window_layout.cpp
// (resize / auto-fit). All operate on the already-recovered Window/Widget records
// (gui/types.h) and reuse the widget allocator, child list and Z-order helpers.
//
// Render/OS leaves (VIBE_Widget_LayoutBounds / VIBE_Widget_DestroyByType /
// VIBE_Util_MemMove) are routed through existing shared declarations or local
// placeholders; the data-model bookkeeping is translated byte-for-byte.

#include "gui/types.h"

namespace guild::gui {

// gilde.exe 0x41d764 — VIBE_Window_PositionCentered  (winSlot@eax, mode@dl)
// Re-lays the window's backing widget at a position derived from `mode`:
//   bit 0 (0x1): x = screenCenterX - w/2   else x = win.x()
//   bit 1 (0x2): y = screenCenterY - h/2   else y = win.y()
// then calls Widget_LayoutBounds(x, y, backingWidget). Returns the LayoutBounds result
// (low byte in the original).
int Window_PositionCentered(int winSlot, char mode);

// gilde.exe 0x41a7a8 — VIBE_Window_RemoveIfActive  (winSlot@eax, a2@ecx, a3@ebx)
// If window `winSlot` is enabled, destroys its backing widget (Widget_DestroyByType)
// and returns 1; otherwise returns 0.
int Window_RemoveIfActive(int winSlot, int a2, int a3);

// gilde.exe 0x41a7dc — VIBE_Window_RemoveChildren  (winSlot@eax, keepBacking@edx)
// Destroys all of window `winSlot`'s child widgets back-to-front. The three "special"
// child slots cached in the window (dword[234..236]) are reset to -1 as they are hit.
// A type-'@' (0x40) window-backing child is only destroyed when keepBacking is nonzero.
// Clears the window text buffer marker and the scroll bookkeeping (dword[145..148]).
// Returns 1 on success, 0 when the slot is out of range or free.
int Window_RemoveChildren(int winSlot, int keepBacking);

// gilde.exe 0x41a024 — VIBE_Window_Scroll  (delta@eax, amount@edx, winSlot@ebx)
// Scrolls window `winSlot` vertically by `amount` (clamped so the content stays within
// [0 .. scrollMaxY]), accumulating into the window's scroll-offset field (dword[148]),
// and adds `delta` to the horizontal scroll accumulator (dword[149]). Returns the
// original's handle value (16 * 56 * winSlot).
int Window_Scroll(int delta, int amount, int winSlot);

} // namespace guild::gui
