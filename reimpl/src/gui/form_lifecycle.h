#pragma once
// guild::gui — Form lifecycle & visibility family (the run-loop side of the Form
// data model, distinct from form.cpp's selector/accessor trio).
//
// These functions sweep the Form's child Windows and their child Widgets to:
//   * toggle render/visibility flags (SetObjectsVisible / SetChildrenVisible),
//   * lift / position / center the Form's Windows on screen
//     (RaiseWindows / CenterChildWindows / PositionChildWindows),
//   * resolve a Form's logical window slot id (GetWindowId), and
//   * tear the whole Form down (Destroy).
//
// All Form/Window/Widget records and the FRM2-loaded form table are REUSED from
// gui/types.h, gui/form.h, gui/window.h and gui/object.h. OS/render leaves
// (VIBE_Widget_DestroyByType / VIBE_Surface_Destroy / VIBE_Widget_LayoutBounds /
// VIBE_ZOrder_RaiseWindow) are routed through the placeholders declared below; the
// data-model bookkeeping is translated byte-for-byte.

#include "gui/types.h"

namespace guild::gui {

// ---------------------------------------------------------------------------
// Screen-center globals (used by the centered-positioning paths).
//   dword_69FFA4  screen center X  (g_screenCenterX)
//   dword_69FFA0  screen center Y  (g_screenCenterY)
// Owned here (no other module defines them); reused via extern.
// ---------------------------------------------------------------------------
extern i32 g_screenCenterX; // dword_69FFA4
extern i32 g_screenCenterY; // dword_69FFA0

// ===== Forward-declared render/layout leaves (placeholders; see form_lifecycle.cpp) =====
// gilde.exe 0x414f98 — VIBE_Widget_DestroyByType: per-type widget teardown.
void Widget_DestroyByType(int widgetIdx, int a2, int a3);
// gilde.exe 0x4234b0 — VIBE_Surface_Destroy (render cluster surface free).
int  Surface_DestroySurface(i32 surfaceHandle);
// gilde.exe 0x41aae8 — VIBE_ZOrder_RaiseWindow: lift a whole window's widgets to front.
int  ZOrder_RaiseWindow(int winSlot);

// gilde.exe 0x41e544 — VIBE_Form_GetWindowId  (formId@eax, slot@edx)
// Returns the window id stored for logical window `slot` of `formId`, or -1 when
// `slot` is out of [0, windowCount]. (676BE4[171*id] bound; 676A64[slot+171*id] value.)
int Form_GetWindowId(int formId, int slot);

// gilde.exe 0x41d634 — VIBE_Form_SetObjectsVisible  (formId@eax, hide@edx)
// When the form is valid, sweeps ALL widget slots: any in-use widget whose +60
// (parentClip) handle equals this form record sets its +52 (renderPtr) to (hide==0).
// Also caches `hide` in the form (dword[104]). No-op for an invalid form.
void Form_SetObjectsVisible(int formId, int hide);

// gilde.exe 0x41d568 — VIBE_Form_SetChildrenVisible  (formId@eax, hide@edx)
// When the form is valid, walks each of its Windows and every child widget, setting
// widget +56 (disabledA) to (hide==0). Caches `hide` in the form (dword[103]).
void Form_SetChildrenVisible(int formId, int hide);

// gilde.exe 0x41be6c — VIBE_Form_RaiseWindows  (formId@eax)
// Raises every Window of the form to the front of the Z-order (ZOrder_RaiseWindow).
void Form_RaiseWindows(int formId);

// gilde.exe 0x41da04 — VIBE_Form_Destroy  (formId@eax)  [246 xrefs]
// For a valid form: destroys each Window's backing widgets (back-to-front), frees the
// two form surfaces, clears the valid flag, and returns. No-op for an invalid form.
void Form_Destroy(int formId);

// gilde.exe 0x41d6ac — VIBE_Form_CenterChildWindows  (formId@al)  [214 xrefs]
// Centers each top-level (groupLink==0 backing widget) Window of the form on the
// screen center, via Widget_LayoutBounds.
void Form_CenterChildWindows(int formId);

// gilde.exe 0x41d990 — VIBE_Form_PositionChildWindows  (formId@eax, mode@dl)
// Positions each top-level Window of the form via Window_PositionCentered(slot, mode).
void Form_PositionChildWindows(int formId, char mode);

} // namespace guild::gui
