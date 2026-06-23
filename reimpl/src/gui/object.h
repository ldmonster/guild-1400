#pragma once
// guild::gui — Object/Widget array and its allocator + the per-object data accessor.
//
// Original global state:
//   dword_69FFB4  Object/Widget array  (740-byte stride, capacity 511)   -> g_widgets
//   dword_62D26C  512-entry widget pointer cache (2048 bytes)            -> g_widgetCache
//   dword_62D24C  high-water widget index                               -> g_widgetHighWater
//
// In the live process the widget array is a flat byte buffer indexed by `740*idx`,
// the pointer cache stores raw pointers, and `widget+12` holds a real Object3D*/
// Window* pointer. We model the array as a real C++ array of Widget records and the
// caches/links as integer indices/handles so the 1:1 arithmetic still reads cleanly.

#include "gui/types.h"

namespace guild::gui {

// ---- Global arrays (original BSS bases in the comments) --------------------
extern Widget g_widgets[kMaxWidgets];          // dword_69FFB4
extern Widget* g_widgetCache[kMaxWidgets + 1]; // dword_62D26C (512 ptr slots)
extern int    g_widgetHighWater;               // dword_62D24C

// The widget +12 field ("dataPtr") holds a real 32-bit pointer in the original
// (Window* for type '@', Object3D* for type 'A', or a small graphics id stashed by
// AddToWindow). On a 64-bit host a pointer does not fit in the i32 field, so we keep
// the pointer-valued payload in this parallel table (indexed by widget slot) and use
// the i32 field only for the integer-id case. WidgetData()/SetWidgetData() are the
// single access point for +12.
extern void* g_widgetData[kMaxWidgets];        // mirrors widget +12 when pointer-valued
// Parallel ownership flag: true when g_widgetData[idx] is a heap block this module must
// free on slot recycle / reset (the original frees the widget's +116/+120/+12 buffer in
// VIBE_Widget_DestroyByType). Borrowed pointers (Window*, field records, scene states)
// keep owned=false and are never freed here.
extern bool  g_widgetDataOwned[kMaxWidgets];

inline void  SetWidgetData(int idx, void* p) {
    if (idx < 0 || idx >= kMaxWidgets) return;
    g_widgetData[idx] = p;
    g_widgetDataOwned[idx] = false; // borrowed by default
}
inline void* WidgetData(int idx)             { return g_widgetData[idx]; }
// Store a HEAP-OWNED buffer at the widget's data slot: it will be std::free()d when the
// slot is recycled (Widget_FreeSlot) or the array is reset (ResetWidgets), matching the
// original's destroy-time release of the per-widget text/raw buffer. Defined in object.cpp.
void SetWidgetDataOwned(int idx, void* heapBlock);

// Reset all GUI core global state to the initial (zeroed) condition. Not present in
// the original (BSS is zero at load); provided so tests start from a clean slate.
// Defined in form.cpp so it can reset the form, window and widget arrays together.
void ResetGuiState();

// Per-array reset helper (resets the widget array + caches + data table).
void ResetWidgets();

// gilde.exe 0x412dac — VIBE_Widget_AllocSlot  (int(void))
// Linearly scans g_widgets for a free slot (inUse/+4 == 0). On success: marks the
// slot in-use, records its pointer in the next free cache slot, updates the
// high-water index, and returns the slot index. Returns -1 when the array is full.
int Widget_AllocSlot();

// gilde.exe (implied by VIBE_Widget_DestroyByType / Window_Destroy free paths) —
// Free a widget slot: clears the inUse flag and removes it from the pointer cache.
// The original frees per-type via VIBE_Widget_DestroyByType @0x414f98 (which also
// releases type-specific buffers); the core slot-recycle is just clearing +4 so the
// next AllocSlot scan reuses it. Exposed here for the data-model tests.
void Widget_FreeSlot(int idx);

// gilde.exe 0x41db9c — VIBE_Object_GetDataPtr  (int(idx@eax))
// Resolves the data payload for widget `idx`:
//   type 'A' (0x41): read the Object3D flag byte at +38 and return
//       0x10 => *(rec+328) ; 0x01 => rec+40 ; 0x02 => *(rec+296)
//   then (or for non-'A'): if +68 (button flag) nonzero => return value (+36)
//                          else if type 'E' (0x45)     => return editText (+120)
//                          else                        => -1
// Returns a raw 32-bit value/handle, matching the original int return.
i32 Object_GetDataPtr(int idx);

} // namespace guild::gui
