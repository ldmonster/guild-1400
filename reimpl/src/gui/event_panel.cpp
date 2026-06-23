#include "gui/event_panel.h"

namespace guild::gui {

// Original BSS arrays (3-dword stride, 16 slots): we model them as one record array.
EventSlot g_eventSlots[kEventPanelSlots];
int g_eventBarForm    = 0;   // dword_1229264
int g_eventHighWater  = 0;   // dword_632268
int g_eventBarVisible = -1;  // dword_63226C
int g_eventSelected   = -1;  // dword_632270 (modelled as a slot index; -1 = none)

namespace {

// ---------------------------------------------------------------------------
// Event-object table.  The original stores a raw scene-object pointer in
// dword_11CB560[3*i]; the bar reads obj+120 (flags), obj+68 (sort time) and writes the
// obj+116 triple back-pointer.  On a 64-bit host we store a small nonzero handle and keep
// the object fields in this side table.
// ---------------------------------------------------------------------------
constexpr int kMaxObjects = 64;
EventObject g_objects[kMaxObjects];
int g_objectCount = 0;

EventPanelHost  g_defaultHost;
EventPanelHost* g_host = &g_defaultHost;

} // namespace

void EventPanel_SetHost(EventPanelHost* host) {
    g_host = host ? host : &g_defaultHost;
}

EventObject* EventPanel_Object(int handle) {
    if (!handle) return nullptr;
    for (int i = 0; i < g_objectCount; ++i)
        if (g_objects[i].handle == handle) return &g_objects[i];
    return nullptr;
}

int EventPanel_RegisterObject(unsigned flags, long long time) {
    if (g_objectCount >= kMaxObjects) return 0;
    EventObject& o = g_objects[g_objectCount];
    o.handle = 0x2000 + g_objectCount;
    o.flags = flags;
    o.time = time;
    o.slotIndex = -1;
    ++g_objectCount;
    return o.handle;
}

void ResetEventPanel() {
    for (auto& s : g_eventSlots) s = EventSlot{0, -1, -1};
    for (auto& o : g_objects) o = EventObject{};
    g_objectCount = 0;
    g_eventBarForm = 0;
    g_eventHighWater = 0;
    g_eventBarVisible = -1;
    g_eventSelected = -1;
}

// Helper: write the object's +116 slot triple (back-pointer + widget + form mirrors).
static void BindObjectToSlot(int objHandle, int slot) {
    EventObject* o = EventPanel_Object(objHandle);
    if (o) o->slotIndex = slot;
    // The widget/form mirrors at (+116)+4/+8 are simply g_eventSlots[slot].widgetId/subForm,
    // which we keep in the slot record itself (the original aliases obj+116 onto the triple).
}

// ===========================================================================
// gilde.exe 0x4c5738 — VIBE_EventPanel_InitBar.
//   dword_1229264 = GameTick_Finalize(0,0,"Panel\\Event_Leiste");
//   Form_SelectWindow(barForm, 0);
//   Widget_LayoutBounds(HIWORD(screenExt)-125, screenExt-33, backWidgetOfCurrentWindow);
//   for (i=0; i!=48; i+=3) dword_11CB558[i] = -1;     // clear all 16 widget-id slots
//   return Form_SetObjectsVisible(barForm, 0);
// (dword_11CB558 is dword_11CB564 - 1 dword, i.e. the widget-id column; we clear widgetId.)
// ===========================================================================
int EventPanel_InitBar() {
    g_eventBarForm = g_host->LoadForm("Panel\\Event_Leiste");
    // Form_SelectWindow + Widget_LayoutBounds position the bar's backing widget; the layout
    // edge is owned by the retained-mode core and is a no-op for the slot table here.
    for (int i = 0; i < kEventPanelSlots; ++i)
        g_eventSlots[i].widgetId = -1;
    g_host->SetFormVisible(g_eventBarForm, 0);
    return 0;
}

// ===========================================================================
// gilde.exe 0x4c53b8 — VIBE_He_FindNearestEventPanelSlot.
//   Scan the 16 slots; track the occupied slot with the smallest +68 time (the oldest
//   event).  If one was found (v0 != 0): DestroySlot(it), FreeHandlerEntry(it), return 15.
//   Else return -1.  (The original also tracks v9, returned via __debugbreak on the
//   "found a candidate but v0==0" impossible path; v9 mirrors the slot index.)
// ===========================================================================
int EventPanel_FindNearestSlot() {
    int oldestHandle = 0;
    int oldestSlot = -1;
    long long bestTime = 0;
    bool haveBest = false;
    for (int i = 0; i < kEventPanelSlots; ++i) {
        int obj = g_eventSlots[i].panelObj;          // dword_11CB560[3*i]
        if (obj && g_eventSlots[i].widgetId != -1) { // occupied
            EventObject* o = EventPanel_Object(obj);
            long long t = o ? o->time : 0;
            if (!haveBest || t < bestTime) {         // GameTime_Compare(...) < 0
                haveBest = true;
                bestTime = t;
                oldestHandle = obj;
                oldestSlot = i;
            }
        }
    }
    if (oldestHandle) {
        EventPanel_DestroySlot(oldestHandle, kEventPanelSlots);
        // VIBE_He_FreeHandlerEntry(oldestHandle) — handler bookkeeping, out of scope.
        return kEventPanelSlots - 1; // 15
    }
    return oldestSlot; // -1 when nothing was occupied
}

// ===========================================================================
// gilde.exe 0x4c5460 — VIBE_EventPanel_CreateSlot.
//   if (!(obj[120] & 8)) { obj[116]=0; return 1; }              // not an event object
//   slot=0; if (slot0 occupied) scan to first free (widgetId != -1 means occupied);
//   if (slot >= 16) slot = FindNearestEventPanelSlot();          // evict oldest
//   if (slot == -1) { obj[116]=0; return 2; }
//   if (slot > highWater) highWater = slot;
//   Form_SelectWindow(barForm, 0);
//   triple = &dword_11CB560[3*slot];  obj[116]=triple;  triple.panelObj = obj;
//   triple.widgetId = Object_AddToWindow(curWin, 0, 32*slot, gfx);  widget[+68] = 1;
//   triple.subForm  = GameTick_Finalize(time, kind, name);  position; hide.
//   return 0;
// ===========================================================================
int EventPanel_CreateSlot(int objHandle, int kind, int time, const char* name, int gfx) {
    EventObject* obj = EventPanel_Object(objHandle);
    unsigned flags = obj ? obj->flags : 0u;
    if ((flags & 8u) == 0) {        // (*(BYTE*)(a1+120) & 8) == 0
        if (obj) obj->slotIndex = 0; // *(a1+116) = 0  (the original stores 0)
        return 1;
    }

    // First-free-slot scan (occupied == widgetId != -1, i.e. dword_11CB564[v8] != -1).
    int slot = 0;
    if (g_eventSlots[0].widgetId != -1) {
        int v8 = 0;
        do {
            v8 += kEventSlotStride;
            ++slot;
        } while (v8 < kEventPanelSlots * kEventSlotStride && g_eventSlots[slot].widgetId != -1);
    }
    if (slot >= kEventPanelSlots)
        slot = EventPanel_FindNearestSlot();
    if (slot == -1) {
        if (obj) obj->slotIndex = 0;
        return 2;
    }

    if (slot > g_eventHighWater)
        g_eventHighWater = slot;

    // Form_SelectWindow(barForm, 0) selects the bar window for the AddToWindow below.
    EventSlot& s = g_eventSlots[slot];
    s.panelObj = objHandle;                 // *triple = a1
    BindObjectToSlot(objHandle, slot);      // *(a1+116) = triple
    s.widgetId = g_host->AddIcon(32 * slot, gfx); // Object_AddToWindow(curWin, 0, 32*slot, gfx)
    // widget[+68] = 1 (clickable button flag) — owned by the widget array; the host's
    // AddIcon returns the assigned id and is responsible for the widget record.
    s.subForm = g_host->BuildEventForm(kind); // GameTick_Finalize(time, kind, name)
    (void)time; (void)name;
    g_host->SetFormVisible(s.subForm, 0);     // Form_SetObjectsVisible(subForm, 0)
    return 0;
}

// ===========================================================================
// gilde.exe 0x4c55b0 — VIBE_EventPanel_DestroySlot.
//   triple = obj[116]; if (!triple) return.
//   if (triple.widgetId != -1) { Form_SelectWindow(barForm,0); DestroyWidget(widgetId);
//                                triple.widgetId = -1; }
//   if (triple.subForm != -1) { Form_Destroy(subForm); triple.subForm = -1; }
//   if (selected == triple) selected = 0;
//   obj[116] = 0;
//   then COMPACT: find first free slot from the front, slide every higher occupied slot
//   down into it (moving all three columns + fixing the moved object's +116), re-laying
//   each moved icon; finally recompute highWater = last occupied slot index.
//   Returns the last scanned widget-id value (preserved verbatim).
// ===========================================================================
int EventPanel_DestroySlot(int objHandle, int arg) {
    (void)arg;
    EventObject* obj = EventPanel_Object(objHandle);
    int slot = obj ? obj->slotIndex : -1;
    if (slot < 0) return objHandle; // *(a1+116) == 0 -> nothing to do

    EventSlot& s = g_eventSlots[slot];
    if (s.widgetId != -1) {
        // Form_SelectWindow(barForm, 0); Widget_DestroyByType(widgetId, ...).
        g_host->DestroyWidget(s.widgetId);
        s.widgetId = -1;
    }
    if (s.subForm != -1) {
        g_host->DestroyForm(s.subForm);
        s.subForm = -1;
    }
    if (g_eventSelected == slot)
        g_eventSelected = -1;     // dword_632270 = 0
    s.panelObj = 0;
    if (obj) obj->slotIndex = -1; // *(a1+116) = 0

    // Compaction: starting at the first free slot (widgetId == -1), pull each subsequent
    // occupied slot down.  Mirrors the original's v9..v12 sliding loop over the three
    // parallel columns.
    int first = 0;
    if (g_eventSlots[0].widgetId != -1) {
        int v5 = 0;
        do {
            v5 += kEventSlotStride;
            ++first;
        } while (v5 < kEventPanelSlots * kEventSlotStride && g_eventSlots[first].widgetId != -1);
    }
    int last = -1;
    if (first < kEventPanelSlots) {
        for (int dst = first; dst < kEventPanelSlots; ++dst) {
            if (dst < kEventPanelSlots - 1) {
                int srcWidget = g_eventSlots[dst + 1].widgetId;
                if (srcWidget != -1) {
                    g_eventSlots[dst].widgetId = srcWidget;
                    g_eventSlots[dst].panelObj = g_eventSlots[dst + 1].panelObj;
                    g_eventSlots[dst].subForm  = g_eventSlots[dst + 1].subForm;
                    BindObjectToSlot(g_eventSlots[dst].panelObj, dst); // fix moved obj +116
                    g_eventSlots[dst + 1].widgetId = -1;
                    g_eventSlots[dst + 1].panelObj = 0;
                    g_eventSlots[dst + 1].subForm  = -1;
                }
            }
            last = g_eventSlots[dst].widgetId;
            if (last != -1) {
                // Widget_LayoutBounds(widget.x - 32, widget.y, widgetId) re-lays the icon;
                // the geometry edge is owned by the core (no-op for the table here).
            }
        }
    }

    // Recompute the high-water occupied slot: last index whose widget-id is set.
    // gilde.exe 0x4c562c — the original writes dword_632268 ONLY when it finds an
    // occupied slot (`if (dword_11CB564[v7] != -1) dword_632268 = v6;`).  When the
    // table is now empty it leaves the high-water value UNCHANGED (verified vs disasm).
    for (int i = 0; i < kEventPanelSlots; ++i)
        if (g_eventSlots[i].widgetId != -1) g_eventHighWater = i;
    return last;
}

// ===========================================================================
// gilde.exe 0x4c57b0 — VIBE_EventPanel_DestroyBar.
//   for (i=0; i!=48; i+=3) if (widgetId != -1) DestroySlot(panelObj, arg);
//   return Form_Destroy(barForm);
// ===========================================================================
int EventPanel_DestroyBar(int arg) {
    for (int i = 0; i < kEventPanelSlots; ++i) {
        if (g_eventSlots[i].widgetId != -1)
            EventPanel_DestroySlot(g_eventSlots[i].panelObj, arg);
    }
    g_host->DestroyForm(g_eventBarForm);
    return 0;
}

// ===========================================================================
// gilde.exe 0x4c5824 — VIBE_EventPanel_ToggleVisible.
//   if (barVisible != -1) {
//     for each occupied slot with a subForm: Form_SetObjectsVisible(subForm, 0);
//     Form_SelectWindow(barForm,0); Widget_LayoutBounds(...); Form_SetObjectsVisible(barForm,0);
//   }
//   if (arg == 0xFFFF) { highWater = -1; barVisible = -1; return -1; }
//   else { Form_SetObjectsVisible(barForm, 0/*shown*/);
//          highWater = first-free-slot scan; barVisible = barForm; return barForm; }
// ===========================================================================
int EventPanel_ToggleVisible(int arg) {
    if (g_eventBarVisible != -1) {
        for (int i = 0; i < kEventPanelSlots; ++i) {
            if (g_eventSlots[i].widgetId != -1 && g_eventSlots[i].subForm != -1)
                g_host->SetFormVisible(g_eventSlots[i].subForm, 0);
        }
        // Form_SelectWindow + Widget_LayoutBounds re-lay the bar (core edge).
        g_host->SetFormVisible(g_eventBarForm, 0);
    }

    if (arg == 0xFFFF) {
        g_eventHighWater = -1;
        g_eventBarVisible = -1;
        return -1;
    }

    g_host->SetFormVisible(g_eventBarForm, 1);
    int count = 0;
    if (g_eventSlots[0].widgetId != -1) {
        int v4 = 0;
        do {
            v4 += kEventSlotStride;
            ++count;
        } while (v4 < kEventPanelSlots * kEventSlotStride && g_eventSlots[count].widgetId != -1);
    }
    g_eventHighWater = count;
    g_eventBarVisible = g_eventBarForm;
    return g_eventBarForm;
}

// ===========================================================================
// gilde.exe 0x4c5918 — VIBE_EventPanel_SetBarVisible.
//   if (barVisible != -1) return Form_SetObjectsVisible(barForm, visible);
//   return visible;
// ===========================================================================
int EventPanel_SetBarVisible(int visible) {
    if (g_eventBarVisible != -1) {
        g_host->SetFormVisible(g_eventBarForm, visible);
        return visible;
    }
    return visible;
}

// ===========================================================================
// gilde.exe 0x4c5938 — VIBE_EventPanel_SelectActiveSlot.
//   if (barVisible == -1) return.
//   (a) if a slot is currently selected and its icon value is now 0 (deselected by the
//       widget layer), hide its pop-out form and clear the selection.
//   (b) walk the 16 slots; the first occupied slot (other than the current selection)
//       whose icon value != 0 becomes the new selection: hide+clear the old one, show +
//       raise the new one's pop-out form.
// The scene-flag side effects (dword_62EB4C music gate) are out of scope; the
// selection-tracking model is translated.
// ===========================================================================
void EventPanel_SelectActiveSlot() {
    if (g_eventBarVisible == -1)
        return;

    int sel = g_eventSelected;
    // (a) If the current selection's icon was cleared, drop it.
    if (sel != -1) {
        EventSlot& cur = g_eventSlots[sel];
        if (cur.widgetId != -1 && g_host->IconValue(cur.widgetId) == 0) {
            if (cur.subForm != 0 && cur.subForm != -1)
                g_host->SetFormVisible(cur.subForm, 0);
            sel = -1;
        }
    }

    // (b) Find the first occupied slot whose icon is now active.
    for (int i = 0; i < kEventPanelSlots; ++i) {
        EventSlot& s = g_eventSlots[i];
        if (s.widgetId == -1) continue;
        if (i == sel) continue;
        if (g_host->IconValue(s.widgetId) == 0) continue;

        // Deselect the previous one (hide its form, clear its icon value).
        if (sel != -1) {
            EventSlot& prev = g_eventSlots[sel];
            if (prev.subForm != 0 && prev.subForm != -1)
                g_host->SetFormVisible(prev.subForm, 0);
            // *(prevWidget+36)=0; *(prevWidget+40)=0 — owned by the widget array.
        }
        // Select this slot: show + raise its pop-out form.
        sel = i;
        g_host->SetFormVisible(s.subForm, 1);
        g_host->RaiseForm(s.subForm);
    }

    g_eventSelected = sel;
}

} // namespace guild::gui
