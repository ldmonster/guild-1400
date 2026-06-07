#pragma once
// guild::gui — the in-world "event bar" (Panel\Event_Leiste) slot manager.
//
// The event bar is a 16-slot strip of clickable event icons along the bottom-right of
// the HUD.  Each active event owns one slot; a slot binds three things, held in three
// parallel global arrays (3-dword / 12-byte stride, 16 slots):
//
//   dword_11CB560[3*i]  panelObj  — the owning event-object handle (0 == free)
//   dword_11CB564[3*i]  widgetId  — the bar icon's widget-array index (-1 == free)
//   dword_11CB568[3*i]  subForm   — an optional pop-out detail form id (-1 == none)
//
// The owning event object stores a back-pointer to its slot triple at object +116, and
// mirrors widgetId at (+116)+4 and subForm at (+116)+8.  Other globals:
//   dword_1229264  bar form id (GameTick_Finalize("Panel\\Event_Leiste"))
//   dword_632268   high-water occupied slot index
//   dword_63226C   bar-visible form id, or -1 when hidden
//   dword_632270   the currently-selected slot triple pointer (0 == none)
//
// CreateSlot adds an icon to the bar (capping at 16, evicting the oldest via
// FindNearestEventPanelSlot when full); DestroySlot frees a slot and compacts the array;
// InitBar/DestroyBar build/tear-down the whole bar; ToggleVisible/SetBarVisible drive
// show/hide; SelectActiveSlot updates which slot's pop-out detail form is shown.
//
// The retained-mode edges (Form_SelectWindow / Object_AddToWindow / Form_Destroy /
// Form_SetObjectsVisible / Widget_LayoutBounds) are REUSED where already translated;
// the scene/sim edges (GameTick_Finalize form-loader, the event-object record, the
// pop-out detail form, GameTime comparison) are forward-declared and mocked in tests.
// This module owns the slot-table layout + alloc/free/compact/select logic 1:1.

#include "gui/types.h"

