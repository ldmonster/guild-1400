#pragma once
// guild::gui — Z-order list operations over the widget pointer cache.
//
// The original keeps the front-to-back draw order in the 512-slot widget pointer
// cache dword_62D26C (== g_widgetCache, 2048 bytes). Each occupied slot holds a
// pointer to a widget record (`dword_69FFB4 + 740*idx`); slot 0 is the front-most
// drawable, higher slots are further back. The three operations below maintain that
// ordered list:
//   VIBE_ZOrder_RemoveObject @0x41aa50 — unlink a widget from the order.
//   VIBE_ZOrder_InsertObject @0x41acac — (re)insert a widget just below the deepest
//       sibling that shares its group/parent, keeping group children contiguous.
//   VIBE_ZOrder_RaiseWindow  @0x41aae8 — lift a whole window's widget run to the front.
//
// We operate directly on g_widgetCache (the same array the allocator/Z-order share),
// translating the byte-offset pointer arithmetic of the original into index arithmetic
// over Widget* entries so the ordering semantics stay byte-for-byte.

#include "gui/types.h"

namespace guild::gui {

// gilde.exe 0x41aa50 — VIBE_ZOrder_RemoveObject  (widgetIdx@eax)
// Scans the cache from the back (slot 511) toward the front for `widgetIdx`; while
// scanning it records the deepest occupied slot above it (v1). On a hit it moves that
// recorded entry down into the found slot and compacts the tail, closing the gap.
void ZOrder_RemoveObject(int widgetIdx);

// gilde.exe 0x41acac — VIBE_ZOrder_InsertObject  (widgetIdx@eax, parentWidgetIdx@edx)
// First removes any existing occurrence of `widgetIdx` (compacting the list). Then,
// scanning from the back, finds the insertion point: the deepest slot whose widget
// either (a) belongs to the same group (groupLink == parentWidgetIdx) and is not a
// window-backing widget (type != '@'/0x40), or (b) whose backWidget chain matches the
// parent's. It inserts `widgetIdx` immediately in front of (one slot above) that entry.
void ZOrder_InsertObject(int widgetIdx, int parentWidgetIdx);

} // namespace guild::gui
