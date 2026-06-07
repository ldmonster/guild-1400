#pragma once
// guild::gui — Form table, the master window selector, and the object accessor trio.
//
// Original global state:
//   dword_676A60  Form array base (171-dword / 684-byte stride)             -> g_forms
//     dword_676A64 == 676A60 + 1 dword  (window-id table view)
//     dword_676BE4 == 676A60 + 97 dwords (window-count view)
//   dword_62D258  current form id (set by SelectWindow when formId != 0)    -> g_currentFormId
//   (window globals dword_62D230 / dword_62D298 live in window.h)

#include "gui/types.h"

namespace guild::gui {

extern Form g_forms[kMaxForms]; // dword_676A60
extern int  g_currentFormId;    // dword_62D258

// gilde.exe 0x41e4cc — VIBE_Form_SelectWindow  (formId@eax, winSlot@edx)  [923 xrefs]
//
// Sets the current form (when formId != 0), validates winSlot against the form's
// window count, resolves the stored window id, and caches the Window* (dword_62D298)
// and current window id (dword_62D230). Returns 1 on success; on an out-of-range slot
// it logs "d2_SetForm(): invalid win number" and returns 0.
int Form_SelectWindow(int formId, int winSlot);

// gilde.exe 0x41db20 — VIBE_Form_GetObjectPtr  (localId@eax, group@edx)
// Resolves the window for `group` within the current form, bounds-checks localId
// against the window's object count, and returns the widget-array index of the
// localId-th child object (or -1 when out of range). The original returns the raw
// pointer `dword_69FFB4 + 740*childIdx`; we return the index (which g_widgets[] maps).
int Form_GetObjectPtr(int localId, int group);

// gilde.exe 0x41dc10 — VIBE_Form_GetObjectDataPtr  (localId, group)
// Same window/bounds resolution as GetObjectPtr, then calls Object_GetDataPtr on the
// resolved child widget. Returns 0 when localId is out of range.
i32 Form_GetObjectDataPtr(int localId, int group);

// gilde.exe 0x41dd14 — VIBE_Form_GetObjectAnimPtr  (localId, group)
// For a type-'A' child returns its 3D/anim data handle (widget +12); otherwise 0
// (also 0 when localId is out of range).
i32 Form_GetObjectAnimPtr(int localId, int group);

// gilde.exe 0x41dea8 — VIBE_Form_GetChildObjectId  (form@eax, group@edx, child@ebx)
// Maps (form, window-group, child-slot) to the child object's widget index. When
// form == -1 the group is taken as a direct window slot; otherwise it indexes the
// form's window-id table. Returns -1 when child is out of range. [125 xrefs]
int Form_GetChildObjectId(int form, int group, int child);

} // namespace guild::gui