namespace guild::gui {

inline constexpr int kEventPanelSlots = 16;        // dword_11CB564[0..45 step 3]; >=16 is full
inline constexpr int kEventSlotStride = 3;         // 3 dwords (12 bytes) per slot

// Object-record offsets the bar touches.
inline constexpr int kEventObjFlagsOffset   = 120; // (a1+120)&8 — "is an event object" bit
inline constexpr int kEventObjSlotOffset    = 116; // a1+116 -> slot triple pointer
inline constexpr int kEventObjTimeOffset    = 68;  // sort key for the oldest-event eviction
inline constexpr int kEventSlotWidgetField  = 4;   // (+116)+4 mirrors the widget id
inline constexpr int kEventSlotFormField    = 8;   // (+116)+8 mirrors the sub-form id

// ---------------------------------------------------------------------------
// Modelled event object.  The original event object is a large scene-object record; the
// bar only reads its flags byte (+120), its sort time (+68..+82), and writes its slot
// back-pointer triple (+116, +4, +8) and the per-bar-icon value (widget +36).  We model
// just those fields so the slot logic is exercisable headlessly.
// ---------------------------------------------------------------------------
struct EventObject {
    int  handle;       // stable nonzero id (== the +0 of the original record)
    unsigned flags;    // +120  (bit 0x8 must be set for CreateSlot to accept it)
    long long time;    // +68   sort key (oldest = smallest, evicted first)
    int  slotIndex;    // which slot owns this object (-1 = none); models +116 triple
};

// ---------------------------------------------------------------------------
// The slot triple (one entry of the three parallel arrays).
// ---------------------------------------------------------------------------
struct EventSlot {
    int panelObj;   // dword_11CB560[3*i]  (event-object handle; 0 = free)
    int widgetId;   // dword_11CB564[3*i]  (-1 = free)
    int subForm;    // dword_11CB568[3*i]  (-1 = none)
};
extern EventSlot g_eventSlots[kEventPanelSlots]; // dword_11CB560/564/568
extern int g_eventBarForm;     // dword_1229264
extern int g_eventHighWater;   // dword_632268
extern int g_eventBarVisible;  // dword_63226C  (-1 = hidden)
extern int g_eventSelected;    // dword_632270  (slot index of the selected triple; -1 = none)

// Reset the whole bar table (BSS is -1/0 at load; the originals init via InitBar).
void ResetEventPanel();

// ---------------------------------------------------------------------------
// Mockable edges.
// ---------------------------------------------------------------------------
struct EventPanelHost {
    virtual ~EventPanelHost() = default;
    // VIBE_GameTick_Finalize(time, kind, name) — load a form / build an event widget; here
    // it returns a stable nonzero id keyed by `name` (for the bar form) or `kind`.
    virtual int  LoadForm(const char* name) { (void)name; return 0; }
    virtual int  BuildEventForm(int kind) { (void)kind; return 0; }
    // The first frame count of an event object (icon add); returns its assigned widget id.
    virtual int  AddIcon(int x, int gfxId) { (void)x; (void)gfxId; return -1; }
    virtual void DestroyWidget(int widgetId) { (void)widgetId; }
    virtual void DestroyForm(int formId) { (void)formId; }
    virtual void SetFormVisible(int formId, int visible) { (void)formId; (void)visible; }
    virtual void RaiseForm(int formId) { (void)formId; }
    // The bar-icon's "selected" value, read by SelectActiveSlot from widget +36.
    virtual int  IconValue(int widgetId) { (void)widgetId; return 0; }
};
void EventPanel_SetHost(EventPanelHost* host);

// Resolve / register an event object by handle (the bar stores a handle, not a pointer).
EventObject* EventPanel_Object(int handle);
int EventPanel_RegisterObject(unsigned flags, long long time); // -> handle

// ---------------------------------------------------------------------------
// The translated functions.
// ---------------------------------------------------------------------------

// gilde.exe 0x4c5738 — VIBE_EventPanel_InitBar.
// Loads the Panel\Event_Leiste form, lays its backing widget out at the bottom-right,
// clears all 16 widget-id slots to -1, and hides the bar.  Returns the hide result.
int EventPanel_InitBar();

// gilde.exe 0x4c53b8 — VIBE_He_FindNearestEventPanelSlot.
// When the bar is full, finds the oldest event (smallest +68 time among occupied slots),
// destroys its slot, frees its handler entry, and returns 15 (the now-free top slot).
// Returns -1 when no occupied slot was found.
int EventPanel_FindNearestSlot();

// gilde.exe 0x4c5460 — VIBE_EventPanel_CreateSlot (obj@eax, kind@bx, time@dx, name@ecx, gfx)
// If `obj` is not an event object (flag 0x8 clear) returns 1.  Else finds the first free
// slot (evicting the oldest when all 16 are taken); on no slot returns 2.  Otherwise binds
// the slot: stores obj, adds the bar icon (widget id, +68 button flag = 1), builds the
// pop-out event form, positions it, hides it, and writes the object's +116 triple.
// Returns 0 on success.
int EventPanel_CreateSlot(int objHandle, int kind, int time, const char* name, int gfx);

// gilde.exe 0x4c55b0 — VIBE_EventPanel_DestroySlot (obj@eax, arg@ebx).
// Frees the object's slot (destroying its widget + pop-out form), clears the selection if
// it pointed here, then compacts the array (sliding higher slots down) and recomputes the
// high-water index.  Returns the last array value scanned (preserved verbatim).
int EventPanel_DestroySlot(int objHandle, int arg);

// gilde.exe 0x4c57b0 — VIBE_EventPanel_DestroyBar (arg@ebx).
// Destroys every occupied slot, then destroys the bar form.  Returns the destroy result.
int EventPanel_DestroyBar(int arg);

// gilde.exe 0x4c5824 — VIBE_EventPanel_ToggleVisible (arg@eax).
// Hides every slot's pop-out form + re-lays the bar; then either hides the whole bar
// (arg == 0xFFFF -> high-water/visible = -1, returns -1) or shows it, recomputing the
// high-water slot and marking the bar visible (returns the bar form id).
int EventPanel_ToggleVisible(int arg);

// gilde.exe 0x4c5918 — VIBE_EventPanel_SetBarVisible (visible@eax).
// When the bar is currently shown (dword_63226C != -1), forwards `visible` to the bar
// form's SetObjectsVisible; otherwise a no-op returning `visible`.
int EventPanel_SetBarVisible(int visible);

// gilde.exe 0x4c5938 — VIBE_EventPanel_SelectActiveSlot.
// Walks the 16 slots; the first occupied slot whose bar icon was clicked (icon value != 0)
// becomes the selection: its pop-out form is shown + raised and the previously-selected
// slot's form is hidden and its icon value cleared.  Updates dword_632270.
void EventPanel_SelectActiveSlot();

} // namespace guild::gui
