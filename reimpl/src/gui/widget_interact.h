#pragma once
// guild::gui — interactive widget behaviours: the active-drag scrollbar driver,
// the slider step-from-buttons path, the drag-state teardown and the per-widget
// scroll-limit setter.
//
// These are the run-loop leaves the GUI dispatcher (VIBE_Widget_ProcessMouseDrag /
// VIBE_Widget_DispatchMouseClick) calls each frame once a control has captured the
// mouse.  They operate on:
//   * the recovered Widget array (gui/types.h, gui/object.h) — for the slider path,
//   * a small global drag-state cluster (the 0x62D3xx / 0x75BExx scrollbar-drag
//     globals) plus the cursor-clamp globals owned by gui/input_state.h,
//   * the live mouse globals (gui/input_state.h: edge/held flags, packed coords).
//
// The value math reuses the already-translated Slider_ComputeStep /
// Scrollbar_SetThumbPosition (gui/slider.h, gui/scrollbar.h); only the latch /
// capture / clamp bookkeeping and the integer button-step path are added here.
//
// Recovered originals:
//   VIBE_Widget_SetScrollLimit  @0x41207c — write the slider/edit mode word (+132).
//   VIBE_Widget_ClearActiveDrag @0x4202e8 — drop the active drag + restore clamp.
//   VIBE_Scrollbar_DragThumb    @0x4208ac — per-frame scrollbar thumb-drag driver.
//   VIBE_Slider_UpdateFromMouse @0x420a04 (integer button-step core) — the
//       dword_672228/dword_672224 +/- button increment/clamp block.
//
// The renderer-bound halves (VIBE_Coord_Transform / VIBE_Coord_ConvertX screen
// geometry in Slider_UpdateFromMouse's mouse-track block, and DrawCheckbox /
// DrawScrollThumb) are owned by the render cluster and deferred; their integer
// cores are what is reproduced here byte-for-byte.

#include "gui/types.h"
#include "gui/scrollbar.h"  // ScrollState, Scrollbar_SetThumbPosition

namespace guild::gui {

// ---------------------------------------------------------------------------
// Scrollbar drag-state cluster (the 0x62D3xx / 0x75BExx globals the drag driver
// parks/restores).  In the original `dword_62D328` is a raw pointer to the active
// scrollbar's type-'A' data record; here it is a pointer to the ScrollState mirror
// (gui/scrollbar.h) extended with the owning-widget slot the driver needs (+304).
// ---------------------------------------------------------------------------

// Extra fields of the active scrollbar data record the drag driver touches beyond
// the value-model fields ScrollState already holds (+24 max, +28 min, +38 flags,
// +296 value).  Modelled as a small wrapper so the original's raw-offset addressing
// reads cleanly while still reusing Scrollbar_SetThumbPosition's ScrollState.
struct ScrollDragRecord {
    ScrollState v;            // value-model fields (+24/+28/+38/+296/+40)
    i32 owner   = 0;          // +304  owning widget slot (dword_62D22C compared to this)
    i32 track32 = 0;          // +32   thumb-track position written during latch
    u8  heldFlag67222C = 0;   // local mirror of dword_67222C for the "page-to-max" branch
};

extern ScrollDragRecord* g_activeDrag;  // dword_62D328 (0 == no active drag)
extern i32 g_dragLatched;               // dword_62D34C (0 until the drag start is captured)
extern i32 g_dragStartValue;            // dword_75BF50 (value at drag start = SetThumb base)
extern i32 g_dragOriginX;               // dword_62D344 (mouse X at drag start = SetThumb thumbHi)
extern i32 g_dragOriginY;               // dword_62D340 (mouse Y at drag start)
extern i32 g_savedClampY0;              // dword_75BEB8 (parked cursor clamp y0)
extern i32 g_savedClampY1;              // dword_75BEC0 (parked cursor clamp y1)
extern i32 g_dragOwnerWidget;           // dword_62D33C (widget that opened a drag-clamp; -1 = none)

void ResetWidgetInteract();  // zero the drag cluster (BSS is zero at load) — test helper

// gilde.exe 0x41207c — VIBE_Widget_SetScrollLimit  (idx@eax, code@dx)
// Stores the 16-bit slider/edit mode word `code` into widget +132 (editFlags).
// Returns the original's `740 * idx` byte offset.
int Widget_SetScrollLimit(int idx, i16 code);

// gilde.exe 0x4202e8 — VIBE_Widget_ClearActiveDrag  (int(void))
// Drops the active scrollbar drag: clears the owning widget's +40 mirror, clears
// dword_62D328, and — if a drag had opened the cursor clamp (dword_62D33C != -1) —
// restores the parked clamp y-bounds and clears the owner marker.  Returns the
// original's last `eax` (the 740*owner byte offset when a drag was active, else 0).
int Widget_ClearActiveDrag();

// gilde.exe 0x4208ac — VIBE_Scrollbar_DragThumb  (int(void))
// Per-frame driver for the active scrollbar (g_activeDrag).  Requires the record's
// drag-enabled bit (+38 & 2).  While a page button is held (g_activeDrag->heldFlag
// 67222C) it pages the value up to max.  On the mouse-down edge (edge20) it, on the
// first frame, latches the drag (captures the start value, the mouse origin and —
// for a vertical scrollbar, +38 sign bit — opens the cursor clamp), then each frame
// recomputes the value via Scrollbar_SetThumbPosition(record, startValue, mouseX,
// originX).  Returns that committed value (or the last eax when inactive).
//   curMouseX / curMouseY are the live packed-coord high words (dword_672210>>16 /
//   unk_67220E>>16); edge20 == dword_672220; held67222C == dword_67222C.
int Scrollbar_DragThumb(int curMouseX, int curMouseY, int edge20, int held67222C);

// gilde.exe 0x420a04 (integer button-step core of VIBE_Slider_UpdateFromMouse) —
// The dword_672228 / dword_672224 block: when the slider's +/- repeat fires
// (clickEdge, or autoRepeat && range>=100), step the current value (+120) down by
// one if its "<" button is held (+732) or up if its ">" button is held (+733), then
// clamp it to [min(+124), max(+128)] honouring the 0x10 "clamp-to-step" flag (+132),
// which caps at step(+140) instead of max.  Marks +96 dirty and writes back +120.
//   clickEdge  == dword_672228 ; autoRepeat == dword_672224.
// Returns the new committed value.
int Slider_StepFromButtons(int widgetIdx, int clickEdge, int autoRepeat);

} // namespace guild::gui
